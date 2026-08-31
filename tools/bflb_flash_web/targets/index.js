/* Target registry, mirroring bflb_flash/targets/__init__.py. */
(function (root, factory) {
  "use strict";
  const isNode = typeof module !== "undefined" && module.exports;
  const bl616 = isNode ? require("./bl616.js") : root.BflbFlashTargetBL616;
  const api = factory(bl616);
  root.BflbFlashTargets = api;
  if (isNode) module.exports = api;
})(typeof globalThis !== "undefined" ? globalThis : window, function (bl616) {
  "use strict";
  const CHIP_DEFS = Object.freeze({ bl616 });
  return Object.freeze({ BL616: bl616, CHIP_DEFS });
});
