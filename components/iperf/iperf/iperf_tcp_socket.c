/**
 * @file iperf_tcp_socket.c
 * @brief Blocking lwIP Socket implementation for Classic iPerf2 TCP tests.
 *
 * Each instance runs in one FreeRTOS worker task. Stop requests are
 * cooperative and are observed after blocking Socket calls return. The worker
 * owns its I/O buffer and publishes completion before deleting itself.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <lwip/inet.h>
#include <lwip/tcp.h>

#define DBG_TAG "IPERF_TCP_SOCKET"
#include "log.h"

#include "iperf_common.h"

/** @brief FreeRTOS worker task name used by TCP Socket instances. */
#define IPERF_TCP_SOCKET_TASK_NAME  "iperf_tcp_socket"
/** @brief Worker stack depth passed to xTaskCreate(), in StackType_t units. */
#define IPERF_TCP_SOCKET_TASK_STACK 512U
/** @brief Requested Socket send and receive timeout, in milliseconds. */
#define IPERF_TCP_SOCKET_TIMEOUT_MS 200U

/**
 * @brief Runtime context for one TCP Socket worker.
 * @note The core owns this context; the worker owns buffer after startup.
 */
typedef struct {
    bflb_iperf_t *iperf; /**< Borrowed instance reference valid until completion. */
    TaskHandle_t task;   /**< Worker handle, cleared immediately before completion. */
    uint8_t *buffer;     /**< Dynamically allocated I/O buffer owned by the worker. */
} iperf_tcp_socket_context_t;

/**
 * @brief Set symmetric receive and send timeouts on a socket.
 * @param[in] socket_fd Socket descriptor.
 * @retval 0 Both timeout options were accepted.
 * @return A negative errno value on failure.
 */
static int iperf_tcp_socket_set_timeout(int socket_fd)
{
    struct timeval timeout;

    timeout.tv_sec = IPERF_TCP_SOCKET_TIMEOUT_MS / 1000U;
    timeout.tv_usec = (IPERF_TCP_SOCKET_TIMEOUT_MS % 1000U) * 1000U;
    if (setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout)) < 0 ||
        setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout)) < 0) {
        return -errno;
    }
    return 0;
}

/**
 * @brief Run a blocking TCP client test.
 * @param[in,out] context TCP Socket context.
 * @retval 0 The configured test limit or a stop request was reached.
 * @return A negative errno value on Socket failure.
 * @post The client socket is closed; context and its buffer remain allocated.
 */
static int iperf_tcp_socket_client(iperf_tcp_socket_context_t *context)
{
    bflb_iperf_t *iperf = context->iperf;
    struct sockaddr_in remote = { 0 };
    struct sockaddr_in local = { 0 };
    int socket_fd = -1;
    int error = -1;

    socket_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (socket_fd < 0) {
        LOG_E("TCP client socket() failed: errno=%d\r\n", errno);
        return -errno;
    }
    error = iperf_tcp_socket_set_timeout(socket_fd);
    if (error != 0) {
        LOG_E("TCP client timeout setup failed: errno=%d\r\n", -error);
        goto exit;
    }
    if (iperf->config.tcp_nodelay != 0U) {
        int enabled = 1;
        if (setsockopt(socket_fd, IPPROTO_TCP, TCP_NODELAY, &enabled, sizeof(enabled)) < 0) {
            error = -errno;
            LOG_E("TCP client setsockopt(TCP_NODELAY) failed: errno=%d\r\n", -error);
            goto exit;
        }
    }
    if (iperf->config.tos != 0U) {
        int tos = iperf->config.tos;
        if (setsockopt(socket_fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) < 0) {
            error = -errno;
            LOG_E("TCP client setsockopt(IP_TOS) failed: errno=%d\r\n", -error);
            goto exit;
        }
    }

    if (iperf->config.local_ip4 != 0U || iperf->config.local_port != 0U) {
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = iperf->config.local_ip4;
        local.sin_port = htons(iperf->config.local_port);
        if (bind(socket_fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
            error = -errno;
            LOG_E("TCP client bind() failed: errno=%d\r\n", -error);
            goto exit;
        }
    }

    remote.sin_family = AF_INET;
    remote.sin_addr.s_addr = iperf->config.remote_ip4;
    remote.sin_port = htons(iperf->config.port);
    iperf_log_client_preamble(iperf);

    if (connect(socket_fd, (struct sockaddr *)&remote, sizeof(remote)) < 0) {
        error = -errno;
        LOG_E("TCP client connect() failed: errno=%d\r\n", -error);
        goto exit;
    }

    socklen_t local_len = sizeof(local);
    getsockname(socket_fd, (struct sockaddr *)&local, &local_len);
    iperf_log_connection(iperf,
                         local.sin_addr.s_addr, ntohs(local.sin_port),
                         remote.sin_addr.s_addr, ntohs(remote.sin_port));

    /* Classic normal mode starts payload immediately without a V1 control header. */
    iperf_test_begin(iperf);

    while (!iperf_limit_reached(iperf, iperf_now_us())) {
        uint16_t length = iperf->config.buffer_len;
        if (iperf->config.amount_bytes != 0U) {
            if (iperf->stats.bytes < iperf->config.amount_bytes) {
                length = (uint16_t)LWIP_MIN((uint64_t)length, (iperf->config.amount_bytes - iperf->stats.bytes));
            } else {
                break;
            }
        }

        int sent = send(socket_fd, context->buffer, length, 0);
        if (sent > 0) {
            iperf_account_transfer(iperf, (uint32_t)sent);
            continue;
        }
        if (sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            continue;
        }
        error = sent == 0 ? -ECONNRESET : -errno;
        LOG_E("TCP client send() failed: sent=%d errno=%d\r\n", sent, sent < 0 ? -error : 0);
        goto exit;
    }
    error = 0;

exit:
    shutdown(socket_fd, SHUT_RDWR);
    close(socket_fd);
    return error;
}

/**
 * @brief Run a single-client blocking TCP server test.
 * @param[in,out] context TCP Socket context.
 * @retval 0 The peer closed normally, a limit was reached, or stop was requested.
 * @return A negative errno value on Socket failure.
 * @post The listening and accepted sockets are closed.
 * @note Receive timeouts allow accept() and recv() to periodically observe
 * stop_requested when the requested Socket options are supported.
 */
static int iperf_tcp_socket_server(iperf_tcp_socket_context_t *context)
{
    bflb_iperf_t *iperf = context->iperf;
    struct sockaddr_in local = { 0 };
    struct sockaddr_in remote = { 0 };
    socklen_t local_len = sizeof(local);
    socklen_t remote_len = sizeof(remote);
    int listen_fd = -1;
    int client_fd = -1;
    int received;
    int enabled = 1;
    int error = -1;

    listen_fd = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listen_fd < 0) {
        LOG_E("TCP server socket() failed: errno=%d\r\n", errno);
        return -errno;
    }
    setsockopt(listen_fd, SOL_SOCKET, SO_REUSEADDR, &enabled, sizeof(enabled));
    error = iperf_tcp_socket_set_timeout(listen_fd);
    if (error != 0) {
        LOG_E("TCP server timeout setup failed: errno=%d\r\n", -error);
        goto exit;
    }

    local.sin_family = AF_INET;
    local.sin_addr.s_addr = iperf->config.local_ip4;
    local.sin_port = htons(iperf->config.port);
    if (bind(listen_fd, (struct sockaddr *)&local, sizeof(local)) < 0 ||
        listen(listen_fd, 1) < 0) {
        error = -errno;
        LOG_E("TCP server bind/listen failed: errno=%d\r\n", -error);
        goto exit;
    }
    iperf_log_server_preamble(iperf);

    /* The receive timeout makes an otherwise blocking accept responsive to stop. */
    while (!iperf->stop_requested) {
        client_fd = accept(listen_fd, (struct sockaddr *)&remote, &remote_len);
        if (client_fd >= 0) {
            break;
        }
        if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
            error = -errno;
            LOG_E("TCP server accept() failed: errno=%d\r\n", -error);
            goto exit;
        }
    }
    if (client_fd < 0) {
        error = 0;
        goto exit;
    }
    close(listen_fd);
    listen_fd = -1;
    if (iperf->config.tos != 0U) {
        int tos = iperf->config.tos;

        if (setsockopt(client_fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)) < 0) {
            error = -errno;
            LOG_E("TCP server setsockopt(IP_TOS) failed: errno=%d\r\n", -error);
            goto exit;
        }
    }

    getsockname(client_fd, (struct sockaddr *)&local, &local_len);
    iperf_log_connection(iperf,
                         local.sin_addr.s_addr, ntohs(local.sin_port),
                         remote.sin_addr.s_addr, ntohs(remote.sin_port));

    error = iperf_tcp_socket_set_timeout(client_fd);
    if (error != 0) {
        LOG_E("TCP server client timeout setup failed: errno=%d\r\n", -error);
        goto exit;
    }
    iperf_test_begin(iperf);

    while (!iperf_limit_reached(iperf, iperf_now_us())) {
        received = recv(client_fd, context->buffer, iperf->config.buffer_len, 0);
        if (received > 0) {
            iperf_account_transfer(iperf, (uint32_t)received);
            continue;
        }
        if (received < 0 &&
            (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR)) {
            continue;
        }
        error = received == 0 ? 0 : -errno;
        if (error < 0) {
            LOG_E("TCP server recv() failed: errno=%d\r\n", -error);
        }
        goto exit;
    }
    error = 0;

exit:
    if (client_fd >= 0) {
        shutdown(client_fd, SHUT_RDWR);
        close(client_fd);
    }
    if (listen_fd >= 0) {
        shutdown(listen_fd, SHUT_RDWR);
        close(listen_fd);
    }
    return error;
}

/**
 * @brief FreeRTOS worker entry for TCP Socket tests.
 * @param[in,out] arg Pointer to an initialized iperf_tcp_socket_context_t.
 * @post The worker buffer is freed and all context references are cleared.
 * @warning iperf_backend_finished() may make the instance and context
 * destroyable. Only self-deletion may follow that call.
 */
static void iperf_tcp_socket_task(void *arg)
{
    iperf_tcp_socket_context_t *context = arg;
    bflb_iperf_t *iperf = context->iperf;
    int error;

    vTaskDelay(pdMS_TO_TICKS(10));

    if (!iperf_backend_started(iperf)) {
        error = 0;
    } else if (iperf->config.role == BFLB_IPERF_ROLE_CLIENT) {
        error = iperf_tcp_socket_client(context);
    } else {
        error = iperf_tcp_socket_server(context);
    }
    free(context->buffer);
    context->buffer = NULL;
    context->task = NULL;
    context->iperf = NULL;

    /* Completion may make context destroyable; only self-delete may follow. */
    iperf_backend_finished(iperf, error);
    vTaskDelete(NULL);
}

/**
 * @brief Allocate resources and start one TCP Socket instance.
 *
 * @param[in,out] iperf Validated instance retained until backend completion.
 * @param[in,out] private_context Zero-initialized TCP Socket private context.
 *
 * @retval 0 The worker task was created successfully.
 * @retval -1 Buffer allocation or task creation failed.
 *
 * @note Success queues asynchronous execution; it does not mean that a TCP
 * connection has already been established.
 */
static int iperf_tcp_socket_start(bflb_iperf_t *iperf, void *private_context)
{
    iperf_tcp_socket_context_t *context = private_context;

    context->iperf = iperf;
    context->buffer = malloc(iperf->config.buffer_len);
    if (context->buffer == NULL) {
        LOG_E("TCP Socket buffer allocation failed: size=%u\r\n", iperf->config.buffer_len);
        return -1;
    }
    memset(context->buffer, 0, iperf->config.buffer_len);
    if (xTaskCreate(iperf_tcp_socket_task, IPERF_TCP_SOCKET_TASK_NAME,
                    IPERF_TCP_SOCKET_TASK_STACK, context,
                    iperf->config.task_priority, &context->task) != pdPASS) {
        LOG_E("TCP Socket task creation failed\r\n");
        free(context->buffer);
        context->buffer = NULL;
        context->iperf = NULL;
        return -1;
    }
    return 0;
}

/**
 * @brief Accept a cooperative stop request for a TCP Socket worker.
 *
 * @param[in] iperf Instance whose stop_requested flag was set by the core.
 * @param[in] private_context TCP Socket private context; unused by this hook.
 *
 * @retval 0 The stop request was accepted.
 *
 * @note This hook does not close sockets or delete the worker. Completion is
 * published after the current blocking operation returns and the worker exits.
 */
static int iperf_tcp_socket_stop(bflb_iperf_t *iperf, void *private_context)
{
    LWIP_UNUSED_ARG(iperf);
    LWIP_UNUSED_ARG(private_context);
    return 0;
}

/** @brief Backend operation table for blocking Classic iPerf2 TCP Socket tests. */
const iperf_backend_ops_t g_iperf_tcp_socket_ops = {
    .context_size = sizeof(iperf_tcp_socket_context_t),
    .start = iperf_tcp_socket_start,
    .stop = iperf_tcp_socket_stop,
};
