/**
 * @file iperf_tcp_raw.c
 * @brief Task-driven lwIP Raw API implementation for Classic iPerf2 TCP tests.
 *
 * Each instance owns one worker task. The receive callback directly returns
 * TCP window credit, accounts payload, and releases pbufs. The worker handles
 * connection events, client transmission, stop requests, and shutdown.
 */

#include <string.h>

#include <lwip/ip_addr.h>
#include <lwip/pbuf.h>
#include <lwip/tcp.h>
#include <lwip/tcpip.h>

#define DBG_TAG "IPERF_TCP_RAW"
#include "log.h"

#include "iperf_common.h"

#if !LWIP_TCPIP_CORE_LOCKING
#error "iPerf2 TCP Raw requires LWIP_TCPIP_CORE_LOCKING"
#endif

/** @brief FreeRTOS worker task name used by TCP Raw instances. */
#define IPERF_TCP_RAW_TASK_NAME    "iperf_tcp_raw"
/** @brief Worker stack depth passed to xTaskCreate(), in StackType_t units. */
#define IPERF_TCP_RAW_TASK_STACK   512U
/** @brief Maximum idle wait before limits and stop requests are rechecked. */
#define IPERF_TCP_RAW_IDLE_WAIT_MS 10U

/** @brief Events copied atomically from lwIP callbacks to the worker. */
typedef struct {
    ip_addr_t local_addr;  /**< Local endpoint captured at establishment. */
    ip_addr_t remote_addr; /**< Remote endpoint captured at establishment. */
    u16_t local_port;      /**< Local endpoint port in host byte order. */
    u16_t remote_port;     /**< Remote endpoint port in host byte order. */
    err_t error;           /**< Latched asynchronous lwIP error. */
    uint8_t connected;     /**< A client connected or a server accepted. */
    uint8_t peer_closed;   /**< The peer delivered an orderly FIN. */
    uint8_t error_pending; /**< error contains a fatal asynchronous error. */
} iperf_tcp_raw_events_t;

/** @brief Complete state for one TCP Raw worker instance. */
typedef struct {
    bflb_iperf_t *iperf;      /**< Borrowed instance valid until completion. */
    struct tcp_pcb *tcp;      /**< Active PCB, accessed only with Core Lock. */
    struct tcp_pcb *listener; /**< Listen PCB, accessed only with Core Lock. */
    TaskHandle_t task;        /**< Worker notified for control and TX events. */
    ip_addr_t connect_addr;   /**< Persistent client destination address. */
    ip_addr_t local_addr;     /**< Established local endpoint. */
    ip_addr_t remote_addr;    /**< Established remote endpoint. */
    u16_t local_port;         /**< Established local port. */
    u16_t remote_port;        /**< Established remote port. */
    err_t async_error;        /**< Fatal asynchronous lwIP error. */
    uint8_t connected;        /**< Latched connection-established event. */
    uint8_t peer_closed;      /**< Latched orderly peer shutdown event. */
    uint8_t error_pending;    /**< async_error is valid. */
} iperf_tcp_raw_context_t;

/** @brief Wake the worker after publishing a control or send event. */
static void iperf_tcp_raw_notify(iperf_tcp_raw_context_t *context)
{
    if (context->task != NULL) {
        xTaskNotifyGive(context->task);
    }
}

/**
 * @brief Detach all iPerf callbacks from an active TCP PCB.
 * @param[in,out] pcb PCB to detach, or NULL.
 * @pre Execute while holding the TCP/IP core lock.
 */
static void iperf_tcp_raw_detach(struct tcp_pcb *pcb)
{
    if (pcb == NULL) {
        return;
    }
    tcp_arg(pcb, NULL);
    tcp_recv(pcb, NULL);
    tcp_sent(pcb, NULL);
    tcp_poll(pcb, NULL, 0U);
    tcp_err(pcb, NULL);
}

/**
 * @brief Consume one received pbuf directly in the lwIP callback.
 * @param[in,out] arg TCP Raw context registered with tcp_arg().
 * @param[in,out] pcb Receiving TCP PCB.
 * @param[in] p Received pbuf, or NULL for orderly peer shutdown.
 * @param[in] error lwIP receive status.
 * @retval ERR_OK The event or pbuf was consumed.
 * @note This callback runs with the TCP/IP core locked. Normal payload does
 * not wake the worker; only control events require task handling.
 */
static err_t iperf_tcp_raw_recv(void *arg, struct tcp_pcb *pcb,
                                struct pbuf *p, err_t error)
{
    iperf_tcp_raw_context_t *context = arg;

    if (p == NULL) {
        iperf_test_end(context->iperf);
        if (error == ERR_OK) {
            context->peer_closed = 1U;
        } else {
            context->async_error = error;
            context->error_pending = 1U;
        }
        iperf_tcp_raw_notify(context);
        return ERR_OK;
    }

    if (error != ERR_OK) {
        iperf_test_end(context->iperf);
        context->async_error = error;
        context->error_pending = 1U;
        pbuf_free(p);
        iperf_tcp_raw_notify(context);
        return ERR_OK;
    }

    tcp_recved(pcb, p->tot_len);
    if (context->iperf->config.role == BFLB_IPERF_ROLE_SERVER) {
        iperf_account_transfer(context->iperf, p->tot_len);
    }
    pbuf_free(p);
    return ERR_OK;
}

/**
 * @brief Wake the client worker when acknowledged data frees send capacity.
 */
static err_t iperf_tcp_raw_sent(void *arg, struct tcp_pcb *pcb, u16_t length)
{
    iperf_tcp_raw_context_t *context = arg;

    LWIP_UNUSED_ARG(pcb);
    LWIP_UNUSED_ARG(length);
    iperf_tcp_raw_notify(context);
    return ERR_OK;
}

/**
 * @brief Publish a fatal asynchronous TCP error.
 * @note lwIP has already released the active PCB before this callback runs.
 */
static void iperf_tcp_raw_error(void *arg, err_t error)
{
    iperf_tcp_raw_context_t *context = arg;

    LOG_E("TCP Raw asynchronous connection error: error=%d\r\n", error);
    context->tcp = NULL;
    context->async_error = error;
    context->error_pending = 1U;
    iperf_test_end(context->iperf);
    iperf_tcp_raw_notify(context);
}

/**
 * @brief Publish successful completion of a client connection attempt.
 */
static err_t iperf_tcp_raw_connected(void *arg, struct tcp_pcb *pcb, err_t error)
{
    iperf_tcp_raw_context_t *context = arg;

    if (error != ERR_OK) {
        LOG_E("TCP Raw connect callback failed: error=%d\r\n", error);
        context->async_error = error;
        context->error_pending = 1U;
        iperf_tcp_raw_notify(context);
        return ERR_OK;
    }

    context->tcp = pcb;
    ip_addr_copy(context->local_addr, pcb->local_ip);
    ip_addr_copy(context->remote_addr, pcb->remote_ip);
    context->local_port = pcb->local_port;
    context->remote_port = pcb->remote_port;
    context->connected = 1U;
    iperf_tcp_raw_notify(context);
    return ERR_OK;
}

/**
 * @brief Accept one server connection and immediately install data callbacks.
 * @note Installing callbacks before returning ensures payload carried by the
 * final handshake ACK is delivered to iperf_tcp_raw_recv().
 */
static err_t iperf_tcp_raw_accept(void *arg, struct tcp_pcb *pcb, err_t error)
{
    iperf_tcp_raw_context_t *context = arg;

    if (pcb == NULL) {
        return error;
    }
    if (error != ERR_OK || context->tcp != NULL ||
        context->iperf->stop_requested) {
        tcp_abort(pcb);
        return ERR_ABRT;
    }

    context->tcp = pcb;
    pcb->tos = context->iperf->config.tos;
    tcp_arg(pcb, context);
    tcp_recv(pcb, iperf_tcp_raw_recv);
    tcp_err(pcb, iperf_tcp_raw_error);
    ip_addr_copy(context->local_addr, pcb->local_ip);
    ip_addr_copy(context->remote_addr, pcb->remote_ip);
    context->local_port = pcb->local_port;
    context->remote_port = pcb->remote_port;
    iperf_log_connection(context->iperf,
                         ip_2_ip4(&context->local_addr)->addr, context->local_port,
                         ip_2_ip4(&context->remote_addr)->addr, context->remote_port);
    iperf_test_begin(context->iperf);
    context->connected = 1U;
    iperf_tcp_raw_notify(context);
    return ERR_OK;
}

/**
 * @brief Atomically transfer all latched callback state to the worker.
 */
static void iperf_tcp_raw_take_events(iperf_tcp_raw_context_t *context,
                                      iperf_tcp_raw_events_t *events)
{
    memset(events, 0, sizeof(*events));

    LOCK_TCPIP_CORE();
    events->connected = context->connected;
    context->connected = 0U;
    events->peer_closed = context->peer_closed;
    context->peer_closed = 0U;
    events->error_pending = context->error_pending;
    context->error_pending = 0U;
    events->error = context->async_error;
    if (events->connected != 0U) {
        ip_addr_copy(events->local_addr, context->local_addr);
        ip_addr_copy(events->remote_addr, context->remote_addr);
        events->local_port = context->local_port;
        events->remote_port = context->remote_port;
    }
    UNLOCK_TCPIP_CORE();
}

/**
 * @brief Fill currently available TCP send capacity with immutable payload.
 * @param[in,out] context Active TCP Raw client context.
 * @retval ERR_OK More data can be submitted without waiting.
 * @retval ERR_MEM Send capacity is exhausted; wait for tcp_sent().
 * @return Another lwIP error on failure.
 * @note tcp_write() references permanent g_iperf_raw_payload without copying.
 */
static err_t iperf_tcp_raw_fill(iperf_tcp_raw_context_t *context)
{
    bflb_iperf_t *iperf = context->iperf;
    struct tcp_pcb *pcb;
    uint64_t remaining = UINT64_MAX;
    uint32_t written = 0U;
    bool send_blocked = false;
    err_t error = ERR_OK;

    if (iperf->config.amount_bytes != 0U) {
        if (iperf->stats.bytes < iperf->config.amount_bytes) {
            remaining = iperf->config.amount_bytes - iperf->stats.bytes;
        } else {
            remaining = 0U;
        }
    }

    LOCK_TCPIP_CORE();
    pcb = context->tcp;
    while (pcb != NULL && remaining != 0U && tcp_sndbuf(pcb) != 0U) {
        uint16_t length = LWIP_MIN(iperf->config.buffer_len, tcp_sndbuf(pcb));

        length = (uint16_t)LWIP_MIN((uint64_t)length, remaining);
        error = tcp_write(pcb, g_iperf_raw_payload, length, 0U);
        if (error == ERR_MEM || error == ERR_BUF) {
            send_blocked = true;
            error = ERR_OK;
            break;
        }
        if (error != ERR_OK) {
            break;
        }
        written += length;
        remaining -= length;
    }
    if (pcb != NULL && remaining != 0U && tcp_sndbuf(pcb) == 0U) {
        send_blocked = true;
    }
    /* Retry previously queued unsent segments even when this pass wrote none. */
    if (pcb != NULL && error == ERR_OK) {
        err_t output_error = tcp_output(pcb);

        if (output_error != ERR_OK && output_error != ERR_MEM &&
            output_error != ERR_BUF) {
            error = output_error;
        }
    }
    UNLOCK_TCPIP_CORE();

    if (written != 0U) {
        iperf_account_transfer(iperf, written);
    }
    return error == ERR_OK && send_blocked ? ERR_MEM : error;
}

/**
 * @brief Close the one-shot server listener after accepting a peer.
 */
static err_t iperf_tcp_raw_close_listener(iperf_tcp_raw_context_t *context)
{
    LOCK_TCPIP_CORE();
    if (context->listener != NULL) {
        tcp_arg(context->listener, NULL);
        tcp_accept(context->listener, NULL);
        tcp_close(context->listener);
        context->listener = NULL;
    }
    UNLOCK_TCPIP_CORE();
    return ERR_OK;
}

/**
 * @brief Release all TCP PCBs and detach callbacks before worker completion.
 * @param[in,out] context TCP Raw context being finalized.
 * @param[in] final_error Current backend result.
 * @return final_error, or a close error when normal shutdown fails.
 */
static int iperf_tcp_raw_cleanup(iperf_tcp_raw_context_t *context,
                                 int final_error)
{
    err_t close_error;

    LOCK_TCPIP_CORE();
    if (context->listener != NULL) {
        tcp_arg(context->listener, NULL);
        tcp_accept(context->listener, NULL);
        tcp_close(context->listener);
        context->listener = NULL;
    }

    if (context->tcp != NULL) {
        struct tcp_pcb *pcb = context->tcp;

        iperf_tcp_raw_detach(pcb);
        context->tcp = NULL;
        close_error = tcp_close(pcb);
        if (close_error != ERR_OK) {
            tcp_abort(pcb);
            if (final_error == 0) {
                final_error = close_error;
            }
        }
    }
    UNLOCK_TCPIP_CORE();
    return final_error;
}

/**
 * @brief Run one asynchronous-connect TCP Raw client from the worker task.
 */
static int iperf_tcp_raw_client(iperf_tcp_raw_context_t *context)
{
    bflb_iperf_t *iperf = context->iperf;
    struct tcp_pcb *pcb;
    ip_addr_t local_addr;
    bool established = false;
    err_t error;

    ip_addr_set_ip4_u32(&local_addr, iperf->config.local_ip4);
    ip_addr_set_ip4_u32(&context->connect_addr, iperf->config.remote_ip4);

    LOCK_TCPIP_CORE();
    pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (pcb == NULL) {
        error = ERR_MEM;
    } else {
        context->tcp = pcb;
        pcb->tos = iperf->config.tos;
        if (iperf->config.local_ip4 != 0U || iperf->config.local_port != 0U) {
            error = tcp_bind(pcb, &local_addr, iperf->config.local_port);
        } else {
            error = ERR_OK;
        }
        if (error == ERR_OK) {
            if (iperf->config.tcp_nodelay != 0U) {
                tcp_nagle_disable(pcb);
            }
            tcp_arg(pcb, context);
            tcp_recv(pcb, iperf_tcp_raw_recv);
            tcp_sent(pcb, iperf_tcp_raw_sent);
            tcp_err(pcb, iperf_tcp_raw_error);
            error = tcp_connect(pcb, &context->connect_addr, iperf->config.port, iperf_tcp_raw_connected);
        }
        if (error != ERR_OK) {
            iperf_tcp_raw_detach(pcb);
            context->tcp = NULL;
            tcp_abort(pcb);
        }
    }
    UNLOCK_TCPIP_CORE();

    if (error != ERR_OK) {
        LOG_E("TCP Raw client PCB/bind/connect failed: error=%d\r\n", error);
        return error;
    }
    iperf_log_client_preamble(iperf);

    while (true) {
        iperf_tcp_raw_events_t events;

        iperf_tcp_raw_take_events(context, &events);
        if (events.connected != 0U && !established) {
            established = true;
            iperf_log_connection(iperf,
                                 ip_2_ip4(&events.local_addr)->addr, events.local_port,
                                 ip_2_ip4(&events.remote_addr)->addr, events.remote_port);
            iperf_test_begin(iperf);
        }
        if (events.error_pending != 0U) {
            return events.error;
        }
        if (iperf_limit_reached(iperf, iperf_now_us())) {
            return ERR_OK;
        }
        if (events.peer_closed != 0U) {
            return ERR_CLSD;
        }

        if (established) {
            error = iperf_tcp_raw_fill(context);
            if (error != ERR_OK && error != ERR_MEM) {
                LOG_E("TCP Raw client data output failed: error=%d\r\n", error);
                return error;
            }
            if (iperf_limit_reached(iperf, iperf_now_us())) {
                return ERR_OK;
            }
            if (error == ERR_MEM) {
                ulTaskNotifyTake(pdTRUE, pdMS_TO_TICKS(IPERF_TCP_RAW_IDLE_WAIT_MS));
            }
        } else {
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        }
    }
}

/**
 * @brief Run one single-client TCP Raw server from the worker task.
 */
static int iperf_tcp_raw_server(iperf_tcp_raw_context_t *context)
{
    bflb_iperf_t *iperf = context->iperf;
    struct tcp_pcb *pcb;
    ip_addr_t local_addr;
    bool established = false;
    err_t error;

    ip_addr_set_ip4_u32(&local_addr, iperf->config.local_ip4);

    LOCK_TCPIP_CORE();
    pcb = tcp_new_ip_type(IPADDR_TYPE_V4);
    if (pcb == NULL) {
        error = ERR_MEM;
    } else {
        pcb->tos = iperf->config.tos;
        error = tcp_bind(pcb, &local_addr, iperf->config.port);
        if (error == ERR_OK) {
            context->listener = tcp_listen_with_backlog_and_err(pcb, 1U, &error);
            if (context->listener != NULL) {
                tcp_arg(context->listener, context);
                tcp_accept(context->listener, iperf_tcp_raw_accept);
                iperf_log_server_preamble(iperf);
            } else {
                tcp_abort(pcb);
            }
        } else {
            tcp_abort(pcb);
        }
    }
    UNLOCK_TCPIP_CORE();

    if (error != ERR_OK) {
        LOG_E("TCP Raw server PCB/bind/listen failed: error=%d\r\n", error);
        return error;
    }

    while (true) {
        iperf_tcp_raw_events_t events;

        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        iperf_tcp_raw_take_events(context, &events);
        if (events.connected != 0U && !established) {
            established = true;
            error = iperf_tcp_raw_close_listener(context);
            if (error != ERR_OK) {
                return error;
            }
        }
        if (events.error_pending != 0U) {
            return events.error;
        }
        if (iperf->stop_requested || events.peer_closed != 0U) {
            return ERR_OK;
        }
    }
}

/**
 * @brief Run one TCP Raw worker and own the complete PCB lifecycle.
 * @warning iperf_backend_finished() may make context destroyable; only task
 * self-deletion may follow it.
 */
static void iperf_tcp_raw_task(void *arg)
{
    iperf_tcp_raw_context_t *context = arg;
    bflb_iperf_t *iperf = context->iperf;
    int error;

    context->task = xTaskGetCurrentTaskHandle();
    vTaskDelay(pdMS_TO_TICKS(10U));

    if (!iperf_backend_started(iperf)) {
        error = 0;
    } else if (iperf->config.role == BFLB_IPERF_ROLE_CLIENT) {
        error = iperf_tcp_raw_client(context);
    } else {
        error = iperf_tcp_raw_server(context);
    }

    error = iperf_tcp_raw_cleanup(context, error);
    iperf_test_end(iperf);
    context->task = NULL;
    context->iperf = NULL;

    iperf_backend_finished(iperf, error);
    vTaskDelete(NULL);
}

/**
 * @brief Create one TCP Raw worker task.
 * @retval 0 The worker task was created successfully.
 * @retval -1 Task creation failed.
 */
static int iperf_tcp_raw_start(bflb_iperf_t *iperf, void *private_context)
{
    iperf_tcp_raw_context_t *context = private_context;

    context->iperf = iperf;
    if (xTaskCreate(iperf_tcp_raw_task, IPERF_TCP_RAW_TASK_NAME,
                    IPERF_TCP_RAW_TASK_STACK, context,
                    iperf->config.task_priority, &context->task) != pdPASS) {
        LOG_E("TCP Raw task creation failed\r\n");
        context->task = NULL;
        context->iperf = NULL;
        return -1;
    }
    return 0;
}

/**
 * @brief Accept a cooperative stop request for a TCP Raw worker.
 * @note The notification wakes a worker blocked on a control event.
 */
static int iperf_tcp_raw_stop(bflb_iperf_t *iperf, void *private_context)
{
    iperf_tcp_raw_context_t *context = private_context;

    LWIP_UNUSED_ARG(iperf);
    iperf_tcp_raw_notify(context);
    return 0;
}

/** @brief Backend operation table for task-driven Classic iPerf2 TCP Raw tests. */
const iperf_backend_ops_t g_iperf_tcp_raw_ops = {
    .context_size = sizeof(iperf_tcp_raw_context_t),
    .start = iperf_tcp_raw_start,
    .stop = iperf_tcp_raw_stop,
};
