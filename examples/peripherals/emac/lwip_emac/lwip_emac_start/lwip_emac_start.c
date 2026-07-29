/**
 * @file lwip_emac_start.c
 * @brief EMAC network interface initialization and link-state management.
 *
 * This module initializes the board-level EMAC pins and lwIP stack, creates
 * and configures the EMAC network interface, starts the DHCP client when
 * enabled, and periodically synchronizes the PHY link state with lwIP.
 */

#include <stdint.h>

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
#define IP_ADDR2                         (uint8_t)123
#define IP_ADDR3                         (uint8_t)100

#define NETMASK_ADDR0                    (uint8_t)255
#define NETMASK_ADDR1                    (uint8_t)255
#define NETMASK_ADDR2                    (uint8_t)255
#define NETMASK_ADDR3                    (uint8_t)0

#define GW_ADDR0                         (uint8_t)192
#define GW_ADDR1                         (uint8_t)168
#define GW_ADDR2                         (uint8_t)123
#define GW_ADDR3                         (uint8_t)1
/** @} */

/** @name EMAC link-status task configuration
 * @{ */
#define LWIP_EMAC_STATUS_TASK_STACK_SIZE 256
#define LWIP_EMAC_STATUS_TASK_PRIORITY   osPriorityHigh
#define LWIP_EMAC_STATUS_POLL_TICKS      200
/** @} */

/** @brief lwIP network interface associated with the EMAC device. */
static struct netif gnetif;

/**
 * @brief Log the current IPv4 configuration when the netif status changes.
 */
static void lwip_emac_netif_status_callback(struct netif *netif)
{
    if (netif_is_up(netif) && !ip4_addr_isany_val(*netif_ip4_addr(netif))) {
        LOG_I("IPv4 address: %s\r\n", ip4addr_ntoa(netif_ip4_addr(netif)));
    }
}

/**
 * @brief Add and configure the EMAC network interface in lwIP.
 *
 * The interface is configured with zeroed IPv4 parameters when DHCP is
 * enabled, or with the module's static IPv4 parameters otherwise. The function
 * also selects the interface as the default and starts the DHCP client when
 * configured.
 *
 * @retval 0 The EMAC network interface was configured successfully.
 * @retval -1 A lwIP netif or DHCP operation failed.
 */
static int lwip_emac_netif_config(void)
{
    ip_addr_t ipaddr;
    ip_addr_t netmask;
    ip_addr_t gateway;
    err_t err;

#if LWIP_DHCP
    ip_addr_set_zero_ip4(&ipaddr);
    ip_addr_set_zero_ip4(&netmask);
    ip_addr_set_zero_ip4(&gateway);
#else
    IP4_ADDR(&ipaddr, IP_ADDR0, IP_ADDR1, IP_ADDR2, IP_ADDR3);
    IP4_ADDR(&netmask, NETMASK_ADDR0, NETMASK_ADDR1, NETMASK_ADDR2, NETMASK_ADDR3);
    IP4_ADDR(&gateway, GW_ADDR0, GW_ADDR1, GW_ADDR2, GW_ADDR3);
#endif

    err = netifapi_netif_add(&gnetif,
                             ip_2_ip4(&ipaddr), ip_2_ip4(&netmask), ip_2_ip4(&gateway),
                             NULL, &eth_emac_if_init, &tcpip_input);
    if (err != ERR_OK) {
        LOG_E("netifapi_netif_add failed: %d\r\n", err);
        return -1;
    }

    netif_set_status_callback(&gnetif, lwip_emac_netif_status_callback);

    err = netifapi_netif_set_default(&gnetif);
    if (err != ERR_OK) {
        LOG_E("netifapi_netif_set_default failed: %d\r\n", err);
        return -1;
    }

#if LWIP_DHCP
    err = netifapi_dhcp_start(&gnetif);
    if (err != ERR_OK) {
        LOG_E("netifapi_dhcp_start failed: %d\r\n", err);
        return -1;
    }
    LOG_I("DHCP client started\r\n");
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
        eth_link_state_update(&gnetif);
    }
}

/**
 * @brief Initialize and start the lwIP EMAC network interface.
 *
 * This function initializes the RMII and MDIO pins, starts the lwIP TCP/IP
 * thread, configures the EMAC network interface, and creates the periodic PHY
 * link-state monitoring task.
 *
 * @retval 0 The EMAC network interface was started successfully.
 * @retval -1 Network-interface configuration or task creation failed.
 */
int lwip_emac_start(void)
{
    BaseType_t ret;
    struct bflb_emac_config_s emac_cfg = {
        .mac_addr = { 0x18, 0xB9, 0x05, 0x12, 0x34, 0x56 },
        .clk_internal_mode = false,
#if defined(BL616CL) || defined(BL618DG)
        .md_clk_div = 79,
#else
        .md_clk_div = 39,
#endif
        .min_frame_len = CONFIG_BSP_LWIP_EMAC_FRAME_SIZE_MIN,
        .max_frame_len = CONFIG_BSP_LWIP_EMAC_FRAME_SIZE_MAX,
    };

    /* emac gpio init */
    board_emac_rmii_gpio_init(BSP_EMAC_RMII_DEFAULT_PORT);
    board_emac_mdio_gpio_init(BSP_EMAC_MDIO_DEFAULT_PORT);

    LOG_I("Initialize lwIP stack\r\n");
    tcpip_init(NULL, NULL);

#if !defined(BL702)
    /* Read the factory MAC address on chips that provide this API. */
    extern int mfg_media_read_macaddr_with_lock(uint8_t mac[6], uint8_t reload);
    mfg_media_read_macaddr_with_lock((uint8_t *)emac_cfg.mac_addr, 1);
#endif
    lwip_emac_if_cfg(NULL, &emac_cfg);
    LOG_I("EMAC MAC address: %02X:%02X:%02X:%02X:%02X:%02X\r\n",
          emac_cfg.mac_addr[0], emac_cfg.mac_addr[1], emac_cfg.mac_addr[2],
          emac_cfg.mac_addr[3], emac_cfg.mac_addr[4], emac_cfg.mac_addr[5]);

    /* configure the lwIP network interface */
    if (lwip_emac_netif_config() < 0) {
        return -1;
    }

    /* create the task that monitors the PHY link state and updates lwIP */
    ret = xTaskCreate(lwip_emac_status_task, "lwip_sta_update", LWIP_EMAC_STATUS_TASK_STACK_SIZE,
                      NULL, LWIP_EMAC_STATUS_TASK_PRIORITY, NULL);
    if (ret != pdPASS) {
        LOG_E("Create EMAC link status task failed\r\n");
        return -1;
    }

    return 0;
}
