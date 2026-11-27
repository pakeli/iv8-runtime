# BOM-06 — secure Navigator device, WebXR, WebGPU, and Presentation objects

Selected on `2026-11-25` after BOM-05 completion (`7c88322`). Covers seven
`[SecureContext]` Navigator subobjects: `bluetooth`, `hid`, `serial`, `usb`,
`xr`, `gpu`, and `presentation`, together with their defining interfaces,
`WGSLLanguageFeatures`, `PresentationReceiver`, and constructible
`PresentationRequest`.

## Boundary to implement against browser evidence

- All seven Navigator getters and associated global interfaces (`Bluetooth`,
  `HID`, `Serial`, `USB`, `XRSystem`, `GPU`, `WGSLLanguageFeatures`,
  `Presentation`, `PresentationRequest`, `PresentationReceiver`) are exposed in
  secure contexts only.
- Prototype key order matches Chrome 158.0.8127.88, including placing
  `requestDevice` / `requestPort` after `constructor` on `HID.prototype`,
  `Serial.prototype`, and `USB.prototype`.
- `navigator.bluetooth` (`EventTarget`): `getAvailability()` resolves on a Page
  task to `true`; `requestDevice(options?)` validates `RequestDeviceOptions`
  and rejects with `SecurityError` without transient user activation.
- `navigator.hid`, `navigator.serial`, and `navigator.usb` (`EventTarget`):
  expose `onconnect` and `ondisconnect`; `getDevices()` / `getPorts()` resolve
  on a Page task to fresh empty arrays; `requestDevice` / `requestPort`
  validate filter dictionaries and reject with `SecurityError` without user
  activation.
- `navigator.xr` (`EventTarget`): exposes `ondevicechange`;
  `isSessionSupported(mode)` validates `XRSessionMode` (`"inline"` -> `true`,
  `"immersive-vr"` / `"immersive-ar"` -> `false`); `requestSession(mode, options?)`
  validates mode and `XRSessionInit` and rejects with `SecurityError` without
  user activation.
- `navigator.gpu`: `getPreferredCanvasFormat()` returns deterministic
  `"bgra8unorm"`; `wgslLanguageFeatures` returns a stable Setlike
  `WGSLLanguageFeatures` with Chrome's 13 feature tokens; `requestAdapter(options?)`
  validates `GPURequestAdapterOptions` (`powerPreference` enum) and resolves to
  `null` in the headless no-GPU profile.
- `navigator.presentation`: writable `defaultRequest` (`PresentationRequest?`,
  initially `null`) and `receiver` (`null`); constructible
  `PresentationRequest(urlOrUrls)` (`EventTarget`) resolves HTTP(S) URLs against
  the Page base URL and exposes `onconnectionavailable`, `start()`,
  `reconnect(id)`, and `getAvailability()`.
