# BOM-01 — offline Navigator capabilities and BOM events

Selected on `2026-11-17` under the user's BOM-first direction. Further
`Symbol.iterator` monitoring is deferred. This Page-host slice follows
`docs/bom_coverage_plan.md`; it does not change the M1 kernel.

## Selected surface

- `navigator.getGamepads()` reports the reference browser's empty-device
  snapshot for a single active document with no attached gamepad. Each call
  returns a fresh array; no device polling, trusted input or automatic gamepad
  events are implemented.
- `navigator.scheduling` is a stable native `Scheduling` instance.
  `isInputPending(options)` performs the browser-observed optional dictionary
  conversion and reports false for this runtime's absence of trusted input.
  Existing JavaScript tasks are not treated as native user input.
- `navigator.connection`, `screen.orientation` and `visualViewport` expose
  their native shared interface prototypes and EventTarget inheritance.
  Existing metadata remains configuration-backed. Synthetic listener/handler
  registration and dispatch reuse the current EventTarget implementation;
  connection/orientation use `onchange`, and viewport adds `onscrollend`.
  No automatic device, resize, network or orientation events are generated.
- `screen.orientation.lock(type)` validates its required enum argument and
  rejects with `NotSupportedError` because the Page has no orientation-lock
  backend. `unlock()` returns undefined without changing configured dimensions.
  Browser-observed Promise and invalid-receiver channels must be preserved.
  Unsupported-operation errors retain the existing named-Error profile; full
  DOMException branding is not introduced by this batch.

All selected functions/getters/setters use native bindings. Names, arities,
constructability, prototype ownership, descriptors and receiver checks derive
from the captured Chrome reference. Existing method/handler identities and
native source rendering follow the shared interface registry. Nested objects
are cached within a generation and listener/handler handles are released before
isolate disposal, including during reload.

The reused EventTarget dispatcher supports function listeners and retains its
existing handler-after-listener-snapshot ordering. Listener objects with
handleEvent and full registration-order handler interleaving remain separate
EventTarget fidelity work. The three selected BOM dispatchEvent entry points
require an actual native Event; inherited objects and user Proxies are rejected
without invoking user properties or traps.

## Monitoring and acceptance

Existing access tracing and call-only watch channels cover these new browser
entries. Internal installation/dispatch remains quiet; JavaScript reentry stays
observable. Add expected-event assertions for valid calls, getter/setter paths
and synchronous failures. Promise settlement remains outside access tracing.

Commit offline browser evidence with browser version, origin, execution flags,
probe hash and capture conditions. Run focused behavior/shape/monitoring tests,
protect History/named/native-binding and prior BOM/monitoring regressions, then
run one full suite against a newly built installed V8 wheel. Record exact
counts and hashes in `docs/bom_01_capabilities_report.md`.

The remaining Navigator device/service API families and Observable-dependent
`when` methods remain in the BOM queue. Cross-platform release validation and
shared SDK deployment are separate from local implementation acceptance.

The subsequent `2026-11-17` user request authorizes updating PR #32 and
V8-linked release builds for this batch. Release evidence is recorded in
`docs/bom_01_release_report.md`; merge/tag/shared installation are separate.
