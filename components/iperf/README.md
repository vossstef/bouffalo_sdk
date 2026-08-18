# Compact iPerf (Classic iPerf2)

[中文](README_cn.md)

`components/iperf` is a compact IPv4 throughput test component for embedded
SDKs. It supports TCP and UDP clients and servers through either the lwIP
Socket API or the lwIP Raw API.

The component and shell command are named `iperf`, but the implemented protocol
is Classic iPerf2 normal mode. It does not support iPerf3 and is not compatible
with an iPerf3 peer.

## Quick Start

### 1. Enable the component

Enable FreeRTOS, lwIP, iPerf2, and Shell in the application configuration:

```text
CONFIG_FREERTOS=y
CONFIG_LWIP=y
CONFIG_IPERF=y
CONFIG_SHELL=y
```

The equivalent `proj.conf` settings are:

```cmake
set(CONFIG_FREERTOS 1)
set(CONFIG_LWIP 1)
set(CONFIG_IPERF 1)
set(CONFIG_SHELL 1)
```

`CONFIG_SHELL` is optional when only the C API is used. Raw backends also
require `LWIP_TCPIP_CORE_LOCKING=1` and `SYS_LIGHTWEIGHT_PROT=1` in the lwIP
configuration. Do not enable another implementation that also exports the
`iperf` shell command, such as `CONFIG_WIFI_IPERF`, in the same application.

### 2. Prepare the desktop peer

Install Classic iPerf2 on the PC. The desktop executable is normally named
`iperf` even though the protocol version is iPerf2:

```bash
iperf --version
```

Do not use `iperf3` for these tests.

### 3. Run a test

Replace `192.168.1.100` with the device IPv4 address.

#### Device as a TCP server

On the device:

```text
iperf -s
```

On the PC:

```bash
iperf -c 192.168.1.100 -t 10 -i 1
```

#### Device as a TCP client

On the PC:

```bash
iperf -s
```

On the device, replace the address with the PC address:

```text
iperf -c 192.168.1.10 -t 10 -i 1
```

#### Device as a UDP server

On the device:

```text
iperf -s -u
```

On the PC, send 20 Mbit/s UDP traffic:

```bash
iperf -c 192.168.1.100 -u -b 20M -l 1470 -t 10
```

#### Device as a UDP client

On the PC:

```bash
iperf -s -u
```

On the device:

```text
iperf -c 192.168.1.10 -u -b 20M -l 1470 -t 10
```

The `-b` option accepts integer bit/s values or a case-insensitive decimal
`K`/`M` suffix. For example, `100K` is 100000 bit/s and `100M` is 100000000
bit/s.

#### Select the Socket backend

Raw is the default backend. Add `-A socket` to a device command to select the
Socket backend:

```text
iperf -s -A socket
iperf -c 192.168.1.10 -A socket -t 10 -N
iperf -s -u -A socket
iperf -c 192.168.1.10 -u -A socket -b 50M -l 1470 -t 10
```

The Socket and Raw backends use the same wire format and may be tested against
the same desktop iPerf2 peer.

### Command Reference

```text
iperf -s|-c <IPv4-address> [-u] [-A socket|raw] [-p port]
  [-l bytes] [-t sec|-n bytes] [-i sec] [-b bit/s[K|M]]
  [-S tos] [-N] [-B IPv4-address]
iperf -a
iperf
iperf -h
```

| Option | Description |
|---|---|
| `-s` | Run as server. |
| `-c <IPv4>` | Run as client and connect to the specified server. |
| `-u` | Use UDP; TCP is the default. |
| `-A socket\|raw` | Select the backend; Raw is the default. |
| `-p <port>` | Server port; default is `5001`. |
| `-l <bytes>` | TCP buffer or UDP datagram length. |
| `-t <sec>` | Client duration; default is 10 seconds. |
| `-n <bytes>` | Client byte limit instead of duration. |
| `-i <sec>` | Report interval; `0` disables periodic reports. |
| `-b <rate>` | UDP client transmit rate in bit/s; accepts case-insensitive `K`/`M` suffixes. Default is 1 Mbit/s. |
| `-S <tos>` | IPv4 TOS value. |
| `-N` | Disable Nagle for a TCP client. |
| `-B <IPv4>` | Bind a local IPv4 address. |
| `-a` | Stop the current shell-managed test. |
| `-h` | Print command help. |

Command notes:

- `iperf` and `iperf -h` both print command help.
- Only one implementation may register the `iperf` shell command.
- Select exactly one of `-s` and `-c`.
- `-t` and `-n` are mutually exclusive.
- `-b` is valid only for a UDP client.
- Only `-b` accepts suffixes. `K/k` means 1000 and `M/m` means 1000000;
  other numeric options remain plain integers.
- The shell command manages one test at a time. The C API supports multiple
  independent instances.
- UDP datagrams must be 80 to 1470 bytes. TCP Socket buffers may be up to
  16384 bytes; TCP Raw buffers may be up to 4096 bytes.

## C API

Include `bflb_iperf.h`. A test has a simple lifecycle:

```text
config_init -> create -> start -> get_state/get_result -> stop -> destroy
```

The following example starts a TCP Raw client:

```c
#include <lwip/ip4_addr.h>
#include <bflb_iperf.h>

bflb_iperf_config_t config;
bflb_iperf_t *iperf;
ip4_addr_t server;

bflb_iperf_config_init(&config);
ip4addr_aton("192.168.1.10", &server);

config.backend = BFLB_IPERF_BACKEND_RAW;
config.role = BFLB_IPERF_ROLE_CLIENT;
config.proto = BFLB_IPERF_PROTO_TCP;
config.remote_ip4 = server.addr;
config.duration_s = 10;

if (bflb_iperf_create(&config, &iperf) == BFLB_IPERF_OK) {
    if (bflb_iperf_start(iperf) != BFLB_IPERF_OK) {
        bflb_iperf_destroy(iperf);
    }
}
```

Main API functions:

| Function | Purpose |
|---|---|
| `bflb_iperf_config_init()` | Initialize a configuration with defaults. |
| `bflb_iperf_create()` | Validate and copy the configuration, then create an instance. |
| `bflb_iperf_start()` | Start the backend worker asynchronously. |
| `bflb_iperf_get_state()` | Read the current lifecycle state. |
| `bflb_iperf_get_result()` | Read a consistent statistics snapshot. |
| `bflb_iperf_stop()` | Request asynchronous termination. |
| `bflb_iperf_destroy()` | Release a completed instance owned by the caller. |

Important API rules:

- `remote_ip4` and `local_ip4` use network byte order.
- One instance represents one test and can be started only once.
- The configuration is copied by `bflb_iperf_create()`.
- `bflb_iperf_stop()` is asynchronous.
- `bflb_iperf_destroy()` does not wait. Retry later if it returns
  `BFLB_IPERF_ERR_BUSY`.
- `done_cb` runs in the backend worker task. Do not call
  `bflb_iperf_destroy()` from that callback.
- Public instance APIs use a FreeRTOS mutex and must not be called from an ISR.
- `local_port` and `task_priority` are available through the C API but are not
  exposed as shell options.

See [include/bflb_iperf.h](include/bflb_iperf.h) for all configuration fields,
result fields, and return values.

## How It Works

The implementation has four independent data paths selected by protocol and
backend:

| Mode | Implementation |
|---|---|
| TCP Socket | Blocking Socket `send`/`recv` loops in a worker task. |
| UDP Socket | Socket datagrams, pacing, loss/jitter tracking, and FIN/AckFIN in a worker task. |
| TCP Raw | lwIP TCP callbacks handle receive data; a worker manages connection events and transmit progress. |
| UDP Raw | The receive callback transfers pbuf ownership to a per-instance queue; a worker parses and accounts datagrams. |

The Raw backend is the default and uses lwIP Raw PCB callbacks with short
TCP/IP Core Lock sections to reduce API and thread overhead. The Socket backend
remains available for integrations that prefer the Socket API. Each backend
instance owns its worker and private state; instances do not share mutable test
state.

UDP uses the Classic iPerf2 normal-mode wire format. The first datagram carries
the test settings and enables 64-bit sequence numbers. Receivers calculate
datagram loss, out-of-order delivery, and jitter. A negative final sequence
number ends the test, and the server returns an AckFIN report.

## Scope and Limitations

Supported:

- IPv4 TCP and UDP client/server
- Socket and Raw backends
- Time and byte client limits
- UDP pacing, loss, out-of-order, and jitter reports
- Classic iPerf2 normal-mode UDP setup and FIN/AckFIN
- Multiple instances through the C API; one instance through the shell

Not supported:

- IPv6
- iPerf3
- Parallel streams
- Reverse, dual, tradeoff, or full-duplex modes
- Enhanced mode and extended statistics
- Multiple simultaneous clients on one server instance
- Classic TCP V1 control-header exchange

Actual throughput depends on the target clock, lwIP memory pools, worker task
priority, network-interface queues, and the selected backend. Validate high-rate
UDP tests on the target board and inspect both iPerf loss statistics and the
network-interface counters.