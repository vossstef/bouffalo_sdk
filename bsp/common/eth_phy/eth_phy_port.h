
#ifndef __ETH_PHY_PORT__
#define __ETH_PHY_PORT__

#include "stdint.h"

struct bflb_device_s;

void eth_phy_delay_ms(uint32_t ms);

const char *bsp_emac_get_device_name(uint8_t port);
struct bflb_device_s *bsp_emac_get_device(uint8_t port);

int eth_phy_mdio_read(struct bflb_device_s *mac_mdio_dev, uint8_t phy_addr, uint8_t reg_addr, uint16_t *data);
int eth_phy_mdio_write(struct bflb_device_s *mac_mdio_dev, uint8_t phy_addr, uint8_t reg_addr, uint16_t data);

#endif
