# BOM-05 implementation & local validation report

Date: `2026-11-25`

**Status: Implemented, Codex-reviewed, and locally validated on macOS arm64.**

## Scope delivered

- Added eight Navigator subobjects raising secure Navigator presence from
  **45/70** to **53/70** and insecure/opaque presence from **33/70** to
  **36/70**:
  - All contexts: `geolocation`, `mediaSession`, `mediaCapabilities`, plus
    global interfaces `Geolocation`, `GeolocationPositionError`,
    `GeolocationPosition`, `GeolocationCoordinates`, `MediaSession`,
    `MediaMetadata`, and `MediaCapabilities`.
  - `[SecureContext]` only: `clipboard`, `credentials`, `locks`, `wakeLock`,
    `keyboard`, plus global interfaces `Clipboard`, `ClipboardItem` (including
    `ClipboardItem.supports`), `CredentialsContainer`, `Credential` (including
    `Credential.isConditionalMediationAvailable`), `LockManager`, `Lock`,
    `WakeLock`, `WakeLockSentinel`, `Keyboard`, and `KeyboardLayoutMap`.

## Verification & Codex review fixes

- Browser references: `test/fixtures/bom_05/{chrome,chrome-insecure,chrome-opaque}.json` (Chrome 158.0.8127.88, fresh headless profiles).
- Independent Codex review (`codex exec review --uncommitted`):
  - Enforced per-name FIFO queueing in `LockManager` so shared requests cannot
    bypass an earlier queued exclusive request.
  - Rejected displaced lock holders immediately with `DOMException("AbortError", ...)`
    when `steal: true` breaks a held lock (verified against Chrome).
  - Validated `LockOptions.signal`, `CredentialRequestOptions.signal`,
    `CredentialCreationOptions.signal`, and `CredentialMediationRequirement`.
  - Normalized string and `Promise<string | Blob>` entries in
    `ClipboardItem.getType()` to native `Blob` instances without consulting
    script-mutable `globalThis.Blob`.
  - Switched `WakeLockSentinel.release()` to `create_runtime_event` so release
    events fire independently of `globalThis.Event`.
  - Retained Blink's empirically verified `value`-before-`done` iterator step
    unpacking order.
- Focused suite: `test/test_bom_05_permission_objects.py` — **6 passed**.
- Full macOS arm64 suite (Python 3.13 wheel): **3466 passed, 2 skipped**.
