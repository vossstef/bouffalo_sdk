/**
 * @file iperf_udp_socket.c
 * @brief Blocking lwIP Socket implementation for Classic iPerf2 UDP tests.
 *
 * Each instance runs in one FreeRTOS worker task. Clients use absolute pacing
 * deadlines and finish with FIN/AckFIN exchange. A server locks onto the first
 * valid setup peer and retains a short duplicate-FIN response window.
 */

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#include <lwip/inet.h>

#define DBG_TAG "IPERF_UDP_SOCKET"
#include "log.h"

#include "iperf_common.h"

/** @brief FreeRTOS worker task name used by UDP Socket instances. */
#define IPERF_UDP_SOCKET_TASK_NAME      "iperf_udp_socket"
/** @brief Worker stack depth passed to xTaskCreate(), in StackType_t units. */
#define IPERF_UDP_SOCKET_TASK_STACK     512U
/** @brief Requested server receive timeout, in milliseconds. */
#define IPERF_UDP_SOCKET_TIMEOUT_MS     200U
/** @brief Maximum client FIN attempts while waiting for AckFIN. */
#define IPERF_UDP_SOCKET_FIN_RETRIES    10U
/** @brief Per-attempt client AckFIN wait time, in milliseconds. */
#define IPERF_UDP_SOCKET_FIN_TIMEOUT_MS 250U
/** @brief Server duplicate-FIN response window, in milliseconds. */
#define IPERF_UDP_SOCKET_ACK_WAIT_MS    1000U

/**
 * @brief Runtime context for one UDP Socket worker.
 * @note The core owns this context; the worker owns buffer after startup.
 */
typedef struct {
    bflb_iperf_t *iperf; /**< Borrowed instance reference valid until completion. */
    TaskHandle_t task;   /**< Worker handle, cleared immediately before completion. */
    uint8_t *buffer;     /**< Worker-owned datagram buffer and mutable wire prefix. */
} iperf_udp_socket_context_t;

/**
 * @brief Attempt to set symmetric receive and send timeouts on a socket.
 * @param[in] socket_fd Socket descriptor.
 * @param[in] timeout_ms Timeout in milliseconds.
 * @note Socket-option failures are intentionally not propagated.
 */
static void iperf_udp_socket_set_timeout(int socket_fd, uint32_t timeout_ms)
{
    struct timeval timeout;

    timeout.tv_sec = timeout_ms / 1000U;
    timeout.tv_usec = (timeout_ms % 1000U) * 1000U;
    setsockopt(socket_fd, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    setsockopt(socket_fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
}

/**
 * @brief Wait until an absolute transmit deadline or a stop request.
 * @param[in] iperf Active instance whose stop flag is observed.
 * @param[in] deadline_us Absolute monotonic deadline in microseconds.
 * @return true when the deadline is reached; false when stop is requested.
 */
static bool iperf_udp_socket_wait_until(bflb_iperf_t *iperf, uint64_t deadline_us)
{
    while (!iperf->stop_requested) {
        uint64_t now_us = iperf_now_us();
        uint64_t wait_ms;

        if (now_us >= deadline_us) {
            return true;
        }

        wait_ms = (deadline_us - now_us + 999ULL) / 1000ULL;
        if (wait_ms > 20ULL) {
            wait_ms = 20ULL;
        }
        vTaskDelay(pdMS_TO_TICKS((uint32_t)wait_ms));
    }
    return false;
}

/**
 * @brief Send UDP FIN datagrams and wait for the server AckFIN report.
 * @param[in,out] context Active UDP Socket context and datagram buffer.
 * @param[in] socket_fd Connected UDP socket descriptor.
 * @param[in] next_id Sequence number following the final data datagram.
 * @retval 0 AckFIN was received or stop was requested.
 * @retval -ETIMEDOUT The configured FIN attempts were exhausted.
 */
static int iperf_udp_socket_wait_report(iperf_udp_socket_context_t *context,
                                        int socket_fd,
                                        uint64_t next_id)
{
    bflb_iperf_t *iperf = context->iperf;
    uint8_t report[BFLB_IPERF_UDP_ACK_SIZE];
    int64_t fin_id = -(int64_t)next_id;
    uint32_t attempt;
    int received;

    for (attempt = 0; attempt < IPERF_UDP_SOCKET_FIN_RETRIES; attempt++) {
        if (iperf->stop_requested) {
            return 0;
        }
        /* Every retry carries the same final sequence boundary. */
        iperf_write_udp_header(context->buffer, fin_id, iperf_now_us());
        send(socket_fd, context->buffer, iperf->config.buffer_len, 0);
        received = recv(socket_fd, report, sizeof(report), 0);
        if (received > 0 && iperf_udp_report_valid(report, (uint16_t)received)) {
            iperf_read_udp_report(iperf, report, (uint16_t)received);
            return 0;
        }
    }
    LOG_E("UDP AckFIN timed out after %u attempts\r\n", IPERF_UDP_SOCKET_FIN_RETRIES);
    return -ETIMEDOUT;
}

/**
 * @brief Send the server AckFIN report and respond to duplicate FIN datagrams.
 * @param[in,out] context Active UDP Socket context and receive buffer.
 * @param[in] socket_fd Connected UDP server socket descriptor.
 * @param[in,out] udp_rx Receiver tracker used to finalize loss accounting.
 * @param[in] fin_id Negative FIN packet ID echoed in the AckFIN report.
 * @retval 0 At least one AckFIN was sent, or stop was requested.
 * @return A negative errno or -EIO if no AckFIN could be sent.
 * @note The response window is extended whenever another FIN is received,
 * allowing the client to recover when a previous AckFIN was lost.
 */
static int iperf_udp_socket_send_report(iperf_udp_socket_context_t *context,
                                        int socket_fd,
                                        iperf_udp_rx_t *udp_rx,
                                        int64_t fin_id)
{
    bflb_iperf_t *iperf = context->iperf;
    uint8_t report[BFLB_IPERF_UDP_ACK_SIZE];
    uint64_t deadline_us;
    int last_error = -EIO;
    int received;
    int sent;
    bool report_sent = false;

    iperf_finish_udp_rx(iperf, udp_rx, (uint64_t)(-fin_id));
    iperf_write_udp_report(report, iperf, fin_id);
    iperf_udp_socket_set_timeout(socket_fd, IPERF_UDP_SOCKET_ACK_WAIT_MS);
    deadline_us = iperf_now_us() + (uint64_t)IPERF_UDP_SOCKET_ACK_WAIT_MS * 1000ULL;

    /* Keep replying while duplicate FINs indicate that an AckFIN was lost. */
    while (!iperf->stop_requested && iperf_now_us() < deadline_us) {
        sent = send(socket_fd, report, sizeof(report), 0);
        if (sent == (int)sizeof(report)) {
            report_sent = true;
        } else {
            last_error = sent < 0 ? -errno : -EIO;
            if (sent >= 0 ||
                (errno != ENOMEM && errno != ENOBUFS && errno != EAGAIN)) {
                break;
            }
            vTaskDelay(1U);
            continue;
        }

        received = recv(socket_fd, context->buffer,
                        iperf->config.buffer_len, 0);
        if (received >= (int)BFLB_IPERF_UDP_HEADER_SIZE &&
            iperf_read_udp_id(context->buffer) < 0) {
            deadline_us = iperf_now_us() +
                          (uint64_t)IPERF_UDP_SOCKET_ACK_WAIT_MS * 1000ULL;
            continue;
        }
    }
    return (iperf->stop_requested || report_sent) ? 0 : last_error;
}

/**
 * @brief Run a paced blocking UDP client test.
 * @param[in,out] context UDP Socket context.
 * @retval 0 The test and FIN/AckFIN exchange completed successfully.
 * @return A negative errno, -EIO, or -ETIMEDOUT on failure.
 * @post The UDP socket is closed; context and its buffer remain allocated.
 */
static int iperf_udp_socket_client(iperf_udp_socket_context_t *context)
{
    bflb_iperf_t *iperf = context->iperf;
    struct sockaddr_in remote = { 0 };
    struct sockaddr_in local = { 0 };
    socklen_t local_len = sizeof(local);
    uint64_t next_deadline_us;
    uint64_t packet_interval_us;
    uint64_t now_us;
    uint64_t packet_id = 2U;
    int socket_fd = -1;
    int sent;
    int error = ERR_OK;

    socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) {
        LOG_E("UDP client socket() failed: errno=%d\r\n", errno);
        return -errno;
    }
    iperf_udp_socket_set_timeout(socket_fd, IPERF_UDP_SOCKET_FIN_TIMEOUT_MS);

    if (iperf->config.local_ip4 != 0U || iperf->config.local_port != 0U) {
        local.sin_family = AF_INET;
        local.sin_addr.s_addr = iperf->config.local_ip4;
        local.sin_port = htons(iperf->config.local_port);
        if (bind(socket_fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
            error = -errno;
            LOG_E("UDP client bind() failed: errno=%d\r\n", -error);
            goto exit;
        }
    }
    if (iperf->config.tos != 0U) {
        int tos = iperf->config.tos;
        setsockopt(socket_fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos));
    }

    remote.sin_family = AF_INET;
    remote.sin_addr.s_addr = iperf->config.remote_ip4;
    remote.sin_port = htons(iperf->config.port);
    iperf_log_client_preamble(iperf);

    if (connect(socket_fd, (struct sockaddr *)&remote, sizeof(remote)) < 0) {
        error = -errno;
        LOG_E("UDP client connect() failed: errno=%d\r\n", -error);
        goto exit;
    }

    getsockname(socket_fd, (struct sockaddr *)&local, &local_len);
    iperf_log_connection(iperf,
                         local.sin_addr.s_addr, ntohs(local.sin_port),
                         remote.sin_addr.s_addr, ntohs(remote.sin_port));

    /* Establish normal mode and advertise the 64-bit sequence layout. */
    iperf_write_udp_client_header(context->buffer, iperf, iperf_now_us());
    sent = send(socket_fd, context->buffer, iperf->config.buffer_len, 0);
    if (sent != (int)iperf->config.buffer_len) {
        error = sent < 0 ? -errno : -EIO;
        LOG_E("UDP client setup send failed: sent=%d expected=%u errno=%d\r\n",
              sent, iperf->config.buffer_len, sent < 0 ? -error : 0);
        goto exit;
    }

    iperf_test_begin(iperf);

    iperf_account_transfer(iperf, (uint32_t)sent);
    uint32_t rate_bps = iperf->config.bandwidth_bps ? iperf->config.bandwidth_bps : BFLB_IPERF_DEFAULT_UDP_RATE_BPS;
    packet_interval_us = (uint64_t)iperf->config.buffer_len * 8ULL * 1000000ULL / rate_bps;
    next_deadline_us = iperf->stats.start_us + packet_interval_us;

    while (!iperf_limit_reached(iperf, iperf_now_us())) {
        uint16_t length = iperf->config.buffer_len;

        /* No receive data is expected before FIN; release any stray packets. */
        while (recv(socket_fd, context->buffer, iperf->config.buffer_len, MSG_DONTWAIT) >= 0) {
        }

        /* Absolute deadlines avoid accumulating delay and send-call execution time. */
        now_us = iperf_now_us();
        if (now_us < next_deadline_us && !iperf_udp_socket_wait_until(iperf, next_deadline_us)) {
            break;
        }

        if (iperf->config.amount_bytes != 0U) {
            if (iperf->stats.bytes < iperf->config.amount_bytes) {
                length = (uint16_t)LWIP_MIN((uint64_t)length, (iperf->config.amount_bytes - iperf->stats.bytes));
            } else {
                length = 0;
            }
        }
        if (length < BFLB_IPERF_UDP_HEADER_SIZE) {
            break;
        }

        /* Keep the first datagram's stateless settings area; update sequence/time only. */
        iperf_write_udp_header(context->buffer, (int64_t)packet_id, iperf_now_us());
        sent = send(socket_fd, context->buffer, length, 0);
        if (sent == (int)length) {
            iperf_account_transfer(iperf, (uint32_t)sent);
            packet_id++;
            next_deadline_us += packet_interval_us;
            continue;
        }
        if (sent < 0 && (errno == ENOMEM || errno == ENOBUFS || errno == EAGAIN)) {
            vTaskDelay(1U);
            continue;
        }
        error = sent < 0 ? -errno : -EIO;
        LOG_E("UDP client data send failed: id=%llu sent=%d expected=%u errno=%d\r\n",
              (unsigned long long)packet_id, sent, length, sent < 0 ? -error : 0);
        goto exit;
    }

    iperf_test_end(iperf);

    if (!iperf->stop_requested) {
        error = iperf_udp_socket_wait_report(context, socket_fd, packet_id);
    }

exit:
    close(socket_fd);
    return error;
}

/**
 * @brief Run a single-peer blocking UDP server test.
 * @param[in,out] context UDP Socket context.
 * @retval 0 The test completed or a stop request was observed.
 * @return A negative errno value on Socket failure.
 * @note Only a valid packet-ID-one setup datagram can select the server peer.
 * Later datagrams from other peers are rejected by the connected UDP socket.
 */
static int iperf_udp_socket_server(iperf_udp_socket_context_t *context)
{
    bflb_iperf_t *iperf = context->iperf;
    struct sockaddr_in local = { 0 };
    struct sockaddr_in remote = { 0 };
    socklen_t remote_len = sizeof(remote);
    iperf_udp_rx_t udp_rx;
    int64_t packet_id;
    int socket_fd = -1;
    int received;
    int error = ERR_OK;
    uint64_t received_us;
    iperf_udp_setup_t setup_type;
    uint8_t peer_set = 0U;

    socket_fd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (socket_fd < 0) {
        LOG_E("UDP server socket() failed: errno=%d\r\n", errno);
        return -errno;
    }
    iperf_udp_socket_set_timeout(socket_fd, IPERF_UDP_SOCKET_TIMEOUT_MS);

    local.sin_family = AF_INET;
    local.sin_addr.s_addr = iperf->config.local_ip4;
    local.sin_port = htons(iperf->config.port);
    if (bind(socket_fd, (struct sockaddr *)&local, sizeof(local)) < 0) {
        error = -errno;
        LOG_E("UDP server bind() failed: errno=%d\r\n", -error);
        goto exit;
    }
    iperf_udp_rx_init(&udp_rx);
    iperf_log_server_preamble(iperf);

    while (!iperf_limit_reached(iperf, iperf_now_us())) {
        received = recvfrom(socket_fd, context->buffer, iperf->config.buffer_len, 0,
                            (struct sockaddr *)&remote, &remote_len);
        received_us = iperf_now_us();
        if (received < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) {
                continue;
            }
            error = -errno;
            LOG_E("UDP server recvfrom() failed: errno=%d\r\n", -error);
            goto exit;
        }
        if (received < (int)BFLB_IPERF_UDP_HEADER_SIZE) {
            continue;
        }

        packet_id = iperf_read_udp_id(context->buffer);
        /* Only a valid setup packet may claim this single-peer server session. */
        if (peer_set == 0U) {
            setup_type = iperf_udp_client_setup_type(context->buffer,
                                                     (uint16_t)received);
            if (setup_type == IPERF_UDP_SETUP_INVALID) {
                continue;
            }
            if (connect(socket_fd, (struct sockaddr *)&remote, remote_len) < 0) {
                error = -errno;
                LOG_E("UDP server connect() failed: errno=%d\r\n", -error);
                goto exit;
            }
            peer_set = 1U;
            iperf_log_connection(iperf,
                                 iperf->config.local_ip4, iperf->config.port,
                                 remote.sin_addr.s_addr, ntohs(remote.sin_port));
            iperf_test_begin(iperf);
            udp_rx.next_id = 1U;
        }
        if (packet_id == 0) {
            continue;
        }
        if (packet_id < 0) {
            error = iperf_udp_socket_send_report(context, socket_fd, &udp_rx, packet_id);
            if (error < 0) {
                LOG_E("UDP server AckFIN send failed: error=%d\r\n", error);
            }
            break;
        }

        iperf_udp_rx_account(iperf, &udp_rx, packet_id,
                             iperf_read_udp_timestamp(context->buffer),
                             received_us, (uint16_t)received);
    }

exit:
    close(socket_fd);
    return error;
}

/**
 * @brief FreeRTOS worker entry for UDP Socket tests.
 * @param[in,out] arg Pointer to an initialized iperf_udp_socket_context_t.
 * @post The worker buffer is freed and all context references are cleared.
 * @warning iperf_backend_finished() may make the instance and context
 * destroyable. Only self-deletion may follow that call.
 */
static void iperf_udp_socket_task(void *arg)
{
    iperf_udp_socket_context_t *context = arg;
    bflb_iperf_t *iperf = context->iperf;
    int error;

    vTaskDelay(pdMS_TO_TICKS(10U));

    if (!iperf_backend_started(iperf)) {
        error = 0;
    } else if (iperf->config.role == BFLB_IPERF_ROLE_CLIENT) {
        error = iperf_udp_socket_client(context);
    } else {
        error = iperf_udp_socket_server(context);
    }

    free(context->buffer);
    context->buffer = NULL;
    context->task = NULL;
    context->iperf = NULL;

    iperf_backend_finished(iperf, error);
    vTaskDelete(NULL);
}

/**
 * @brief Allocate resources and start one UDP Socket instance.
 *
 * @param[in,out] iperf Validated instance retained until backend completion.
 * @param[in,out] private_context Zero-initialized UDP Socket private context.
 *
 * @retval 0 The worker task was created successfully.
 * @retval -1 Buffer allocation or task creation failed.
 *
 * @note Success queues asynchronous execution; it does not mean traffic has
 * already started.
 */
static int iperf_udp_socket_start(bflb_iperf_t *iperf, void *private_context)
{
    iperf_udp_socket_context_t *context = private_context;

    context->iperf = iperf;
    context->buffer = malloc(iperf->config.buffer_len);
    if (context->buffer == NULL) {
        LOG_E("UDP Socket buffer allocation failed: size=%u\r\n", iperf->config.buffer_len);
        return -1;
    }
    /* Payload remains constant; only the UDP prefix/control area is overwritten. */
    memset(context->buffer, '0', iperf->config.buffer_len);
    if (xTaskCreate(iperf_udp_socket_task, IPERF_UDP_SOCKET_TASK_NAME,
                    IPERF_UDP_SOCKET_TASK_STACK, context,
                    iperf->config.task_priority, &context->task) != pdPASS) {
        LOG_E("UDP Socket task creation failed\r\n");
        free(context->buffer);
        context->buffer = NULL;
        context->iperf = NULL;
        return -1;
    }
    return 0;
}

/**
 * @brief Accept a cooperative stop request for a UDP Socket worker.
 *
 * @param[in] iperf Instance whose stop_requested flag was set by the core.
 * @param[in] private_context UDP Socket private context; unused by this hook.
 *
 * @retval 0 The stop request was accepted.
 *
 * @note Completion is published by the worker after its current pacing or
 * Socket wait returns; this hook does not force-delete the task.
 */
static int iperf_udp_socket_stop(bflb_iperf_t *iperf, void *private_context)
{
    LWIP_UNUSED_ARG(iperf);
    LWIP_UNUSED_ARG(private_context);
    return 0;
}

/** @brief Backend operation table for blocking Classic iPerf2 UDP Socket tests. */
const iperf_backend_ops_t g_iperf_udp_socket_ops = {
    .context_size = sizeof(iperf_udp_socket_context_t),
    .start = iperf_udp_socket_start,
    .stop = iperf_udp_socket_stop,
};
