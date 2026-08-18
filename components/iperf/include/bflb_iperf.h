/**
 * @file bflb_iperf.h
 * @brief Compact Classic iPerf2 public API.
 *
 * This API provides independent IPv4 test instances with Socket and lwIP Raw
 * backends. TCP and UDP client/server roles are supported in Classic iPerf2
 * normal mode.
 */

#ifndef BFLB_IPERF_H
#define BFLB_IPERF_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** @defgroup bflb_iperf iPerf
 *  @brief Compact Classic iPerf2 throughput tester.
 *  @{
 */

#define BFLB_IPERF_DEFAULT_PORT         5001U    /**< Default server port. */
#define BFLB_IPERF_DEFAULT_TIME_S       10U      /**< Default test time in seconds. */
#define BFLB_IPERF_DEFAULT_INTERVAL_S   1U       /**< Default report interval in seconds. */
#define BFLB_IPERF_DEFAULT_TCP_LEN      4096U    /**< Default TCP I/O buffer size in bytes. */
#define BFLB_IPERF_DEFAULT_UDP_LEN      1470U    /**< Default UDP datagram size in bytes. */
#define BFLB_IPERF_DEFAULT_UDP_RATE_BPS 1000000U /**< Default UDP transmit rate in bits/s. */

#define BFLB_IPERF_OK                   0  /**< Operation completed successfully. */
#define BFLB_IPERF_ERR_INVALID          -1 /**< Invalid argument, state, or resource failure. */
#define BFLB_IPERF_ERR_BUSY             -2 /**< Instance is active or still releasing resources. */

/**
 * @brief Opaque iPerf instance owned by its creator.
 * @note Public instance APIs use a mutex and must not be called from an ISR.
 */
typedef struct bflb_iperf bflb_iperf_t;

/**
 * @brief iPerf transport backend.
 */
typedef enum {
    BFLB_IPERF_BACKEND_SOCKET = 0, /**< lwIP Socket API backend. */
    BFLB_IPERF_BACKEND_RAW,        /**< lwIP Raw API backend. */
} bflb_iperf_backend_t;

/**
 * @brief iPerf endpoint role.
 */
typedef enum {
    BFLB_IPERF_ROLE_CLIENT = 0, /**< Generate traffic as a client. */
    BFLB_IPERF_ROLE_SERVER,     /**< Receive traffic as a server. */
} bflb_iperf_role_t;

/**
 * @brief Test transport protocol.
 */
typedef enum {
    BFLB_IPERF_PROTO_TCP = 0, /**< TCP throughput test. */
    BFLB_IPERF_PROTO_UDP,     /**< UDP throughput test. */
} bflb_iperf_proto_t;

/**
 * @brief Test lifecycle state.
 */
typedef enum {
    BFLB_IPERF_STATE_IDLE = 0, /**< No test has been started. */
    BFLB_IPERF_STATE_STARTING, /**< Backend startup is in progress. */
    BFLB_IPERF_STATE_RUNNING,  /**< Test traffic is active. */
    BFLB_IPERF_STATE_STOPPING, /**< A stop request is being processed. */
    BFLB_IPERF_STATE_DONE,     /**< Test completed successfully. */
    BFLB_IPERF_STATE_ERROR,    /**< Test terminated with an error. */
} bflb_iperf_state_t;

/**
 * @brief Snapshot of the current or completed test result.
 */
typedef struct {
    bflb_iperf_state_t state; /**< Lifecycle state at snapshot time. */
    int error;                /**< Final backend error in DONE/ERROR state; zero indicates success. */
    uint64_t bytes;           /**< Total payload bytes accounted. */
    uint64_t duration_us;     /**< Elapsed test time in microseconds. */
    uint64_t bits_per_second; /**< Average throughput in bits per second. */
    uint32_t datagrams;       /**< UDP datagrams sent, or expected by a completed receiver. */
    uint32_t lost;            /**< UDP datagrams considered lost. */
    uint32_t out_of_order;    /**< UDP datagrams received out of order. */
    uint32_t jitter_us;       /**< UDP inter-arrival jitter in microseconds. */
} bflb_iperf_result_t;

/**
 * @brief Direct completion callback.
 *
 * The callback runs in the backend worker task that finishes the test. The
 * result pointer is valid only for the duration of the callback.
 *
 * @param[in] iperf Completed instance.
 * @param[in] result Immutable final result snapshot.
 * @param[in] user_data User value copied from the configuration.
 *
 * @note The callback must not call bflb_iperf_destroy(). The creator must
 * destroy the instance later from a safe control context.
 */
typedef void (*bflb_iperf_done_cb_t)(bflb_iperf_t *iperf,
                                     const bflb_iperf_result_t *result,
                                     void *user_data);

/**
 * @brief Configuration copied when an iPerf instance is created.
 *
 * Call bflb_iperf_config_init() before overriding individual fields. All
 * configuration fields are copied by bflb_iperf_create().
 *
 * @note Only the user_data pointer value is copied. The caller must keep the
 * object referenced by user_data valid until the completion callback returns.
 * @note buffer_len must be 80--1470 bytes for UDP, at most 16384 bytes for
 * TCP Socket, and at most 4096 bytes for TCP Raw. task_priority must be less
 * than configMAX_PRIORITIES after default selection.
 * @note In UDP byte-limit mode, amount_bytes must contain the setup datagram;
 * any partial final datagram must be large enough for the UDP sequence header.
 */
typedef struct {
    bflb_iperf_backend_t backend; /**< Transport backend. */
    bflb_iperf_role_t role;       /**< Client or server role. */
    bflb_iperf_proto_t proto;     /**< TCP or UDP protocol. */
    uint32_t remote_ip4;          /**< Required client destination IPv4 address in network byte order. */
    uint32_t local_ip4;           /**< Local IPv4 address in network byte order; zero binds any. */
    uint16_t port;                /**< Server destination/listen port; zero selects the default. */
    uint16_t local_port;          /**< Client source port; zero selects an ephemeral port. */
    uint16_t buffer_len;          /**< TCP I/O buffer or UDP datagram size; zero selects a protocol default. */
    uint16_t interval_s;          /**< Report interval in seconds; zero disables periodic reports. */
    uint32_t duration_s;          /**< Client duration in seconds when amount_bytes is zero. */
    uint64_t amount_bytes;        /**< Client byte limit; zero selects duration mode. */
    uint32_t bandwidth_bps;       /**< UDP client rate in bits/s; zero selects the default. */
    uint8_t tos;                  /**< IPv4 type-of-service value. */
    uint8_t tcp_nodelay;          /**< Nonzero disables Nagle for a TCP client. */
    uint8_t task_priority;        /**< Backend worker priority; zero selects the default. */
    bflb_iperf_done_cb_t done_cb; /**< Optional direct completion callback. */
    void *user_data;              /**< Opaque value passed to done_cb. */
} bflb_iperf_config_t;

/**
 * @brief Initialize a configuration with default values.
 *
 * The defaults select a TCP Raw server on port 5001. A NULL pointer is
 * ignored.
 *
 * @param[out] config Configuration structure to initialize.
 *
 * @note Protocol-dependent buffer and bandwidth defaults may be selected
 * later by bflb_iperf_create() when their fields remain zero.
 */
void bflb_iperf_config_init(bflb_iperf_config_t *config);

/**
 * @brief Create an independent iPerf instance.
 *
 * The caller owns the returned instance and must eventually destroy it. One
 * instance represents one test and can be started only once.
 *
 * @param[in] config Test configuration.
 * @param[out] iperf Receives the new instance on success.
 * @retval BFLB_IPERF_OK Instance created.
 * @retval BFLB_IPERF_ERR_INVALID Invalid configuration or allocation failure.
 */
int bflb_iperf_create(const bflb_iperf_config_t *config, bflb_iperf_t **iperf);

/**
 * @brief Start an iPerf instance.
 *
 * Every backend runs in an independent worker task. Raw PCB operations use
 * short TCP/IP Core Lock sections. A test may still be in STARTING state when
 * this function returns.
 *
 * @param[in] iperf Instance to start.
 * @retval BFLB_IPERF_OK Test accepted.
 * @retval BFLB_IPERF_ERR_INVALID Invalid instance or lifecycle state.
 * @return A backend-specific negative error may also be returned.
 */
int bflb_iperf_start(bflb_iperf_t *iperf);

/**
 * @brief Request termination of the active test.
 *
 * The request may complete asynchronously. Calling this function when no test
 * is active succeeds without effect.
 *
 * @param[in] iperf Instance to stop.
 * @retval BFLB_IPERF_OK Stop requested or no test was active.
 * @retval BFLB_IPERF_ERR_INVALID Invalid instance or destruction has started.
 * @return A backend-specific negative error on failure.
 */
int bflb_iperf_stop(bflb_iperf_t *iperf);

/**
 * @brief Read a consistent snapshot of test statistics.
 *
 * @param[in] iperf Instance to inspect.
 * @param[out] result Destination for the result snapshot.
 * @retval BFLB_IPERF_OK Result returned successfully.
 * @retval BFLB_IPERF_ERR_INVALID An argument is NULL or destruction has started.
 */
int bflb_iperf_get_result(bflb_iperf_t *iperf, bflb_iperf_result_t *result);

/**
 * @brief Get the current iPerf lifecycle state.
 *
 * @param[in] iperf Instance to inspect.
 * @return Current test state, or BFLB_IPERF_STATE_ERROR when the instance is
 * NULL or destruction has started.
 */
bflb_iperf_state_t bflb_iperf_get_state(bflb_iperf_t *iperf);

/**
 * @brief Destroy an instance owned by the caller.
 *
 * This function never waits. It succeeds only after the backend has released
 * all asynchronous resources and the completion callback has returned. Once
 * destruction starts, no thread may issue another API call with the pointer.
 *
 * @param[in] iperf Instance to destroy.
 * @retval BFLB_IPERF_OK Instance destroyed; the pointer is no longer valid.
 * @retval BFLB_IPERF_ERR_BUSY Test activity or callback release is incomplete.
 * @retval BFLB_IPERF_ERR_INVALID iperf is NULL or destruction already started.
 */
int bflb_iperf_destroy(bflb_iperf_t *iperf);

/** @} */

#ifdef __cplusplus
}
#endif

#endif /* BFLB_IPERF_H */
