/**
 * @file iperf_common.c
 * @brief Common Classic iPerf2 wire-format, statistics, and reporting helpers.
 */

#include <string.h>

#include <bflb_irq.h>
#include <bflb_mtimer.h>
#include <lwip/ip4_addr.h>

#define DBG_TAG "IPERF"
#include "log.h"

#include "iperf_common.h"

/** @brief Permanent zero-filled payload referenced by Raw backend sends. */
const uint8_t g_iperf_raw_payload[BFLB_IPERF_RAW_TCP_BUFFER_LEN];

static void iperf_report_periodic(bflb_iperf_t *iperf, uint64_t now_us);

/**
 * @brief Read the monotonic hardware timer.
 * @return Current monotonic timestamp in microseconds.
 */
uint64_t iperf_now_us(void)
{
    return bflb_mtimer_get_time_us();
}

/**
 * @brief Encode an unsigned 32-bit integer in network byte order.
 * @param[out] buffer Destination containing at least four writable bytes.
 * @param[in] value Host-order value to encode.
 */
void iperf_put_u32(uint8_t *buffer, uint32_t value)
{
    buffer[0] = (uint8_t)(value >> 24);
    buffer[1] = (uint8_t)(value >> 16);
    buffer[2] = (uint8_t)(value >> 8);
    buffer[3] = (uint8_t)value;
}

/**
 * @brief Decode an unsigned 32-bit integer in network byte order.
 * @param[in] buffer Source containing at least four readable bytes.
 * @return Decoded host-order value.
 */
uint32_t iperf_get_u32(const uint8_t *buffer)
{
    return ((uint32_t)buffer[0] << 24) |
           ((uint32_t)buffer[1] << 16) |
           ((uint32_t)buffer[2] << 8) |
           (uint32_t)buffer[3];
}

/**
 * @brief Encode the modern Classic iPerf2 UDP sequence and timestamp prefix.
 * @param[out] buffer Destination containing at least
 * BFLB_IPERF_UDP_HEADER_SIZE writable bytes.
 * @param[in] id Signed packet identifier; a negative value denotes FIN.
 * @param[in] now_us Packet timestamp in microseconds.
 */
void iperf_write_udp_header(uint8_t *buffer, int64_t id, uint64_t now_us)
{
    uint64_t wire_id = (uint64_t)id;

    iperf_put_u32(buffer, (uint32_t)wire_id);
    iperf_put_u32(buffer + 4, (uint32_t)(now_us / 1000000ULL));
    iperf_put_u32(buffer + 8, (uint32_t)(now_us % 1000000ULL));
    iperf_put_u32(buffer + 12, (uint32_t)(wire_id >> 32));
}

/**
 * @brief Decode a signed 64-bit packet identifier from a modern UDP prefix.
 * @param[in] buffer Source containing at least
 * BFLB_IPERF_UDP_HEADER_SIZE readable bytes.
 * @return Signed packet identifier; a negative value denotes FIN.
 */
int64_t iperf_read_udp_id(const uint8_t *buffer)
{
    uint64_t wire_id = (uint64_t)iperf_get_u32(buffer) |
                       ((uint64_t)iperf_get_u32(buffer + 12) << 32);

    return (int64_t)wire_id;
}

/**
 * @brief Decode the sender timestamp from a modern Classic iPerf2 UDP prefix.
 * @param[in] buffer Source containing at least
 * BFLB_IPERF_UDP_HEADER_SIZE readable bytes.
 * @return Sender timestamp in microseconds.
 */
uint64_t iperf_read_udp_timestamp(const uint8_t *buffer)
{
    return (uint64_t)iperf_get_u32(buffer + 4) * 1000000ULL +
           iperf_get_u32(buffer + 8);
}

/**
 * @brief Encode a normal-mode UDP client setup header with packet ID one.
 * @param[out] buffer Destination containing at least
 * BFLB_IPERF_UDP_CLIENT_HEADER_SIZE writable bytes.
 * @param[in] iperf Client instance supplying normalized test settings.
 * @param[in] now_us Packet timestamp in microseconds.
 * @note Byte amount is saturated to INT32_MAX; duration is encoded as a
 * negative number of centiseconds, as required by Classic iPerf2.
 */
void iperf_write_udp_client_header(uint8_t *buffer,
                                   const bflb_iperf_t *iperf,
                                   uint64_t now_us)
{
    uint32_t flags = BFLB_IPERF_HEADER_SEQNO64B |
                     BFLB_IPERF_HEADER_EXTEND |
                     BFLB_IPERF_HEADER_LEN_BIT |
                     ((BFLB_IPERF_UDP_CLIENT_HEADER_SIZE << 1U) &
                      BFLB_IPERF_HEADER_LEN_MASK);
    uint32_t rate_bps = iperf->config.bandwidth_bps ? iperf->config.bandwidth_bps : BFLB_IPERF_DEFAULT_UDP_RATE_BPS;
    uint32_t amount;
    uint64_t duration_cs;

    /* Upstream normal mode uses packet ID one for its first settings datagram. */
    memset(buffer, 0, BFLB_IPERF_UDP_CLIENT_HEADER_SIZE);
    iperf_write_udp_header(buffer, 1, now_us);
    iperf_put_u32(buffer + 16, flags);
    iperf_put_u32(buffer + 20, 1U);
    iperf_put_u32(buffer + 24, iperf->config.port);
    iperf_put_u32(buffer + 28, iperf->config.buffer_len);
    if (iperf->config.amount_bytes != 0U) {
        amount = (uint32_t)((iperf->config.amount_bytes > INT32_MAX) ? INT32_MAX : iperf->config.amount_bytes);
    } else {
        duration_cs = (uint64_t)iperf->config.duration_s * 100ULL;
        if (duration_cs > (uint64_t)INT32_MAX) {
            duration_cs = (uint64_t)INT32_MAX;
        }
        amount = (uint32_t)(-(int32_t)duration_cs);
    }
    iperf_put_u32(buffer + 36, amount);
    iperf_put_u32(buffer + 64, rate_bps);
}

/**
 * @brief Identify a supported normal-mode UDP client setup header.
 * @param[in] buffer Received datagram prefix.
 * @param[in] length Number of readable bytes in buffer.
 * @retval IPERF_UDP_SETUP_LEGACY A sequence-64 normal-mode header without an
 * extended settings area.
 * @retval IPERF_UDP_SETUP_MODERN A complete extended normal-mode header.
 * @retval IPERF_UDP_SETUP_INVALID The header is truncated or unsupported.
 */
iperf_udp_setup_t iperf_udp_client_setup_type(const uint8_t *buffer,
                                              uint16_t length)
{
    int64_t packet_id;
    uint32_t flags;

    if (length < BFLB_IPERF_UDP_HEADER_SIZE + sizeof(flags)) {
        return IPERF_UDP_SETUP_INVALID;
    }
    packet_id = iperf_read_udp_id(buffer);
    if (packet_id != 0 && packet_id != 1) {
        return IPERF_UDP_SETUP_INVALID;
    }

    flags = iperf_get_u32(buffer + BFLB_IPERF_UDP_HEADER_SIZE);
    if ((flags & BFLB_IPERF_HEADER_SEQNO64B) == 0U) {
        return IPERF_UDP_SETUP_INVALID;
    }
    if ((flags & BFLB_IPERF_HEADER_EXTEND) == 0U) {
        return IPERF_UDP_SETUP_LEGACY;
    }
    if (packet_id != 1 || length < BFLB_IPERF_UDP_CLIENT_HEADER_SIZE) {
        return IPERF_UDP_SETUP_INVALID;
    }
    return IPERF_UDP_SETUP_MODERN;
}

/**
 * @brief Validate a modern or legacy UDP AckFIN report.
 * @param[in] buffer Received report prefix.
 * @param[in] length Number of readable bytes in buffer.
 * @retval true A supported version-1 server report is present.
 * @retval false The report is truncated or unsupported.
 */
bool iperf_udp_report_valid(const uint8_t *buffer, uint16_t length)
{
    if (length >= BFLB_IPERF_UDP_ACK_SIZE &&
        (iperf_get_u32(buffer + BFLB_IPERF_UDP_HEADER_SIZE) &
         BFLB_IPERF_HEADER_VERSION1) != 0U) {
        return true;
    }
    if (length >= BFLB_IPERF_UDP_LEGACY_ACK_SIZE &&
        (iperf_get_u32(buffer + BFLB_IPERF_UDP_LEGACY_HEADER_SIZE) &
         BFLB_IPERF_HEADER_VERSION1) != 0U) {
        return true;
    }
    return false;
}

/**
 * @brief Initialize receiver-side UDP sequence and jitter tracking.
 * @param[out] tracker Tracker to reset.
 * @post All counters are zero and no previous transit sample is recorded.
 */
void iperf_udp_rx_init(iperf_udp_rx_t *tracker)
{
    memset(tracker, 0, sizeof(*tracker));
    tracker->last_transit_us = INT64_MIN;
}

/**
 * @brief Account one received UDP data datagram.
 * @param[in,out] iperf Instance whose transfer statistics are updated.
 * @param[in,out] tracker Receiver sequence and jitter tracker.
 * @param[in] id Nonnegative UDP packet identifier.
 * @param[in] sent_us Sender timestamp in microseconds.
 * @param[in] received_us Local datagram arrival time in microseconds.
 * @param[in] length Received UDP payload length in bytes.
 * @note Jitter uses Classic iPerf2's RFC 3550-style 1/16 smoothing. Sequence
 * gaps are treated as loss candidates and late packets cancel prior gaps.
 */
void iperf_udp_rx_account(bflb_iperf_t *iperf, iperf_udp_rx_t *tracker,
                          int64_t id, uint64_t sent_us, uint64_t received_us,
                          uint16_t length)
{
    iperf_stats_t *stats = &iperf->stats;
    int64_t transit_us = (int64_t)(received_us - sent_us);
    int64_t delta_us;
    int64_t jitter_delta;
    uint64_t sequence = (uint64_t)id;
    uint64_t gap;

    stats->bytes += length;
    stats->datagrams++;

    /* Apply the RFC 3550-style 1/16 smoothing used by Classic iPerf2 jitter. */
    if (tracker->last_transit_us != INT64_MIN) {
        delta_us = transit_us - tracker->last_transit_us;
        if (delta_us < 0) {
            delta_us = -delta_us;
        }
        jitter_delta = delta_us - (int64_t)(tracker->jitter_q4 >> 4);
        tracker->jitter_q4 = (uint32_t)((int64_t)tracker->jitter_q4 + jitter_delta);
        stats->jitter_us = tracker->jitter_q4 >> 4;
    }
    tracker->last_transit_us = transit_us;

    /* Sequence gaps add loss candidates; late packets cancel gaps as reordering. */
    if (sequence >= tracker->next_id) {
        gap = sequence - tracker->next_id;
        tracker->gap_count += (uint32_t)((gap > (uint64_t)UINT32_MAX) ? (uint64_t)UINT32_MAX : gap);
        tracker->next_id = sequence + 1U;
    } else {
        stats->out_of_order++;
    }
    stats->lost = (tracker->gap_count > stats->out_of_order) ?
                      (tracker->gap_count - stats->out_of_order) :
                      0U;
    iperf_report_periodic(iperf, iperf_now_us());
}

/**
 * @brief Finalize receiver loss accounting when a UDP FIN arrives.
 * @param[in,out] iperf Instance whose end time and loss statistics are updated.
 * @param[in,out] tracker Receiver sequence tracker.
 * @param[in] next_id Sequence number following the final client data packet.
 * @note Missing tail datagrams identified by next_id are included in the final
 * loss total.
 */
void iperf_finish_udp_rx(bflb_iperf_t *iperf,
                         iperf_udp_rx_t *tracker,
                         uint64_t next_id)
{
    iperf_stats_t *stats = &iperf->stats;
    uint64_t expected_datagrams;
    uint64_t gap;

    iperf_test_end(iperf);
    /* The FIN identifier carries the sequence number after the last data packet. */
    if (next_id > tracker->next_id) {
        gap = next_id - tracker->next_id;
        tracker->gap_count += (uint32_t)((gap > (uint64_t)UINT32_MAX) ? (uint64_t)UINT32_MAX : gap);
        tracker->next_id = next_id;
    }
    stats->lost = (tracker->gap_count > stats->out_of_order) ?
                      (tracker->gap_count - stats->out_of_order) :
                      0U;

    /* IDs start at one; FIN carries the ID following the final data packet. */
    expected_datagrams = next_id > 0U ? next_id - 1U : 0U;
    stats->datagrams = (uint32_t)LWIP_MIN(expected_datagrams, (uint64_t)UINT32_MAX);
}

/**
 * @brief Encode a modern UDP server AckFIN report.
 * @param[out] buffer Destination containing at least
 * BFLB_IPERF_UDP_ACK_SIZE writable bytes.
 * @param[in] iperf Instance supplying finalized server statistics.
 * @param[in] fin_id Negative FIN packet identifier echoed to the client.
 */
void iperf_write_udp_report(uint8_t *buffer,
                            const bflb_iperf_t *iperf,
                            int64_t fin_id)
{
    const iperf_stats_t *stats = &iperf->stats;
    uint64_t end_us = stats->end_us == 0U ? iperf_now_us() : stats->end_us;
    uint64_t duration_us = end_us - stats->start_us;

    /* AckFIN is a 16-byte UDP prefix followed by the base 40-byte report. */
    memset(buffer, 0, BFLB_IPERF_UDP_ACK_SIZE);
    iperf_write_udp_header(buffer, fin_id, iperf_now_us());
    iperf_put_u32(buffer + 16, BFLB_IPERF_HEADER_VERSION1);
    iperf_put_u32(buffer + 20, (uint32_t)(stats->bytes >> 32));
    iperf_put_u32(buffer + 24, (uint32_t)stats->bytes);
    iperf_put_u32(buffer + 28, (uint32_t)(duration_us / 1000000ULL));
    iperf_put_u32(buffer + 32, (uint32_t)(duration_us % 1000000ULL));
    iperf_put_u32(buffer + 36, stats->lost);
    iperf_put_u32(buffer + 40, stats->out_of_order);
    iperf_put_u32(buffer + 44, stats->datagrams);
    iperf_put_u32(buffer + 48, stats->jitter_us / 1000000U);
    iperf_put_u32(buffer + 52, stats->jitter_us % 1000000U);
}

/**
 * @brief Import statistics from a modern or legacy UDP AckFIN report.
 * @param[in,out] iperf Client instance whose UDP result is updated.
 * @param[in] buffer Received report prefix.
 * @param[in] length Number of readable bytes in buffer.
 * @note Unsupported or truncated reports are ignored without modifying the
 * imported server statistics.
 */
void iperf_read_udp_report(bflb_iperf_t *iperf,
                           const uint8_t *buffer,
                           uint16_t length)
{
    iperf_stats_t *stats = &iperf->stats;
    uint16_t offset;

    if (!iperf_udp_report_valid(buffer, length)) {
        return;
    }
    if (length >= BFLB_IPERF_UDP_ACK_SIZE &&
        (iperf_get_u32(buffer + BFLB_IPERF_UDP_HEADER_SIZE) &
         BFLB_IPERF_HEADER_VERSION1) != 0U) {
        offset = BFLB_IPERF_UDP_HEADER_SIZE;
    } else {
        offset = BFLB_IPERF_UDP_LEGACY_HEADER_SIZE;
    }
    stats->lost = iperf_get_u32(buffer + offset + 20U);
    stats->out_of_order = iperf_get_u32(buffer + offset + 24U);
    stats->datagrams = iperf_get_u32(buffer + offset + 28U);
    stats->jitter_us = iperf_get_u32(buffer + offset + 32U) * 1000000U +
                       iperf_get_u32(buffer + offset + 36U);
    stats->udp_report_received = true;
}

/**
 * @brief Print the PC iPerf-style client connection preamble.
 * @param[in] iperf Client instance supplying protocol, destination, datagram,
 * bandwidth, and TCP window settings.
 */
void iperf_log_client_preamble(const bflb_iperf_t *iperf)
{
    ip4_addr_t remote_ip4 = { .addr = iperf->config.remote_ip4 };

    LOG_RI("\r\n");
    LOG_RI("------------------------------------------------------------\r\n");
    LOG_I("Client connecting to %s, %s port %u\r\n",
          ip4addr_ntoa(&remote_ip4),
          iperf->config.proto == BFLB_IPERF_PROTO_UDP ? "UDP" : "TCP",
          iperf->config.port);

    if (iperf->config.proto == BFLB_IPERF_PROTO_UDP) {
        uint32_t bandwidth_bps = iperf->config.bandwidth_bps;
        uint32_t bandwidth_mbps_x10;

        if (bandwidth_bps == 0U) {
            bandwidth_bps = BFLB_IPERF_DEFAULT_UDP_RATE_BPS;
        }
        bandwidth_mbps_x10 = (uint32_t)(((uint64_t)bandwidth_bps + 50000ULL) /
                                        100000ULL);
        LOG_I("Sending %u byte datagrams\r\n", iperf->config.buffer_len);
        LOG_I("UDP target bandwidth: %u.%01u Mbits/sec\r\n",
              bandwidth_mbps_x10 / 10U,
              bandwidth_mbps_x10 % 10U);
    } else {
        uint32_t window_kbytes_x10 = ((uint32_t)TCP_WND * 10U + 512U) / 1024U;

        LOG_I("TCP window size: %u.%01u KByte (configured)\r\n",
              window_kbytes_x10 / 10U,
              window_kbytes_x10 % 10U);
    }
    LOG_RI("------------------------------------------------------------\r\n");
}

/**
 * @brief Print the PC iPerf-style server listening preamble.
 * @param[in] iperf Server instance supplying protocol, listen port, datagram,
 * and TCP window settings.
 */
void iperf_log_server_preamble(const bflb_iperf_t *iperf)
{
    LOG_RI("\r\n");
    LOG_RI("------------------------------------------------------------\r\n");
    LOG_I("Server listening on %s port %u\r\n",
          iperf->config.proto == BFLB_IPERF_PROTO_UDP ? "UDP" : "TCP",
          iperf->config.port);

    if (iperf->config.proto == BFLB_IPERF_PROTO_UDP) {
        LOG_I("Receiving up to %u byte datagrams\r\n",
              iperf->config.buffer_len);
    } else {
        uint32_t window_kbytes_x10 = ((uint32_t)TCP_WND * 10U + 512U) / 1024U;

        LOG_I("TCP window size: %u.%01u KByte (configured)\r\n",
              window_kbytes_x10 / 10U,
              window_kbytes_x10 % 10U);
    }
    LOG_RI("------------------------------------------------------------\r\n");
}

/**
 * @brief Print an established endpoint tuple and report column heading.
 * @param[in] iperf Active instance supplying the reporting context.
 * @param[in] local_ip4 Local IPv4 address in network byte order; zero is
 * rendered as an unspecified wildcard.
 * @param[in] local_port Local port in host byte order.
 * @param[in] remote_ip4 Peer IPv4 address in network byte order.
 * @param[in] remote_port Peer port in host byte order.
 */
void iperf_log_connection(const bflb_iperf_t *iperf,
                          uint32_t local_ip4,
                          uint16_t local_port,
                          uint32_t remote_ip4,
                          uint16_t remote_port)
{
    char local_text[IP4ADDR_STRLEN_MAX];
    char remote_text[IP4ADDR_STRLEN_MAX];
    ip4_addr_t local = { .addr = local_ip4 };
    ip4_addr_t remote = { .addr = remote_ip4 };

    if (local_ip4 == 0U) {
        strcpy(local_text, "*");
    } else {
        ip4addr_ntoa_r(&local, local_text, sizeof(local_text));
    }
    ip4addr_ntoa_r(&remote, remote_text, sizeof(remote_text));
    LOG_I("[  1] local %s port %u connected with %s port %u\r\n",
          local_text, local_port, remote_text, remote_port);
    LOG_I("[ ID] Interval       Transfer     Bandwidth\r\n");
}

/**
 * @brief Record the test start time and initialize interval-report baselines.
 * @param[in,out] iperf Instance whose statistics are initialized.
 * @note This operation is idempotent; an existing start timestamp is retained.
 */
void iperf_test_begin(bflb_iperf_t *iperf)
{
    iperf_stats_t *stats = &iperf->stats;
    uintptr_t irq_flags;

    irq_flags = bflb_irq_save();
    if (stats->start_us != 0U) {
        bflb_irq_restore(irq_flags);
        return;
    }
    stats->start_us = iperf_now_us();
    stats->last_report_us = stats->start_us;
    stats->last_report_bytes = 0U;
    bflb_irq_restore(irq_flags);
}

/**
 * @brief Record the test end time once.
 * @param[in,out] iperf Instance whose statistics are finalized.
 * @note No end time is recorded before the test starts, and an existing end
 * timestamp is retained.
 */
void iperf_test_end(bflb_iperf_t *iperf)
{
    uintptr_t irq_flags = bflb_irq_save();

    if (iperf->stats.start_us != 0U && iperf->stats.end_us == 0U) {
        iperf->stats.end_us = iperf_now_us();
    }
    bflb_irq_restore(irq_flags);
}

/**
 * @brief Emit an interval report when the configured deadline has elapsed.
 * @param[in,out] iperf Active instance whose report baselines are maintained.
 * @param[in] now_us Current monotonic timestamp in microseconds.
 * @note No report is emitted when interval reporting is disabled or the test
 * has not started.
 */
static void iperf_report_periodic(bflb_iperf_t *iperf, uint64_t now_us)
{
    iperf_stats_t *stats = &iperf->stats;
    uint64_t interval_us;
    uint64_t interval_bytes;
    uint64_t interval_start_ds;
    uint64_t interval_end_ds;
    uint64_t bps;
    uint64_t mbytes_x100;
    uint32_t mbps_x10;

    if (iperf->config.interval_s == 0U || stats->start_us == 0U) {
        return;
    }

    interval_us = now_us - stats->last_report_us;
    if (interval_us < (uint64_t)iperf->config.interval_s * 1000000ULL) {
        return;
    }

    /* Interval bandwidth uses deltas so previous traffic is not counted again. */
    interval_bytes = stats->bytes - stats->last_report_bytes;
    bps = interval_bytes * 8ULL * 1000000ULL / interval_us;
    mbytes_x100 = (interval_bytes * 100ULL + (512ULL * 1024ULL)) / (1024ULL * 1024ULL);
    mbps_x10 = (uint32_t)((bps + 50000ULL) / 100000ULL);
    interval_start_ds = (stats->last_report_us - stats->start_us + 50000ULL) / 100000ULL;
    interval_end_ds = (now_us - stats->start_us + 50000ULL) / 100000ULL;

    LOG_I("[%u] %2llu.%01llu-%2llu.%01llu sec  %llu.%02llu MByte  %3u.%01u Mbits/sec\r\n",
          0U,
          (unsigned long long)(interval_start_ds / 10ULL),
          (unsigned long long)(interval_start_ds % 10ULL),
          (unsigned long long)(interval_end_ds / 10ULL),
          (unsigned long long)(interval_end_ds % 10ULL),
          (unsigned long long)(mbytes_x100 / 100ULL),
          (unsigned long long)(mbytes_x100 % 100ULL),
          mbps_x10 / 10U,
          mbps_x10 % 10U);
    stats->last_report_us = now_us;
    stats->last_report_bytes = stats->bytes;
}

/**
 * @brief Account one successful transfer and emit any due interval report.
 * @param[in,out] iperf Active instance whose transfer statistics are updated.
 * @param[in] length Number of payload bytes to add.
 * @note UDP transfers also increment the datagram count; TCP transfers only
 * update the byte count.
 */
void iperf_account_transfer(bflb_iperf_t *iperf, uint32_t length)
{
    iperf_account_transfer_at(iperf, length, iperf_now_us());
}

/**
 * @brief Account one transfer using an already captured report timestamp.
 * @param[in,out] iperf Active instance.
 * @param[in] length Number of payload bytes to add.
 * @param[in] now_us Timestamp associated with the accounted data.
 */
void iperf_account_transfer_at(bflb_iperf_t *iperf, uint32_t length,
                               uint64_t now_us)
{
    iperf->stats.bytes += length;
    if (iperf->config.proto == BFLB_IPERF_PROTO_UDP) {
        iperf->stats.datagrams++;
    }
    iperf_report_periodic(iperf, now_us);
}

/**
 * @brief Finalize timing and print the aggregate transfer report.
 * @param[in,out] iperf Completed instance whose final statistics are reported.
 * @note UDP servers print locally measured receive statistics. UDP clients
 * print jitter, loss, and out-of-order counts only after importing a valid
 * AckFIN report. An instance that never started produces no report.
 */
void iperf_test_report_finish(bflb_iperf_t *iperf)
{
    iperf_stats_t *stats = &iperf->stats;
    uint64_t duration_us;
    uint64_t duration_ds;
    uint64_t bps;
    uint64_t mbytes_x100;
    uint32_t mbps_x10;

    if (stats->start_us == 0U) {
        return;
    }
    iperf_test_end(iperf);
    duration_us = stats->end_us - stats->start_us;
    duration_ds = (duration_us + 50000ULL) / 100000ULL;
    bps = duration_us == 0U ? 0U : (stats->bytes * 8ULL * 1000000ULL / duration_us);
    mbytes_x100 = (stats->bytes * 100ULL + (512ULL * 1024ULL)) / (1024ULL * 1024ULL);
    mbps_x10 = (uint32_t)((bps + 50000ULL) / 100000ULL);

    LOG_I("[SUM] 0.0-%llu.%01llu sec  %llu.%02llu MByte  %u.%01u Mbits/sec\r\n",
          (unsigned long long)(duration_ds / 10ULL),
          (unsigned long long)(duration_ds % 10ULL),
          (unsigned long long)(mbytes_x100 / 100ULL),
          (unsigned long long)(mbytes_x100 % 100ULL),
          mbps_x10 / 10U,
          mbps_x10 % 10U);

    if (iperf->config.proto == BFLB_IPERF_PROTO_UDP &&
        (iperf->config.role == BFLB_IPERF_ROLE_SERVER ||
         stats->udp_report_received)) {
        uint32_t loss_percent_x10 = 0;
        if (stats->datagrams) {
            loss_percent_x10 = (uint32_t)(((uint64_t)stats->lost * 1000ULL + stats->datagrams / 2U) / stats->datagrams);
        }
        LOG_I("    jitter %u.%03u ms  lost %u/%u (%u.%01u%%)  out-of-order %u\r\n",
              stats->jitter_us / 1000U,
              stats->jitter_us % 1000U,
              stats->lost,
              stats->datagrams,
              loss_percent_x10 / 10U,
              loss_percent_x10 % 10U,
              stats->out_of_order);
    } else if (iperf->config.proto == BFLB_IPERF_PROTO_UDP &&
               iperf->config.role == BFLB_IPERF_ROLE_CLIENT) {
        LOG_W("failed to receive a valid UDP server report\r\n");
    }

    LOG_RI("------------------------------------------------------------\r\n");
}

/**
 * @brief Determine whether traffic generation must stop.
 * @param[in] iperf Instance whose stop flag and configured client limits are
 * inspected.
 * @param[in] now_us Current monotonic timestamp in microseconds.
 * @retval true A stop request, byte limit, or duration limit was reached.
 * @retval false Traffic may continue.
 * @note Servers ignore local duration and byte limits and terminate on peer
 * protocol events.
 */
bool iperf_limit_reached(const bflb_iperf_t *iperf, uint64_t now_us)
{
    const iperf_stats_t *stats = &iperf->stats;

    if (iperf->stop_requested) {
        return true;
    }
    /* Servers terminate on TCP close or UDP FIN rather than local client limits. */
    if (iperf->config.role == BFLB_IPERF_ROLE_SERVER) {
        return false;
    }
    if (iperf->config.amount_bytes != 0U && stats->bytes >= iperf->config.amount_bytes) {
        return true;
    }
    if (iperf->config.duration_s == 0U || stats->start_us == 0U) {
        return false;
    }
    if ((now_us - stats->start_us) >= (uint64_t)iperf->config.duration_s * 1000000ULL) {
        return true;
    }
    return false;
}

/**
 * @brief Copy internal statistics into a public result snapshot.
 * @param[in] iperf Instance to inspect.
 * @param[out] result Destination result structure.
 * @note The statistics are copied inside an interrupt critical section to
 * prevent torn 64-bit reads on 32-bit targets. A running test uses the current
 * monotonic timestamp when calculating duration and bandwidth.
 */
void iperf_result_snapshot(const bflb_iperf_t *iperf,
                           bflb_iperf_result_t *result)
{
    uintptr_t irq_flags;
    uint64_t start_us;
    uint64_t end_us;

    memset(result, 0, sizeof(*result));
    /* A short critical section prevents torn 64-bit reads on 32-bit targets. */
    irq_flags = bflb_irq_save();
    result->state = iperf->state;
    result->error = iperf->error;
    result->bytes = iperf->stats.bytes;
    result->datagrams = iperf->stats.datagrams;
    result->lost = iperf->stats.lost;
    result->out_of_order = iperf->stats.out_of_order;
    result->jitter_us = iperf->stats.jitter_us;
    start_us = iperf->stats.start_us;
    end_us = iperf->stats.end_us;
    bflb_irq_restore(irq_flags);

    if (end_us == 0U && start_us != 0U) {
        end_us = iperf_now_us();
    }
    result->duration_us = start_us == 0U ? 0U : end_us - start_us;
    result->bits_per_second = result->duration_us == 0U ?
                                  0U :
                                  (result->bytes * 8ULL * 1000000ULL / result->duration_us);
}
