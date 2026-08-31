"use strict";

const { autoDetect } = require("@serialport/bindings-cpp");
const { SerialPortStream } = require("@serialport/stream");
const { concatBytes, bytesToHex, formatErrorCode } = require("../protocol.js");
const { BAUD_RATE } = require("../loader.js");

const SerialBinding = autoDetect();

class NodeSerialTransport {
  constructor(path) {
    this.path = path;
    this.baudRate = BAUD_RATE;
    this.port = null;
    this.buffer = new Uint8Array();
    this.waiters = new Set();
    this.readError = null;
    this.closed = false;
    this.signals = { dataTerminalReady: false, requestToSend: false };
  }

  async open() {
    this.port = new SerialPortStream({
      binding: SerialBinding,
      path: this.path,
      baudRate: this.baudRate,
      dataBits: 8,
      stopBits: 1,
      parity: "none",
      rtscts: false,
      autoOpen: false,
    });
    this.port.on("data", (chunk) => {
      this.buffer = concatBytes(this.buffer, new Uint8Array(chunk));
      this.wakeWaiters();
    });
    this.port.on("error", (error) => {
      this.readError = error;
      this.wakeWaiters();
    });
    this.port.on("close", () => {
      if (!this.closed) this.readError = new Error("串口已关闭");
      this.wakeWaiters();
    });
    await new Promise((resolve, reject) => {
      this.port.open((error) => (error ? reject(error) : resolve()));
    });
  }

  wakeWaiters() {
    for (const resolve of this.waiters) resolve();
    this.waiters.clear();
  }

  async waitForData(timeoutMs) {
    if (this.readError) throw this.readError;
    return new Promise((resolve, reject) => {
      let timer;
      const wake = () => {
        clearTimeout(timer);
        this.waiters.delete(wake);
        resolve();
      };
      timer = setTimeout(() => {
        this.waiters.delete(wake);
        reject(new Error("等待设备响应超时"));
      }, timeoutMs);
      this.waiters.add(wake);
    });
  }

  async readExactly(length, timeoutMs = 3000) {
    const deadline = Date.now() + timeoutMs;
    while (this.buffer.length < length) {
      const remaining = deadline - Date.now();
      if (remaining <= 0) throw new Error("等待设备响应超时");
      await this.waitForData(remaining);
    }
    const result = this.buffer.slice(0, length);
    this.buffer = this.buffer.slice(length);
    return result;
  }

  async readUntil(sequence, timeoutMs = 3000) {
    const deadline = Date.now() + timeoutMs;
    while (true) {
      const index = findSequence(this.buffer, sequence);
      if (index >= 0) {
        const result = this.buffer.slice(0, index + sequence.length);
        this.buffer = this.buffer.slice(index + sequence.length);
        return result;
      }
      const remaining = deadline - Date.now();
      if (remaining <= 0) throw new Error("等待握手响应超时");
      await this.waitForData(remaining);
    }
  }

  async write(bytes) {
    const buffer = Buffer.from(bytes.buffer, bytes.byteOffset, bytes.byteLength);
    await new Promise((resolve, reject) => {
      this.port.write(buffer, (error) => {
        if (error) return reject(error);
        this.port.drain((drainError) => (drainError ? reject(drainError) : resolve()));
      });
    });
  }

  clearInput() {
    this.buffer = new Uint8Array();
    if (this.port?.isOpen) this.port.flush(() => {});
  }

  async readAck(timeoutMs = 3000) {
    const ack = await this.readExactly(2, timeoutMs);
    const text = String.fromCharCode(ack[0], ack[1]);
    if (text === "OK" || text === "PD") return { status: text };
    if (text === "FL") {
      const errorBytes = await this.readExactly(2, timeoutMs);
      const code = errorBytes[0] | (errorBytes[1] << 8);
      throw new Error(`设备返回错误 ${formatErrorCode(code)}`);
    }
    throw new Error(`无效设备响应 0x${bytesToHex(ack)}`);
  }

  async readResponse(timeoutMs = 3000) {
    const ack = await this.readAck(timeoutMs);
    if (ack.status !== "OK") throw new Error(`意外设备响应 ${ack.status}`);
    const lengthBytes = await this.readExactly(2, timeoutMs);
    const length = lengthBytes[0] | (lengthBytes[1] << 8);
    return this.readExactly(length, timeoutMs);
  }

  async setSignals(signals) {
    if (signals.dataTerminalReady !== undefined) {
      this.signals.dataTerminalReady = signals.dataTerminalReady;
    }
    if (signals.requestToSend !== undefined) {
      this.signals.requestToSend = signals.requestToSend;
    }
    await new Promise((resolve, reject) => {
      this.port.set({
        dtr: this.signals.dataTerminalReady,
        rts: this.signals.requestToSend,
      }, (error) => (error ? reject(error) : resolve()));
    });
  }

  async close() {
    this.closed = true;
    this.wakeWaiters();
    if (!this.port?.isOpen) return;
    await new Promise((resolve, reject) => {
      this.port.close((error) => (error ? reject(error) : resolve()));
    });
  }
}

function findSequence(haystack, needle) {
  outer: for (let index = 0; index <= haystack.length - needle.length; index += 1) {
    for (let inner = 0; inner < needle.length; inner += 1) {
      if (haystack[index + inner] !== needle[inner]) continue outer;
    }
    return index;
  }
  return -1;
}

module.exports = { NodeSerialTransport };
