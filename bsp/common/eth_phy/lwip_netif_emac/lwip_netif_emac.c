/* Includes ------------------------------------------------------------------*/
/* emac and phy */
#include "bflb_emac.h"

#include "eth_phy.h"
#include "ephy_general.h"
#include "ephy_lan8720.h"

#include "lwip/opt.h"
#include "lwip/tcpip.h"
#include "lwip/timeouts.h"
#include "lwip/netif.h"
#if LWIP_DHCP
#include "lwip/dhcp.h"
#endif
#if LWIP_IPV6
#include "lwip/ethip6.h"
#endif
#include "lwip/netifapi.h"
#include "netif/etharp.h"

#include "lwip_netif_emac.h"

/* os */
#include <FreeRTOS.h>
#include "queue.h"

#define DBG_TAG "LWIP_EMAC"
#include "log.h"

/* Network interface name */
#define IFNAME0 'e'
#define IFNAME1 'x'

static void lwip_emac_irq_cb(void *arg, uint32_t irq_event, struct bflb_emac_trans_desc_s *trans_desc);
static void ethernetif_input(void *argument);
static err_t emac_low_level_output(struct netif *netif, struct pbuf *p);

/* tx buff def */
static uint8_t ATTR_NOCACHE_NOINIT_RAM_SECTION __ALIGNED(32) emac_tx_buff[CONFIG_BSP_LWIP_EMAC_TX_BUFF_CNT][CONFIG_BSP_LWIP_EMAC_TX_BUFF_SIZE];
/* rx buff def */
static uint8_t ATTR_NOCACHE_NOINIT_RAM_SECTION __ALIGNED(32) emac_rx_buff[CONFIG_BSP_LWIP_EMAC_RX_BUFF_CNT][CONFIG_BSP_LWIP_EMAC_RX_BUFF_SIZE];

/* tx pool queue */
static QueueHandle_t tx_pool_queue;
/* rx process queue */
static QueueHandle_t rx_process_queue;
/* rx task  */
static TaskHandle_t emac_rx_handle;

/* emac */
static struct bflb_device_s *emacx;
/* eth phy */
static eth_phy_ctrl_t phy_ctrl;

/* emac debug info */
struct lwip_emac_debug_info_s {
    struct {
        uint32_t push_cnt;
        uint32_t success_cnt;
        uint32_t error_cnt;
        uint64_t total_size;
    } tx;
    struct {
        uint32_t push_cnt;
        uint32_t success_cnt;
        uint32_t error_cnt;
        uint32_t busy_cnt;
        uint64_t total_size;
    } rx;
};

static volatile struct lwip_emac_debug_info_s emac_debug_info;

/* phy cfg */
static eth_phy_init_cfg_t phy_cfg = {
    .speed_mode = EPHY_SPEED_MODE_AUTO_NEGOTIATION,
#if (defined(EMAC_SPEED_10M_SUPPORT) && EMAC_SPEED_10M_SUPPORT)
    .local_auto_negotiation_ability = EPHY_ABILITY_100M_TX | EPHY_ABILITY_100M_FULL_DUPLEX | EPHY_ABILITY_10M_T | EPHY_ABILITY_10M_FULL_DUPLEX,
#else
    .local_auto_negotiation_ability = EPHY_ABILITY_100M_TX | EPHY_ABILITY_100M_FULL_DUPLEX,
#endif
};

/* emac cfg */
static struct bflb_emac_config_s emac_cfg = {
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

void lwip_emac_if_cfg(eth_phy_init_cfg_t *phy_cfg_new, struct bflb_emac_config_s *emac_cfg_new)
{
    if (phy_cfg_new) {
        phy_cfg = *phy_cfg_new;
    }
    if (emac_cfg_new) {
        emac_cfg = *emac_cfg_new;
    }
}

static int emac_low_level_init(struct netif *netif)
{
    int ret;

    /* maximum transfer unit */
    netif->mtu = CONFIG_BSP_LWIP_EMAC_NETIF_MTU;

    /* set MAC hardware address length */
    netif->hwaddr_len = ETH_HWADDR_LEN;

    netif->hwaddr[0] = emac_cfg.mac_addr[0];
    netif->hwaddr[1] = emac_cfg.mac_addr[1];
    netif->hwaddr[2] = emac_cfg.mac_addr[2];
    netif->hwaddr[3] = emac_cfg.mac_addr[3];
    netif->hwaddr[4] = emac_cfg.mac_addr[4];
    netif->hwaddr[5] = emac_cfg.mac_addr[5];

    /* emac init */
    emacx = bsp_emac_get_device(BSP_EMAC_RMII_DEFAULT_PORT);
    if (emacx == NULL) {
        LOG_E("device_get error\r\n");
        return -ERR_IF;
    }
    bflb_emac_init(emacx, &emac_cfg);
    bflb_emac_irq_attach(emacx, lwip_emac_irq_cb, NULL);
    // bflb_emac_feature_control(emacx, EMAC_CMD_SET_RX_PROMISCUOUS, false);

    /* scan eth_phy */
    phy_ctrl.mac_mdio_dev = bsp_emac_get_device(BSP_EMAC_MDIO_DEFAULT_PORT);
    if (phy_ctrl.mac_mdio_dev == NULL) {
        LOG_E("mdio device_get error\r\n");
        return -ERR_IF;
    }

    ret = eth_phy_scan(&phy_ctrl, BSP_EMAC_PHY_DEFAULT_SCAN_START, BSP_EMAC_PHY_DEFAULT_SCAN_END);
    if (ret < 0) {
        return -ERR_IF;
    }
    /* eth_phy init */
    ret = eth_phy_init(&phy_ctrl, &phy_cfg);
    if (ret < 0) {
        return -ERR_IF;
    }

    /* LAN8720 Timing Adjustment: When in ref_clk input mode, invert the rx_clk. */
    if ((emac_cfg.clk_internal_mode == false) &&
        (phy_ctrl.phy_drv->phy_id == EPHY_LAN8720_ID)) {
        LOG_W("Invert rx_clk for LAN8720 Timing Adjustment.\r\n");
        bflb_emac_feature_control(emacx, EMAC_CMD_SET_MAC_RX_CLK_INVERT, true);
    }

    /* tx pool queue init */
    tx_pool_queue = xQueueCreate(CONFIG_BSP_LWIP_EMAC_TX_BUFF_CNT, sizeof(struct bflb_emac_trans_desc_s));

    /* rx process queue init */
    rx_process_queue = xQueueCreate(CONFIG_BSP_LWIP_EMAC_RX_BUFF_CNT, sizeof(struct bflb_emac_trans_desc_s));

    /* initialize tx buffer pool */
    for (int i = 0; i < CONFIG_BSP_LWIP_EMAC_TX_BUFF_CNT; i++) {
        struct bflb_emac_trans_desc_s tx_desc = {
            .buff_addr = emac_tx_buff[i],
        };
        xQueueSend(tx_pool_queue, &tx_desc, portMAX_DELAY);
    }

    /* initialize hardware rx descriptor queue */
    for (int i = 0; i < CONFIG_BSP_LWIP_EMAC_RX_BUFF_CNT; i++) {
        struct bflb_emac_trans_desc_s rx_desc = {
            .buff_addr = emac_rx_buff[i],
        };
        bflb_emac_queue_rx_push(emacx, &rx_desc);
        emac_debug_info.rx.push_cnt += 1;
    }

    /* create the task that handles the ETH_MAC */
    LOG_I("[OS] Starting emac rx task...\r\n");
    xTaskCreate(ethernetif_input, (char *)"emac_rx_task", CONFIG_BSP_LWIP_EMAC_RX_STACK_SIZE, netif, osPriorityHigh, &emac_rx_handle);

    /* Keep EMAC running across PHY link changes. Stopping the controller resets
     * its BD traversal position and requires a complete descriptor rebuild. */
    bflb_emac_feature_control(emacx, EMAC_CMD_SET_TX_EN, true);
    bflb_emac_feature_control(emacx, EMAC_CMD_SET_RX_EN, true);

    return ERR_OK;
}

/* tx/rx done callback  */
static void lwip_emac_irq_cb(void *arg, uint32_t irq_event, struct bflb_emac_trans_desc_s *trans_desc)
{
    BaseType_t pxHigherPriorityTaskWoken = pdFALSE;

    switch (irq_event) {
        case EMAC_IRQ_EVENT_RX_BUSY:
            LOG_W("rx busy\r\n");
            /* debug */
            emac_debug_info.rx.busy_cnt++;
            break;

        case EMAC_IRQ_EVENT_RX_CTRL_FRAME:
            LOG_W("rx ctrl frame, drop.\r\n");
            trans_desc->attr_flag = 0;
            trans_desc->err_status = 0;
            bflb_emac_queue_rx_push(emacx, trans_desc);
            /* debug */
            emac_debug_info.rx.push_cnt++;
            emac_debug_info.rx.success_cnt++;
            emac_debug_info.rx.total_size += trans_desc->data_len;
            break;

        case EMAC_IRQ_EVENT_RX_ERR_FRAME:
            LOG_W("rx err frame sta %d, drop.\r\n", trans_desc->err_status);
            trans_desc->attr_flag = 0;
            trans_desc->err_status = 0;
            bflb_emac_queue_rx_push(emacx, trans_desc);
            /* debug */
            emac_debug_info.rx.push_cnt++;
            emac_debug_info.rx.error_cnt++;
            break;

        case EMAC_IRQ_EVENT_RX_FRAME:
            xQueueSendFromISR(rx_process_queue, trans_desc, &pxHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
            /* debug */
            emac_debug_info.rx.success_cnt++;
            emac_debug_info.rx.total_size += trans_desc->data_len;
            break;

        case EMAC_IRQ_EVENT_TX_FRAME:
            xQueueSendFromISR(tx_pool_queue, trans_desc, &pxHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
            /* debug */
            emac_debug_info.tx.success_cnt++;
            emac_debug_info.tx.total_size += trans_desc->data_len;
            break;

        case EMAC_IRQ_EVENT_TX_ERR_FRAME:
            xQueueSendFromISR(tx_pool_queue, trans_desc, &pxHigherPriorityTaskWoken);
            portYIELD_FROM_ISR(pxHigherPriorityTaskWoken);
            /* debug */
            if (trans_desc->err_status & (~EMAC_TX_STA_ERR_CS)) {
                LOG_W("tx err sta:%d\r\n", trans_desc->err_status);
                emac_debug_info.tx.error_cnt++;
            } else {
                emac_debug_info.tx.success_cnt++;
                emac_debug_info.tx.total_size += trans_desc->data_len;
            }
            break;

        default:
            break;
    }
}

/**
  * @brief This function should do the actual transmission of the packet. The packet is
  * contained in the pbuf that is passed to the function. This pbuf
  * might be chained.
  *
  * @param netif the lwip network interface structure for this ethernetif
  * @param p the MAC packet to send (e.g. IP packet including MAC addresses and type)
  * @return ERR_OK if the packet could be sent
  *         an err_t value if the packet couldn't be sent
  *
  * @note Returning ERR_MEM here if a DMA queue of your MAC is full can lead to
  *       strange results. You might consider waiting for space in the DMA queue
  *       to become available since the stack doesn't retry to send a packet
  *       dropped because of memory failure (except for the TCP timers).
  */
static err_t emac_low_level_output(struct netif *netif, struct pbuf *p)
{
    struct bflb_emac_trans_desc_s trans_desc;
    uint16_t byte_copy;

    if (p->tot_len > CONFIG_BSP_LWIP_EMAC_TX_BUFF_SIZE) {
        LOG_E("tx tot_len size over\r\n");
        return ERR_BUF;
    }

    if (xQueueReceive(tx_pool_queue, &trans_desc, CONFIG_BSP_LWIP_EMAC_GET_TXBUF_TIMEOUT) == pdFALSE) {
        LOG_W("no tx buff\r\n");
        return ERR_MEM;
    }

    byte_copy = pbuf_copy_partial(p, trans_desc.buff_addr, p->tot_len, 0);
    if (byte_copy != p->tot_len) {
        LOG_E("tx copy failed\r\n");
        xQueueSend(tx_pool_queue, &trans_desc, 0);
        return ERR_BUF;
    }

    trans_desc.data_len = byte_copy;
    trans_desc.attr_flag = 0;
    trans_desc.err_status = 0;

    emac_debug_info.tx.push_cnt += 1;
    bflb_emac_queue_tx_push(emacx, &trans_desc);

    return ERR_OK;
}

/**
  * @brief This function is the ethernetif_input task, it is processed when a packet
  * is ready to be read from the interface. It uses the function low_level_input()
  * that should handle the actual reception of bytes from the network
  * interface. Then the type of the received packet is determined and
  * the appropriate input function is called.
  *
  * @param netif the lwip network interface structure for this ethernetif
  */
static void ethernetif_input(void *argument)
{
    struct netif *netif = (struct netif *)argument;
    struct pbuf *p;
    struct bflb_emac_trans_desc_s trans_desc;

    while (1) {
        xQueueReceive(rx_process_queue, &trans_desc, portMAX_DELAY);

#if PBUF_POOL_SIZE > 0
        p = pbuf_alloc(PBUF_RAW, trans_desc.data_len, PBUF_POOL);
#else
        p = pbuf_alloc(PBUF_RAW, trans_desc.data_len, PBUF_RAM);
#endif

        if (p != NULL) {
            if (pbuf_take(p, trans_desc.buff_addr, trans_desc.data_len) != ERR_OK ||
                netif->input(p, netif) != ERR_OK) {
                pbuf_free(p);
            }
        }

        emac_debug_info.rx.push_cnt += 1;
        bflb_emac_queue_rx_push(emacx, &trans_desc);
    }
}
/**
  * @brief Should allocate a pbuf and transfer the bytes of the incoming
  * packet from the interface into the pbuf.
  *
  * @param netif the lwip network interface structure for this ethernetif
  * @return a pbuf filled with the received packet (including MAC header)
  *         NULL on memory error
  */

/**
  * @brief Should be called at the beginning of the program to set up the
  * network interface. It calls the function low_level_init() to do the
  * actual setup of the hardware.
  *
  * This function should be passed as a parameter to netif_add().
  *
  * @param netif the lwip network interface structure for this ethernetif
  * @return ERR_OK if the loopif is initialized
  *         ERR_MEM if private data couldn't be allocated
  *         any other err_t on error
  */
err_t eth_emac_if_init(struct netif *netif)
{
    int ret;

    LWIP_ASSERT("netif != NULL", (netif != NULL));

#if LWIP_NETIF_HOSTNAME
    /* Initialize interface hostname */
    netif->hostname = "lwip";
#endif /* LWIP_NETIF_HOSTNAME */

    netif->name[0] = IFNAME0;
    netif->name[1] = IFNAME1;

    /* We directly use etharp_output() here to save a function call.
     * You can instead declare your own function an call etharp_output()
     * from it if you have to do some checks before sending (e.g. if link
     * is available...) */
    netif->output = etharp_output;
    netif->linkoutput = emac_low_level_output;
    /* device capabilities */
    netif->flags |= NETIF_FLAG_BROADCAST | NETIF_FLAG_ETHARP;

#if LWIP_IPV6
    netif->output_ip6 = ethip6_output;
    netif->flags |= (NETIF_FLAG_ETHERNET | NETIF_FLAG_IGMP | NETIF_FLAG_MLD6);
#endif

    /* initialize the hardware */
    ret = emac_low_level_init(netif);
    if (ret < 0) {
        return ret;
    }

    /* set netif up */
    netif_set_up(netif);
    return ERR_OK;
}

/**
 * @brief Configure the EMAC speed and duplex mode for the negotiated PHY mode.
 *
 * @param[in] speed Negotiated PHY speed and duplex mode.
 */
static void emac_link_mode_config(int speed)
{
    /* 10M/100M speed mode */
    if (speed == EPHY_SPEED_MODE_10M_HALF_DUPLEX || speed == EPHY_SPEED_MODE_10M_FULL_DUPLEX) {
#if (defined(EMAC_SPEED_10M_SUPPORT) && EMAC_SPEED_10M_SUPPORT)
        bflb_emac_feature_control(emacx, EMAC_CMD_SET_SPEED_10M, 0);
#else
        LOG_E("10M speed mode not support, please check EMAC_SPEED_10M_SUPPORT config !!!\r\n");
#endif
    } else {
        bflb_emac_feature_control(emacx, EMAC_CMD_SET_SPEED_100M, 0);
    }

    /* full/half duplex mode */
    if (speed == EPHY_SPEED_MODE_10M_FULL_DUPLEX || speed == EPHY_SPEED_MODE_100M_FULL_DUPLEX) {
        bflb_emac_feature_control(emacx, EMAC_CMD_SET_FULL_DUPLEX, true);
    } else {
        bflb_emac_feature_control(emacx, EMAC_CMD_SET_FULL_DUPLEX, false);
    }

    if (speed == EPHY_SPEED_MODE_10M_HALF_DUPLEX) {
        LOG_I("eth_phy speed: 10M_HALF_DUPLEX\r\n");
    } else if (speed == EPHY_SPEED_MODE_10M_FULL_DUPLEX) {
        LOG_I("eth_phy speed: 10M_FULL_DUPLEX\r\n");
    } else if (speed == EPHY_SPEED_MODE_100M_HALF_DUPLEX) {
        LOG_I("eth_phy speed: 100M_HALF_DUPLEX\r\n");
    } else if (speed == EPHY_SPEED_MODE_100M_FULL_DUPLEX) {
        LOG_I("eth_phy speed: 100M_FULL_DUPLEX\r\n");
    }
}

/**
 * @brief Synchronize the lwIP link state with the Ethernet PHY.
 *
 * The EMAC controller remains enabled across link changes because stopping it
 * resets the hardware BD traversal position. TX/RX queues and descriptors are
 * initialized once in emac_low_level_init() and are not rebuilt here.
 *
 * @param[in] netif lwIP network interface associated with the EMAC device.
 */
void eth_link_state_update(struct netif *netif)
{
    static int speed_mode = 0;
    static int link_sta = 0;

    int sta = eth_phy_ctrl(&phy_ctrl, EPHY_CMD_GET_LINK_STA, 0);
    int speed = eth_phy_ctrl(&phy_ctrl, EPHY_CMD_GET_SPEED_MODE, 0);

    if (sta != EPHY_LINK_STA_UP) {
        if (link_sta == EPHY_LINK_STA_UP) {
            LOG_W("Lwip Eth Emac LinkDown !!!\r\n");
            netifapi_netif_set_link_down(netif);
        }

        link_sta = 0;
        speed_mode = 0;
        return;
    }

    if (link_sta != EPHY_LINK_STA_UP) {
        LOG_W("Lwip Eth Emac LinkUp !!!\r\n");
        emac_link_mode_config(speed);
        speed_mode = speed;
        link_sta = sta;
        netifapi_netif_set_link_up(netif);
        return;
    }

    if (speed_mode != speed) {
        LOG_W("Lwip Eth Emac Speed Mode Has Changed !!!\r\n");
        netifapi_netif_set_link_down(netif);
        emac_link_mode_config(speed);
        speed_mode = speed;
        netifapi_netif_set_link_up(netif);
    }
}

#ifdef CONFIG_SHELL
#include <shell.h>

int lwip_emac_info_cmd(int argc, char **argv)
{
    uint32_t tx_db_avail = bflb_emac_feature_control(emacx, EMAC_CMD_GET_TX_DB_AVAILABLE, 0);
    uint32_t rx_db_avail = bflb_emac_feature_control(emacx, EMAC_CMD_GET_RX_DB_AVAILABLE, 0);

    LOG_I("TX: success cnt:%d, error cnt:%d, total size:%lldByte\r\n",
          emac_debug_info.tx.success_cnt, emac_debug_info.tx.error_cnt, emac_debug_info.tx.total_size);
    LOG_I("    push_cnt:%d, tx_db available:%d, tx_bd_ptr:%d\r\n",
          emac_debug_info.tx.push_cnt, tx_db_avail, bflb_emac_feature_control(emacx, EMAC_CMD_GET_TX_BD_PTR, 0));

    LOG_I("RX: success cnt:%d, error cnt:%d, total size:%lldByte\r\n",
          emac_debug_info.rx.success_cnt, emac_debug_info.rx.error_cnt, emac_debug_info.rx.total_size);
    LOG_I("    push_cnt:%d, rx_db available:%d, rx_bd_ptr:%d, busy cnt:%d\r\n",
          emac_debug_info.rx.push_cnt, rx_db_avail, bflb_emac_feature_control(emacx, EMAC_CMD_GET_RX_BD_PTR, 0), emac_debug_info.rx.busy_cnt);
    LOG_RI("\r\n");

    return 0;
}
SHELL_CMD_EXPORT_ALIAS(lwip_emac_info_cmd, lwip_emac_info, put netif emac info);

#endif
