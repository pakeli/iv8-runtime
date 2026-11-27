# BOM-04 implementation & local validation report

Date: `2026-11-25`

**Status: Implemented, Codex-reviewed, and locally validated on macOS arm64.**

## Scope delivered

- Added the ten remaining method-only `[SecureContext]` operations on
  `Navigator.prototype`: `canShare`, `share`, `setAppBadge`, `clearAppBadge`,
  `registerProtocolHandler`, `unregisterProtocolHandler`, `getUserMedia`,
  `webkitGetUserMedia`, `requestMIDIAccess`, and `requestMediaKeySystemAccess`,
  raising secure Navigator presence from **35/70** to **45/70** (insecure and
  opaque remain **33/70**).
- Added native constructible `DOMException` (`Error.prototype` inheritance,
  `code`/`name`/`message` accessors, 25 legacy error constants on constructor
  and prototype, single `NewTarget.prototype` lookup on `Reflect.construct`,
  and V8 stack capture on platform-created instances).
- Added software `org.w3.clearkey` EME support (`MediaKeySystemAccess`,
  `MediaKeys`, `MediaKeySession`, `MediaKeyStatusMap`, exposed as globals in
  secure contexts only), matching Chrome 157.0.8103.80's configuration filter,
  prototype key order, and task-delivered `createMediaKeys()`.
- Added native `File` private branding in `file_constructor` so `ShareData.files`
  distinguishes `File` instances from `Blob` and plain objects.

## Explicit profile boundaries

- Offline Page profile has no transient user activation (`share` rejects with
  `NotAllowedError` after converting `ShareData`), no media capture permission
  (`getUserMedia` / `webkitGetUserMedia` invoke error callbacks on a Page task
  with `NotAllowedError`), no MIDI grant (`requestMIDIAccess` rejects on a Page
  task with `NotAllowedError`), and no external hardware CDM (`com.widevine.alpha`
  and unknown key systems reject with `NotSupportedError`).
- `setAppBadge`, `clearAppBadge`, `registerProtocolHandler`, and
  `unregisterProtocolHandler` perform full WebIDL/RFC3986/URL/same-origin
  validation without modifying host OS state.
- Pre-existing non-BOM error paths remain unchanged to preserve prior milestone
  contracts.

## Verification

- Browser references: `test/fixtures/bom_04/{chrome,chrome-insecure,chrome-media-keys,chrome-dom-exception}.json` (Chrome 157.0.8103.80, fresh headless profiles).
- Independent Codex review (`codex exec review --uncommitted`):
  - Verified all three findings empirically against headless Chrome:
    1. Iterator step unpacking order (`value` before `done`): confirmed Chrome/Blink `UnpackIteratorResult` reads `value` before `done` even on terminal steps; pinned by test and comment.
    2. Nested `MediaTrackConstraints` dictionary conversion in `getUserMedia` / `webkitGetUserMedia`: fixed to convert Chrome's 39 constraint keys (`aspectRatio`..`zoom`, `advanced`) synchronously before callback validation.
    3. ClearKey codec syntax validation: tightened `avc1`/`avc3`, `vp09`, `av01`, and `mp4a` profile/level validation to reject malformed identifiers such as `avc1.garbage` and `mp4a.40.999`.
- Focused suite: `test/test_bom_04_navigator_methods.py` — **9 passed** (covering all captured call cases, ClearKey configuration and codec matrices, `DOMException`, receiver channels, nested track constraints, secure/insecure reload lifecycle, `watch_apis`, and `access_trace`).
- Full macOS arm64 suite (Python 3.13 wheel): **3460 passed, 2 skipped**.
