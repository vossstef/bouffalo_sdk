# bflb_flash_web

Pure static Web Serial flasher for a BL616 `whole.bin`. It uses the BL616
BootROM flash commands directly and does not require an `eflash_loader.bin`.

Serve this directory from localhost and open it in desktop Chrome or Edge:

```sh
python3 -m http.server 8000 --directory tools/bflb_flash_web
```

The page writes the selected file at flash address `0x00000000`, performs an
XIP SHA-256 verification, and can reset the board after a successful write. It
opens both Web Serial and the Node.js serial port at 2000000 baud so the BL616
BootROM auto-baud handshake and flash transfer use one rate without switching
or reopening the port.

Run the protocol unit test with:

```sh
npm --prefix tools/bflb_flash_web test
```

The browser and local Node.js CLI use the same `protocol.js`, `loader.js`, and
`targets/bl616.js` implementation:

```sh
npm --prefix tools/bflb_flash_web install
node tools/bflb_flash_web/cli.js \
  --port /dev/ttyUSB0 \
  --file examples/wifi/sta/wifi_tcp/build/build_out/flash/whole.bin
```
