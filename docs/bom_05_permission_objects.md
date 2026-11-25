# BOM-05 — Navigator permission, lock, session, and media-capability objects

Selected on `2026-11-25` after BOM-04 completion (`4a68fbf`). Covers eight
Navigator subobjects: `geolocation`, `clipboard`, `credentials`, `locks`,
`wakeLock`, `keyboard`, `mediaSession`, and `mediaCapabilities`, together with
their defining interfaces and constructible `ClipboardItem` and `MediaMetadata`
types.

## Boundary to implement against browser evidence

- Context exposure on `Navigator.prototype`:
  - Exposed in all contexts (secure, insecure, opaque): `geolocation`,
    `mediaSession`, `mediaCapabilities`, plus global interfaces `Geolocation`,
    `GeolocationPositionError`, `GeolocationPosition`, `GeolocationCoordinates`,
    `MediaSession`, `MediaMetadata`, and `MediaCapabilities`.
  - Exposed in `[SecureContext]` only: `clipboard`, `credentials`, `locks`,
    `wakeLock`, `keyboard`, plus global interfaces `Clipboard`, `ClipboardItem`,
    `CredentialsContainer`, `Credential`, `LockManager`, `Lock`, `WakeLock`,
    `WakeLockSentinel`, `Keyboard`, and `KeyboardLayoutMap`.
- Each Navigator getter returns a stable per-generation `HostObject` instance
  sharing its defining prototype and rejecting invalid receivers with
  `TypeError("Illegal invocation")`.
- `navigator.geolocation`: validates callback and `PositionOptions`
  (`enableHighAccuracy`, `maximumAge`, `timeout`) synchronously; queues a Page
  host task invoking the optional error callback (`this === undefined`) with
  `GeolocationPositionError` (`code = 1` insecure-origin message on insecure
  contexts; `code = 3` `"Timeout expired"` when `timeout === 0` on secure
  contexts; `code = 1` `"User denied Geolocation"` otherwise). `watchPosition`
  returns incrementing positive watch IDs cancelable via `clearWatch(id)` before
  delivery.
- `navigator.clipboard` (`EventTarget`): exposes `onclipboardchange`; `read()`
  and `readText()` reject on a Page task with `NotAllowedError`; `write(items)`
  validates `sequence<ClipboardItem>`, resolving synchronously on `[]` and
  rejecting on a Page task with `NotAllowedError` when non-empty;
  `writeText(text)` converts `DOMString` and rejects on a Page task with
  `NotAllowedError`. `new ClipboardItem(items)` constructs native
  `ClipboardItem` instances with `types` and `getType(type)`.
- `navigator.credentials`: `get(options?)` and `create(options?)` validate
  options synchronously, rejecting unsupported/empty type combinations with
  `NotSupportedError` and resolving supported silent/ungranted credential
  queries on a Page task to `null`; `preventSilentAccess()` resolves on a Page
  task to `undefined`; `store(credential)` validates `Credential` brand.
- `navigator.locks`: per-Page in-memory Web Locks queue on non-opaque origins
  (`SecurityError` on opaque origins such as `file:`), supporting `query()`
  (`{held, pending}`), `request(name, [options,] callback)` with `exclusive` and
  `shared` modes, `ifAvailable`, `steal`, leading-hyphen rejection, and Promise
  adoption until lock release.
- `navigator.wakeLock`: `request(type = "screen")` validates `WakeLockType` and
  resolves on a Page task to a native `WakeLockSentinel` (`EventTarget`,
  `released`, `type`, `onrelease`, `release()` dispatching `"release"`).
- `navigator.keyboard`: `getLayoutMap()` resolves on a Page task to a
  deterministic 48-key US `KeyboardLayoutMap` (`size`, `get`, `has`, `entries`,
  `keys`, `values`, `forEach`, `[Symbol.iterator]`); `lock(keyCodes?)` validates
  optional `sequence<DOMString>` and resolves on a Page task to `undefined`;
  `unlock()` returns `undefined`.
- `navigator.mediaSession` & `MediaMetadata`: writable `metadata`
  (`MediaMetadata?`) and `playbackState` (`"none" | "paused" | "playing"`),
  `setActionHandler` (15 Chrome `MediaSessionAction` values), `setCameraActive`,
  `setMicrophoneActive`, and `setPositionState` validation; constructible
  `MediaMetadata` with `title`, `artist`, `album`, resolved frozen `artwork`
  snapshots, and empty `chapterInfo`.
- `navigator.mediaCapabilities`: `decodingInfo(configuration)` and
  `encodingInfo(configuration)` validate `MediaDecodingConfiguration` /
  `MediaEncodingConfiguration` dictionaries and resolve deterministic software
  codec capability results (`supported`, `smooth`, `powerEfficient`, and
  `keySystemAccess`).
