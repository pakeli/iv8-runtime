# WIN-MON-01 local acceptance report

**Subsequent combined release verified (`2026-11-15`).** This code is included in source `40d1c1f`, which passes all 12 release lanes. See [combined release evidence](bom_release_report.md). The original local-stage measurements and artifact identities below are retained as historical evidence.

Date: `2026-11-15`. **Implemented and locally accepted; release verification
pending.** User request: fill the monitoring gaps in PRES-03 before adding more
APIs. Boundary: [window_monitoring.md](window_monitoring.md).

## Actual monitoring coverage

| Scope | Evidence |
|---|---|
| All 49 mapped Window names | Structured native get with returned value/synchronous exception completion |
| Currently supported global writes | Sweep of 32 implemented data/setter paths: 32 completed sets, exact read-back identity and zero call-only watch hits |
| All 22 new callable globals | watch_apis plus structured call, arguments, synchronous return/exception |
| Current native shell methods | Controlled sweep: 127 methods, 127 watch hits, 127 completed calls |
| Shell/global and namespace accessors | Structured get/set and safe completion; ordinary instance accessors retain existing generic tracing |

The name/path ledger is [window_monitoring_coverage.json](window_monitoring_coverage.json).
The 32 write paths come from the implemented PRES-03 descriptors; this task does
not add the browser setters or backend behavior absent from that bounded profile.
No API reference name appeared or disappeared: the same 22-object membership
probe is identical to PRES-03 (Window ordinary 128/128, handlers 125/125,
excluded interface entries still 821 missing).

The previous minimal audit could see only open/close watch hits and CSS.escape/
indexedDB.open structured calls. The new tests assert real events/hits per
entry, including reportError, NodeFilter, flags, event and the object globals;
merely running code while tracing is enabled is not acceptance evidence.

## Behavior and shape preservation

Native data properties intercept direct data reads/writes while retaining data
descriptors, exact stored function/value identity, writable/enumerable/
configurable flags, inheritance receiver behavior, defineProperty and deletion.
Real native function accessors keep accessor descriptors. The same completion
helpers record property values and synchronous exceptions without converting
them into method-call summaries. A generation-owned Window host identity is
shared by the new mapped Window callbacks; shell entries use their host identity.

No JS wrapper, Proxy, Function.prototype.toString override or M1 execution-kernel
change is introduced. Native names, lengths, source, construction behavior,
own prototypes and shared interface relationships remain intact. Original
coercions and exception identity are checked with monitoring enabled/disabled.
Scope includes filter/stop/capacity, watch-channel independence, nested call
order, load/Page isolation and break-on-hit without an attached inspector.

The pinned V8 15.0.245.19 experiment records 14 passing data-semantics checks,
first with a standalone API experiment, then the actual new helper linked with
the existing AccessTrace/AccessSummary implementation. Evidence:
`data/window-data-trace-2026-11-15/verification.json`, `product_probe.cpp` and
`product-result.json`. Its standalone trace lookup/type-summary substitutions
are explicit in the verification file. Five new Page tests additionally verify
real-product data redefinition, read-only/delete, inherited/frozen receivers and
nested watch order.

## Local acceptance and artifact identity

- Base source: `8286f3b993ea903c3a38e20e2bf00dc4e5de5197`. Final local commit/tree,
  changed-source hashes and log hashes:
  `data/window-monitoring-2026-11-15/source-receipt.json`.
- **56 new tests** in `test_presence_03_window_monitoring.py` and
  `test_window_native_data_monitoring.py`. Existing PRES-01/02/03, Watch,
  structured tracing, shape, History and named-access focused checks pass:
  **550 passed**. Full real-V8 macOS arm64/Python 3.13:
  **2547 passed, 2 skipped**.
- Logs: `data/window-monitoring-2026-11-15/cp313/focused-accepted.log` and
  `full-accepted.log`; controlled method sweep:
  `data/window-monitoring-2026-11-15/shell-method-sweep.json`.
  Supported-write sweep: `data/window-monitoring-2026-11-15/global-write-sweep.json`.
- Wheel: `iv8-0.0.1-cp313-cp313-macosx_11_0_arm64.whl`, SHA-256
  `97cdace8cc5a757d004f5ef7bbbe1ae4a5450c5e312501b566c2c5c128f485f7`.
- Native SHA-256:
  `64050ee145f2490c2a26d028b3d52d86aa7e407d7377d9611696b8bd81289961`.
- Isolated installation: `data/window-monitoring-2026-11-15/cp313/site`.
  Existing immutable PRES-03 Chrome fixtures are reused for shape/type baselines;
  this observation-plumbing change needs no new browser subsystem behavior.
- No hosted run/artifact IDs or full release-matrix results exist for this local
  batch. The shared iv8-test SDK remains BIND-01 native SHA-256
  `d29024112409b373a9a714d9dcd9bb532e22716b2164d5d9b4f773c9b94eef89`.

## Explicit observation limits

- watch_apis remains native-call-only; property reads/writes belong to structured
  tracing. Calling a user replacement JS function is not a native API call.
- A Promise-returning call is recorded as returned Promise/object. Later
  fulfillment/rejection is not written back as a synchronous call exception.
- V8 evaluates native data getters for descriptor queries; an additional get is
  expected without changing the JavaScript descriptor result.
- Read-only/frozen writes and construction attempts rejected before a native
  callback do not invent a setter/call event. Read-only-value behavior and errors
  remain unchanged.
- Data-property value assignments/redefinitions retain monitoring. Explicit
  redefinition to a user JS accessor ends that native data path.
- Arbitrary plain objects, namespace child constants/data-method references and
  returned plain values are not covered by a V8-wide hook. Declared native
  accessor paths and actual native shell calls are the scoped child operations.

Next work remains the BOM-first Navigator/subobject queue. This monitoring slice
does not implement additional API backends or deploy a new shared SDK.
