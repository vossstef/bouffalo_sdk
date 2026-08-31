/* Shared BL616 flashing workflow for browsers and Node.js. */
(function (root, factory) {
  "use strict";
  const isNode = typeof module !== "undefined" && module.exports;
  const protocol = isNode ? require("./protocol.js") : root.BflbFlashProtocol;
  const targets = isNode ? require("./targets/index.js") : root.BflbFlashTargets;
  const nodeCrypto = isNode ? require("node:crypto") : null;
  const api = factory(protocol, targets, nodeCrypto);
  root.BflbFlasher = api;
  if (isNode) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : window, function (protocol, targets, nodeCrypto) {
  "use strict";

  const {
    COMMAND,
    buildFrame,
    bytesToHex,
    concatBytes,
    flashSizeFromJedec,
    uint32LE,
  } = protocol;

  const TARGET = targets.BL616;
  const BAUD_RATE = TARGET.CONFIG.baudRate;
  const WRITE_CHUNK_SIZE = TARGET.CONFIG.writeChunkSize;
  const ERASE_TIMEOUT_MS = TARGET.CONFIG.eraseTimeoutMs;

  function delay(ms) {
    return new Promise((resolve) => setTimeout(resolve, ms));
  }

  async function sha256(data) {
    if (nodeCrypto) return new Uint8Array(nodeCrypto.createHash("sha256").update(data).digest());
    if (!globalThis.crypto?.subtle) throw new Error("当前环境不支持 SHA-256");
    return new Uint8Array(await globalThis.crypto.subtle.digest("SHA-256", data));
  }

  class BL616Flasher {
    constructor(transport, callbacks = {}) {
      this.transport = transport;
      this.onLog = callbacks.onLog || (() => {});
      this.onProgress = callbacks.onProgress || (() => {});
      this.isCancelled = callbacks.isCancelled || (() => false);
    }

    log(message, level = "info") {
      this.onLog(message, level);
    }

    progress(value, stage, detail) {
      this.onProgress(value, stage, detail);
    }

    ensureNotCancelled() {
      if (!this.isCancelled()) return;
      const error = new Error("用户取消烧写");
      error.name = "AbortError";
      throw error;
    }

    async autoResetIntoBootrom() {
      try {
        await this.transport.setSignals({ dataTerminalReady: false, requestToSend: false });
        await delay(200);
        for (let count = 0; count < 2; count += 1) {
          await this.transport.setSignals({ dataTerminalReady: false, requestToSend: true });
          await delay(50);
          await this.transport.setSignals({ dataTerminalReady: false, requestToSend: false });
          await delay(100);
        }
      } catch (error) {
        throw new Error(`自动复位失败，请关闭自动复位并手动进入 BootROM：${error.message}`);
      }
    }

    async handshake(autoReset) {
      const ok = new TextEncoder().encode("OK");
      const syncLength = syncLengthForBaud(this.transport.baudRate || BAUD_RATE);
      for (let attempt = 1; attempt <= 3; attempt += 1) {
        this.ensureNotCancelled();
        if (autoReset) await this.autoResetIntoBootrom();
        this.transport.clearInput();
        await delay(50);
        await this.transport.write(new Uint8Array(syncLength).fill(0x55));
        try {
          await this.transport.readUntil(ok, 1200);
          this.transport.clearInput();
          this.log(`BootROM 握手成功（第 ${attempt} 次）`, "success");
          return;
        } catch (_) {
          this.log(`BootROM 握手第 ${attempt} 次未响应`, "warn");
          if (!autoReset) await delay(500);
        }
      }
      throw new Error("BootROM 握手失败。请按住 BOOT，按一下 RESET，再松开 BOOT 后重试");
    }

    async sendBootCommand(command, payload = new Uint8Array(), response = false, timeoutMs = 3000) {
      await this.transport.write(buildFrame(command, payload, false));
      return response
        ? this.transport.readResponse(timeoutMs)
        : this.transport.readAck(timeoutMs);
    }

    async sendFlashCommand(command, payload = new Uint8Array(), response = false, timeoutMs = 3000) {
      await this.transport.write(buildFrame(command, payload, true));
      return response
        ? this.transport.readResponse(timeoutMs)
        : this.transport.readAck(timeoutMs);
    }

    async setDeviceTimeout(version, timeoutMs) {
      if (version === "a0") {
        const payload = concatBytes(uint32LE(0x6102df04), uint32LE((timeoutMs << 16) | 0x1200));
        await this.sendBootCommand(COMMAND.MEMORY_WRITE, payload);
      } else {
        await this.sendBootCommand(COMMAND.SET_TIMEOUT, uint32LE(timeoutMs));
      }
    }

    async configureBaudRate() {
      if (this.transport.baudRate !== BAUD_RATE) {
        throw new Error(`串口必须以 ${BAUD_RATE} baud 打开`);
      }
      const payload = concatBytes(uint32LE(1), uint32LE(BAUD_RATE));
      await this.sendFlashCommand(COMMAND.CLOCK_SET, payload);
      this.transport.clearInput();
      await delay(10);
      this.log(`串口使用 ${BAUD_RATE} baud`);
    }

    async eraseFlash(length) {
      const payload = concatBytes(uint32LE(0), uint32LE(length - 1));
      await this.transport.write(buildFrame(COMMAND.FLASH_ERASE, payload, true));
      while (true) {
        const ack = await this.transport.readAck(ERASE_TIMEOUT_MS);
        if (ack.status === "OK") return;
        if (ack.status === "PD") this.log("Flash 正在擦除", "warn");
      }
    }

    async writeFlash(data, version) {
      await this.setDeviceTimeout(version, 2000);
      for (let offset = 0; offset < data.length; offset += WRITE_CHUNK_SIZE) {
        this.ensureNotCancelled();
        const chunk = data.subarray(offset, Math.min(offset + WRITE_CHUNK_SIZE, data.length));
        await this.sendFlashCommand(COMMAND.FLASH_WRITE, concatBytes(uint32LE(offset), chunk));
        const written = offset + chunk.length;
        this.progress(15 + (written / data.length) * 70, "write", { written, total: data.length });
      }
      await this.sendFlashCommand(COMMAND.FLASH_WRITE_CHECK);
    }

    async verifyFlash(data) {
      this.progress(88, "hash");
      const localHash = await sha256(data);
      await this.sendFlashCommand(COMMAND.FLASH_XIP_READ_START);
      let deviceHash;
      try {
        deviceHash = await this.sendFlashCommand(
          COMMAND.FLASH_XIP_READ_SHA,
          concatBytes(uint32LE(0), uint32LE(data.length)),
          true,
          Math.max(5000, Math.ceil(data.length / (1024 * 1024)) * 5000),
        );
        if (deviceHash.length !== 32) {
          throw new Error(`设备 SHA-256 响应长度应为 32，实际为 ${deviceHash.length}`);
        }
      } finally {
        await this.sendFlashCommand(COMMAND.FLASH_XIP_READ_FINISH);
      }
      if (bytesToHex(localHash) !== bytesToHex(deviceHash)) {
        throw new Error(`SHA-256 校验失败：本地 ${bytesToHex(localHash)}，设备 ${bytesToHex(deviceHash)}`);
      }
      this.log(`SHA-256 ${bytesToHex(localHash)}`, "success");
      return localHash;
    }

    async resetToRun() {
      try {
        await this.transport.setSignals({ dataTerminalReady: true });
        await delay(50);
        await this.transport.setSignals({ requestToSend: true });
        await delay(50);
        await this.transport.setSignals({ requestToSend: false });
      } catch (error) {
        this.log(`无法通过 DTR/RTS 复位，请手动按 RESET：${error.message}`, "warn");
      }
    }

    async flashWhole(data, options = {}) {
      if (!(data instanceof Uint8Array)) data = new Uint8Array(data);
      if (data.length < 256) throw new Error("文件太小，不是有效的 BL616 whole.bin");
      if (String.fromCharCode(...data.slice(0, 4)) !== "BFNP") {
        throw new Error("文件 0x0 处没有 BL616 Boot Header (BFNP)");
      }

      const autoReset = options.autoReset !== false;
      const runAfterFlash = options.runAfterFlash !== false;
      const startedAt = Date.now();
      this.progress(1, "handshake");
      this.log("开始烧写 whole.bin 到 0x00000000");
      await this.handshake(autoReset);

      this.progress(4, "boot-info");
      await delay(100);
      const bootInfo = await this.sendBootCommand(COMMAND.GET_BOOT_INFO, new Uint8Array(), true);
      const version = TARGET.detectVersion(bootInfo);
      this.log(`检测到 BL616 ${version.toUpperCase()}，BootROM 0x${bytesToHex(bootInfo.slice(0, 4))}`);

      await this.setDeviceTimeout(version, 10000);
      await this.configureBaudRate();
      this.progress(7, "flash-config");
      const flashPin = TARGET.flashPinFromBootInfo(bootInfo);
      const flashConfig = TARGET.flashConfigFromBootInfo(bootInfo);
      this.log(`Flash pin 0x${flashPin.toString(16).padStart(2, "0")}`);
      await this.sendFlashCommand(COMMAND.FLASH_SET_PARA, uint32LE(flashConfig));
      const jedec = await this.sendFlashCommand(COMMAND.FLASH_READ_JEDEC_ID, new Uint8Array(), true);
      const flashSize = flashSizeFromJedec(jedec);
      this.log(`Flash JEDEC ID 0x${bytesToHex(jedec.slice(0, 3))}，容量 ${flashSize} 字节`);
      if (data.length > flashSize) {
        throw new Error(`whole.bin 为 ${data.length} 字节，超过 Flash 容量 ${flashSize} 字节`);
      }

      this.progress(10, "erase");
      await this.eraseFlash(data.length);
      this.progress(15, "write", { written: 0, total: data.length });
      await this.writeFlash(data, version);
      this.progress(87, "verify");
      const hash = await this.verifyFlash(data);
      this.progress(100, "done");
      if (runAfterFlash) await this.resetToRun();

      return {
        bootInfo,
        version,
        jedec,
        flashSize,
        flashConfig,
        flashPin,
        sha256: hash,
        elapsedMs: Date.now() - startedAt,
      };
    }
  }

  function syncLengthForBaud(baudRate) {
    return Math.min(512, Math.floor(0.006 * baudRate / 10));
  }

  return {
    BL616Flasher,
    BAUD_RATE,
    ERASE_TIMEOUT_MS,
    WRITE_CHUNK_SIZE,
    sha256,
    syncLengthForBaud,
  };
});
