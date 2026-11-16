# XHR-01 interface and monitoring acceptance

Date: `2026-11-16`. **Implemented and locally accepted; full release verification pending.**
Boundary: [xml_http_request_shape.md](xml_http_request_shape.md).

## Delivered behavior

The Page host layer now exposes native XMLHttpRequest, XMLHttpRequestEventTarget
and XMLHttpRequestUpload interfaces with browser-derived inheritance, constants,
prototype order, descriptors, receiver checks and Function metadata. Only XHR is
publicly constructible. Surfaces are complete before first construction; native
functions retain their canonical identities. Upload is SameObject per request.
Configuration/handler storage and pre-transfer response guards use the measured
Chrome profile. Existing explicit EventTarget dispatch works on request/upload.

`send` is the user-requested native placeholder. Debug automatically prints safe
CALL argument summaries and undefined return; prod stays silent. The other
request-method placeholders let scripts reach send without introducing HTTP,
request state transitions, response content, transfer events or scheduled work.
Arguments are not coerced or serialized by these stubs. Secure-only Chrome extras
are omitted on ordinary insecure origins. The new trace filters are
`xml_http_request` and `xml_http_request_upload`; existing Window constructor and
host_object prototype attribution remain unchanged.

## Verification

- Fresh Chrome **157.0.8103.51** secure-loopback and insecure-origin captures are
  committed under `test/fixtures/xml_http_request/` with probe hashes, UTC times,
  command flags and conditions. Reference traffic stays on loopback and does not
  perform a successful XHR transfer. Offline tests read fixtures, not Chrome or HTTP.
- **62 new tests pass**: 35 interface/behavior cases and 27 native trace cases.
  The changed existing Web IDL checks plus interface cases pass **155 tests**.
- All three release workflows share the same **28-module** installed-wheel gate.
  **643 passed** on the local candidate; full suite **2933 passed, 2 skipped**.
- Independent review and isolated-wheel checks found no blocking issue in native
  identity, eager surfaces, ownership/reload, secure-origin differences, hostile
  argument safety, config coercion provenance or debug/prod output.
- V8 remains pinned to **15.0.245.19**. The existing monolith is reused and the
  native extension/wheel rebuilt for local macOS arm64 / Python 3.13.

Local wheel SHA-256:
`af21805f7d55b0e295cd8ea2bb12f8099bc70ec3d4eafd120d329e0ca17549bd`.
Build, gate, full logs and source/native hashes live under ignored
`data/xml-http-request-shape-2026-11-16/`; see `source-receipt.json`.

## Limits and release status

This is an interface/monitoring profile, not XHR networking compatibility. The
stubs keep UNSENT/status zero and do not validate/retain URLs/headers, send/cancel
HTTP or synthesize events. There is no response body/JSON/XML delivery, CORS,
ProgressEvent, FormData, Observable/EventTarget.when or JS→Python callback addition.
The inherited EventTarget base retains its older profile (own-key order differs
from Chrome and `when` is absent); this batch aligns the three XHR interfaces,
not that base interface. DOMException remains absent: response guards are Error values with measured
name/message/code; the safe trace reports Error as constructor and preserves
InvalidStateError text. Symbol/structural queries, independently installed
callbacks, other returned values and independent array/index monitoring remain
separate strict-use gaps.

This batch updates PR #32. Required CI results are recorded after push. The full
12-lane release matrix has not been rerun for this source; prior fixed-source
release/fallback records remain separate. No merge, version tag, dependency
upgrade or shared SDK installation is performed; BIND-01 remains deployed.
