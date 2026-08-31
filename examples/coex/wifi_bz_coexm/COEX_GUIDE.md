# Wi-Fi/Bluetooth Coexistence Guide

本文说明如何在 BL616、BL616CL 和 BL618DG 上配置和使用 Wi-Fi/Bluetooth coexistence，并提供
`examples/coex/wifi_bz_coexm` 的参考命令。

量产项目应在 BSP 中固定 RF path、天线拓扑和 SPDT GPIO。应用只选择运行策略，不应在业务运行期间
切换硬件拓扑。

## 1. 适用配置

本 Guide 的标准运行示例适用于以下配置：

| 芯片 | Wi-Fi | RF topology | 默认策略 | 可选策略 |
|---|---|---|---|---|
| BL616 | 2.4 GHz | Combo shared path | Hardware-only | PS-PTA |
| BL616CL | 2.4 GHz | Combo shared path | Hardware-only | PS-PTA |
| BL618DG | 2.4 GHz | Combo shared path | Hardware-only | PS-PTA |

BL618DG standalone dual-antenna、standalone single-antenna/SPDT 或其他射频装配应使用对应产品的
BSP、PHYRF library 和板级配置，不直接套用 combo 示例。

Wi-Fi 2.4 GHz 和 5 GHz 是同一 Wi-Fi radio 的可选工作频段。同一时刻只使用其中一个频段，
不是 2.4 GHz 与 5 GHz 同时工作的双频并发模式。

## 2. 硬件拓扑

### 2.1 Combo shared path

```text
BT/BLE/15.4 ─┐
             ├─ shared internal RF path ─┐
Wi-Fi 2.4G ──┘                           ├─ antenna
Wi-Fi 5G ──────────────────── diplexer ──┘
```

- Board Config：`WIFI_MGMR_COEX_BOARD_COMBO_SHARED_PATH`。
- 不使用外部 SPDT。
- Wi-Fi 与 BZ 的访问由硬件 PTA 协调。
- 需要软件时间片时，由应用显式选择 PS-PTA。

### 2.2 Standalone dual antenna

```text
BT/BLE/15.4 standalone RF ─────────────── antenna 1

Wi-Fi 2.4G ─┐
             ├─ diplexer ─────────────── antenna 2
Wi-Fi 5G ────┘
```

- Board Config：`WIFI_MGMR_COEX_BOARD_STANDALONE_DUAL_ANT`。
- 不使用 SPDT。
- BZ 与 Wi-Fi 使用独立 RF path 和天线。
- 板级初始化应选择 standalone BZ path，并与 Board Config 保持一致。

### 2.3 Standalone single antenna with SPDT

```text
BT/BLE/15.4 standalone RF ─┐
                            ├─ SPDT ─┐
Wi-Fi 2.4G ─────────────────┘        ├─ diplexer ─ antenna
Wi-Fi 5G ────────────────────────────┘
```

- Board Config：`WIFI_MGMR_COEX_BOARD_STANDALONE_SINGLE_ANT_SPDT`。
- Wi-Fi 2.4G 与 BZ 通过外部 SPDT 共用 2.4G 天线入口。
- SPDT GPIO 必须来自产品原理图，不能使用其他板卡的 GPIO 配置。
- BSP 负责 GPIO pinmux 和极性，应用只提供实际连接的 GPIO 编号。

Combo 和 standalone + SPDT 都可能使用一根外部天线，但两者的 RF path 和初始化方式不同。产品配置
必须使用明确的 topology 名称，不能只使用“单天线”描述。

## 3. 构建 Demo

```text
# BL616
make CHIP=bl616 BOARD=bl616dk

# BL616CL
make CHIP=bl616cl BOARD=bl616cldk

# BL618DG AP core
make CHIP=bl618dg BOARD=bl618dgdk CPU_ID=ap
```

BL618DG B0 coexistence 构建使用：

```text
drivers/soc/bl618dg/phyrf/lib-gcc_10.2.0-toolchain_V2.6.1/
libbl618dg_phyrf_b0_bz.a
```

产品应固定已验证的 PHYRF library，不在同一产品版本中混用不同 PHYRF library。

## 4. 产品 API

头文件：

```c
#include "wifi_mgmr_coex.h"
```

运行接口：

```c
int wifi_mgmr_coex_start(wifi_mgmr_coex_runtime_policy_t policy);
int wifi_mgmr_coex_stop(void);
int wifi_mgmr_coex_status_get(struct wifi_mgmr_coex_status *status);
int wifi_mgmr_coex_duty_set(uint8_t active_ms);
int wifi_mgmr_coex_protection_set(bool enable);
```

BL618DG 板级配置接口：

```c
int wifi_mgmr_coex_board_configure(
    enum wifi_mgmr_coex_board_topology topology,
    int spdt_gpio);
```

## 5. Runtime Policy

| Policy | 用途 |
|---|---|
| `WIFI_MGMR_COEX_RUNTIME_BOARD_DEFAULT` | 使用产品默认策略；标准配置下等同于 hardware-only |
| `WIFI_MGMR_COEX_RUNTIME_HARDWARE_ONLY` | 应用板级硬件共存配置，不启动 STA PS/TBTT 时间片 |
| `WIFI_MGMR_COEX_RUNTIME_PS_PTA_REQUIRED` | 在已连接的 2.4 GHz STA 上使用 PS-PTA 软件时间片 |

推荐默认调用：

```c
ret = wifi_mgmr_coex_start(WIFI_MGMR_COEX_RUNTIME_BOARD_DEFAULT);
```

只有产品明确需要软件时间片时，才选择 `WIFI_MGMR_COEX_RUNTIME_PS_PTA_REQUIRED`。5 GHz 和
AP-only 场景使用 `BOARD_DEFAULT` 或 `HARDWARE_ONLY`。

## 6. 初始化顺序

### 6.1 BL616/BL616CL

BL616 和 BL616CL 使用固定 combo topology，不要求应用配置 topology 或 SPDT GPIO。

```text
系统启动
  -> 初始化 Wi-Fi/Bluetooth stack
  -> 可选：连接前开启 Connection Protection
  -> STA 连接成功或 AP 启动成功
  -> wifi_mgmr_coex_start(...)
  -> 业务运行
```

### 6.2 BL618DG

BL618DG 在启动阶段完成板级 RF 初始化和 Board Config：

```text
系统启动
  -> BSP 初始化产品固定的 RF path
  -> wifi_mgmr_coex_board_configure(...)
  -> 初始化 Wi-Fi/Bluetooth stack
  -> 可选：连接前开启 Connection Protection
  -> STA 连接成功或 AP 启动成功
  -> wifi_mgmr_coex_start(...)
  -> 业务运行
```

RF path 与 Board Config 必须描述同一个硬件拓扑。Board Config 在启动阶段配置一次；运行期间不切换
topology。产品代码应将 RF 初始化和 Board Config 封装在 BSP 中，不要求最终用户分别调用。

## 7. Board Config 示例

### 7.1 Combo 标准配置

```c
ret = wifi_mgmr_coex_board_configure(
    WIFI_MGMR_COEX_BOARD_COMBO_SHARED_PATH,
    -1);
```

Standalone dual-antenna 和 standalone single-antenna/SPDT 的 RF path、GPIO pinmux、极性和
Board Config 应由对应产品 BSP 统一完成。应用不应复制其他板卡的 standalone 初始化代码或 GPIO
编号。

## 8. Connection Protection

Connection Protection 用于保护 Wi-Fi scan、connect、key management 和 DHCP 等连接阶段。它与
`wifi_mgmr_coex_start()` 的运行配置相互独立，并且默认关闭。

需要连接保护的产品在 Wi-Fi 连接前调用：

```c
ret = wifi_mgmr_coex_protection_set(true);
if (ret != WIFI_MGMR_COEX_OK) {
    /* Handle the configuration error. */
}

/* Start Wi-Fi scan/connect. */
```

不使用 Wi-Fi/Bluetooth coexistence 的应用不需要开启 Connection Protection。

## 9. Hardware-only 示例

```c
int ret;

/* STA connected or AP started; band/channel are valid. */
ret = wifi_mgmr_coex_start(WIFI_MGMR_COEX_RUNTIME_BOARD_DEFAULT);
if (ret != WIFI_MGMR_COEX_OK) {
    /* Handle the error before starting coexistence traffic. */
}
```

`wifi_mgmr_coex_start()` 应在 STA 连接成功或 AP 启动成功，并获得有效 band/channel 后调用。
STA 断开并重新连接或 AP 重新启动后，应在 Wi-Fi 再次 ready 后重新调用该接口。

## 10. PS-PTA 示例

PS-PTA 仅适用于已连接的 2.4 GHz STA。AP-only 和 5 GHz 场景不使用 PS-PTA。

```c
int ret;

ret = wifi_mgmr_coex_duty_set(50);
if (ret != WIFI_MGMR_COEX_OK) {
    /* Handle invalid duty configuration. */
}

ret = wifi_mgmr_coex_start(WIFI_MGMR_COEX_RUNTIME_PS_PTA_REQUIRED);
if (ret != WIFI_MGMR_COEX_OK) {
    /* Handle the error before starting coexistence traffic. */
}
```

Duty 范围为 `10～90 ms`，表示约 100 ms 周期中的 Wi-Fi active window。应用可以根据 Wi-Fi 和
Bluetooth/BLE 的业务需求调整该值。

调用 PS-PTA start 前，普通 STA PS 必须处于关闭状态。Coex active 期间不要调用普通 STA PS
控制接口；调用 `wifi_mgmr_coex_stop()` 后普通 STA PS 保持关闭，应用可再按自身策略配置。

## 11. Stop 和状态查询

停止共存：

```c
ret = wifi_mgmr_coex_stop();
```

查询状态：

```c
struct wifi_mgmr_coex_status status;

ret = wifi_mgmr_coex_status_get(&status);
if (ret == WIFI_MGMR_COEX_OK) {
    /* status.active: coexistence is active. */
    /* status.ps_pta_running: PS-PTA is running. */
}
```

重复 start 相同配置和重复 stop 均为幂等操作。切换 runtime policy 时，应先调用 stop，再使用新 policy
调用 start。

`wifi_mgmr_coex_stop()` 只停止运行期共存，不改变 Connection Protection 配置。不再需要连接保护时，
应另行调用 `wifi_mgmr_coex_protection_set(false)`。

## 12. 返回值

| 返回值 | 含义 | 建议处理 |
|---|---|---|
| `WIFI_MGMR_COEX_OK` | 操作成功 | 继续运行 |
| `WIFI_MGMR_COEX_ERR_INVALID_ARGUMENT` | 参数非法 | 检查 policy、duty 和 Board Config |
| `WIFI_MGMR_COEX_ERR_NOT_SUPPORTED` | 当前产品配置不支持该组合 | 检查芯片、RF topology 和 BSP 配置 |
| `WIFI_MGMR_COEX_ERR_NOT_READY` | Wi-Fi 连接或 AP 尚未就绪 | 等待 STA/AP ready 后重试 |
| `WIFI_MGMR_COEX_ERR_BUSY` | 当前配置正在使用 | 切换 runtime policy 时先 stop；更换硬件拓扑时按 BSP 启动流程重启 |
| `WIFI_MGMR_COEX_ERR_APPLY_FAILED` | 硬件配置未完成 | 停止业务并保存启动日志 |

应用必须检查 API 返回值，只有返回 `WIFI_MGMR_COEX_OK` 后才认为共存配置已经生效。

## 13. 典型场景

Demo 提供以下产品 API 对应命令：

```text
wifi_coex_start <board_default|hardware_only|ps_pta>
wifi_coex_stop
wifi_coex_status
wifi_coex_duty_set <10-90>
wifi_coex_protection <0|1>
```

### 13.1 BL618DG 拓扑准备

BL618DG 每次启动只选择一组与产品硬件一致的拓扑配置：

| 产品拓扑 | Demo RF 初始化 | Demo Board Config |
|---|---|---|
| Combo shared path | `board_rf_single_ant_init` | `wifi_coex_debug_board_config combo` |
| Standalone dual antenna | `board_rf_dual_ant_init` | `wifi_coex_debug_board_config standalone_dual_ant` |
| Standalone single antenna/SPDT | `board_rf_single_ant_spdt_init <bt_gpio> <wifi_2g_gpio>` | `wifi_coex_debug_board_config standalone_single_ant <spdt_gpio>` |

单天线/SPDT 场景中的 GPIO 参数必须来自产品原理图。未连接的控制方向使用 `-1`，Board Config
中的 `spdt_gpio` 使用实际连接到 RF switch 的 GPIO。完成拓扑准备后再执行 `wifi_bt_init`。

上述 `board_rf_*` 和 `wifi_coex_debug_board_config` 命令用于展示 BSP 初始化过程。量产应用应在
BSP 中完成等价配置，不向最终用户暴露这些命令。BL616 和 BL616CL 使用固定 combo topology，
不执行本步骤。

### 13.2 Combo STA + BLE

BL616/BL616CL 从 `wifi_bt_init` 开始；BL618DG 先完成 13.1 中的 combo 拓扑准备：

```text
# 仅 BL618DG
board_rf_single_ant_init
wifi_coex_debug_board_config combo

wifi_bt_init
wifi_sta_connect ...
wifi_coex_start board_default
wifi_coex_status
```

`board_default` 使用 hardware-only。只有 2.4 GHz STA 明确需要软件时间片时，才改用：

```text
wifi_coex_duty_set 50
wifi_coex_start ps_pta
```

### 13.3 Combo AP + BLE

AP 场景使用 hardware-only，不使用 PS-PTA。以下示例在 2.4 GHz channel 6 启动 SoftAP：

```text
# 仅 BL618DG
board_rf_single_ant_init
wifi_coex_debug_board_config combo

wifi_bt_init
wifi_ap_start -s coex_ap -k 12345678 -c 6
wifi_coex_start board_default
wifi_coex_status
```

### 13.4 Standalone dual antenna + STA/AP + BLE

双天线产品由 BSP 初始化 standalone BZ RF path，不配置 SPDT。Demo 的拓扑准备命令为：

```text
board_rf_dual_ant_init
wifi_coex_debug_board_config standalone_dual_ant
wifi_bt_init
```

然后只选择一种 Wi-Fi 角色：

```text
# STA
wifi_sta_connect ...
wifi_coex_start board_default

# 或者选择 AP
wifi_ap_start -s coex_ap -k 12345678 -c 6
wifi_coex_start board_default
```

Wi-Fi ready 并且 `wifi_coex_start` 返回成功后，即可启动或继续运行 BLE/BT 业务。

### 13.5 Standalone single antenna/SPDT + STA/AP + BLE

单天线/SPDT 产品由 BSP 统一完成 standalone RF path、GPIO pinmux、极性和 Board Config。以下命令
仅表示 demo 中的等价启动顺序：

```text
board_rf_single_ant_spdt_init <bt_gpio> <wifi_2g_gpio>
wifi_coex_debug_board_config standalone_single_ant <spdt_gpio>
wifi_bt_init
```

随后按 STA 或 AP 角色启动 Wi-Fi，再启动 hardware-only：

```text
# STA
wifi_sta_connect ...
wifi_coex_start board_default

# 或者选择 AP
wifi_ap_start -s coex_ap -k 12345678 -c 6
wifi_coex_start board_default
```

应用不得在 Wi-Fi/Bluetooth 业务运行期间切换 SPDT GPIO、RF path 或 Board Config。

## 14. 产品集成检查表

- [ ] 已根据原理图确认 RF topology。
- [ ] BL618DG RF path、Board Config 和 PHYRF library 与产品硬件一致。
- [ ] 使用 SPDT 时，GPIO 来自当前产品原理图。
- [ ] Board Config 在 Wi-Fi/Bluetooth stack 启动前完成，并且运行期间不修改。
- [ ] STA 连接或 AP 启动并获得有效 band/channel 后再调用 `wifi_mgmr_coex_start()`。
- [ ] STA 重连或 AP 重新启动后，在 Wi-Fi ready 后重新调用 `wifi_mgmr_coex_start()`。
- [ ] 应用检查所有 Coex API 返回值。
- [ ] 未选择 PS-PTA 时使用 `BOARD_DEFAULT`。
- [ ] PS-PTA 只用于 2.4 GHz STA，并且启动前普通 STA PS 已关闭。
- [ ] Connection Protection 只在需要连接阶段保护的产品中开启。
- [ ] 停止运行期共存和关闭 Connection Protection 分别调用对应接口。
- [ ] 切换 runtime policy 前先调用 `wifi_mgmr_coex_stop()`。
- [ ] 完成 Wi-Fi-only、BT/BLE-only 和并发业务验证。
