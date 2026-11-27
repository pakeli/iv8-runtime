# BOM-04 Navigator method & DOMException browser reference

Captured on **2026-11-23** / **2026-11-24** using fresh temporary headless
profiles in **Google Chrome 157.0.8103.80**, macOS arm64, over a local CDP pipe
with external DNS disabled and no user profile, device, permission grant, or
virtual-time shortcut.

- `probe.js` -> `chrome.json` (secure loopback HTTP) and `chrome-insecure.json`
  (insecure hostname mapped to loopback): shapes, descriptors, receiver brand
  checks, argument conversion ordering, and synchronous/microtask/task outcomes
  for `canShare`, `share`, `setAppBadge`, `clearAppBadge`,
  `registerProtocolHandler`, `unregisterProtocolHandler`, `getUserMedia`,
  `webkitGetUserMedia`, `requestMIDIAccess`, and `requestMediaKeySystemAccess`.
- `media_keys_probe.js` -> `chrome-media-keys.json`: ClearKey vs external CDM
  configuration matrix and `MediaKeySystemAccess` / `MediaKeys` /
  `MediaKeySession` shapes and method outcomes.
- `dom_exception_probe.js` -> `chrome-dom-exception.json`: `DOMException`
  constructor, `Error.prototype` inheritance, 25 legacy constants, prototype key
  order, legacy error-code mapping, and platform stack attachment.
