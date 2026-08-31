/**
 * @file lwip_emac_start.c
 * @brief EMAC network interface initialization and link-state management.
 *
 * This module initializes the board-level EMAC pins and lwIP stack, creates
 * and configures the EMAC network interface, starts the DHCP client when
 * enabled, and periodically synchronizes the PHY link state with lwIP.
 */

#include <stdint.h>
#include <string.h>

#include "bflb_emac.h"

#include "FreeRTOS.h"
#include "task.h"

#include "lwip/opt.h"
#include "lwip/netif.h"
#include "lwip/netifapi.h"
#include "lwip/tcpip.h"
#if LWIP_DHCP
#include "lwip/dhcp.h"
#endif

#include "board.h"
#include "eth_phy.h"
#include "lwip_netif_emac.h"
#include "lwip_emac_start.h"

#define DBG_TAG "LWIP_EMAC_START"
#include "log.h"

/** @name Static IPv4 configuration
 *
 * These values are used when the lwIP DHCP client is disabled.
 * @{
 */
#define IP_ADDR0                         (uint8_t)192
#define IP_ADDR1                         (uint8_t)168
#define IP_ADDR2_PORT0                   (uint8_t)123
#define IP_ADDR2_PORT1                   (uint8_t)124
#define IP_ADDR3                         (uint8_t)100

#define NETMASK_ADDR0                    (uint8_t)255
#define NETMASK_ADDR1                    (uint8_t)255
#define NETMASK_ADDR2                    (uint8_t)255
#define NETMASK_ADDR3                    (uint8_t)0

#define GW_ADDR0                         (uint8_t)192
#define GW_ADDR1                         (uint8_t)168
#define GW_ADDR3                         (uint8_t)1
/** @} */

/** @name EMAC link-status task configuration
 * @{ */
#define LWIP_EMAC_STATUS_TASK_STACK_SIZE 256
#define LWIP_EMAC_STATUS_TASK_PRIORITY   osPriorityHigh
#define LWIP_EMAC_STATUS_POLL_TICKS      200
/** @} */

/* Per-port DMA buffer size, derived from each port's frame size/alignment
 * (CONFIG_EMAC{0,1}_FRAME_SIZE_MAX / CONFIG_EMAC{0,1}_BUFFER_ALIGNMENT). */
#define LWIP_EMAC0_BUFFER_SIZE \
    ((CONFIG_EMAC0_FRAME_SIZE_MAX + CONFIG_EMAC0_BUFFER_ALIGNMENT - 1U) & ~(CONFIG_EMAC0_BUFFER_ALIGNMENT - 1U))
#define LWIP_EMAC1_BUFFER_SIZE \
    ((CONFIG_EMAC1_FRAME_SIZE_MAX + CONFIG_EMAC1_BUFFER_ALIGNMENT - 1U) & ~(CONFIG_EMAC1_BUFFER_ALIGNMENT - 1U))

/* Compile-time maximum number of EMAC ports, taken from CONFIG_EMAC_DEV_COUNT
 * (defaults to 1 = single port). It only sizes the static buffer/context
 * arrays; the number of ports actually enabled at runtime is capped by this
 * maximum. */
#if defined(CONFIG_EMAC_DEV_COUNT)
#define LWIP_EMAC_MAX_PORT_COUNT (CONFIG_EMAC_DEV_COUNT)
#else
#define LWIP_EMAC_MAX_PORT_COUNT (1)
#endif

/* Per-port parameter selection. With CONFIG_EMAC_DUAL_CFG = 1 each port uses
 * its own CONFIG_EMAC{0,1}_* group; otherwise both ports use the EMAC0 group. */
#if (LWIP_EMAC_MAX_PORT_COUNT > 1) && defined(CONFIG_EMAC_DUAL_CFG) && CONFIG_EMAC_DUAL_CFG
#define LWIP_EMAC_MTU(i)   ((i) == 0 ? CONFIG_EMAC0_MTU : CONFIG_EMAC1_MTU)
#define LWIP_EMAC_RXSTK(i) ((i) == 0 ? CONFIG_EMAC0_RX_STACK_SIZE : CONFIG_EMAC1_RX_STACK_SIZE)
#define LWIP_EMAC_TXTMO(i) ((i) == 0 ? CONFIG_EMAC0_TX_BUF_TIMEOUT : CONFIG_EMAC1_TX_BUF_TIMEOUT)
#define LWIP_EMAC_RXCNT(i) ((i) == 0 ? CONFIG_EMAC0_RX_BUFF_CNT : CONFIG_EMAC1_RX_BUFF_CNT)
#define LWIP_EMAC_TXCNT(i) ((i) == 0 ? CONFIG_EMAC0_TX_BUFF_CNT : CONFIG_EMAC1_TX_BUFF_CNT)
#define LWIP_EMAC_FSMIN(i) ((i) == 0 ? CONFIG_EMAC0_FRAME_SIZE_MIN : CONFIG_EMAC1_FRAME_SIZE_MIN)
#define LWIP_EMAC_FSMAX(i) ((i) == 0 ? CONFIG_EMAC0_FRAME_SIZE_MAX : CONFIG_EMAC1_FRAME_SIZE_MAX)
#define LWIP_EMAC_ALIGN(i) ((i) == 0 ? CONFIG_EMAC0_BUFFER_ALIGNMENT : CONFIG_EMAC1_BUFFER_ALIGNMENT)
#define LWIP_EMAC_SPD(i)   ((i) == 0 ? CONFIG_EMAC0_SPEED_MODE : CONFIG_EMAC1_SPEED_MODE)
#define LWIP_EMAC_ABIL(i)  ((i) == 0 ? CONFIG_EMAC0_PHY_ABILITY : CONFIG_EMAC1_PHY_ABILITY)
#define LWIP_EMAC_BUFSZ(i) (((LWIP_EMAC_FSMAX(i)) + (LWIP_EMAC_ALIGN(i)) - 1U) & ~((LWIP_EMAC_ALIGN(i)) - 1U))
#else
#define LWIP_EMAC_MTU(i)   (CONFIG_EMAC0_MTU)
#define LWIP_EMAC_RXSTK(i) (CONFIG_EMAC0_RX_STACK_SIZE)
#define LWIP_EMAC_TXTMO(i) (CONFIG_EMAC0_TX_BUF_TIMEOUT)
#define LWIP_EMAC_RXCNT(i) (CONFIG_EMAC0_RX_BUFF_CNT)
#define LWIP_EMAC_TXCNT(i) (CONFIG_EMAC0_TX_BUFF_CNT)
#define LWIP_EMAC_FSMIN(i) (CONFIG_EMAC0_FRAME_SIZE_MIN)
#define LWIP_EMAC_FSMAX(i) (CONFIG_EMAC0_FRAME_SIZE_MAX)
#define LWIP_EMAC_ALIGN(i) (CONFIG_EMAC0_BUFFER_ALIGNMENT)
#define LWIP_EMAC_SPD(i)   (CONFIG_EMAC0_SPEED_MODE)
#define LWIP_EMAC_ABIL(i)  (CONFIG_EMAC0_PHY_ABILITY)
#define LWIP_EMAC_BUFSZ(i) (LWIP_EMAC0_BUFFER_SIZE)
#endif

/* Dual-EMAC MAC address derivation (BL618DG only). */
#if defined(BL618DG)
#define LWIP_EMAC_FACTORY_MAC_PORT (1U)
#define LWIP_EMAC_DERIVED_MAC_XOR  (0x81U)
#endif

/** Number of EMAC ports actually enabled at runtime. */
static uint8_t lwip_emac_port_count;

/* phy cfg */
static const eth_phy_init_cfg_t emac_phy_cfg[LWIP_EMAC_MAX_PORT_COUNT] = {
    {
        .speed_mode = LWIP_EMAC_SPD(0),
        .local_auto_negotiation_ability = LWIP_EMAC_ABIL(0),
    },
#if LWIP_EMAC_MAX_PORT_COUNT > 1
    {
        .speed_mode = LWIP_EMAC_SPD(1),
        .local_auto_negotiation_ability = LWIP_EMAC_ABIL(1),
    },
#endif
};

/* emac cfg */
static const struct bflb_emac_config_s emac_hw_cfg[LWIP_EMAC_MAX_PORT_COUNT] = {
    {
        .mac_addr = { 0x18, 0xB9, 0x05, 0x12, 0x34, 0x56 },
        .clk_internal_mode = false,
#if defined(BL616CL) || defined(BL618DG)
        .md_clk_div = 79,
#else
        .md_clk_div = 39,
#endif
        .min_frame_len = LWIP_EMAC_FSMIN(0),
        .max_frame_len = LWIP_EMAC_FSMAX(0),
    },
#if LWIP_EMAC_MAX_PORT_COUNT > 1
    {
        .mac_addr = { 0x18, 0xB9, 0x05, 0x12, 0x34, 0x56 },
        .clk_internal_mode = false,
        .md_clk_div = 79,
        .min_frame_len = LWIP_EMAC_FSMIN(1),
        .max_frame_len = LWIP_EMAC_FSMAX(1),
    },
#endif
};

/** @brief lwIP network interface associated with the EMAC device. */
static struct netif gnetif[LWIP_EMAC_MAX_PORT_COUNT];
static lwip_emac_port_ctx_t emac_ctx[LWIP_EMAC_MAX_PORT_COUNT] = {
    {
        .task_name = "emac0_rx",
        .hostname = "emac0",
    },
#if LWIP_EMAC_MAX_PORT_COUNT > 1
    {
        .task_name = "emac1_rx",
        .hostname = "emac1",
    },
#endif
};
/* Per-port DMA buffer pools. Each port gets its own arrays so the buffer
 * count / frame size / alignment can differ between ports. */
static uint8_t ATTR_NOCACHE_NOINIT_RAM_SECTION __ALIGNED(CONFIG_EMAC0_BUFFER_ALIGNMENT)
    emac0_tx_buffer[CONFIG_EMAC0_TX_BUFF_CNT][LWIP_EMAC0_BUFFER_SIZE];
static uint8_t ATTR_NOCACHE_NOINIT_RAM_SECTION __ALIGNED(CONFIG_EMAC0_BUFFER_ALIGNMENT)
    emac0_rx_buffer[CONFIG_EMAC0_RX_BUFF_CNT][LWIP_EMAC0_BUFFER_SIZE];
#if LWIP_EMAC_MAX_PORT_COUNT > 1
static uint8_t ATTR_NOCACHE_NOINIT_RAM_SECTION __ALIGNED(CONFIG_EMAC1_BUFFER_ALIGNMENT)
    emac1_tx_buffer[CONFIG_EMAC1_TX_BUFF_CNT][LWIP_EMAC1_BUFFER_SIZE];
static uint8_t ATTR_NOCACHE_NOINIT_RAM_SECTION __ALIGNED(CONFIG_EMAC1_BUFFER_ALIGNMENT)
    emac1_rx_buffer[CONFIG_EMAC1_RX_BUFF_CNT][LWIP_EMAC1_BUFFER_SIZE];
#endif

/* Indexed accessors so the per-port cfg can point at the right pool. */
static void *const emac_tx_buffer[LWIP_EMAC_MAX_PORT_COUNT] = {
    (void *)emac0_tx_buffer,
#if LWIP_EMAC_MAX_PORT_COUNT > 1
    (void *)emac1_tx_buffer,
#endif
};
static void *const emac_rx_buffer[LWIP_EMAC_MAX_PORT_COUNT] = {
    (void *)emac0_rx_buffer,
#if LWIP_EMAC_MAX_PORT_COUNT > 1
    (void *)emac1_rx_buffer,
#endif
};
static const size_t emac_tx_buffer_size[LWIP_EMAC_MAX_PORT_COUNT] = {
    sizeof(emac0_tx_buffer),
#if LWIP_EMAC_MAX_PORT_COUNT > 1
    sizeof(emac1_tx_buffer),
#endif
};
static const size_t emac_rx_buffer_size[LWIP_EMAC_MAX_PORT_COUNT] = {
    sizeof(emac0_rx_buffer),
#if LWIP_EMAC_MAX_PORT_COUNT > 1
    sizeof(emac1_rx_buffer),
#endif
};

/**
 * @brief Log the current IPv4 configuration when the netif status changes.
 */
static void lwip_emac_netif_status_callback(struct netif *netif)
{
    lwip_emac_port_ctx_t *ctx = (lwip_emac_port_ctx_t *)netif->state;

    if (netif_is_up(netif) && !ip4_addr_isany_val(*netif_ip4_addr(netif))) {
        LOG_I("[EMAC%u] IPv4 address: %s\r\n", ctx->cfg.port, ip4addr_ntoa(netif_ip4_addr(netif)));
    }
}

/**
 * @brief Add and configure the EMAC network interfaces in lwIP.
 *
 * The interfaces are configured with zeroed IPv4 parameters when DHCP is
 * enabled, or with the module's static IPv4 parameters otherwise. The function
 * also selects the first interface as the default and starts the DHCP clients
 * when configured.
 *
 * @retval 0 The EMAC network interfaces were configured successfully.
 * @retval -1 A lwIP netif or DHCP operation failed.
 */
static int lwip_emac_netif_config(void)
{
    err_t err;

    for (uint8_t i = 0; i < lwip_emac_port_count; i++) {
        ip_addr_t ipaddr;
        ip_addr_t netmask;
        ip_addr_t gateway;

#if LWIP_DHCP
        ip_addr_set_zero_ip4(&ipaddr);
        ip_addr_set_zero_ip4(&netmask);
        ip_addr_set_zero_ip4(&gateway);
#else
        /* The static IPv4 subnet follows the EMAC data port, so a single-port
         * setup that uses port 1 still gets the port-1 subnet. */
        uint8_t subnet = emac_ctx[i].cfg.port == 1 ? IP_ADDR2_PORT1 : IP_ADDR2_PORT0;

        IP4_ADDR(&ipaddr, IP_ADDR0, IP_ADDR1, subnet, IP_ADDR3);
        IP4_ADDR(&netmask, NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
        IP4_ADDR(&gateway, GW_ADDR0, GW_ADDR1, subnet, GW_ADDR3);
#endif

        err = netifapi_netif_add(&gnetif[i], ip_2_ip4(&ipaddr), ip_2_ip4(&netmask), ip_2_ip4(&gateway), &emac_ctx[i],
                                 eth_emac_if_init, tcpip_input);
        if (err != ERR_OK) {
            LOG_E("[EMAC%u] netifapi_netif_add failed: %d\r\n", emac_ctx[i].cfg.port, err);
            return -1;
        }
        netif_set_status_callback(&gnetif[i], lwip_emac_netif_status_callback);
    }

    err = netifapi_netif_set_default(&gnetif[0]);
    if (err != ERR_OK) {
        LOG_E("[EMAC%u] netifapi_netif_set_default failed: %d\r\n", emac_ctx[0].cfg.port, err);
        return -1;
    }

#if LWIP_DHCP
    for (uint8_t i = 0; i < lwip_emac_port_count; i++) {
        err = netifapi_dhcp_start(&gnetif[i]);
        if (err != ERR_OK) {
            LOG_E("[EMAC%u] netifapi_dhcp_start failed: %d\r\n", emac_ctx[i].cfg.port, err);
            return -1;
        }
        LOG_I("[EMAC%u] DHCP client started\r\n", emac_ctx[i].cfg.port);
    }
#endif

    return 0;
}

/**
 * @brief Periodically update the lwIP link state from the Ethernet PHY.
 *
 * @param[in] pvParameters Unused FreeRTOS task parameter.
 *
 * @note This task runs indefinitely and deletes neither itself nor the EMAC
 *       network interface.
 */
static void lwip_emac_status_task(void *pvParameters)
{
    (void)pvParameters;

    LOG_I("EMAC link status task started\r\n");
    while (1) {
        vTaskDelay(LWIP_EMAC_STATUS_POLL_TICKS);
        for (uint8_t i = 0; i < lwip_emac_port_count; i++) {
            eth_link_state_update(&gnetif[i]);
        }
    }
}

/**
 * @brief Initialize and start the lwIP EMAC network interfaces.
 *
 * The application may either use the Kconfig defaults or provide a per-port
 * configuration. Defaults (port_cfgs == NULL or port_count == 0): the port
 * count comes from CONFIG_EMAC_DEV_COUNT (default 1), and each port's
 * EMAC/RMII data port and MDIO management port from CONFIG_EMAC0_* /
 * CONFIG_EMAC1_* (defaults 0 and 1). Otherwise port_cfgs selects each port's
 * EMAC/RMII data port (cfg.port) and MDIO management port (cfg.mdio_port),
 * which may differ to enable cross-wired RMII/MDIO layouts. The remaining cfg
 * fields are filled with module defaults.
 *
 * @param[in] port_cfgs  Optional per-port configurations; NULL selects defaults.
 * @param[in] port_count Number of ports; 0 selects defaults (chip max).
 *
 * @retval 0 All configured EMAC network interfaces were started successfully.
 * @retval -1 Invalid configuration or network-interface/task creation failed.
 */
int lwip_emac_start(const lwip_emac_port_cfg_t *port_cfgs, uint8_t port_count)
{
    static const uint8_t default_mac[6] = { 0x18, 0xB9, 0x05, 0x12, 0x34, 0x56 };
    lwip_emac_port_cfg_t default_cfgs[LWIP_EMAC_MAX_PORT_COUNT];
    BaseType_t ret;
    uint8_t factory_mac[6];

    if (port_cfgs == NULL || port_count == 0) {
        /* Defaults: per-port RMII/MDIO IDs from the Kconfig macros. */
        port_count = LWIP_EMAC_MAX_PORT_COUNT;
        for (uint8_t i = 0; i < port_count; i++) {
            default_cfgs[i].port = (i == 0) ? CONFIG_EMAC0_RMII_ID : CONFIG_EMAC1_RMII_ID;
            default_cfgs[i].mdio_port = (i == 0) ? CONFIG_EMAC0_MDIO_ID : CONFIG_EMAC1_MDIO_ID;
        }
        port_cfgs = default_cfgs;
    } else if (port_count > LWIP_EMAC_MAX_PORT_COUNT) {
        LOG_E("Invalid EMAC port config: count=%u (max %u)\r\n", port_count, LWIP_EMAC_MAX_PORT_COUNT);
        return -1;
    }
    lwip_emac_port_count = port_count;

    /* emac gpio init: RMII data pins follow cfg.port, MDIO pins follow cfg.mdio_port */
    for (uint8_t i = 0; i < lwip_emac_port_count; i++) {
        board_emac_rmii_gpio_init(port_cfgs[i].port);
        board_emac_mdio_gpio_init(port_cfgs[i].mdio_port);
    }

    LOG_I("Initialize lwIP stack\r\n");
    tcpip_init(NULL, NULL);

    memcpy(factory_mac, default_mac, sizeof(factory_mac));
#if !defined(BL702)
    /* Read the factory MAC address on chips that provide this API. */
    extern int mfg_media_read_macaddr_with_lock(uint8_t mac[6], uint8_t reload);
    mfg_media_read_macaddr_with_lock(factory_mac, 1);
#endif

    /* configure the EMAC ports */
    for (uint8_t i = 0; i < lwip_emac_port_count; i++) {
        lwip_emac_port_cfg_t cfg = {
            .port = port_cfgs[i].port,
            .mdio_port = port_cfgs[i].mdio_port,
            .phy_scan_start = EPHY_ADDR_MIN,
            .phy_scan_end = EPHY_ADDR_MAX,
            .mtu = LWIP_EMAC_MTU(i),
            .rx_stack_size = LWIP_EMAC_RXSTK(i),
            .tx_buf_timeout = LWIP_EMAC_TXTMO(i),
            .buffer_stride = LWIP_EMAC_BUFSZ(i),
            .phy_cfg = emac_phy_cfg[i],
            .emac_cfg = emac_hw_cfg[i],
            .tx_buffer = emac_tx_buffer[i],
            .tx_buffer_size = emac_tx_buffer_size[i],
            .rx_buffer = emac_rx_buffer[i],
            .rx_buffer_size = emac_rx_buffer_size[i],
        };

        memcpy(cfg.emac_cfg.mac_addr, factory_mac, sizeof(factory_mac));
        cfg.emac_cfg.mac_addr[0] &= (uint8_t)~0x01u;
#if LWIP_EMAC_MAX_PORT_COUNT > 1
        if (cfg.port != LWIP_EMAC_FACTORY_MAC_PORT) {
            cfg.emac_cfg.mac_addr[0] |= 0x02u;
            cfg.emac_cfg.mac_addr[5] ^= LWIP_EMAC_DERIVED_MAC_XOR;
        }
#endif
        emac_ctx[i].cfg = cfg;

        LOG_I("[EMAC%u] EMAC MAC address: %02X:%02X:%02X:%02X:%02X:%02X\r\n", cfg.port, cfg.emac_cfg.mac_addr[0],
              cfg.emac_cfg.mac_addr[1], cfg.emac_cfg.mac_addr[2], cfg.emac_cfg.mac_addr[3], cfg.emac_cfg.mac_addr[4],
              cfg.emac_cfg.mac_addr[5]);
    }

    /* configure the lwIP network interface */
    if (lwip_emac_netif_config() < 0) {
        return -1;
    }

    /* create the task that monitors the PHY link state and updates lwIP */
    ret = xTaskCreate(lwip_emac_status_task, "lwip_sta_update", LWIP_EMAC_STATUS_TASK_STACK_SIZE, NULL,
                      LWIP_EMAC_STATUS_TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        LOG_E("Create EMAC link status task failed\r\n");
        return -1;
    }

    return 0;
}

#ifdef CONFIG_SHELL
#include <shell.h>

/**
 * @brief Show the link state of every enabled EMAC port.
 *
 * Reads the link state and speed cached by lwip_emac_status_task().
 */
static int lwip_emac_link_cmd(int argc, char **argv)
{
    (void)argc;
    (void)argv;

    for (uint8_t i = 0; i < lwip_emac_port_count; i++) {
        uint8_t port = emac_ctx[i].cfg.port;

        if (emac_ctx[i].link_sta == EPHY_LINK_STA_UP) {
            const char *speed = "100M";
            const char *duplex = "full";

            switch (emac_ctx[i].speed_mode) {
                case EPHY_SPEED_MODE_10M_HALF_DUPLEX:
                    speed = "10M";
                    duplex = "half";
                    break;
                case EPHY_SPEED_MODE_10M_FULL_DUPLEX:
                    speed = "10M";
                    duplex = "full";
                    break;
                case EPHY_SPEED_MODE_100M_HALF_DUPLEX:
                    speed = "100M";
                    duplex = "half";
                    break;
                default:
                    break;
            }
            LOG_I("[EMAC%u] link: UP   speed: %s %s-duplex\r\n", port, speed, duplex);
        } else {
            LOG_I("[EMAC%u] link: DOWN\r\n", port);
        }
    }

    return 0;
}
SHELL_CMD_EXPORT_ALIAS(lwip_emac_link_cmd, lwip_emac_link, show emac link status);
#endif

uint8_t lwip_emac_started_count(void)
{
    return lwip_emac_port_count;
}

lwip_emac_port_ctx_t *lwip_emac_started_ctx(uint8_t index)
{
    return (index < lwip_emac_port_count) ? &emac_ctx[index] : NULL;
}

struct netif *lwip_emac_started_netif(uint8_t index)
{
    return (index < lwip_emac_port_count) ? &gnetif[index] : NULL;
}
