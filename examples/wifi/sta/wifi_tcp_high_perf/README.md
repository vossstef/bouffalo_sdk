# WiFi TCP high performance

## Support CHIP

| CHIP                   | Remark |
|:----------------------:|:------:|
| BL616/BL616CL/BL618DG  |        |

## Compile

```bash
make CHIP=<chipname> BOARD=<boardname>
```

For example:

```bash
make CHIP=bl616 BOARD=bl616dk
```

The demo always uses the high performance TCP profile. It selects `tcp_bench`,
uses no-copy TCP TX pbufs, and configures `TCP_SND_BUF=208*TCP_MSS`,
`TCP_WND=48*TCP_MSS`, `TCP_OOSEQ_MAX_PBUFS=6`, `FHOST_RX_BUF_CNT=8`,
and `LWIP_HEAP_SIZE=106K`.

For BL618DG:

```bash
make CHIP=bl618dg BOARD=bl618dgdk CPU_ID=ap CONFIG_ROMAPI=n
```

## Flash

```bash
make flash CHIP=<chipname> COMX=xxx
```
