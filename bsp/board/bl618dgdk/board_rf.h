#ifndef __BOARD_RF_H__
#define __BOARD_RF_H__

#if __has_include("board_rf_overlay.h")
#include "board_rf_overlay.h"
#else

enum board_ctl_ops {
  /* @ rf configuration start {  */
  BRD_CTL_RF_RESET_DEFAULT,

  BRD_CTL_RF_INIT_WLAN,
  BRD_CTL_RF_DEINIT_WLAN,

  BRD_CTL_RF_INIT_BZ,
  BRD_CTL_RF_DEINIT_BZ,

  BRD_CTL_RF_INIT_ALL,
  BRD_CTL_RF_DEINIT_ALL,

  BRD_CTL_RF_SET_XTAL,
  BRD_CTL_RF_SET_CAPCODE,
  /* } rf configuration end @ */

};

int board_rf_ctl(enum board_ctl_ops ops, ...);

/** single antenna 
 *  ┌────────────┐
 *  │   BT PATH ─┼────── NC
 *  │            │       ┌────────────┐
 *  │   2G PATH ─┼──────►│  2G / 5G   │
 *  │   5G PATH ─┼──────►│  Diplexer  │──► Antenna
 *  └────────────┘       └────────────┘
 * */
void board_rf_single_ant_init(void);

/** single antenna with spdt
 *  ┌────────────┐       ┌──────┐
 *  │   BT PATH ─┼──────►| SPDT |      ┌────────────┐
 *  │   2G PATH ─┼──────►|      |─────►│  2G / 5G   │
 *  │            │       └──────┘      │            │──► Antenna
 *  │   5G PATH ─┼────────────────────►│  Diplexer  │
 *  └────────────┘                     └────────────┘
 *
 * @param pin_bt_path Even GPIO whose high level selects the BT path, or -1
 *                    when that switch-control input is not connected.
 * @param pin_2g_path Odd GPIO whose high level selects the 2G path, or -1
 *                    when that switch-control input is not connected.
 *
 * GPIO_FUNC_SPDT outputs high on an even GPIO and low on an odd GPIO when BT
 * wins PTA arbitration. The GPIO parity and the external switch truth table
 * must match the parameter roles above.
 * */
#ifdef CONFIG_WIFI6
void board_rf_single_ant_spdt_init(int pin_bt_path, int pin_2g_path);
#endif

/** force external spdt to use bt path, normally on bt path + 5g wifi for soft ap application
 * */
void board_rf_single_ant_spdt_force_bt_init(int pin_bt_path, int pin_2g_path);

/** normally for application which wants to use 2g path for bt/ieee 802.15.4 with this ant desgin
 * */
void board_rf_single_ant_spdt_force_2g_init(int pin_bt_path, int pin_2g_path);

/** dual antenna
 *  ┌────────────┐
 *  │   BT PATH ─┼───────────────────────► Antenna
 *  │            │       ┌────────────┐
 *  │   2G PATH ─┼──────►│  2G / 5G   │
 *  │   5G PATH ─┼──────►│  Diplexer  │──► Antenna
 *  └────────────┘       └────────────┘ 
 * */
void board_rf_dual_ant_init(void);

#endif
#endif
