# wifi_bz_coexm

Wi-Fi/BT/BLE/BZ coexistence validation demo for BL616, BL616CL and
BL618DG. The demo exposes the product coexistence API and a separately named
laboratory debug command set. Legacy RF-context TDMA is not part of this demo.

For hardware topology, product API, initialization order, supported modes and
integration restrictions, see [COEX_GUIDE.md](COEX_GUIDE.md).

## Build

```text
# BL616
make CHIP=bl616 BOARD=bl616dk

# BL616CL
make CHIP=bl616cl BOARD=bl616cldk

# BL618DG AP core
make CHIP=bl618dg BOARD=bl618dgdk CPU_ID=ap
```

BL618DG B0 coexistence builds must link
`libbl618dg_phyrf_b0_bz.a`.

## Startup

BL616 and BL616CL initialize RF during boot. Start the protocol stacks with:

```text
wifi_bt_init
```

BL618DG does not initialize RF or start Wi-Fi/Bluetooth automatically. Use
this order:

1. Run exactly one `board_rf_*` command matching the physical RF and antenna
   wiring.
2. Configure the matching WiFi6 Board Config metadata.
3. Run `wifi_bt_init` once.
4. Start STA or AP and wait for a valid band/channel.
5. Run `wifi_coex_start` for a backend-supported plan.

RF initialization must precede `wifi_bt_init`. Reboot before changing the
physical topology.

### BL618DG Topology Mapping

| Physical topology | RF initialization | Matching Board Config |
|---|---|---|
| Combo/shared path | `board_rf_single_ant_init` | `wifi_coex_debug_board_config combo` |
| Standalone dual antenna | `board_rf_dual_ant_init` | `wifi_coex_debug_board_config standalone_dual_ant` |
| Standalone single antenna with dynamic SPDT | `board_rf_single_ant_spdt_init <bt-gpio> <2g-gpio>` | `wifi_coex_debug_board_config standalone_single_ant <connected-gpio>` |

The RF command configures and calibrates the physical path. Board Config is
immutable resolver input and does not initialize PHYRF. The two commands must
describe the same hardware.

The current Board Config API records one SPDT control GPIO. For a board using
one switch-control input, pass that GPIO to both layers. Examples:

```text
# Even GPIO: high selects the BT path
board_rf_single_ant_spdt_init 18 -1
wifi_coex_debug_board_config standalone_single_ant 18

# Odd GPIO: high selects the Wi-Fi 2G path
board_rf_single_ant_spdt_init -1 11
wifi_coex_debug_board_config standalone_single_ant 11
```

`GPIO_FUNC_SPDT` derives polarity from GPIO parity: an even GPIO is high
when BT wins PTA arbitration, while an odd GPIO is low. The GPIO parity and
the external RF-switch truth table must match.

`board_rf_single_ant_spdt_bt_init` and
`board_rf_single_ant_spdt_2g_init` are fixed-path RF diagnostics. They do not
define a product Activation topology and must not be treated as a replacement
for Board Config.

## Product Coex Interface

The product interface does not expose VIF, STA/AP role, channel, RF path,
SPDT GPIO or register values:

```c
int wifi_mgmr_coex_start(wifi_mgmr_coex_runtime_policy_t policy);
int wifi_mgmr_coex_stop(void);
int wifi_mgmr_coex_status_get(struct wifi_mgmr_coex_status *status);
int wifi_mgmr_coex_duty_set(uint8_t active_ms);
int wifi_mgmr_coex_protection_set(bool enable);
```

Equivalent shell commands:

```text
wifi_coex_start <board_default|hardware_only|ps_pta>
wifi_coex_stop
wifi_coex_status
wifi_coex_duty_set <10-90>
wifi_coex_protection <0|1>
```

`wifi_coex_start` requires a ready radio context: STA connected or AP
started with a valid band and channel. Repeating start with the same effective
configuration and repeating stop are idempotent. Stop before selecting a
different policy.

`hardware_only` applies the resolved hardware recipe without starting
PS-PTA. `ps_pta` explicitly requires software duty slicing and does not
silently fall back. `board_default` resolves to `hardware_only` on all
supported chips.

Duty is the Wi-Fi active time in each approximately 100 ms PS-PTA period. It
can be set before start. It is saved but has no runtime effect in
hardware-only mode.

Connection Protection is independent from Activation and disabled by default.
Enable it before scan/connect only in products that require connection-stage
protection:

```text
wifi_coex_protection 1
wifi_sta_connect ...
# after the radio is ready
wifi_coex_start board_default
```

## Current Backend Boundary

Board Config and the resolver recognize the BL618DG combo, standalone
dual-antenna and standalone single-antenna SPDT topologies. Product Activation
is fail-closed: the current BL618DG backend has a complete apply/restore plan
only for 2.4 GHz combo.

| Chip/topology | Current product start |
|---|---|
| BL616 2.4G combo | hardware-only and explicit PS-PTA |
| BL616CL 2.4G combo | hardware-only and explicit PS-PTA |
| BL618DG 2.4G combo | hardware-only and explicit PS-PTA |
| BL618DG standalone or 5G | resolver/debug available; start returns not supported until a complete backend plan is approved |

Do not restore the old channel-update fallback to bypass this boundary.

## Laboratory Debug Commands

The demo enables `CONFIG_WIFI_COEX_DEBUG_CLI`:

```text
wifi_coex_debug_status
wifi_coex_debug_context
wifi_coex_debug_resolve <board_default|hardware_only|ps_pta>
wifi_coex_debug_board_config ...
```

BL618DG path diagnostics:

```text
wifi_coex_debug_bt_path
wifi_coex_debug_combo_path
wifi_coex_debug_rfparam_init
wifi_coex_debug_bt_spdt <0|1>
wifi_coex_debug_bt_overlay <0|1> [margin:0-63]
wifi_coex_debug_bt_adj_pwr <0|1> [ble|154|bt|wifi] [power]
```

Topology, path, SPDT, overlay, adjusted-power and RF-parameter changes are
rejected while Activation or Connection Protection owns coexistence hardware:

```text
wifi_coex_stop
wifi_coex_protection 0
```

Normal start/stop does not dump registers. Use
`wifi_coex_debug_status` explicitly when MMIO evidence is required.

## Example Flows

BL616/BL616CL hardware-only:

```text
wifi_bt_init
wifi_sta_connect ...
wifi_coex_start board_default
wifi_coex_status
```

BL616/BL616CL explicit PS-PTA:

```text
wifi_coex_duty_set 50
wifi_coex_start ps_pta
wifi_coex_status
```

BL618DG 2.4G combo:

```text
board_rf_single_ant_init
wifi_coex_debug_board_config combo
wifi_bt_init
wifi_sta_connect ...
wifi_coex_start hardware_only
wifi_coex_status
```

The demo also enables `iperf`, `ble_tp_test` and `ps_extend` for
coexistence throughput and CPU-load validation.

## Integration Limitation

BL618DG physical RF initialization and WiFi6 Board Config currently use two
explicit commands. This is intentional during integration so the BSP remains
the owner of RF initialization and WiFi6 remains the owner of resolver
metadata. Product applications should wrap both calls in one board-specific
startup function so customers cannot select mismatched topologies.
