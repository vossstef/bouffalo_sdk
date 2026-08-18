/**
 * @file iperf_cli.c
 * @brief Shell argument parsing for the compact iPerf2 public API.
 */

#include <errno.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <FreeRTOS.h>
#include <lwip/ip4_addr.h>
#include <shell.h>
#include <timers.h>
#include <utils_getopt.h>

#define DBG_TAG "IPERF_CLI"
#include "log.h"

#include "bflb_iperf.h"

/** @brief Interval between asynchronous instance destruction attempts. */
#define IPERF_DESTROY_RETRY_MS 10U

/** @brief Single iPerf instance managed by the shell command. */
static bflb_iperf_t *s_cli_iperf;

/** @brief True while a completed instance awaits asynchronous destruction. */
static volatile bool s_cli_releasing;

/** @brief Reusable periodic timer that destroys completed CLI instances. */
static TimerHandle_t s_cli_destroy_timer;

/** @brief User-visible usage text for the iperf shell command. */
#define IPERF_USAGE                                                          \
    "iperf -s|-c <IPv4-address> [-u] [-A socket|raw] [-p port] [-l bytes] "  \
    "[-t sec|-n bytes] [-i sec] [-b bit/s[K|M]] [-S tos] [-N] [-B host]\r\n" \
    "iperf -a\r\n"                                                           \
    "iperf [-h]\r\n"                                                         \
    "  -s        server mode\r\n"                                            \
    "  -c addr   client mode (IPv4 address)\r\n"                             \
    "  -u        UDP (default TCP)\r\n"                                      \
    "  -A name   backend: socket or raw (default raw)\r\n"                   \
    "  -p port   server port (default 5001)\r\n"                             \
    "  -l bytes  buffer/datagram length\r\n"                                 \
    "  -t sec    client duration (default 10)\r\n"                           \
    "  -n bytes  client byte limit instead of duration\r\n"                  \
    "  -i sec    report interval, zero disables periodic reports\r\n"        \
    "  -b rate   UDP client bandwidth in bit/s; K/M suffix accepted\r\n"     \
    "  -S tos    IPv4 TOS value\r\n"                                         \
    "  -N        disable Nagle for TCP client\r\n"                           \
    "  -B addr   bind local IPv4 address\r\n"                                \
    "  -a        stop the running test\r\n"

/**
 * @brief Destroy a completed CLI instance outside its completion callback.
 * @param[in] timer Periodic cleanup timer holding the completed instance.
 * @note A busy result means backend completion publication is still finishing;
 * the timer retries after IPERF_DESTROY_RETRY_MS. Commands remain blocked
 * until destruction.
 */
static void iperf_destroy_timer_cb(TimerHandle_t timer)
{
    bflb_iperf_t *iperf = pvTimerGetTimerID(timer);
    int result;

    if (!s_cli_releasing || iperf == NULL) {
        xTimerStop(timer, 0U);
        return;
    }
    result = bflb_iperf_destroy(iperf);

    if (result == BFLB_IPERF_ERR_BUSY) {
        LOG_W("instance cleanup still in progress, retrying\r\n");
        return;
    }
    if (result != BFLB_IPERF_OK) {
        xTimerStop(timer, 0U);
        LOG_E("failed to destroy completed instance: %d\r\n", result);
        return;
    }

    vTimerSetTimerID(timer, NULL);
    s_cli_iperf = NULL;
    s_cli_releasing = false;
    if (xTimerStop(timer, 0U) != pdPASS) {
        LOG_E("failed to stop instance cleanup timer\r\n");
    }
    LOG_I("iperf instance destroyed\r\n");
}

/**
 * @brief Schedule asynchronous destruction of a completed CLI instance.
 *
 * @param[in] iperf Completed CLI-owned instance.
 * @param[in] result Immutable final result snapshot.
 * @param[in] user_data Opaque callback value; unused by the CLI.
 *
 * @note This callback runs in the backend worker task and therefore must not
 * destroy the instance directly.
 */
static void iperf_done_cb(bflb_iperf_t *iperf,
                          const bflb_iperf_result_t *result,
                          void *user_data)
{
    (void)user_data;

    if (result->error != 0) {
        LOG_E("iperf test failed: state=%d error=%d bytes=%llu duration_us=%llu\r\n",
              result->state, result->error,
              (unsigned long long)result->bytes,
              (unsigned long long)result->duration_us);
    }

    s_cli_releasing = true;
    vTimerSetTimerID(s_cli_destroy_timer, iperf);
    if (xTimerStart(s_cli_destroy_timer, portMAX_DELAY) != pdPASS) {
        LOG_E("failed to schedule instance cleanup\r\n");
    }
}

/**
 * @brief Ensure the reusable asynchronous-destruction timer exists.
 * @retval 0 The timer is ready.
 * @retval -1 Timer allocation failed.
 */
static int iperf_prepare_destroy_timer(void)
{
    if (s_cli_destroy_timer != NULL) {
        return 0;
    }
    s_cli_destroy_timer = xTimerCreate("iperf_cleanup",
                                       pdMS_TO_TICKS(IPERF_DESTROY_RETRY_MS),
                                       pdTRUE, NULL,
                                       iperf_destroy_timer_cb);
    return s_cli_destroy_timer != NULL ? 0 : -1;
}

/**
 * @brief Parse an unsigned 64-bit integer with C base-prefix support.
 * @param[in] text NUL-terminated numeric string.
 * @param[out] value Parsed value.
 * @retval 0 Parse succeeded.
 * @retval -1 Input is invalid or out of range.
 */
static int iperf_parse_u64(const char *text, uint64_t *value)
{
    char *end;
    unsigned long long parsed;

    if (text == NULL || text[0] == '-' || text[0] == '\0') {
        return -1;
    }
    errno = 0;
    parsed = strtoull(text, &end, 0);
    if (errno != 0 || end == text || *end != '\0') {
        return -1;
    }
    *value = (uint64_t)parsed;
    return 0;
}

/**
 * @brief Parse an unsigned 32-bit integer.
 * @param[in] text NUL-terminated numeric string.
 * @param[out] value Parsed value.
 * @retval 0 Parse succeeded.
 * @retval -1 Input is invalid or out of range.
 */
static int iperf_parse_u32(const char *text, uint32_t *value)
{
    uint64_t parsed;

    if (iperf_parse_u64(text, &parsed) < 0 || parsed > UINT32_MAX) {
        return -1;
    }
    *value = (uint32_t)parsed;
    return 0;
}

/**
 * @brief Parse an unsigned 16-bit integer.
 * @param[in] text NUL-terminated numeric string.
 * @param[out] value Parsed value.
 * @retval 0 Parse succeeded.
 * @retval -1 Input is invalid or out of range.
 */
static int iperf_parse_u16(const char *text, uint16_t *value)
{
    uint32_t parsed;

    if (iperf_parse_u32(text, &parsed) < 0 || parsed > UINT16_MAX) {
        return -1;
    }
    *value = (uint16_t)parsed;
    return 0;
}

/**
 * @brief Parse an unsigned 8-bit integer.
 * @param[in] text NUL-terminated numeric string.
 * @param[out] value Parsed value.
 * @retval 0 Parse succeeded.
 * @retval -1 Input is invalid or out of range.
 */
static int iperf_parse_u8(const char *text, uint8_t *value)
{
    uint32_t parsed;

    if (iperf_parse_u32(text, &parsed) < 0 || parsed > UINT8_MAX) {
        return -1;
    }
    *value = (uint8_t)parsed;
    return 0;
}

/**
 * @brief Parse an IPv4 address into lwIP network byte order.
 * @param[in] text Dotted-decimal IPv4 string.
 * @param[out] address Parsed IPv4 address in network byte order.
 * @retval 0 Parse succeeded.
 * @retval -1 Input is not a valid IPv4 address.
 */
static int iperf_parse_ip4(const char *text, uint32_t *address)
{
    ip4_addr_t ip4;

    if (!ip4addr_aton(text, &ip4)) {
        return -1;
    }
    *address = ip4.addr;
    return 0;
}

/**
 * @brief Parse a bandwidth in bits per second with an optional K/M suffix.
 * @param[in] text NUL-terminated integer with optional K, k, M, or m suffix.
 * @param[out] value Parsed bandwidth in bits per second.
 * @retval 0 Parse succeeded.
 * @retval -1 Input is invalid or exceeds UINT32_MAX.
 * @note K and M use decimal multipliers of 1000 and 1000000.
 */
static int iperf_parse_bandwidth(const char *text, uint32_t *value)
{
    char *end;
    uint64_t multiplier = 1U;
    unsigned long long parsed;

    if (text == NULL || text[0] == '-' || text[0] == '\0') {
        return -1;
    }
    errno = 0;
    parsed = strtoull(text, &end, 0);
    if (errno != 0 || end == text) {
        return -1;
    }
    if ((end[0] == 'K' || end[0] == 'k') && end[1] == '\0') {
        multiplier = 1000U;
    } else if ((end[0] == 'M' || end[0] == 'm') && end[1] == '\0') {
        multiplier = 1000000U;
    } else if (end[0] != '\0') {
        return -1;
    }
    if ((uint64_t)parsed > UINT32_MAX / multiplier) {
        return -1;
    }
    *value = (uint32_t)((uint64_t)parsed * multiplier);
    return 0;
}

/**
 * @brief Parse the iperf shell command and invoke the public API.
 *
 * The command manages one instance. The -a option is a standalone stop
 * request; a completed instance is destroyed asynchronously. Client and bind
 * addresses must be dotted-decimal IPv4 literals.
 *
 * @param[in] argc Number of shell arguments.
 * @param[in] argv Shell argument vector.
 *
 * @note Commands are rejected while asynchronous destruction is pending.
 */
static void iperf_cmd(int argc, char **argv)
{
    bflb_iperf_config_t config;
    getopt_env_t env;
    uint8_t client = 0U;
    uint8_t server = 0U;
    uint8_t stop = 0U;
    uint8_t bandwidth_set = 0U;
    uint8_t time_set = 0U;
    uint8_t amount_set = 0U;
    int option;

    if (argc <= 1 ||
        (argc == 2 && strcmp(argv[1], "-h") == 0)) {
        LOG_I(IPERF_USAGE);
        return;
    }
    if (s_cli_releasing) {
        LOG_E("previous test is still releasing\r\n");
        return;
    }

    /* Start from public API defaults, then override only explicit CLI options. */
    bflb_iperf_config_init(&config);
    utils_getopt_init(&env, 0);
    while ((option = utils_getopt(&env, argc, argv, ":aA:c:sup:l:t:n:i:b:S:NB:h")) != -1) {
        switch (option) {
            case 'a':
                stop = 1U;
                break;
            case 'A':
                if (strcmp(env.optarg, "socket") == 0) {
                    config.backend = BFLB_IPERF_BACKEND_SOCKET;
                } else if (strcmp(env.optarg, "raw") == 0) {
                    config.backend = BFLB_IPERF_BACKEND_RAW;
                } else {
                    LOG_E("backend must be socket or raw\r\n");
                    return;
                }
                break;
            case 'c':
                if (iperf_parse_ip4(env.optarg, &config.remote_ip4) < 0) {
                    LOG_E("invalid client IPv4 address\r\n");
                    return;
                }
                client = 1U;
                break;
            case 's':
                server = 1U;
                break;
            case 'u':
                config.proto = BFLB_IPERF_PROTO_UDP;
                break;
            case 'p':
                if (iperf_parse_u16(env.optarg, &config.port) < 0) {
                    goto invalid_value;
                }
                break;
            case 'l':
                if (iperf_parse_u16(env.optarg, &config.buffer_len) < 0) {
                    goto invalid_value;
                }
                break;
            case 't':
                if (iperf_parse_u32(env.optarg, &config.duration_s) < 0 ||
                    config.duration_s == 0U) {
                    goto invalid_value;
                }
                time_set = 1U;
                break;
            case 'n':
                if (iperf_parse_u64(env.optarg, &config.amount_bytes) < 0 ||
                    config.amount_bytes == 0U) {
                    goto invalid_value;
                }
                config.duration_s = 0U;
                amount_set = 1U;
                break;
            case 'i':
                if (iperf_parse_u16(env.optarg, &config.interval_s) < 0) {
                    goto invalid_value;
                }
                break;
            case 'b':
                if (iperf_parse_bandwidth(env.optarg, &config.bandwidth_bps) < 0) {
                    goto invalid_value;
                }
                bandwidth_set = 1U;
                break;
            case 'S':
                if (iperf_parse_u8(env.optarg, &config.tos) < 0) {
                    goto invalid_value;
                }
                break;
            case 'N':
                config.tcp_nodelay = 1U;
                break;
            case 'B':
                if (iperf_parse_ip4(env.optarg, &config.local_ip4) < 0) {
                    LOG_E("invalid local IPv4 address\r\n");
                    return;
                }
                break;
            case 'h':
                LOG_I(IPERF_USAGE);
                return;
            case ':':
                LOG_E("option -%c requires a value\r\n", env.optopt);
                return;
            default:
                LOG_I(IPERF_USAGE);
                return;
        }
    }

    /* Reject positional arguments left after utils_getopt() finishes. */
    if (env.optind < argc) {
        LOG_E("unexpected argument: %s\r\n", argv[env.optind]);
        return;
    }
    /* Keep stop as a standalone control command, not a test configuration. */
    if (stop != 0U) {
        if (client != 0U || server != 0U) {
            LOG_E("-a cannot be combined with -c or -s\r\n");
            return;
        }
        if (s_cli_iperf == NULL) {
            LOG_I("no iperf test exists\r\n");
            return;
        }
        if (bflb_iperf_stop(s_cli_iperf) < 0) {
            LOG_E("stop request failed\r\n");
        }
        return;
    }
    /* Exactly one endpoint role is required before building the API request. */
    if (client == server) {
        LOG_E("select exactly one of -c and -s\r\n");
        return;
    }
    if (time_set != 0U && amount_set != 0U) {
        LOG_E("-t and -n are mutually exclusive\r\n");
        return;
    }
    config.role = client != 0U ? BFLB_IPERF_ROLE_CLIENT : BFLB_IPERF_ROLE_SERVER;
    if (bandwidth_set != 0U && config.proto != BFLB_IPERF_PROTO_UDP) {
        LOG_E("-b is only valid for UDP\r\n");
        return;
    }
    /* Select protocol-specific sizes only when the user omitted -l and -b. */
    if (config.buffer_len == 0U) {
        config.buffer_len = config.proto == BFLB_IPERF_PROTO_UDP ?
                                BFLB_IPERF_DEFAULT_UDP_LEN :
                                BFLB_IPERF_DEFAULT_TCP_LEN;
    }
    if (config.proto == BFLB_IPERF_PROTO_UDP && config.bandwidth_bps == 0U) {
        config.bandwidth_bps = BFLB_IPERF_DEFAULT_UDP_RATE_BPS;
    }
    config.done_cb = iperf_done_cb;
    config.user_data = NULL;

    /* The core supports many instances; this CLI explicitly owns one slot. */
    if (s_cli_iperf != NULL) {
        LOG_E("an iperf test is already active\r\n");
        return;
    }
    if (iperf_prepare_destroy_timer() < 0) {
        LOG_E("failed to create instance cleanup timer\r\n");
        return;
    }
    if (bflb_iperf_create(&config, &s_cli_iperf) < 0) {
        LOG_E("create failed (invalid config or no resources)\r\n");
        return;
    }
    if (bflb_iperf_start(s_cli_iperf) < 0) {
        LOG_E("start failed (invalid state or no backend resources)\r\n");
        if (bflb_iperf_destroy(s_cli_iperf) == BFLB_IPERF_OK) {
            s_cli_iperf = NULL;
        }
        return;
    }
    return;

invalid_value:
    LOG_E("invalid numeric value\r\n");
}

SHELL_CMD_EXPORT_ALIAS(iperf_cmd, iperf, compact iperf command);
