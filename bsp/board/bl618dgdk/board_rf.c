
#if __has_include("board_rf_overlay.h")
/* Use board_rf_overlay.c instead of this file */
#else

#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <errno.h>

#include "bflb_gpio.h"
#include "board_rf.h"
#include "shell.h"

#include "wl_api.h"
#include "rfparam_adapter.h"
#include "bl618dg_hbn.h"
#ifdef CONFIG_WIFI6
#include "coexm.h"
#endif

#define USER_UNUSED(a) ((void)(a))

extern void cmd_set_btble_standalone(int argc, char **argv);
extern void cmd_set_btble_combo(int argc, char **argv);

static struct wl_cfg_t * board_rf_phyrf_cfg_get(void)
{
#if defined(WL_API_RMEM_EN) && WL_API_RMEM_EN
    return wl_cfg_get((uint8_t *)WL_API_RMEM_ADDR);
#else
    return wl_cfg_get();
#endif
}

static int ctl_rf_configuration(enum board_ctl_ops ops, va_list args)
{
  struct wl_cfg_t *wl_cfg = board_rf_phyrf_cfg_get();
  int ret;

  switch (ops) {
  case BRD_CTL_RF_RESET_DEFAULT:
      do {
        uint32_t xtal_value;
        int full_cal = va_arg(args, int);

        HBN_Get_Xtal_Value(&xtal_value);

        /* reset to default param */
        wl_cfg->en_param_load = full_cal ? 1 : 0;
        wl_cfg->en_full_cal = full_cal ? 1 : 0;
        wl_cfg->mode = WL_API_MODE_ALL;
        wl_cfg->param.xtalfreq_hz = xtal_value;
        //wl_cfg->param.xtalcapcode_in = 32;
        //wl_cfg->param.xtalcapcode_out = 32;
        wl_cfg->capcode_set = rfparam_set_capcode;
        wl_cfg->capcode_get = rfparam_get_capcode;
        wl_cfg->param_load = rfparam_load;
      } while(0);
      break;

  case BRD_CTL_RF_INIT_WLAN:
    wl_cfg->mode = WL_API_MODE_WLAN;
    break;

  case BRD_CTL_RF_INIT_BZ:
    wl_cfg->mode = WL_API_MODE_BZ;
    break;

  case BRD_CTL_RF_INIT_ALL:
    wl_cfg->mode = WL_API_MODE_ALL;
    break;

  case BRD_CTL_RF_SET_XTAL:
    do {
      int xtal = va_arg(args, int);
      wl_cfg->param.xtalfreq_hz = xtal;
    } while (0);
    break;

  case BRD_CTL_RF_SET_CAPCODE:
    do {
      int cap_in = va_arg(args, int);
      int cap_out = va_arg(args, int);
      wl_cfg->param.xtalcapcode_in = cap_in;
      wl_cfg->param.xtalcapcode_out = cap_out;
    } while (0);
    break;

  default:
    break;
  }

  ret = wl_init();
  if (ret != WL_API_STATUS_OK) {
    return -EINVAL;
  }
  wl_cfg->en_param_load = 0;
  wl_cfg->en_full_cal = 0;

  return 0;
}

/* board configuration */
int board_rf_ctl(enum board_ctl_ops ops, ...)
{
  va_list ops_arg;
  int ret = -ENOSYS;

  va_start(ops_arg, ops);

  if (ops >= BRD_CTL_RF_RESET_DEFAULT && ops <= BRD_CTL_RF_SET_CAPCODE) {
    ret = ctl_rf_configuration(ops, ops_arg);
  } else {
    va_end(ops_arg);
    return -ENOSYS;
  }

  va_end(ops_arg);
  return ret;
}

void board_rf_single_ant_init(void)
{
    board_rf_phyrf_cfg_get();
    cmd_set_btble_combo(0, NULL);
    if (0 != rfparam_init(0, NULL, 0)) {
        printf("PHY RF init failed!\r\n");
        return;
    }
    cmd_set_btble_combo(0, NULL);
}

#ifdef CONFIG_WIFI6
void board_rf_single_ant_spdt_init(int pin_bt_path, int pin_2g_path)
{
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");

    if (pin_bt_path >= 0) {
        bflb_gpio_init(gpio, pin_bt_path, GPIO_FUNC_GPIO | GPIO_OUTPUT | GPIO_SMT_EN | GPIO_DRV_0);
        bflb_gpio_reset(gpio, pin_bt_path);
    }
    if (pin_2g_path >= 0) {
        bflb_gpio_init(gpio, pin_2g_path, GPIO_FUNC_GPIO | GPIO_OUTPUT | GPIO_SMT_EN | GPIO_DRV_0);
        bflb_gpio_set(gpio, pin_2g_path);
    }

    board_rf_phyrf_cfg_get();
    cmd_set_btble_combo(0, NULL);
    if (0 != rfparam_init(0, NULL, 0)) {
        printf("PHY RF init failed!\r\n");
        return;
    }
  
    if (pin_bt_path >= 0) {
        bflb_gpio_set(gpio, pin_bt_path);
    }
    if (pin_2g_path >= 0) {
        bflb_gpio_reset(gpio, pin_2g_path);
    }
    cmd_set_btble_standalone(0, NULL);
    if (0 != rfparam_init(0, NULL, 0)) {
        printf("PHY RF init failed!\r\n");
        return;
    }
  
    if (pin_bt_path >= 0) {
        bflb_gpio_init(gpio, pin_bt_path, GPIO_FUNC_SPDT | GPIO_ALTERNATE);
    }
    if (pin_2g_path >= 0) {
        bflb_gpio_init(gpio, pin_2g_path, GPIO_FUNC_SPDT | GPIO_ALTERNATE);
    }
    coexm_bt_set_spdt_ctrl(true);
}
#endif

void board_rf_single_ant_spdt_force_bt_init(int pin_bt_path, int pin_2g_path)
{
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");

    if (pin_bt_path >= 0) {
        bflb_gpio_init(gpio, pin_bt_path, GPIO_FUNC_GPIO | GPIO_OUTPUT | GPIO_SMT_EN | GPIO_DRV_0);
        bflb_gpio_reset(gpio, pin_bt_path);
    }
    if (pin_2g_path >= 0) {
        bflb_gpio_init(gpio, pin_2g_path, GPIO_FUNC_GPIO | GPIO_OUTPUT | GPIO_SMT_EN | GPIO_DRV_0);
        bflb_gpio_set(gpio, pin_2g_path);
    }

    board_rf_phyrf_cfg_get();
    cmd_set_btble_combo(0, NULL);
    if (0 != rfparam_init(0, NULL, 0)) {
        printf("PHY RF init failed!\r\n");
        return;
    }
  
    if (pin_bt_path >= 0) {
        bflb_gpio_set(gpio, pin_bt_path);
    }
    if (pin_2g_path >= 0) {
        bflb_gpio_reset(gpio, pin_2g_path);
    }
    cmd_set_btble_standalone(0, NULL);
    if (0 != rfparam_init(0, NULL, 0)) {
        printf("PHY RF init failed!\r\n");
        return;
    }
}

void board_rf_single_ant_spdt_force_2g_init(int pin_bt_path, int pin_2g_path)
{
    struct bflb_device_s *gpio = bflb_device_get_by_name("gpio");

    if (pin_bt_path >= 0) {
        bflb_gpio_init(gpio, pin_bt_path, GPIO_FUNC_GPIO | GPIO_OUTPUT | GPIO_SMT_EN | GPIO_DRV_0);
        bflb_gpio_reset(gpio, pin_bt_path);
    }
    if (pin_2g_path >= 0) {
        bflb_gpio_init(gpio, pin_2g_path, GPIO_FUNC_GPIO | GPIO_OUTPUT | GPIO_SMT_EN | GPIO_DRV_0);
        bflb_gpio_set(gpio, pin_2g_path);
    }

    board_rf_phyrf_cfg_get();
    cmd_set_btble_combo(0, NULL);
    if (0 != rfparam_init(0, NULL, 0)) {
        printf("PHY RF init failed!\r\n");
        return;
    }
    cmd_set_btble_combo(0, NULL);
}

void board_rf_dual_ant_init(void)
{
    board_rf_phyrf_cfg_get();
    cmd_set_btble_combo(0, NULL);
    if (0 != rfparam_init(0, NULL, 0)) {
        printf("PHY RF init failed!\r\n");
        return;
    }

    cmd_set_btble_standalone(0, NULL);
    if (0 != rfparam_init(0, NULL, 0)) {
        printf("PHY RF init failed!\r\n");
        return;
    }
}

int cmd_board_rf_single_init(int argc, char **argv)
{
    board_rf_single_ant_init();

    printf ("Board RF initialized for single antennas.\r\n");

    return 0;
}

#ifdef CONFIG_WIFI6
int cmd_board_rf_single_ant_spdt_init(int argc, char **argv)
{
    int pin_bt_path = -1, pin_2g_path = -1;

    if (argc >= 2) {
        pin_bt_path = atoi(argv[1]);
    }
    if (argc >= 3) {
        pin_2g_path = atoi(argv[2]);
    }

    if (pin_bt_path >= 0 || pin_2g_path >= 0) {
        board_rf_single_ant_spdt_init(pin_bt_path, pin_2g_path);

        printf ("SPDT BT path pin %d, 2G path pin %d.\r\n", pin_bt_path, pin_2g_path);
        printf ("Board RF initialized for single antennas with spdt.\r\n");
    }
    else {
        printf ("command usage: board_rf_single_ant_spdt_init <pin-bt-path> <pin-2g-path>\r\n", pin_bt_path, pin_2g_path);
    }

    return 0;
}
#endif

int cmd_board_rf_single_ant_spdt_bt_init(int argc, char **argv)
{
    int pin_bt_path = -1, pin_2g_path = -1;

    if (argc >= 2) {
        pin_bt_path = atoi(argv[1]);
    }
    if (argc >= 3) {
        pin_2g_path = atoi(argv[2]);
    }

    if (pin_bt_path >= 0 || pin_2g_path >= 0) {
        board_rf_single_ant_spdt_force_bt_init(pin_bt_path, pin_2g_path);
        printf ("SPDT BT path pin %d, 2G path pin %d.\r\n", pin_bt_path, pin_2g_path);
        printf ("Board RF initialized for single antennas with spdt, and force bluetooth to use bt path.\r\n");
    }
    else {
        printf ("command usage: board_rf_single_ant_spdt_bt_init <pin-bt-path> <pin-2g-path>\r\n", pin_bt_path, pin_2g_path);
    }
    return -1;
}

int cmd_board_rf_single_ant_spdt_2g_init(int argc, char **argv)
{
    int pin_bt_path = -1, pin_2g_path = -1;

    if (argc >= 2) {
        pin_bt_path = atoi(argv[1]);
    }
    if (argc >= 3) {
        pin_2g_path = atoi(argv[2]);
    }

    if (pin_bt_path >= 0 || pin_2g_path >= 0) {
        board_rf_single_ant_spdt_force_2g_init(pin_bt_path, pin_2g_path);

        printf ("SPDT BT path pin %d, 2G path pin %d.\r\n", pin_bt_path, pin_2g_path);
        printf ("Board RF initialized for single antennas with spdt, and force bluetooth to use 2g path.\r\n");
    }
    else {
        printf ("command usage: board_rf_single_ant_spdt_2g_init <pin-bt-path> <pin-2g-path>\r\n", pin_bt_path, pin_2g_path);
    }

    return 0;
}

int cmd_board_rf_dual_ant_init(int argc, char **argv)
{
    board_rf_dual_ant_init();

    printf ("Board RF initialized for dual antennas.");

    return 0;
}

SHELL_CMD_EXPORT_ALIAS(cmd_board_rf_single_init, board_rf_single_ant_init, init single ant);
#ifdef CONFIG_WIFI6
SHELL_CMD_EXPORT_ALIAS(cmd_board_rf_single_ant_spdt_init, board_rf_single_ant_spdt_init, init single ant/spdt);
#endif
SHELL_CMD_EXPORT_ALIAS(cmd_board_rf_single_ant_spdt_bt_init, board_rf_single_ant_spdt_bt_init, init single ant/spdt to use bt path);
SHELL_CMD_EXPORT_ALIAS(cmd_board_rf_single_ant_spdt_2g_init, board_rf_single_ant_spdt_2g_init, init single ant/spdt to use 2g path);
SHELL_CMD_EXPORT_ALIAS(cmd_board_rf_dual_ant_init, board_rf_dual_ant_init, init dual ant hardware);

#endif
