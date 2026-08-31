/****************************************************************************
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "FreeRTOS.h"
#include "task.h"
#include "timers.h"

#include <lwip/tcpip.h>
#include <lwip/sockets.h>
#include <lwip/netdb.h>

#include "wifi_mgmr_ext.h"
#include "wifi_mgmr_coex.h"

#include "bflb_irq.h"
#include "bflb_uart.h"

#include "rfparam_adapter.h"
#include "async_event.h"
#include "board.h"
#include "shell.h"


#include "bluetooth.h"
#include "conn.h"
#include "conn_internal.h"
#if defined(BL702)
#include "ble_lib_api.h"
#elif defined(BL602)
#include "ble_lib_api.h"
#include "bl602_glb.h"
#include "rfparam_adapter.h"
#elif defined(BL616)
#include "btble_lib_api.h"
#include "bl616_glb.h"
#include "rfparam_adapter.h"
#elif defined(BL616CL)
#include "btble_lib_api.h"
#include "bl616cl_glb.h"
#include "rfparam_adapter.h"
#elif defined(BL618DG)
#include "btble_lib_api.h"
#include "bl618dg_glb.h"
#include "rfparam_adapter.h"
#endif

#include "ble_cli_cmds.h"
#include "hci_driver.h"
#include "hci_core.h"
#if defined(CONFIG_BT_SETTINGS)
#include "bflb_mtd.h"
#include "easyflash.h"
#endif

#define DBG_TAG "MAIN"
#include "log.h"
#include "fhost_api.h"
#include "wifi_mgmr.h"
#include "mm.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/****************************************************************************
 * Private Types
 ****************************************************************************/

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct bflb_device_s *uart0;

extern void shell_init_with_task(struct bflb_device_s *shell);

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Functions
 ****************************************************************************/


void wifi_event_handler(async_input_event_t ev, void *priv)
{
    uint32_t code = ev->code;
    switch (code) {
        case CODE_WIFI_ON_INIT_DONE: {
            LOG_I("[APP] [EVT] %s, CODE_WIFI_ON_INIT_DONE\r\n", __func__);
            wifi_mgmr_task_start();
        } break;
        case CODE_WIFI_ON_MGMR_DONE: {
            LOG_I("[APP] [EVT] %s, CODE_WIFI_ON_MGMR_DONE\r\n", __func__);
        } break;
        case CODE_WIFI_ON_SCAN_DONE: {
            LOG_I("[APP] [EVT] %s, CODE_WIFI_ON_SCAN_DONE\r\n", __func__);
            wifi_mgmr_sta_scanlist();
        } break;
        case CODE_WIFI_ON_CONNECTED: {
            LOG_I("[APP] [EVT] %s, CODE_WIFI_ON_CONNECTED\r\n", __func__);
            void mm_sec_keydump();
            mm_sec_keydump();
        } break;
        #ifdef CODE_WIFI_ON_GOT_IP_ABORT
        case CODE_WIFI_ON_GOT_IP_ABORT: {
            LOG_I("[APP] [EVT] %s, CODE_WIFI_ON_GOT_IP_ABORT\r\n", __func__);
        } break;
        #endif
        #ifdef CODE_WIFI_ON_GOT_IP_TIMEOUT
        case CODE_WIFI_ON_GOT_IP_TIMEOUT: {
            LOG_I("[APP] [EVT] %s, CODE_WIFI_ON_GOT_IP_TIMEOUT\r\n", __func__);
        } break;
        #endif
        case CODE_WIFI_ON_GOT_IP: {
            LOG_I("[APP] [EVT] %s, CODE_WIFI_ON_GOT_IP\r\n", __func__);
            LOG_I("[SYS] Memory left is %d Bytes\r\n", kfree_size(0));
        } break;
        case CODE_WIFI_ON_DISCONNECT: {
            LOG_I("[APP] [EVT] %s, CODE_WIFI_ON_DISCONNECT\r\n", __func__);
        } break;
        case CODE_WIFI_ON_AP_STARTED: {
            LOG_I("[APP] [EVT] %s, CODE_WIFI_ON_AP_STARTED\r\n", __func__);
        } break;
        case CODE_WIFI_ON_AP_STOPPED: {
            LOG_I("[APP] [EVT] %s, CODE_WIFI_ON_AP_STOPPED\r\n", __func__);
        } break;
        case CODE_WIFI_ON_AP_STA_ADD: {
            LOG_I("[APP] [EVT] [AP] [ADD] %lld\r\n", xTaskGetTickCount());
        } break;
        case CODE_WIFI_ON_AP_STA_DEL: {
            LOG_I("[APP] [EVT] [AP] [DEL] %lld\r\n", xTaskGetTickCount());
        } break;
        default: {
            LOG_I("[APP] [EVT] Unknown code %u \r\n", code);
        }
    }
}

void wifi_start_firmware_task(void *param)
{
    LOG_I("Starting wifi ...\r\n");

    async_register_event_filter(EV_WIFI, wifi_event_handler, NULL);

    wifi_task_create();

    LOG_I("Starting fhost ...\r\n");
    fhost_init();

    vTaskDelete(NULL);
}

static void ble_connected(struct bt_conn *conn, uint8_t err)
{
    if (err || conn->type != BT_CONN_TYPE_LE) {
        return;
    }

    printf("%s", __func__);
}

static void ble_disconnected(struct bt_conn *conn, uint8_t reason)
{
    int ret;

    if (conn->type != BT_CONN_TYPE_LE) {
        return;
    }

    printf("%s", __func__);

    ret = set_adv_enable(true);
    if (ret) {
        printf("Restart adv fail.\r\n");
    }
}

static struct bt_conn_cb ble_conn_callbacks = {
    .connected = ble_connected,
    .disconnected = ble_disconnected,
};

void bt_enable_cb(int err)
{
    if (!err) {
        bt_addr_le_t bt_addr;

        bt_get_local_public_address(&bt_addr);
        printf("BD_ADDR:(MSB)%02x:%02x:%02x:%02x:%02x:%02x(LSB)\r\n",
               bt_addr.a.val[5], bt_addr.a.val[4], bt_addr.a.val[3], bt_addr.a.val[2], bt_addr.a.val[1], bt_addr.a.val[0]);

        bt_set_name("COEX-BT");
        bt_conn_cb_register(&ble_conn_callbacks);
        ble_cli_register();

#if defined(CONFIG_BT_BREDR)
        extern int bredr_cli_register(void);
        bredr_cli_register();
#endif
    }
}

void wifi_bt_create_task(void *param) 
{
    if (rfparam_cfg_get()) {
        tcpip_init(NULL, NULL);

        LOG_I("Starting wifi ...\r\n");

        async_register_event_filter(EV_WIFI, wifi_event_handler, NULL);

        wifi_task_create();

        LOG_I("Starting fhost ...\r\n");
        fhost_init();

        LOG_I("Starting bluetooth ...\r\n");
        #if defined(BL602)
        ble_controller_init(configMAX_PRIORITIES - 1);
        #else
        btble_controller_init(configMAX_PRIORITIES - 1);
        #endif

        hci_driver_init();
        bt_enable(bt_enable_cb);
    }
#if defined (BL618DG)
    else {
        LOG_I("RF not initialized.\r\n");
    }
#endif

    vTaskDelete(NULL);
}

int main(void)
{
    board_init();

    uart0 = bflb_device_get_by_name("uart0");
    shell_init_with_task(uart0);
       
    #if defined(CONFIG_BT_SETTINGS)
    bflb_mtd_init();
    /* ble stack need easyflash kv */
    easyflash_init();
    #endif

#if defined (BL616) || defined (BL616CL)
    if (0 != rfparam_init(0, NULL, 0)) {
        LOG_I("PHY RF init failed!\r\n");
        return 0;
    }
#else
    LOG_I("Please refer to board_rf.h/c and README.md for antenna structure and application scenario.\r\n");
    LOG_I("Use the appropriate board_rf_* command for rf initialization.\r\n");
#endif
    LOG_I("And then, run wifi_bt_init command to start wifi & bluetooth stack tasks.\r\n");

    vTaskStartScheduler();

    while (1) {
    }
}

int cmd_wifi_bt_create_task(int argc, char **argv)
{
    xTaskCreate(wifi_bt_create_task, "wifi bt init", 1024, NULL, 10, NULL);

    return 0;
}
SHELL_CMD_EXPORT_ALIAS(cmd_wifi_bt_create_task, wifi_bt_init, Initialize wifi and bt stack);

static bool coex_board_gpio_parse(const char *text, int *gpio_pin)
{
    char *end = NULL;
    long value;

    if (text == NULL || text[0] == '\0' || gpio_pin == NULL) {
        return false;
    }

    value = strtol(text, &end, 10);
    if (*end != '\0' || value < 0 || value >= GPIO_PIN_MAX) {
        return false;
    }

    *gpio_pin = (int)value;
    return true;
}

static const char *coex_board_config_error_name(int error)
{
    switch (error) {
    case WIFI_MGMR_COEX_BOARD_CONFIG_ERR_INVALID_ARGUMENT:
        return "invalid_argument";
    case WIFI_MGMR_COEX_BOARD_CONFIG_ERR_NOT_SUPPORTED:
        return "not_supported";
    case WIFI_MGMR_COEX_BOARD_CONFIG_ERR_BUSY:
        return "busy";
    case WIFI_MGMR_COEX_BOARD_CONFIG_ERR_GPIO_PREPARE:
        return "gpio_prepare_failed";
    case WIFI_MGMR_COEX_BOARD_CONFIG_ERR_NOT_CONFIGURED:
        return "not_configured";
    default:
        return "unknown";
    }
}

static void coex_board_config_usage(const char *command)
{
    printf("usage:\r\n");
    printf("  %s combo\r\n", command);
    printf("  %s standalone_dual_ant\r\n", command);
    printf("  %s standalone_single_ant <spdt_gpio>\r\n", command);
    printf("  %s show\r\n", command);
}

int cmd_wifi_coex_debug_board_config(int argc, char **argv)
{
    enum wifi_mgmr_coex_board_topology topology;
    int spdt_gpio = -1;
    int ret;

    if (argc == 2 && strcmp(argv[1], "show") == 0) {
        wifi_mgmr_coex_debug_context_dump();
        return 0;
    }

    if (argc == 2 && strcmp(argv[1], "combo") == 0) {
        topology = WIFI_MGMR_COEX_BOARD_COMBO_SHARED_PATH;
    } else if (argc == 2 &&
               strcmp(argv[1], "standalone_dual_ant") == 0) {
        topology = WIFI_MGMR_COEX_BOARD_STANDALONE_DUAL_ANT;
    } else if (argc == 3 &&
               strcmp(argv[1], "standalone_single_ant") == 0 &&
               coex_board_gpio_parse(argv[2], &spdt_gpio)) {
        topology = WIFI_MGMR_COEX_BOARD_STANDALONE_SINGLE_ANT_SPDT;
    } else {
        coex_board_config_usage(argv[0]);
        return -1;
    }

    ret = wifi_mgmr_coex_board_configure(topology, spdt_gpio);
    if (ret != WIFI_MGMR_COEX_BOARD_CONFIG_OK) {
        printf("coex board config failed: %s (%d)\r\n",
               coex_board_config_error_name(ret), ret);
        return ret;
    }

    printf("coex board config applied\r\n");
    return 0;
}

int cmd_wifi_coex_debug_context_dump(int argc, char **argv)
{
    if (argc != 1) {
        printf("usage: %s\r\n", argv[0]);
        return -1;
    }

    wifi_mgmr_coex_debug_context_dump();
    return 0;
}

int cmd_wifi_coex_debug_resolve(int argc, char **argv)
{
    wifi_mgmr_coex_runtime_policy_t policy;

    if (argc != 2) {
        printf("usage: %s <board_default|hardware_only|ps_pta>\r\n",
               argv[0]);
        return -1;
    }

    if (strcmp(argv[1], "board_default") == 0) {
        policy = WIFI_MGMR_COEX_RUNTIME_BOARD_DEFAULT;
    } else if (strcmp(argv[1], "hardware_only") == 0) {
        policy = WIFI_MGMR_COEX_RUNTIME_HARDWARE_ONLY;
    } else if (strcmp(argv[1], "ps_pta") == 0) {
        policy = WIFI_MGMR_COEX_RUNTIME_PS_PTA_REQUIRED;
    } else {
        printf("usage: %s <board_default|hardware_only|ps_pta>\r\n",
               argv[0]);
        return -1;
    }

    return wifi_mgmr_coex_debug_resolve_dump(policy);
}

SHELL_CMD_EXPORT_ALIAS(cmd_wifi_coex_debug_board_config, wifi_coex_debug_board_config,
                       configure coex board topology);
SHELL_CMD_EXPORT_ALIAS(cmd_wifi_coex_debug_context_dump, wifi_coex_debug_context,
                       dump coex input context);
SHELL_CMD_EXPORT_ALIAS(cmd_wifi_coex_debug_resolve, wifi_coex_debug_resolve,
                       resolve coex policy without applying it);

#if defined(BL618DG)
int cmd_wifi_coex_debug_bt_path(int argc, char **argv)
{
    extern void cmd_set_btble_standalone(int argc, char **argv);

    if (wifi_mgmr_coex_debug_reconfiguration_blocked()) {
        return -1;
    }

    cmd_set_btble_standalone(0, NULL);
    return 0;
}

int cmd_wifi_coex_debug_combo_path(int argc, char **argv)
{
    extern void cmd_set_btble_combo(int argc, char **argv);

    if (wifi_mgmr_coex_debug_reconfiguration_blocked()) {
        return -1;
    }

    cmd_set_btble_combo(0, NULL);
    return 0;
}

int cmd_wifi_coex_debug_rfparam_init(int argc, char **argv)
{
    if (argc != 1) {
        printf("usage: %s\r\n", argv[0]);
        return -1;
    }
    if (wifi_mgmr_coex_debug_reconfiguration_blocked()) {
        return -1;
    }

    if (0 != rfparam_init(0, NULL, 0)) {
        LOG_I("PHY RF init failed!\r\n");
        return -1;
    }

    return 0;
}

SHELL_CMD_EXPORT_ALIAS(cmd_wifi_coex_debug_bt_path, wifi_coex_debug_bt_path, setup bt path);
SHELL_CMD_EXPORT_ALIAS(cmd_wifi_coex_debug_combo_path, wifi_coex_debug_combo_path, setup combo path);
SHELL_CMD_EXPORT_ALIAS(cmd_wifi_coex_debug_rfparam_init, wifi_coex_debug_rfparam_init,
                       reinit rf param while coex inactive);
#endif
