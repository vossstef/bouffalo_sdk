/* Common target configuration, mirroring bflb_flash/targets/common.py. */
(function (root, factory) {
  "use strict";
  const api = factory();
  root.BflbFlashTargetCommon = api;
  if (typeof module !== "undefined" && module.exports) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : window, function () {
  "use strict";

  function uartConfig(options) {
    return Object.freeze({
      chip: options.chip,
      baudRate: options.baudRate ?? 2000000,
      writeChunkSize: options.writeChunkSize ?? 2048,
      eraseTimeoutMs: options.eraseTimeoutMs ?? 100000,
    });
  }

  return { uartConfig };
});
