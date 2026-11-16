# XMLHttpRequest Chrome reference

Captured on **2026-11-16** using **Google Chrome 157.0.8103.51**, macOS arm64,
in a fresh temporary headless profile. Each JSON records the actual UTC capture
time, browser/OS, command-line flags, origin, probe SHA-256 and fixture request
list. Temporary profile paths are normalized to `<temporary-profile>`.

- [probe.js](probe.js) / [chrome.json](chrome.json) capture the three constructors,
  constructor and instance prototype chains, all own prototype descriptors,
  native function metadata, default field/handler values, upload identity,
  receiver brands, synthetic upload dispatch and local validation errors.
- [extra_probe.js](extra_probe.js) / [chrome_additional.json](chrome_additional.json)
  capture responseType/response getter guards, Uint32 timeout conversion, handler
  value retention, required-argument errors and coercion before state checks.

The primary origin is HTTP `127.0.0.1`, which Chrome treats as a secure context.
`chrome.json.additional_profiles.insecure` is a separate fresh capture of the
same probe at `xhr-fixture.invalid`, with DNS mapped to `127.0.0.1` and all other
DNS blocked. That origin reports `isSecureContext: false`. Background networking
is disabled; the recorded HTTP requests are only the loopback fixture document
and, if requested by Chrome, its empty favicon. No business site is contacted.

No successful XHR transport is attempted: `open()` uses a never-requested local
`/unused` URL; `send()` is called only on a fresh UNSENT request and throws
InvalidStateError. There are no canned XHR responses or transport event-order
fixtures. The upload event test uses explicit synthetic `Event` dispatch.

## Scope of comparison

This reference supports interface shape, native binding metadata, initial
values and receiver/handler semantics. It does not require a shape-only runtime
stub to implement HTTP, response delivery, configured responses or the full XHR
lifecycle. The captured local `open`/`send`/header/MIME errors and coercion order
are browser evidence; compare those behaviors only when the runtime contract
approves them, and keep deliberate no-transport differences explicit.

`setAttributionReporting` and `setPrivateToken` are present as native,
non-constructable, length-one methods in the secure capture. Both are absent
from the insecure capture. They are additional Chrome entries beyond the seven
usual request methods and must not be assumed present for every origin.

Handler ownership is observable: `onreadystatechange` belongs to
`XMLHttpRequest.prototype`; `onloadstart`, `onprogress`, `onabort`, `onerror`,
`onload`, `ontimeout` and `onloadend` belong to
`XMLHttpRequestEventTarget.prototype` and are shared by request/upload instances.
Assigning a non-object number/string/undefined clears a handler to null;
assigning `{}` or `[]` retains that exact object. A store restricted to Function
values would lose this getter-visible browser behavior. Synthetic handler order
in this capture reflects earlier handler-slot registration; it is not a blanket
rule that handlers always run before ordinary listeners.

All three constructors are native, length zero, have an own `prototype` and
support the constructor internal method. Only `new XMLHttpRequest()` succeeds;
the EventTarget/Upload interface constructors throw Illegal constructor.
`XMLHttpRequest.prototype` inherits from
`XMLHttpRequestEventTarget.prototype`, which inherits from `EventTarget.prototype`;
Upload inherits from that same intermediate prototype. `xhr.upload` has stable
identity. The five ready-state constants are readonly, enumerable,
non-configurable own data on both the XHR constructor and its prototype.

## Own-key order

The JSON object insertion order in each `prototypeSurface` preserves
`Reflect.ownKeys`; do not sort it when comparing or regenerating. Constructor
`function.ownKeys` and the instance/prototype-chain arrays are also ordered.
The secure prototype orders are:

```json
{
  "XMLHttpRequest": [
    "onreadystatechange",
    "readyState",
    "timeout",
    "withCredentials",
    "upload",
    "responseURL",
    "status",
    "statusText",
    "responseType",
    "response",
    "responseText",
    "UNSENT",
    "OPENED",
    "HEADERS_RECEIVED",
    "LOADING",
    "DONE",
    "abort",
    "getAllResponseHeaders",
    "getResponseHeader",
    "open",
    "overrideMimeType",
    "send",
    "setRequestHeader",
    "constructor",
    "responseXML",
    "setAttributionReporting",
    "setPrivateToken",
    "Symbol(Symbol.toStringTag)"
  ],
  "XMLHttpRequestEventTarget": [
    "onloadstart",
    "onprogress",
    "onabort",
    "onerror",
    "onload",
    "ontimeout",
    "onloadend",
    "constructor",
    "Symbol(Symbol.toStringTag)"
  ],
  "XMLHttpRequestUpload": [
    "constructor",
    "Symbol(Symbol.toStringTag)"
  ]
}
```

For the insecure XHR prototype, omit only `setAttributionReporting` and
`setPrivateToken`; all remaining keys keep the same order. Native methods are
writable/enumerable/configurable data properties, have no own `prototype`, and
are non-constructable; accessors and their source/name/length are recorded in
full. Interface constructor `prototype` properties are readonly, non-enumerable
and non-configurable. The exact per-member records remain authoritative.
