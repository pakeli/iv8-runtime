# BOM-06 implementation & local validation report

Date: `2026-11-25`

**Status: Implemented, Codex-reviewed, and locally validated on macOS arm64.**

## Scope delivered

- Added seven `[SecureContext]` Navigator subobjects (`bluetooth`, `hid`,
  `serial`, `usb`, `xr`, `gpu`, `presentation`) and ten associated global
  interfaces (`Bluetooth`, `HID`, `Serial`, `USB`, `XRSystem`, `GPU`,
  `WGSLLanguageFeatures`, `Presentation`, `PresentationRequest`,
  `PresentationReceiver`), raising secure Navigator presence from **53/70** to
  **60/70** (insecure/opaque remain **36/70**).
- Preserved Chrome's exact prototype key ordering, including trailing
  `requestDevice` / `requestPort` after `constructor` on `HID.prototype`,
  `Serial.prototype`, and `USB.prototype`.

## Verification & Codex review fixes

- Browser references: `test/fixtures/bom_06/{chrome,chrome-insecure}.json` (Chrome 158.0.8127.88, fresh headless profiles).
- Independent Codex review (`codex exec review --uncommitted`):
  - Converted nested filter dictionary members (`USBDeviceFilter`,
    `HIDDeviceFilter`, `SerialPortFilter`, `BluetoothLEScanFilterInit`) before
    user-activation checks.
  - Converted `XRSessionInit.optionalFeatures` and `requiredFeatures` entries via
    WebIDL `DOMString` conversion before activation checks.
  - Captured pristine `Array.prototype[Symbol.iterator]` at generation bootstrap
    so `WGSLLanguageFeatures` iteration is immune to `Array.prototype` mutation.
  - Empirically verified against headless Chrome that `new PresentationRequest(iterable)`
    performs two `Symbol.iterator` lookups (overload resolution + sequence
    conversion); pinned with a regression test.
- Focused suite: `test/test_bom_06_device_objects.py` — **5 passed**.
- Full macOS arm64 suite (Python 3.13 wheel): **3471 passed, 2 skipped**.
