/**
 * @file iperf_common.h
 * @brief Private contracts shared by the iPerf core and independent backends.
 * @note This header is private to components/iperf/iperf.
 */

#ifndef IPERF_COMMON_H
#define IPERF_COMMON_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <FreeRTOS.h>
#include <semphr.h>
#include <task.h>

#include "bflb_iperf.h"

/** @brief Size of the modern UDP sequence and timestamp prefix, in bytes. */
#define BFLB_IPERF_UDP_HEADER_SIZE        16U
/** @brief Size of the Classic iPerf2 version-1 client header, in bytes. */
#define BFLB_IPERF_UDP_CLIENT_V1_SIZE     24U
/** @brief Size of the extended client settings area, in bytes. */
#define BFLB_IPERF_UDP_CLIENT_EXT_SIZE    40U
/** @brief Total size of a modern normal-mode UDP setup header, in bytes. */
#define BFLB_IPERF_UDP_CLIENT_HEADER_SIZE (BFLB_IPERF_UDP_HEADER_SIZE + BFLB_IPERF_UDP_CLIENT_V1_SIZE + BFLB_IPERF_UDP_CLIENT_EXT_SIZE)
/** @brief Size of the base server AckFIN report, in bytes. */
#define BFLB_IPERF_SERVER_HEADER_SIZE     40U
/** @brief Total size of a modern UDP AckFIN datagram prefix, in bytes. */
#define BFLB_IPERF_UDP_ACK_SIZE           (BFLB_IPERF_UDP_HEADER_SIZE + BFLB_IPERF_SERVER_HEADER_SIZE)
/** @brief Size of the legacy 32-bit UDP sequence prefix, in bytes. */
#define BFLB_IPERF_UDP_LEGACY_HEADER_SIZE 12U
/** @brief Total size of a legacy UDP AckFIN report, in bytes. */
#define BFLB_IPERF_UDP_LEGACY_ACK_SIZE    (BFLB_IPERF_UDP_LEGACY_HEADER_SIZE + BFLB_IPERF_SERVER_HEADER_SIZE)
/** @brief Classic iPerf2 flag indicating a version-1 control header. */
#define BFLB_IPERF_HEADER_VERSION1        0x80000000UL
/** @brief Classic iPerf2 flag indicating an extended client header. */
#define BFLB_IPERF_HEADER_EXTEND          0x40000000UL
/** @brief Classic iPerf2 flag indicating 64-bit UDP sequence numbers. */
#define BFLB_IPERF_HEADER_SEQNO64B        0x08000000UL
/** @brief Classic iPerf2 flag indicating an encoded header length. */
#define BFLB_IPERF_HEADER_LEN_BIT         0x00010000UL
/** @brief Mask containing the encoded client header length. */
#define BFLB_IPERF_HEADER_LEN_MASK        0x000001FEUL

/** @brief Maximum TCP buffer size for the lwIP Raw backend. */
#define BFLB_IPERF_RAW_TCP_BUFFER_LEN     4096U

/**
 * @brief Mutable statistics produced by one backend instance.
 * @note Timestamps use the monotonic microsecond clock returned by
 * iperf_now_us(); zero denotes a test boundary that has not been recorded.
 */
typedef struct {
    uint64_t start_us;         /**< Test start time, or zero before traffic begins. */
    uint64_t end_us;           /**< Test end time, or zero while the test is active. */
    uint64_t bytes;            /**< Accounted payload bytes. */
    uint64_t last_report_us;   /**< Timestamp of the previous interval report. */
    uint64_t last_report_bytes;/**< Byte count at the previous interval report. */
    uint32_t datagrams;        /**< UDP datagrams received, or expected after FIN. */
    uint32_t lost;             /**< Estimated lost UDP datagrams. */
    uint32_t out_of_order;     /**< UDP datagrams received out of order. */
    uint32_t jitter_us;        /**< Smoothed UDP inter-arrival jitter in microseconds. */
    bool udp_report_received;  /**< A valid UDP server AckFIN report was imported. */
} iperf_stats_t;

/**
 * @brief Receiver-side UDP sequence and jitter tracker.
 * @note Initialize with iperf_udp_rx_init() before accounting data packets.
 */
typedef struct {
    uint64_t next_id;       /**< Next expected nonnegative UDP sequence number. */
    uint32_t gap_count;     /**< Accumulated sequence-gap candidates. */
    int64_t last_transit_us;/**< Previous transit time, or INT64_MIN without a sample. */
    uint32_t jitter_q4;     /**< Jitter accumulator in Q4 microseconds. */
} iperf_udp_rx_t;

/** @brief Supported Classic iPerf2 UDP client setup formats. */
typedef enum {
    IPERF_UDP_SETUP_INVALID = 0,
    IPERF_UDP_SETUP_LEGACY,
    IPERF_UDP_SETUP_MODERN,
} iperf_udp_setup_t;

/**
 * @brief Operations supplied by one protocol/backend implementation.
 *
 * The core allocates and zero-initializes context_size bytes for context.
 * After a successful start, the backend must eventually publish exactly one
 * completion through iperf_backend_finished().
 */
typedef struct {
    size_t context_size; /**< Bytes required for the private backend context. */
    int (*start)(bflb_iperf_t *iperf, void *context); /**< Start an instance; zero on acceptance, otherwise a negative error. */
    int (*stop)(bflb_iperf_t *iperf, void *context);  /**< Request asynchronous stop; zero on acceptance, otherwise a negative error. */
} iperf_backend_ops_t;

/**
 * @brief Complete private representation of the public opaque handle.
 * @note active_calls and destroy_started are coordinated with an IRQ critical
 * section; other lifecycle fields are protected by lock where required.
 */
struct bflb_iperf {
    SemaphoreHandle_t lock;              /**< Mutex protecting public lifecycle state. */
    bflb_iperf_config_t config;           /**< Validated and normalized configuration copy. */
    volatile bflb_iperf_state_t state;    /**< Current lifecycle state. */
    volatile bool stop_requested;         /**< Cooperative stop flag observed by the backend. */
    int error;                            /**< Final backend error code. */
    iperf_stats_t stats;                  /**< Mutable traffic statistics. */
    const iperf_backend_ops_t *ops;        /**< Selected immutable backend operations. */
    void *backend_context;                /**< Core-owned private backend storage. */
    uint16_t active_calls;                /**< Public API callers and mutex waiters. */
    bool backend_released;                /**< Backend no longer accesses instance storage. */
    bool callback_running;                /**< Completion callback is executing. */
    bool destroy_started;                 /**< Destruction published to reject new callers. */
};

/** @brief Blocking TCP Socket backend operations. */
extern const iperf_backend_ops_t g_iperf_tcp_socket_ops;
/** @brief Task-driven TCP Raw backend operations. */
extern const iperf_backend_ops_t g_iperf_tcp_raw_ops;
/** @brief Blocking UDP Socket backend operations. */
extern const iperf_backend_ops_t g_iperf_udp_socket_ops;
/** @brief Task-driven UDP Raw backend operations. */
extern const iperf_backend_ops_t g_iperf_udp_raw_ops;

/** @brief Permanent immutable payload referenced by zero-copy Raw sends. */
extern const uint8_t g_iperf_raw_payload[BFLB_IPERF_RAW_TCP_BUFFER_LEN];

/**
 * @brief Read the monotonic hardware timer.
 * @return Current timestamp in microseconds.
 */
uint64_t iperf_now_us(void);

/**
 * @brief Write one unsigned 32-bit value in network byte order.
 * @param[out] buffer Destination containing at least four bytes.
 * @param[in] value Value to encode.
 */
void iperf_put_u32(uint8_t *buffer, uint32_t value);

/**
 * @brief Read one unsigned 32-bit value in network byte order.
 * @param[in] buffer Source containing at least four bytes.
 * @return Decoded host-order value.
 */
uint32_t iperf_get_u32(const uint8_t *buffer);

/**
 * @brief Write the modern UDP sequence and timestamp prefix.
 * @param[out] buffer Destination containing BFLB_IPERF_UDP_HEADER_SIZE bytes.
 * @param[in] id Signed packet ID; a negative value denotes FIN.
 * @param[in] now_us Packet timestamp in microseconds.
 */
void iperf_write_udp_header(uint8_t *buffer, int64_t id, uint64_t now_us);

/**
 * @brief Read a signed 64-bit UDP packet ID from a modern prefix.
 * @param[in] buffer Source containing BFLB_IPERF_UDP_HEADER_SIZE bytes.
 * @return Signed packet ID.
 */
int64_t iperf_read_udp_id(const uint8_t *buffer);

/**
 * @brief Read the sender timestamp from a modern UDP prefix.
 * @param[in] buffer Source containing BFLB_IPERF_UDP_HEADER_SIZE bytes.
 * @return Sender timestamp in microseconds.
 */
uint64_t iperf_read_udp_timestamp(const uint8_t *buffer);

/**
 * @brief Write a normal-mode UDP client setup header with packet ID one.
 * @param[out] buffer Destination containing BFLB_IPERF_UDP_CLIENT_HEADER_SIZE bytes.
 * @param[in] iperf Instance supplying normalized client settings.
 * @param[in] now_us Packet timestamp in microseconds.
 */
void iperf_write_udp_client_header(uint8_t *buffer,
                                   const bflb_iperf_t *iperf,
                                   uint64_t now_us);

/**
 * @brief Identify a supported normal-mode UDP client setup header.
 * @param[in] buffer Received datagram prefix.
 * @param[in] length Number of available bytes.
 * @return Detected setup format, or IPERF_UDP_SETUP_INVALID.
 */
iperf_udp_setup_t iperf_udp_client_setup_type(const uint8_t *buffer,
                                              uint16_t length);

/**
 * @brief Validate a modern or legacy UDP AckFIN report.
 * @param[in] buffer Received report prefix.
 * @param[in] length Number of available bytes.
 * @return true when a supported version-1 report header is present.
 */
bool iperf_udp_report_valid(const uint8_t *buffer, uint16_t length);

/**
 * @brief Initialize receiver-side UDP sequence and jitter tracking.
 * @param[out] tracker Tracker to reset.
 */
void iperf_udp_rx_init(iperf_udp_rx_t *tracker);

/**
 * @brief Account one received UDP data datagram.
 * @param[in,out] iperf Instance whose statistics are updated.
 * @param[in,out] tracker Receiver sequence and jitter tracker.
 * @param[in] id Nonnegative UDP packet ID.
 * @param[in] sent_us Sender timestamp in microseconds.
 * @param[in] received_us Local datagram arrival time in microseconds.
 * @param[in] length Received datagram length in bytes.
 */
void iperf_udp_rx_account(bflb_iperf_t *iperf, iperf_udp_rx_t *tracker,
                          int64_t id, uint64_t sent_us, uint64_t received_us,
                          uint16_t length);

/**
 * @brief Finalize receiver loss accounting when a UDP FIN arrives.
 * @param[in,out] iperf Instance whose statistics are finalized.
 * @param[in,out] tracker Receiver sequence tracker.
 * @param[in] next_id Sequence number following the final client data packet.
 */
void iperf_finish_udp_rx(bflb_iperf_t *iperf, iperf_udp_rx_t *tracker,
                         uint64_t next_id);

/**
 * @brief Encode a modern UDP server AckFIN report.
 * @param[out] buffer Destination containing BFLB_IPERF_UDP_ACK_SIZE bytes.
 * @param[in] iperf Instance supplying finalized server statistics.
 * @param[in] fin_id Negative FIN packet ID echoed to the client.
 */
void iperf_write_udp_report(uint8_t *buffer, const bflb_iperf_t *iperf,
                            int64_t fin_id);

/**
 * @brief Import statistics from a modern or legacy UDP AckFIN report.
 * @param[in,out] iperf Client instance whose UDP result is updated.
 * @param[in] buffer Received report prefix.
 * @param[in] length Number of available bytes.
 */
void iperf_read_udp_report(bflb_iperf_t *iperf, const uint8_t *buffer,
                           uint16_t length);

/**
 * @brief Print the PC iPerf-style client connection preamble.
 * @param[in] iperf Client instance supplying protocol and destination settings.
 */
void iperf_log_client_preamble(const bflb_iperf_t *iperf);

/**
 * @brief Print the PC iPerf-style server listening preamble.
 * @param[in] iperf Server instance supplying protocol and listen settings.
 */
void iperf_log_server_preamble(const bflb_iperf_t *iperf);

/**
 * @brief Print the established endpoint tuple and report column heading.
 * @param[in] iperf Active instance supplying the protocol.
 * @param[in] local_ip4 Local IPv4 address in network byte order.
 * @param[in] local_port Local port in host byte order.
 * @param[in] remote_ip4 Peer IPv4 address in network byte order.
 * @param[in] remote_port Peer port in host byte order.
 */
void iperf_log_connection(const bflb_iperf_t *iperf,
                          uint32_t local_ip4,
                          uint16_t local_port,
                          uint32_t remote_ip4,
                          uint16_t remote_port);

/**
 * @brief Record the test start time once.
 * @param[in,out] iperf Instance whose statistics are initialized.
 */
void iperf_test_begin(bflb_iperf_t *iperf);

/**
 * @brief Record the test end time once.
 * @param[in,out] iperf Instance whose statistics are finalized.
 */
void iperf_test_end(bflb_iperf_t *iperf);

/**
 * @brief Account one successful transfer and emit any due interval report.
 * @param[in,out] iperf Active instance.
 * @param[in] length Number of payload bytes to add.
 * @note UDP transfers also increment the datagram count; TCP transfers only
 * update the byte count.
 */
void iperf_account_transfer(bflb_iperf_t *iperf, uint32_t length);

/**
 * @brief Account one transfer using an already captured report timestamp.
 * @param[in,out] iperf Active instance.
 * @param[in] length Number of payload bytes to add.
 * @param[in] now_us Timestamp associated with the accounted data.
 */
void iperf_account_transfer_at(bflb_iperf_t *iperf, uint32_t length, uint64_t now_us);

/**
 * @brief Finalize timing and print the summary report.
 * @param[in,out] iperf Completed instance.
 */
void iperf_test_report_finish(bflb_iperf_t *iperf);

/**
 * @brief Determine whether a client limit or stop request has been reached.
 * @param[in] iperf Instance to inspect.
 * @param[in] now_us Current monotonic timestamp in microseconds.
 * @return true when traffic generation should stop; false otherwise.
 * @note Servers ignore local duration and byte limits and terminate on peer events.
 */
bool iperf_limit_reached(const bflb_iperf_t *iperf, uint64_t now_us);

/**
 * @brief Copy internal statistics into a public result snapshot.
 * @param[in] iperf Instance to inspect.
 * @param[out] result Destination result structure.
 */
void iperf_result_snapshot(const bflb_iperf_t *iperf,
                           bflb_iperf_result_t *result);

/**
 * @brief Publish that backend startup has reached its execution context.
 * @param[in,out] iperf Instance entering the backend execution context.
 * @return true when traffic should run, false when an early stop was requested.
 */
bool iperf_backend_started(bflb_iperf_t *iperf);

/**
 * @brief Release backend ownership and directly notify completion.
 * @param[in,out] iperf Instance being completed.
 * @param[in] error Final backend error; zero indicates successful completion.
 * @note After this function returns, the backend must not access iperf or its
 * private context again.
 */
void iperf_backend_finished(bflb_iperf_t *iperf, int error);

#endif /* IPERF_COMMON_H */