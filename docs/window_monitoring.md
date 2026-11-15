# WIN-MON-01 — monitor the PRES-03 Window additions

Authorized `2026-11-15`: fill the monitoring gaps found after the 49 ordinary
Window names were exposed. This changes observation plumbing, not the declared
offline execution capabilities or the M1 JSContext kernel.

## Required coverage

- All 22 new callable globals, including reportError and NodeFilter, enter
  watch_apis and the structured Window call channel, with safe argument/return
  summaries and synchronous exception completion.
- All 49 top-level names have structured native get/get_method coverage. Real
  accessors and their supported setters report get/set completion. Native data
  callbacks retain data descriptors and exact stored values/function identities.
- Shell methods enter both channels using paths such as CSS.escape and
  indexedDB.open. Shell global accessors and namespace accessors enter structured
  property tracing; ordinary HostObject instance properties keep their existing
  generic tracing without duplicate records.
- Preserve observable types, descriptors, identities, function names/arities/
  native source/constructability, shared prototypes, return values, coercions,
  errors, generation cleanup and monitor enable/filter/stop/capacity behavior.

## Observation boundaries

watch_apis remains a call-only channel; property reads do not become watch hits.
Promise-returning operations complete synchronously with the returned Promise;
this does not add Promise-settlement tracing. Read-only assignments rejected
before a native setter and construction rejected by the engine before callback
entry do not produce invented native call/set events.

V8 evaluates native data getters while producing property descriptors, so such
queries can also produce a native get event while retaining the same data
descriptor. Explicit replacement with a user-defined JS accessor ends the native
property path. This is not a V8-wide hook for arbitrary JS objects/data properties
or every nested constant/method-reference read; native shell method calls and
declared accessor paths are the scoped child operations.

## Acceptance

Tests must assert actual events and watch hits, not merely run code with tracing
enabled. Reuse the immutable PRES-03 Chrome shape fixtures and add focused
49-name/call/property, on/off transparency, lifecycle and negative-control cases.
Then run one full local real-V8 suite. Record binary/source/log identity and keep
local acceptance distinct from release verification. Do not automatically run a
release matrix or replace the shared BIND-01 SDK.
