#!/usr/bin/env node
"use strict";

const fs = require("node:fs");
const path = require("node:path");
const { BL616Flasher } = require("./loader.js");
const { NodeSerialTransport } = require("./node/serial.js");

function usage() {
  console.log("Usage: node cli.js --port /dev/ttyUSB0 --file whole.bin [--manual] [--no-run]");
}

function parseArgs(argv) {
  const options = { port: null, file: null, autoReset: true, runAfterFlash: true };
  for (let index = 0; index < argv.length; index += 1) {
    const arg = argv[index];
    if (arg === "--port") options.port = argv[++index];
    else if (arg === "--file") options.file = argv[++index];
    else if (arg === "--manual") options.autoReset = false;
    else if (arg === "--no-run") options.runAfterFlash = false;
    else if (arg === "--help" || arg === "-h") options.help = true;
    else throw new Error(`未知参数：${arg}`);
  }
  return options;
}

function stageText(stage, detail) {
  const names = {
    handshake: "进入 BootROM",
    "boot-info": "读取芯片信息",
    "flash-config": "配置 Flash",
    erase: "擦除 Flash",
    verify: "校验写入结果",
    hash: "计算本地 SHA-256",
    done: "完成",
  };
  if (stage === "write" && detail) return `写入 ${detail.written}/${detail.total}`;
  return names[stage] || stage;
}

let progressLineOpen = false;
let progressLineLength = 0;

function finishProgressLine() {
  if (!progressLineOpen) return;
  process.stdout.write("\n");
  progressLineOpen = false;
  progressLineLength = 0;
}

function renderProgress(value, stage, detail) {
  const percent = Math.max(0, Math.min(100, Math.round(value)));
  const width = 30;
  const filled = Math.round(percent * width / 100);
  const bar = `${"#".repeat(filled)}${"-".repeat(width - filled)}`;
  const line = `[${bar}] ${String(percent).padStart(3)}%  ${stageText(stage, detail)}`;
  const padding = " ".repeat(Math.max(0, progressLineLength - line.length));
  process.stdout.write(`\r${line}${padding}`);
  progressLineOpen = true;
  progressLineLength = line.length;
}

async function main() {
  const options = parseArgs(process.argv.slice(2));
  if (options.help) {
    usage();
    return;
  }
  if (!options.port || !options.file) {
    usage();
    process.exitCode = 2;
    return;
  }

  const filePath = path.resolve(options.file);
  const image = new Uint8Array(fs.readFileSync(filePath));
  let cancelled = false;
  process.once("SIGINT", () => {
    cancelled = true;
    finishProgressLine();
    console.error("\n正在停止，当前命令完成后退出...");
  });

  const transport = new NodeSerialTransport(options.port);
  let lastPercent = -1;
  const flasher = new BL616Flasher(transport, {
    onLog: (message) => {
      finishProgressLine();
      console.log(message);
    },
    onProgress: (value, stage, detail) => {
      const percent = Math.round(value);
      if (percent === lastPercent && stage === "write") return;
      if (!process.stdout.isTTY) {
        if (stage === "write" && percent < 100 && percent % 10 !== 0) return;
        console.log(`${String(percent).padStart(3)}%  ${stageText(stage, detail)}`);
        lastPercent = percent;
        return;
      }
      renderProgress(value, stage, detail);
      lastPercent = percent;
    },
    isCancelled: () => cancelled,
  });

  console.log(`串口: ${options.port}`);
  console.log(`文件: ${filePath} (${image.length} 字节)`);
  await transport.open();
  try {
    const result = await flasher.flashWhole(image, options);
    finishProgressLine();
    console.log(`BL616 ${result.version.toUpperCase()} 烧写成功，用时 ${(result.elapsedMs / 1000).toFixed(1)} 秒`);
  } finally {
    await transport.close();
  }
}

main().catch((error) => {
  finishProgressLine();
  console.error(`失败: ${error.message}`);
  process.exitCode = 1;
});
