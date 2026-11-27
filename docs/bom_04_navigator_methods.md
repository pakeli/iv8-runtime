# BOM-04 — secure Navigator capability methods, ClearKey EME, and DOMException

Selected on `2026-11-24` as the first continuation slice after BOM-03 under the
BOM-first coverage queue (`docs/bom_coverage_plan.md`). Covers the ten remaining
method-only Navigator entries in the fixed secure Chrome inventory:
`canShare`, `share`, `setAppBadge`, `clearAppBadge`, `registerProtocolHandler`,
`unregisterProtocolHandler`, `getUserMedia`, `webkitGetUserMedia`,
`requestMIDIAccess`, and `requestMediaKeySystemAccess`, together with the
required constructible `DOMException` dependency and the software ClearKey
`MediaKeySystemAccess` / `MediaKeys` / `MediaKeySession` / `MediaKeyStatusMap`
surface returned by `requestMediaKeySystemAccess`.

## Boundary to implement against browser evidence

- All ten Navigator methods are `[SecureContext]` data properties on
  `Navigator.prototype`, absent in insecure HTTP and opaque `about:blank`
  contexts. `getUserMedia` and `webkitGetUserMedia` are distinct native
  function objects (`length = 3`). Arities match Chrome (`canShare` 0, `share`
  0, `setAppBadge` 0, `clearAppBadge` 0, `registerProtocolHandler` 2,
  `unregisterProtocolHandler` 2, `requestMIDIAccess` 0,
  `requestMediaKeySystemAccess` 2).
- Invalid-receiver calls follow Chrome per-method channels: synchronous
  `TypeError("Illegal invocation")` for non-Promise operations (`canShare`,
  `registerProtocolHandler`, `unregisterProtocolHandler`, `getUserMedia`,
  `webkitGetUserMedia`) and Promise rejection with
  `TypeError("Failed to execute '<name>' on '<Interface>': Illegal invocation")`
  for Promise-returning operations (`share`, `setAppBadge`, `clearAppBadge`,
  `requestMIDIAccess`, `requestMediaKeySystemAccess`, and Promise-returning EME
  methods).
- `canShare` and `share` convert `ShareData` dictionaries in lexicographical IDL
  order (`files`, `text`, `title`, `url`), validating `sequence<File>` via native
  `File` branding (rejecting `Blob` and primitives). `canShare` validates
  non-empty share payload and HTTP(S) URL resolution against the Page base URL.
  `share` converts `ShareData` first and then rejects synchronously with
  `DOMException` (`NotAllowedError`) because the offline Page has no transient
  user activation.
- `setAppBadge` converts optional `[EnforceRange] unsigned long long` in
  `[0, Number.MAX_SAFE_INTEGER]` and resolves synchronously to `undefined`;
  `clearAppBadge` ignores extra arguments and resolves synchronously to
  `undefined`. Neither modifies host OS dock/taskbar state.
- `registerProtocolHandler` and `unregisterProtocolHandler` validate arity,
  DOMString/USVString conversion order, RFC3986 scheme syntax, `web+` ASCII-alpha
  suffix rules, Blink's scheme safelist, `%s` placeholder presence, URL syntax
  after removing `%s` relative to the Page base URL, HTTP(S) / potentially
  trustworthy URL requirements, and same-origin restriction, throwing native
  `TypeError` or `DOMException` (`SecurityError` / `SyntaxError`) and returning
  `undefined` on valid inputs without OS handler registration.
- `getUserMedia` and `webkitGetUserMedia` validate arity, `MediaStreamConstraints`
  (`audio`, `video`), and non-nullable callback arguments synchronously, throw
  `TypeError` when neither audio nor video is requested, and otherwise queue a
  non-cancelable Page host task invoking the error callback (`this === undefined`)
  with `DOMException("Permission denied", "NotAllowedError")`.
- `requestMIDIAccess` converts optional `MIDIOptions` (`sysex`) synchronously and
  queues a Page host task rejecting its Promise with
  `DOMException("Permission to use Web MIDI API was not granted.", "NotAllowedError")`.
- `requestMediaKeySystemAccess` validates arity, `keySystem` DOMString, and
  `supportedConfigurations` sequence conversion in order, rejects empty
  `keySystem` or empty `supportedConfigurations` with `TypeError`, supports
  software `org.w3.clearkey` temporary-session configurations (resolving to
  native `MediaKeySystemAccess` with `getConfiguration()` and task-delivered
  `createMediaKeys()`), and rejects external CDMs or unsupported configurations
  with `DOMException("Unsupported keySystem or supportedConfigurations.", "NotSupportedError")`.
- `DOMException` is exposed globally in both secure and insecure contexts,
  inheriting from `Error.prototype`, with native `code`/`name`/`message`
  accessors, the 25 legacy error constants on constructor and prototype, single
  `NewTarget.prototype` read under `Reflect.construct`, and V8 stack capture on
  platform-created instances. Pre-existing non-BOM error paths remain unchanged.

## Observation and acceptance

All ten Navigator methods, `DOMException`, and ClearKey EME interfaces enter
existing access tracing and `watch_apis`. User dictionary getters, iterators,
primitive conversions, and callback bodies run under user attribution; internal
wrapper materialization and exception capture stay quiet. Focused offline tests
pin shapes, descriptors, receiver checks, conversion order, synchronous vs task
completion, reloads, Page isolation, and access traces against captured Chrome
fixtures before running the regression gate and full suite on macOS.
