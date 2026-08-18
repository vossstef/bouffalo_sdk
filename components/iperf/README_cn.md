# 精简版 iPerf（Classic iPerf2）

[English](README.md)

`components/iperf` 是面向嵌入式 SDK 的精简 IPv4 吞吐量测试组件。它支持
TCP/UDP client 和 server，并可选择 lwIP Socket API 或 lwIP Raw API 后端。

组件和 Shell 命令名为 `iperf`，但实现的是 Classic iPerf2 normal mode。
本组件不支持 iPerf3，也不能与 iPerf3 对端互测。

## 快速上手

### 1. 启用组件

在应用配置中启用 FreeRTOS、lwIP、iPerf2 和 Shell：

```text
CONFIG_FREERTOS=y
CONFIG_LWIP=y
CONFIG_IPERF=y
CONFIG_SHELL=y
```

也可以在 `proj.conf` 中设置：

```cmake
set(CONFIG_FREERTOS 1)
set(CONFIG_LWIP 1)
set(CONFIG_IPERF 1)
set(CONFIG_SHELL 1)
```

仅使用 C API 时可以不启用 `CONFIG_SHELL`。Raw 后端还要求 lwIP 配置启用
`LWIP_TCPIP_CORE_LOCKING=1` 和 `SYS_LIGHTWEIGHT_PROT=1`。同一应用中不要再启用
其他也会注册 `iperf` Shell 命令的实现，例如 `CONFIG_WIFI_IPERF`。

### 2. 准备 PC 端工具

在 PC 上安装 Classic iPerf2。虽然协议版本是 iPerf2，但桌面端程序通常仍叫
`iperf`：

```bash
iperf --version
```

请勿使用 `iperf3` 与本组件互测。

### 3. 运行测试

以下示例假设设备地址是 `192.168.1.100`。

#### 设备作为 TCP Server

设备端：

```text
iperf -s
```

PC 端：

```bash
iperf -c 192.168.1.100 -t 10 -i 1
```

#### 设备作为 TCP Client

PC 端先启动 server：

```bash
iperf -s
```

设备端使用 PC 的地址连接：

```text
iperf -c 192.168.1.10 -t 10 -i 1
```

#### 设备作为 UDP Server

设备端：

```text
iperf -s -u
```

PC 端发送 20 Mbit/s UDP 流量：

```bash
iperf -c 192.168.1.100 -u -b 20M -l 1470 -t 10
```

#### 设备作为 UDP Client

PC 端：

```bash
iperf -s -u
```

设备端：

```text
iperf -c 192.168.1.10 -u -b 20M -l 1470 -t 10
```

`-b` 可以使用整数 bit/s，也可以使用不区分大小写的十进制 `K`/`M` 后缀。
例如，`100K` 表示 100000 bit/s，`100M` 表示 100000000 bit/s。

#### 选择 Socket 后端

默认使用 Raw 后端。在设备端命令中加入 `-A socket` 即可选择 Socket 后端：

```text
iperf -s -A socket
iperf -c 192.168.1.10 -A socket -t 10 -N
iperf -s -u -A socket
iperf -c 192.168.1.10 -u -A socket -b 50M -l 1470 -t 10
```

Socket 和 Raw 后端使用相同的线上数据格式，可以与同一个桌面 iPerf2 工具互测。

### 命令说明

```text
iperf -s|-c <IPv4-address> [-u] [-A socket|raw] [-p port]
  [-l bytes] [-t sec|-n bytes] [-i sec] [-b bit/s[K|M]]
  [-S tos] [-N] [-B IPv4-address]
iperf -a
iperf
iperf -h
```

| 参数 | 说明 |
|---|---|
| `-s` | 作为 server 运行。 |
| `-c <IPv4>` | 作为 client 连接指定 server。 |
| `-u` | 使用 UDP；默认使用 TCP。 |
| `-A socket\|raw` | 选择后端；默认使用 Raw。 |
| `-p <port>` | Server 端口；默认是 `5001`。 |
| `-l <bytes>` | TCP buffer 或 UDP datagram 长度。 |
| `-t <sec>` | Client 运行时间；默认 10 秒。 |
| `-n <bytes>` | 按总字节数运行，替代运行时间。 |
| `-i <sec>` | 报告间隔；`0` 表示关闭周期报告。 |
| `-b <rate>` | UDP client 发送速率，单位为 bit/s；支持不区分大小写的 `K`/`M` 后缀。默认 1 Mbit/s。 |
| `-S <tos>` | IPv4 TOS 值。 |
| `-N` | TCP client 关闭 Nagle。 |
| `-B <IPv4>` | 绑定本地 IPv4 地址。 |
| `-a` | 停止 Shell 当前管理的测试。 |
| `-h` | 显示命令帮助。 |

使用时注意：

- `iperf` 和 `iperf -h` 都会显示命令帮助。
- 同一应用只能由一个实现注册 `iperf` Shell 命令。
- `-s` 和 `-c` 必须且只能选择一个。
- `-t` 和 `-n` 不能同时使用。
- `-b` 只适用于 UDP client。
- 只有 `-b` 支持后缀：`K/k` 表示 1000，`M/m` 表示 1000000；其他数值
  参数仍只接受整数。
- Shell 同一时间只管理一个测试；C API 支持多个独立实例。
- UDP datagram 长度必须为 80～1470 字节；TCP Socket buffer 最大为
  16384 字节，TCP Raw buffer 最大为 4096 字节。

## C API

使用时包含 `bflb_iperf.h`。一个测试实例的基本生命周期是：

```text
config_init -> create -> start -> get_state/get_result -> stop -> destroy
```

以下示例启动一个 TCP Raw client：

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

主要接口：

| 接口 | 作用 |
|---|---|
| `bflb_iperf_config_init()` | 使用默认值初始化配置。 |
| `bflb_iperf_create()` | 校验并复制配置，然后创建实例。 |
| `bflb_iperf_start()` | 异步启动 backend worker。 |
| `bflb_iperf_get_state()` | 获取当前生命周期状态。 |
| `bflb_iperf_get_result()` | 获取一致的统计快照。 |
| `bflb_iperf_stop()` | 异步请求停止测试。 |
| `bflb_iperf_destroy()` | 由创建者释放已经结束的实例。 |

接口使用注意事项：

- `remote_ip4` 和 `local_ip4` 使用网络字节序。
- 一个实例只代表一次测试，只能启动一次。
- `bflb_iperf_create()` 会复制配置结构。
- `bflb_iperf_stop()` 是异步操作。
- `bflb_iperf_destroy()` 不等待资源释放；返回 `BFLB_IPERF_ERR_BUSY` 时应稍后重试。
- `done_cb` 在 backend worker task 中执行，不能在回调中调用
  `bflb_iperf_destroy()`。
- 公共实例接口使用 FreeRTOS mutex，不能从 ISR 调用。
- `local_port` 和 `task_priority` 可以通过 C API 配置，但没有对应 Shell 参数。

完整配置字段、结果字段和返回值请查看
[include/bflb_iperf.h](include/bflb_iperf.h)。

## 实现原理

组件根据协议和后端选择四条相互独立的数据路径：

| 模式 | 实现方式 |
|---|---|
| TCP Socket | Worker task 中执行阻塞式 Socket `send`/`recv` 循环。 |
| UDP Socket | Worker task 负责 datagram、发送节拍、丢包/jitter 和 FIN/AckFIN。 |
| TCP Raw | lwIP TCP callback 处理接收数据，worker 管理连接事件和发送进度。 |
| UDP Raw | 接收 callback 将 pbuf 所有权交给实例 RX queue，worker 解析并统计 datagram。 |

Raw 后端是默认选择，直接使用 lwIP Raw PCB callback，并只在短时间内持有
TCP/IP Core Lock，以减少 API 和线程切换开销。对于倾向使用 Socket API 的集成，
仍可显式选择 Socket 后端。每个后端实例拥有独立的 worker 和私有状态，不共享
可变测试状态。

UDP 使用 Classic iPerf2 normal-mode 数据格式。第一个 datagram 携带测试配置并
声明使用 64 位序号；接收端根据序号和时间戳计算丢包、乱序和 jitter。Client 最后
发送负序号 FIN，server 返回 AckFIN 报告。

## 功能范围与限制

支持：

- IPv4 TCP/UDP client 和 server
- Socket/Raw 两种后端
- Client 按时间或字节数运行
- UDP 发送节拍、丢包、乱序和 jitter 报告
- Classic iPerf2 normal-mode UDP setup 和 FIN/AckFIN
- C API 多实例；Shell 单实例

暂不支持：

- IPv6
- iPerf3
- Parallel streams
- Reverse、dual、tradeoff 和 full-duplex 模式
- Enhanced mode 和扩展统计
- 单个 server 实例同时服务多个 client
- Classic TCP V1 控制头交互

实际吞吐量会受到目标芯片时钟、lwIP 内存池、worker task 优先级、网卡队列和后端
类型影响。验证高速 UDP 时，应同时观察 iPerf 丢包统计和网卡接口计数。