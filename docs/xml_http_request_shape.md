# XHR-01 — native interface shape and monitoring placeholders

Authorized `2026-11-16`. The user selects **interface shape and monitoring only**
and explicitly requests a `send()` placeholder that prints its parameters.
This stage changes the Page host layer, not the M1 kernel or HTTP transport.

## Approved surface

- Existing native `XMLHttpRequest` becomes constructible to a real native host.
  `XMLHttpRequestEventTarget` and `XMLHttpRequestUpload` have browser-derived
  native interface objects but remain illegal public constructors. Both XHR
  and Upload inherit XMLHttpRequestEventTarget → EventTarget.
- Provide the browser's state constants on constructor/prototype; own native
  methods, IDL getters/setters, prototype ownership/descriptors/key order,
  receiver rules and native Function shape follow the captured Chrome profile.
  A fresh instance has no fabricated own browser fields. `upload` is SameObject
  for one XHR and a distinct native EventTarget for each XHR.
- XHR and Upload expose seven shared event-handler attributes; only XHR owns
  `onreadystatechange`. Object-valued handler assignments are retained exactly,
  primitive assignments clear to null, and only callable handlers are invoked
  during existing explicit user EventTarget dispatch. No automatic XHR events.
- Local configuration covers `timeout` unsigned-long conversion,
  `withCredentials` boolean conversion, and recognized `responseType` values.
  Initial response/state/status values and responseText/responseXML type guards
  follow browser measurements. No response content, document parsing or transfer.
- Seven usual XHR native methods are exposed. In the existing secure-context
  profile also expose Chrome's native `setAttributionReporting` and
  `setPrivateToken`; omit them on the captured insecure-origin profile.

## Explicit placeholder behavior

`open`, `setRequestHeader`, `send`, `abort`, `overrideMimeType` and the two
secure-context methods return undefined after receiver/basic required-argument
validation. `getResponseHeader` returns null and `getAllResponseHeaders` returns
the empty string. Request arguments remain untouched: no implicit URL/header/body
coercion or serialization. Unlike a real browser these stubs do not transition
readyState, validate/retain request headers, resolve URLs, send/cancel anything,
schedule work or synthesize success/error/progress/timeout events. State stays
UNSENT, status stays zero, and response values remain their pre-transfer baseline.

In debug mode the existing native CALL trace prints `send(...)` argument
summaries and returned undefined automatically. Prod stays silent. Logging uses
the existing safe summaries (type-only objects and complete strings since the
`2026-11-16` full-value update), not arbitrary object
JSON or user getters; it does not add a Python callback/console side channel.
Trace filters add `xml_http_request` and `xml_http_request_upload`.
Manual tracing/watches, cached methods, prototype references, property writes,
brand errors and caller-origin filtering use the same native binding paths.

## Reference and verification

Primary specification: [WHATWG XMLHttpRequest](https://xhr.spec.whatwg.org/).
Shape/initial/configuration behavior is measured on Chrome 157.0.8103.51 with a
fresh headless profile and loopback-only secure/insecure reference documents;
fixtures/probe/conditions live in `test/fixtures/xml_http_request/`. Only invalid
UNSENT send cases were captured; no real XHR transport traffic was performed.
The placeholder divergences above are intentional and must not be presented as
XHR state-machine or networking compatibility. The runtime still has no
DOMException interface: local response guards use an Error carrying the measured
`name`, message and code. Existing non-executing trace summaries therefore report
the Error constructor plus the InvalidStateError text; full DOMException
prototype/constructor fidelity is deferred.

Focused offline acceptance covers complete selected native shape before first
construction, constants/flags/defaults/handlers, local setter behavior and user
coercions, stub arguments/results/no-events, real monitoring entries, identities,
invalid brands and stop/filter/capacity/reload/disposal/Page isolation. ProgressEvent,
FormData/Request/Response, response bytes/JSON/XML, real HTTP/CORS/cookies and
JS→Python callbacks remain separate. Current results/source/binary/release status
are recorded in `docs/xml_http_request_shape_report.md`.
