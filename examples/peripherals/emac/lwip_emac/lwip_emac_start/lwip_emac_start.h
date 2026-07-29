/**
 * @file lwip_emac_start.h
 * @brief Public interface for starting the lwIP EMAC network interface.
 */

#ifndef LWIP_EMAC_START_H
#define LWIP_EMAC_START_H

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Initialize and start the lwIP EMAC network interface.
 *
 * This function initializes the board-level EMAC pins and lwIP stack,
 * configures the EMAC network interface, starts DHCP when enabled, and creates
 * the periodic PHY link-state monitoring task.
 *
 * @retval 0 The EMAC network interface was started successfully.
 * @retval -1 Network-interface configuration or task creation failed.
 */
int lwip_emac_start(void);

#ifdef __cplusplus
}
#endif

#endif /* LWIP_EMAC_START_H */
