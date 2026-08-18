/**
 * @file bflb_iperf.c
 * @brief Multi-instance iPerf public API and backend dispatch.
 */

#include <stdlib.h>
#include <string.h>

#include <bflb_irq.h>

#include "iperf_common.h"

/** @brief Default worker priority, clamped to the configured FreeRTOS range. */
#define IPERF_DEFAULT_TASK_PRIORITY ((configMAX_PRIORITIES > 10U) ? 10U : (configMAX_PRIORITIES - 1U))
/** @brief Maximum generic Socket I/O buffer size, in bytes. */
#define IPERF_MAX_BUFFER_LEN        16384U
/** @brief Maximum UDP datagram size accepted by all backends, in bytes. */
#define IPERF_MAX_UDP_LEN           1470U

/**
 * @brief Immutable protocol/backend operation dispatch table.
 * @note The first index is bflb_iperf_proto_t and the second index is
 * bflb_iperf_backend_t; iperf_validate_config() validates both before lookup.
 */
static const iperf_backend_ops_t *const s_backends[][2] = {
    [BFLB_IPERF_PROTO_TCP] = {
        [BFLB_IPERF_BACKEND_SOCKET] = &g_iperf_tcp_socket_ops,
        [BFLB_IPERF_BACKEND_RAW] = &g_iperf_tcp_raw_ops,
    },
    [BFLB_IPERF_PROTO_UDP] = {
        [BFLB_IPERF_BACKEND_SOCKET] = &g_iperf_udp_socket_ops,
        [BFLB_IPERF_BACKEND_RAW] = &g_iperf_udp_raw_ops,
    },
};

/**
 * @brief Validate and normalize a copied configuration.
 *
 * @param[in,out] config Configuration copy to validate and populate with defaults.
 *
 * @retval BFLB_IPERF_OK The normalized configuration is valid.
 * @retval BFLB_IPERF_ERR_INVALID A field or cross-field constraint is invalid.
 *
 * @note config must not be NULL.
 */
static int iperf_validate_config(bflb_iperf_config_t *config)
{
    if ((unsigned int)config->backend > BFLB_IPERF_BACKEND_RAW ||
        (unsigned int)config->role > BFLB_IPERF_ROLE_SERVER ||
        (unsigned int)config->proto > BFLB_IPERF_PROTO_UDP) {
        return BFLB_IPERF_ERR_INVALID;
    }
    if (config->role == BFLB_IPERF_ROLE_CLIENT && config->remote_ip4 == 0U) {
        return BFLB_IPERF_ERR_INVALID;
    }
    if (config->port == 0U) {
        config->port = BFLB_IPERF_DEFAULT_PORT;
    }
    if (config->role == BFLB_IPERF_ROLE_CLIENT &&
        config->duration_s == 0U && config->amount_bytes == 0U) {
        config->duration_s = BFLB_IPERF_DEFAULT_TIME_S;
    }
    if (config->amount_bytes != 0U) {
        config->duration_s = 0U;
    }
    if (config->buffer_len == 0U) {
        config->buffer_len = config->proto == BFLB_IPERF_PROTO_UDP ?
                                 BFLB_IPERF_DEFAULT_UDP_LEN :
                                 BFLB_IPERF_DEFAULT_TCP_LEN;
    }
    if (config->buffer_len > IPERF_MAX_BUFFER_LEN ||
        (config->proto == BFLB_IPERF_PROTO_UDP &&
         config->buffer_len > IPERF_MAX_UDP_LEN) ||
        (config->backend == BFLB_IPERF_BACKEND_RAW &&
         config->proto == BFLB_IPERF_PROTO_TCP &&
         config->buffer_len > BFLB_IPERF_RAW_TCP_BUFFER_LEN)) {
        return BFLB_IPERF_ERR_INVALID;
    }
    if (config->proto == BFLB_IPERF_PROTO_UDP &&
        config->buffer_len < BFLB_IPERF_UDP_CLIENT_HEADER_SIZE) {
        return BFLB_IPERF_ERR_INVALID;
    }
    if (config->task_priority == 0U) {
        config->task_priority = IPERF_DEFAULT_TASK_PRIORITY;
    }
    if (config->task_priority >= configMAX_PRIORITIES) {
        return BFLB_IPERF_ERR_INVALID;
    }
    if (config->amount_bytes != 0U && config->proto == BFLB_IPERF_PROTO_UDP &&
        (config->amount_bytes < BFLB_IPERF_UDP_CLIENT_HEADER_SIZE ||
         (config->amount_bytes % config->buffer_len != 0U &&
          config->amount_bytes % config->buffer_len < BFLB_IPERF_UDP_HEADER_SIZE))) {
        return BFLB_IPERF_ERR_INVALID;
    }
    return BFLB_IPERF_OK;
}

/**
 * @brief Enter a public API call before waiting for the instance mutex.
 *
 * @param[in,out] iperf Instance whose lifetime and mutex are acquired.
 *
 * @retval BFLB_IPERF_OK The lifetime reference and mutex were acquired.
 * @retval BFLB_IPERF_ERR_INVALID The instance is NULL or destruction started.
 *
 * @note On success, the caller must use iperf_call_leave() or an equivalent
 * path to release both active_calls and lock. This function may block and must
 * not be called from an ISR.
 */
static int iperf_call_enter(bflb_iperf_t *iperf)
{
    uintptr_t irq_flags;

    if (iperf == NULL) {
        return BFLB_IPERF_ERR_INVALID;
    }

    /* Count mutex waiters so destroy cannot delete a mutex beneath a caller. */
    irq_flags = bflb_irq_save();
    if (iperf->destroy_started) {
        bflb_irq_restore(irq_flags);
        return BFLB_IPERF_ERR_INVALID;
    }
    iperf->active_calls++;
    bflb_irq_restore(irq_flags);
    xSemaphoreTake(iperf->lock, portMAX_DELAY);
    return BFLB_IPERF_OK;
}

/**
 * @brief Leave a public API call and release its lifetime reference.
 *
 * @param[in,out] iperf Instance entered by iperf_call_enter().
 *
 * @pre The current path holds iperf->lock and owns one active_calls reference.
 * @post The reference is released and the instance mutex is unlocked.
 */
static void iperf_call_leave(bflb_iperf_t *iperf)
{
    uintptr_t irq_flags = bflb_irq_save();

    iperf->active_calls--;
    bflb_irq_restore(irq_flags);
    xSemaphoreGive(iperf->lock);
}

void bflb_iperf_config_init(bflb_iperf_config_t *config)
{
    if (config == NULL) {
        return;
    }
    memset(config, 0, sizeof(*config));
    config->backend = BFLB_IPERF_BACKEND_RAW;
    config->role = BFLB_IPERF_ROLE_SERVER;
    config->proto = BFLB_IPERF_PROTO_TCP;
    config->port = BFLB_IPERF_DEFAULT_PORT;
    config->interval_s = BFLB_IPERF_DEFAULT_INTERVAL_S;
    config->duration_s = BFLB_IPERF_DEFAULT_TIME_S;
    config->task_priority = IPERF_DEFAULT_TASK_PRIORITY;
}

int bflb_iperf_create(const bflb_iperf_config_t *config,
                      bflb_iperf_t **iperf)
{
    bflb_iperf_config_t checked;
    bflb_iperf_t *created;

    if (config == NULL || iperf == NULL) {
        return BFLB_IPERF_ERR_INVALID;
    }
    *iperf = NULL;
    checked = *config;
    if (iperf_validate_config(&checked) != BFLB_IPERF_OK) {
        return BFLB_IPERF_ERR_INVALID;
    }

    created = calloc(1, sizeof(*created));
    if (created == NULL) {
        return BFLB_IPERF_ERR_INVALID;
    }
    created->lock = xSemaphoreCreateMutex();
    if (created->lock == NULL) {
        free(created);
        return BFLB_IPERF_ERR_INVALID;
    }
    created->config = checked;
    created->state = BFLB_IPERF_STATE_IDLE;
    created->backend_released = true;
    created->ops = s_backends[checked.proto][checked.backend];
    created->backend_context = calloc(1, created->ops->context_size);
    if (created->backend_context == NULL) {
        vSemaphoreDelete(created->lock);
        free(created);
        return BFLB_IPERF_ERR_INVALID;
    }

    *iperf = created;
    return BFLB_IPERF_OK;
}

int bflb_iperf_start(bflb_iperf_t *iperf)
{
    uintptr_t irq_flags;
    int result;

    if (iperf_call_enter(iperf) != BFLB_IPERF_OK) {
        return BFLB_IPERF_ERR_INVALID;
    }
    if (iperf->state != BFLB_IPERF_STATE_IDLE) {
        iperf_call_leave(iperf);
        return BFLB_IPERF_ERR_INVALID;
    }

    iperf->state = BFLB_IPERF_STATE_STARTING;
    iperf->backend_released = false;
    iperf->stop_requested = false;
    /* Keep the lifetime reference while allowing asynchronous completion. */
    xSemaphoreGive(iperf->lock);

    result = iperf->ops->start(iperf, iperf->backend_context);
    if (result < 0) {
        /* A failed start contractually leaves no asynchronous backend owner. */
        xSemaphoreTake(iperf->lock, portMAX_DELAY);
        if (!iperf->backend_released) {
            iperf->error = result;
            iperf->state = BFLB_IPERF_STATE_ERROR;
            iperf->backend_released = true;
        }
        xSemaphoreGive(iperf->lock);
    }
    xSemaphoreTake(iperf->lock, portMAX_DELAY);
    irq_flags = bflb_irq_save();
    iperf->active_calls--;
    bflb_irq_restore(irq_flags);
    xSemaphoreGive(iperf->lock);
    return result;
}

int bflb_iperf_stop(bflb_iperf_t *iperf)
{
    bflb_iperf_state_t previous;
    uintptr_t irq_flags;
    int result = BFLB_IPERF_OK;

    if (iperf_call_enter(iperf) != BFLB_IPERF_OK) {
        return BFLB_IPERF_ERR_INVALID;
    }
    previous = iperf->state;
    if (previous != BFLB_IPERF_STATE_STARTING &&
        previous != BFLB_IPERF_STATE_RUNNING) {
        iperf_call_leave(iperf);
        return BFLB_IPERF_OK;
    }
    iperf->stop_requested = true;
    iperf->state = BFLB_IPERF_STATE_STOPPING;
    if (previous == BFLB_IPERF_STATE_STARTING &&
        iperf->config.backend == BFLB_IPERF_BACKEND_RAW) {
        /* The already queued Raw start callback consumes this stop flag. */
        iperf_call_leave(iperf);
        return BFLB_IPERF_OK;
    }
    /* Hold the lifetime reference until the backend stop request returns. */
    xSemaphoreGive(iperf->lock);

    result = iperf->ops->stop(iperf, iperf->backend_context);
    if (result < 0) {
        xSemaphoreTake(iperf->lock, portMAX_DELAY);
        if (iperf->state == BFLB_IPERF_STATE_STOPPING) {
            iperf->stop_requested = false;
            iperf->state = BFLB_IPERF_STATE_RUNNING;
        }
        xSemaphoreGive(iperf->lock);
    }
    xSemaphoreTake(iperf->lock, portMAX_DELAY);
    irq_flags = bflb_irq_save();
    iperf->active_calls--;
    bflb_irq_restore(irq_flags);
    xSemaphoreGive(iperf->lock);
    return result;
}

int bflb_iperf_get_result(bflb_iperf_t *iperf,
                          bflb_iperf_result_t *result)
{
    if (result == NULL || iperf_call_enter(iperf) != BFLB_IPERF_OK) {
        return BFLB_IPERF_ERR_INVALID;
    }
    iperf_result_snapshot(iperf, result);
    iperf_call_leave(iperf);
    return BFLB_IPERF_OK;
}

bflb_iperf_state_t bflb_iperf_get_state(bflb_iperf_t *iperf)
{
    bflb_iperf_state_t state;

    if (iperf_call_enter(iperf) != BFLB_IPERF_OK) {
        return BFLB_IPERF_STATE_ERROR;
    }
    state = iperf->state;
    iperf_call_leave(iperf);
    return state;
}

int bflb_iperf_destroy(bflb_iperf_t *iperf)
{
    uintptr_t irq_flags;

    if (iperf == NULL) {
        return BFLB_IPERF_ERR_INVALID;
    }
    if (xSemaphoreTake(iperf->lock, 0U) != pdTRUE) {
        return BFLB_IPERF_ERR_BUSY;
    }

    irq_flags = bflb_irq_save();
    if (iperf->active_calls != 0U) {
        bflb_irq_restore(irq_flags);
        xSemaphoreGive(iperf->lock);
        return BFLB_IPERF_ERR_BUSY;
    }
    if (iperf->destroy_started) {
        bflb_irq_restore(irq_flags);
        xSemaphoreGive(iperf->lock);
        return BFLB_IPERF_ERR_INVALID;
    }
    bflb_irq_restore(irq_flags);

    if (!iperf->backend_released || iperf->callback_running ||
        iperf->state == BFLB_IPERF_STATE_STARTING ||
        iperf->state == BFLB_IPERF_STATE_RUNNING ||
        iperf->state == BFLB_IPERF_STATE_STOPPING) {
        xSemaphoreGive(iperf->lock);
        return BFLB_IPERF_ERR_BUSY;
    }

    /* Publish destruction before releasing the mutex to reject later callers. */
    irq_flags = bflb_irq_save();
    if (iperf->active_calls != 0U) {
        bflb_irq_restore(irq_flags);
        xSemaphoreGive(iperf->lock);
        return BFLB_IPERF_ERR_BUSY;
    }
    iperf->destroy_started = true;
    bflb_irq_restore(irq_flags);
    xSemaphoreGive(iperf->lock);

    free(iperf->backend_context);
    vSemaphoreDelete(iperf->lock);
    free(iperf);
    return BFLB_IPERF_OK;
}

bool iperf_backend_started(bflb_iperf_t *iperf)
{
    bool run;

    xSemaphoreTake(iperf->lock, portMAX_DELAY);
    run = !iperf->stop_requested;
    iperf->state = run ? BFLB_IPERF_STATE_RUNNING : BFLB_IPERF_STATE_STOPPING;
    xSemaphoreGive(iperf->lock);
    return run;
}

void iperf_backend_finished(bflb_iperf_t *iperf, int error)
{
    bflb_iperf_done_cb_t done_cb;
    bflb_iperf_result_t result;
    void *user_data;

    /* Freeze all backend-owned data before releasing its lifetime ownership. */
    iperf_test_end(iperf);
    iperf_test_report_finish(iperf);
    xSemaphoreTake(iperf->lock, portMAX_DELAY);
    if (iperf->backend_released) {
        xSemaphoreGive(iperf->lock);
        return;
    }
    iperf->error = error;
    iperf->state = error == 0 ? BFLB_IPERF_STATE_DONE : BFLB_IPERF_STATE_ERROR;
    done_cb = iperf->config.done_cb;
    user_data = iperf->config.user_data;
    iperf_result_snapshot(iperf, &result);
    iperf->callback_running = done_cb != NULL;
    xSemaphoreGive(iperf->lock);

    /* Invoke user code without the instance mutex to allow result inspection. */
    if (done_cb != NULL) {
        done_cb(iperf, &result, user_data);
        xSemaphoreTake(iperf->lock, portMAX_DELAY);
        iperf->callback_running = false;
    } else {
        xSemaphoreTake(iperf->lock, portMAX_DELAY);
    }
    /* Publish destroyability only after user callback execution is complete. */
    iperf->backend_released = true;
    xSemaphoreGive(iperf->lock);
}