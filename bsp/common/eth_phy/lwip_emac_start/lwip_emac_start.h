/**
 * @file lwip_emac_start.h
 * @brief Public interface for starting the lwIP EMAC network interface.
 */

#ifndef LWIP_EMAC_START_H
#define LWIP_EMAC_START_H

#include "lwip_netif_emac.h"
#include "eth_phy.h"

/* Default EMAC port configuration. These defaults apply when the project
 * defconfig does not define the CONFIG_EMAC*_ macros (single-port default).
 * A project overrides them by setting e.g. CONFIG_EMAC_DEV_COUNT = 2 or
 * CONFIG_EMAC0_RMII_ID = 1 in its defconfig. */
#ifndef CONFIG_EMAC_DEV_COUNT
#define CONFIG_EMAC_DEV_COUNT 1
#endif
#ifndef CONFIG_EMAC0_RMII_ID
#define CONFIG_EMAC0_RMII_ID 0
#endif
#ifndef CONFIG_EMAC0_MDIO_ID
#define CONFIG_EMAC0_MDIO_ID 0
#endif
#ifndef CONFIG_EMAC1_RMII_ID
#define CONFIG_EMAC1_RMII_ID 1
#endif
#ifndef CONFIG_EMAC1_MDIO_ID
#define CONFIG_EMAC1_MDIO_ID 1
#endif

/* Per-port parameter groups. The EMAC1 group defaults to the EMAC0 group so
 * both ports run the same configuration. Set CONFIG_EMAC_DUAL_CFG = 1 and
 * override CONFIG_EMAC1_* in the project defconfig to give port 1 its own
 * parameters. */
#ifndef CONFIG_EMAC_DUAL_CFG
#define CONFIG_EMAC_DUAL_CFG 0
#endif

/* ---- EMAC0 parameter group (global default) ---- */
#ifndef CONFIG_EMAC0_MTU
#define CONFIG_EMAC0_MTU (1500)
#endif
#ifndef CONFIG_EMAC0_RX_STACK_SIZE
#define CONFIG_EMAC0_RX_STACK_SIZE (512)
#endif
#ifndef CONFIG_EMAC0_TX_BUF_TIMEOUT
#define CONFIG_EMAC0_TX_BUF_TIMEOUT (10)
#endif
#ifndef CONFIG_EMAC0_RX_BUFF_CNT
#define CONFIG_EMAC0_RX_BUFF_CNT (10)
#endif
#ifndef CONFIG_EMAC0_TX_BUFF_CNT
#define CONFIG_EMAC0_TX_BUFF_CNT (6)
#endif
#ifndef CONFIG_EMAC0_FRAME_SIZE_MIN
#define CONFIG_EMAC0_FRAME_SIZE_MIN (14 + 46 + 4)
#endif
#ifndef CONFIG_EMAC0_FRAME_SIZE_MAX
#define CONFIG_EMAC0_FRAME_SIZE_MAX (14 + 4 + 1500 + 4)
#endif
#ifndef CONFIG_EMAC0_BUFFER_ALIGNMENT
#define CONFIG_EMAC0_BUFFER_ALIGNMENT (32U)
#endif
#ifndef CONFIG_EMAC0_SPEED_MODE
#define CONFIG_EMAC0_SPEED_MODE (EPHY_SPEED_MODE_AUTO_NEGOTIATION)
#endif
#ifndef CONFIG_EMAC0_PHY_ABILITY
#if (defined(EMAC_SPEED_10M_SUPPORT) && EMAC_SPEED_10M_SUPPORT)
#define CONFIG_EMAC0_PHY_ABILITY \
    (EPHY_ABILITY_100M_TX | EPHY_ABILITY_100M_FULL_DUPLEX | EPHY_ABILITY_10M_T | EPHY_ABILITY_10M_FULL_DUPLEX)
#else
#define CONFIG_EMAC0_PHY_ABILITY (EPHY_ABILITY_100M_TX | EPHY_ABILITY_100M_FULL_DUPLEX)
#endif
#endif

/* ---- EMAC1 parameter group (defaults follow EMAC0) ---- */
#ifndef CONFIG_EMAC1_MTU
#define CONFIG_EMAC1_MTU (CONFIG_EMAC0_MTU)
#endif
#ifndef CONFIG_EMAC1_RX_STACK_SIZE
#define CONFIG_EMAC1_RX_STACK_SIZE (CONFIG_EMAC0_RX_STACK_SIZE)
#endif
#ifndef CONFIG_EMAC1_TX_BUF_TIMEOUT
#define CONFIG_EMAC1_TX_BUF_TIMEOUT (CONFIG_EMAC0_TX_BUF_TIMEOUT)
#endif
#ifndef CONFIG_EMAC1_RX_BUFF_CNT
#define CONFIG_EMAC1_RX_BUFF_CNT (CONFIG_EMAC0_RX_BUFF_CNT)
#endif
#ifndef CONFIG_EMAC1_TX_BUFF_CNT
#define CONFIG_EMAC1_TX_BUFF_CNT (CONFIG_EMAC0_TX_BUFF_CNT)
#endif
#ifndef CONFIG_EMAC1_FRAME_SIZE_MIN
#define CONFIG_EMAC1_FRAME_SIZE_MIN (CONFIG_EMAC0_FRAME_SIZE_MIN)
#endif
#ifndef CONFIG_EMAC1_FRAME_SIZE_MAX
#define CONFIG_EMAC1_FRAME_SIZE_MAX (CONFIG_EMAC0_FRAME_SIZE_MAX)
#endif
#ifndef CONFIG_EMAC1_BUFFER_ALIGNMENT
#define CONFIG_EMAC1_BUFFER_ALIGNMENT (CONFIG_EMAC0_BUFFER_ALIGNMENT)
#endif
#ifndef CONFIG_EMAC1_SPEED_MODE
#define CONFIG_EMAC1_SPEED_MODE (CONFIG_EMAC0_SPEED_MODE)
#endif
#ifndef CONFIG_EMAC1_PHY_ABILITY
#define CONFIG_EMAC1_PHY_ABILITY (CONFIG_EMAC0_PHY_ABILITY)
#endif

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the lwIP EMAC network interfaces.
 *
 * The application may either use the chip default or provide a per-port
 * configuration. For every configured port, lwip_emac_port_cfg_t.port selects
 * the EMAC/RMII data port (TX/RX) and lwip_emac_port_cfg_t.mdio_port selects
 * the MDIO management port used to access the PHY. The two may differ, which
 * enables cross-wired RMII/MDIO layouts.
 *
 * Defaults (port_cfgs == NULL or port_count == 0), chosen by Kconfig:
 *   - port count: CONFIG_EMAC_DEV_COUNT (default 1; set to 2 for BL618DG);
 *   - per-port RMII/MDIO ids: CONFIG_EMAC0_* and CONFIG_EMAC1_*.
 *
 * This function initializes the RMII/MDIO pins for every enabled port, starts
 * the lwIP stack, configures the network interfaces, starts DHCP when enabled,
 * and creates the periodic PHY link-state monitoring task.
 *
 * @param[in] port_cfgs  Optional per-port configurations; NULL selects defaults.
 * @param[in] port_count Number of ports; 0 selects defaults (chip max).
 *
 * @note Call once from task context. Runtime stop or restart is not supported.
 *
 * @retval 0 All configured EMAC network interfaces were started.
 * @retval -1 Invalid configuration or network-interface/task creation failed.
 */
int lwip_emac_start(const lwip_emac_port_cfg_t *port_cfgs, uint8_t port_count);

/**
 * @brief Number of EMAC ports started by lwip_emac_start().
 * @return Port count (0 before lwip_emac_start()).
 */
uint8_t lwip_emac_started_count(void);

/**
 * @brief Get the port context of a started port, by slot index.
 *
 * @param[in] index Slot index, 0 .. lwip_emac_started_count() - 1.
 * @return Port context, NULL when index is out of range.
 */
lwip_emac_port_ctx_t *lwip_emac_started_ctx(uint8_t index);

/**
 * @brief Get the lwIP netif of a started port, by slot index.
 *
 * @param[in] index Slot index, 0 .. lwip_emac_started_count() - 1.
 * @return Network interface, NULL when index is out of range.
 */
struct netif *lwip_emac_started_netif(uint8_t index);

#ifdef __cplusplus
}
#endif

#endif /* LWIP_EMAC_START_H */
