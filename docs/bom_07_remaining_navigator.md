# BOM-07 — remaining Navigator APIs and DOMRect geometry types

Selected on `2026-11-25` after BOM-06 completion (`538a31d`). Completes all ten
remaining `Navigator` members from the Chrome 157 baseline inventory (`60/70 ->
70/70` in secure contexts; `36/70 -> 38/70` in insecure and opaque contexts):

- All contexts: `ink`, `windowControlsOverlay`
- `[SecureContext]` only: `cpuPerformance`, `devicePosture`, `login`, `managed`,
  `mediaDevices`, `serviceWorker`, `storageBuckets`, `virtualKeyboard`

Also adds the seventeen associated global interfaces observed in Chrome
`158.0.8127.88` (`DOMRectReadOnly`, `DOMRect`, `Ink`,
`DelegatedInkTrailPresenter`, `WindowControlsOverlay`, `InputDeviceInfo` in all
contexts; `MediaDevices`, `MediaDeviceInfo`, `ServiceWorkerContainer`,
`ServiceWorker`, `ServiceWorkerRegistration`, `StorageBucketManager`,
`StorageBucket`, `NavigatorLogin`, `NavigatorManagedData`, `DevicePosture`,
`VirtualKeyboard` in secure contexts only).

## Boundary to implement against browser evidence

- Context exposure matches Chrome `158.0.8127.88`, including the Blink quirk
  where `InputDeviceInfo` is exposed on `window` in insecure/opaque contexts and
  still inherits from the unexposed `MediaDeviceInfo` interface object and
  prototype (`Object.getPrototypeOf(InputDeviceInfo) === MediaDeviceInfo` without
  installing `window.MediaDeviceInfo` in insecure contexts).
- Prototype key order matches Chrome `158.0.8127.88`, including placing
  `onerror` after `constructor` on `ServiceWorker.prototype`, `persist` after
  `constructor` on `StorageBucket.prototype`, and exact member order (with
  `unregister`/`update` before `backgroundFetch`..`pushManager` and `cookies`
  after `constructor`) on `ServiceWorkerRegistration.prototype`.
- `DOMRectReadOnly` and `DOMRect` (`DOMRect : DOMRectReadOnly`): constructible
  (`length = 0`, WebIDL `unrestricted double` arguments defaulting to `0`,
  preserving `NaN`, `±Infinity`, and `-0`), static `fromRect(other?)` (`length = 0`,
  reading `height`, `width`, `x`, `y` from `DOMRectInit`), `top`/`right`/`bottom`/`left`
  using ECMAScript `Math.min`/`Math.max` semantics, `toJSON()`, and `DOMRect`
  own `x`/`y`/`width`/`height` read-write accessors.
- `navigator.cpuPerformance` (`[SecureContext]`): deterministic integer tier
  derived solely from `HostEnvironment::hardware_concurrency` (`<= 0 -> 0`,
  `<= 2 -> 1`, `<= 4 -> 2`, `< 8 -> 3`, `>= 8 -> 4`; yields `4` under the
  default `hardwareConcurrency = 8` profile), never querying host hardware.
- `navigator.mediaDevices` (`EventTarget`, `[SecureContext]`): `ondevicechange`,
  `getSupportedConstraints()` (Chrome's 36 boolean keys), `enumerateDevices()`
  resolving on a Page task to `[InputDeviceInfo("audioinput"),
  InputDeviceInfo("videoinput"), MediaDeviceInfo("audiooutput")]` with redacted
  `""` IDs/labels and `{}` `getCapabilities()`, `getUserMedia(constraints?)`
  validating `UserMediaStreamConstraints` and rejecting on a Page task with
  `NotAllowedError`, `getDisplayMedia(options?)` validating
  `DisplayMediaStreamOptions` (`video` defaulting to `true`) and rejecting on a
  Page task with `NotAllowedError` instead of opening an OS picker, and
  `setCaptureHandleConfig(config?)` validating 1024-UTF-16 `handle` length and
  `permittedOrigins` wildcard/tuple-origin rules.
- `navigator.serviceWorker` (`EventTarget`, `[SecureContext]`): `controller`
  (`null`), cached pending `ready` `Promise`, `oncontrollerchange`, `onmessage`,
  `onmessageerror`, `startMessages()`, `getRegistration(clientURL?)` (rejecting
  cross-origin/invalid URLs with `SecurityError` and resolving same-origin on a
  Page task to `undefined`), `getRegistrations()` (resolving on a Page task to
  `[]`), and `register(scriptURL, options?)` (validating `RegistrationOptions`
  enums `WorkerType`/`ServiceWorkerUpdateViaCache`, HTTP(S) protocol, `%2f`/`%5c`
  path restrictions, and same-origin script/scope before rejecting on a Page
  task with `TypeError` without performing network I/O).
- `navigator.storageBuckets` (`[SecureContext]`): per-Page in-memory bucket map
  with Chrome bucket-name validation (`1..63` chars, `[a-z0-9][a-z0-9_-]*`,
  quoted in `open` errors and unquoted in `delete` errors), `StorageBucketOptions`
  validation (`quota != 0`, valid future `expires`), `open`, `keys`, `delete`,
  and `StorageBucket` instances (`name`, `persisted`, `persist`, `estimate`,
  `expires`, `setExpires`, offline `IDBFactory`/`CacheStorage` shells on
  `indexedDB`/`caches`, and `NotSupportedError` on `getDirectory()`).
- `navigator.login` (`[SecureContext]`): `setStatus(status)` validating
  `LoginStatus` (`"logged-in" | "logged-out"`) and resolving on a Page task to
  `undefined`.
- `navigator.managed` (`EventTarget`, `[SecureContext]`):
  `onmanagedconfigurationchange` and `getManagedConfiguration(keys)` converting
  `sequence<DOMString>` and rejecting on a Page task with `NotAllowedError`.
- `navigator.ink` (all contexts): `requestPresenter(param?)` validating optional
  `InkPresenterParam.presentationArea` as nullable `Element` and resolving
  synchronously to `DelegatedInkTrailPresenter` (`presentationArea` getter and
  `updateInkTrailStartPoint(evt, style)` arity/`PointerEvent` check).
- `navigator.devicePosture` (`EventTarget`, `[SecureContext]`): `type`
  (`"continuous"`) and `onchange`.
- `navigator.virtualKeyboard` (`EventTarget`, `[SecureContext]`): `[SameObject]`
  `boundingRect` (`DOMRect(0, 0, 0, 0)`), writable `overlaysContent` (`false`),
  `ongeometrychange`, `show()`, and `hide()`.
- `navigator.windowControlsOverlay` (`EventTarget`, all contexts): `visible`
  (`false`), `ongeometrychange`, and `getTitlebarAreaRect()` returning a fresh
  `DOMRect(0, 0, 0, 0)` per call.
