#ifndef WIFI_MGMR_COEX_H
#define WIFI_MGMR_COEX_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Product-level coexistence runtime policy. */
typedef enum
{
    /** Safe default: apply the hardware plan without starting PS-PTA. */
    WIFI_MGMR_COEX_RUNTIME_BOARD_DEFAULT = 0,
    WIFI_MGMR_COEX_RUNTIME_HARDWARE_ONLY = 1,
    WIFI_MGMR_COEX_RUNTIME_PS_PTA_REQUIRED = 2,
} wifi_mgmr_coex_runtime_policy_t;

/** Product-level coexistence status. */
struct wifi_mgmr_coex_status
{
    bool active;
    bool ps_pta_running;
    wifi_mgmr_coex_runtime_policy_t effective_runtime;
    uint8_t band;
    uint8_t duty_active_ms;
};

/** Product-level coexistence control result. */
enum wifi_mgmr_coex_error
{
    WIFI_MGMR_COEX_OK = 0,
    WIFI_MGMR_COEX_ERR_INVALID_ARGUMENT = -1,
    WIFI_MGMR_COEX_ERR_NOT_SUPPORTED = -2,
    WIFI_MGMR_COEX_ERR_NOT_READY = -3,
    WIFI_MGMR_COEX_ERR_BUSY = -4,
    WIFI_MGMR_COEX_ERR_APPLY_FAILED = -5,
};

/** RF path and antenna wiring fixed by the product board. */
enum wifi_mgmr_coex_board_topology {
    WIFI_MGMR_COEX_BOARD_COMBO_SHARED_PATH = 0,
    WIFI_MGMR_COEX_BOARD_STANDALONE_DUAL_ANT,
    WIFI_MGMR_COEX_BOARD_STANDALONE_SINGLE_ANT_SPDT,
};

/** Startup-time coexistence integration data. */
struct wifi_mgmr_coex_board_config {
    enum wifi_mgmr_coex_board_topology topology;
    int8_t spdt_gpio;
};

enum wifi_mgmr_coex_board_config_status {
    WIFI_MGMR_COEX_BOARD_CONFIG_OK = 0,
    WIFI_MGMR_COEX_BOARD_CONFIG_ERR_INVALID_ARGUMENT = -1,
    WIFI_MGMR_COEX_BOARD_CONFIG_ERR_NOT_SUPPORTED = -2,
    WIFI_MGMR_COEX_BOARD_CONFIG_ERR_BUSY = -3,
    WIFI_MGMR_COEX_BOARD_CONFIG_ERR_GPIO_PREPARE = -4,
    WIFI_MGMR_COEX_BOARD_CONFIG_ERR_NOT_CONFIGURED = -5,
};

/**
 * Configure and lock board-level coexistence wiring.
 *
 * This startup integration API does not apply a coexistence recipe, switch
 * the PHYRF path, or start PS-PTA. Repeating the same configuration is
 * idempotent; changing a locked configuration returns BUSY.
 */
int wifi_mgmr_coex_board_configure(
    enum wifi_mgmr_coex_board_topology topology, int spdt_gpio);

/** Read the immutable board configuration snapshot. */
int wifi_mgmr_coex_board_config_get(
    struct wifi_mgmr_coex_board_config *config);

int wifi_mgmr_coex_start(wifi_mgmr_coex_runtime_policy_t policy);
int wifi_mgmr_coex_stop(void);
int wifi_mgmr_coex_status_get(struct wifi_mgmr_coex_status *status);
int wifi_mgmr_coex_duty_set(uint8_t active_ms);
int wifi_mgmr_coex_protection_set(bool enable);

/* Explicit diagnostics. These APIs are not required by product coex flow. */
int wifi_mgmr_coex_debug_resolve_dump(
    wifi_mgmr_coex_runtime_policy_t policy);
void wifi_mgmr_coex_debug_context_dump(void);
bool wifi_mgmr_coex_debug_reconfiguration_blocked(void);

#ifdef __cplusplus
}
#endif

#endif /* WIFI_MGMR_COEX_H */
