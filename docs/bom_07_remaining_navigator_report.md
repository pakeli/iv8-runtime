# BOM-07 implementation & local validation report

Date: `2026-11-26`

**Status: Implemented, Codex-reviewed, and locally validated on macOS arm64.**

## Scope delivered

- Added the final ten `Navigator` members (`ink`, `windowControlsOverlay` in all
  contexts; `cpuPerformance`, `devicePosture`, `login`, `managed`,
  `mediaDevices`, `serviceWorker`, `storageBuckets`, `virtualKeyboard` in
  `[SecureContext]`), raising secure `Navigator` coverage against
  `docs/browser_api_inventory.json` from **60/70** to **70/70** and
  insecure/opaque coverage from **36/70** to **38/70**.
- Added seventeen associated global interfaces (`DOMRectReadOnly`, `DOMRect`,
  `Ink`, `DelegatedInkTrailPresenter`, `WindowControlsOverlay`, `InputDeviceInfo`
  in all contexts; `MediaDevices`, `MediaDeviceInfo`, `ServiceWorkerContainer`,
  `ServiceWorker`, `ServiceWorkerRegistration`, `StorageBucketManager`,
  `StorageBucket`, `NavigatorLogin`, `NavigatorManagedData`, `DevicePosture`,
  `VirtualKeyboard` in secure contexts), matching Chrome's insecure
  `InputDeviceInfo -> MediaDeviceInfo` prototype chain and exact prototype key
  ordering (`ServiceWorker.prototype.onerror`, `StorageBucket.prototype.persist`,
  and `ServiceWorkerRegistration.prototype`'s interleaved member order +
  trailing `cookies`).

## Verification & Codex review fixes

- Browser references: `test/fixtures/bom_07/{chrome,chrome-insecure,chrome-opaque}.json`
  (Google Chrome `158.0.8127.88`, fresh headless profiles).
- Independent Codex review (`codex exec review --uncommitted`) — all five code
  findings verified against headless Chrome `158.0.8127.88` and fixed:
  - Converted recognized `MediaTrackConstraintSet` values (numeric, boolean/string,
    `pointsOfInterest`, and nested `advanced` constraint sets) before applying
    the offline `NotAllowedError` permission denial.
  - Converted all ten `DisplayMediaStreamOptions` dictionary members in Chrome's
    lexicographical order (`audio`, `audioSelection`, `controller`,
    `monitorTypeSurfaces`, `preferCurrentTab`, `selfBrowserSurface`,
    `surfaceSwitching`, `systemAudio`, `video`, `windowAudio`) including
    `DisplayMediaIncludeOrExclude` / `DisplayMediaSystemWindowOrExclude` enum
    validation and `CaptureController` brand checks.
  - Resolved `ServiceWorkerContainer.prototype.getRegistration` and `register`
    against `PageState::current_url()` so `history.pushState` / `replaceState`
    updates are reflected immediately.
  - Treated explicit `null` for `StorageBucketOptions.quota` and `expires` as
    WebIDL non-nullable integer `0` (triggering Chrome's zero-quota and invalid
    expiration `TypeError`s) instead of as omitted.
  - Evicted expired buckets in `StorageBucketManager` before `keys()` and
    `open()` so expired deadlines remove the bucket and allow fresh recreation.
- Focused suite: `test/test_bom_07_remaining_navigator.py` — **7 passed**.
- Full macOS arm64 suite (Python 3.13 wheel): **3478 passed, 2 skipped**.
