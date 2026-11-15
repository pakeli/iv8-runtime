# PRES-03 — Window ordinary-member presence completion

Authorized `2026-11-15`: the user explicitly requests simple exposure completion
before detailed behavior work and confirms **the 49 missing ordinary Window
members**, excluding the separate 821 missing global interface entries.

## Approved boundary

Expose every missing ordinary name from the fixed Chrome 157 inventory, with
the observed property/function/object/constant kind. Native functions retain
browser-derived names/arities/non-constructability; existing defining owners
and per-context prototype extension paths remain intact. Namespace and object
shells keep stable per-generation identity and release handles before teardown.
The first acceptance criterion is name presence and safe access, not complete
implementation of the named subsystem or its recursively nested interfaces.

Reuse the existing animation-frame scheduler for the prefixed operations.
Provide current window.event and bounded reportError delivery on the existing
synthetic event path. Browser-profile constants/flags and reference no-op legacy
operations can use deterministic values. Operations needing absent networking,
windows, storage engines, devices, selection/layout/graphics or other backends
fail explicitly (synchronous NotSupportedError or rejected Promise as declared),
rather than reporting external execution success.

The user explicitly approves re-exposing Window.open in this presence batch.
It remains a named, non-constructable native operation that reports unsupported
execution; this does not reinstate the previously withdrawn anonymous,
constructable callback or claim a real browsing context was created. Earlier
absent-open profile expectations are superseded for this candidate only.

## Acceptance and cadence

Capture the 49 members' real-browser observable kinds and function metadata in
an isolated loopback-only reference fixture. Test all names/types/native shapes,
basic reused scheduling/events, stable object identity and generation isolation.
Run focused regressions then one full local real-V8 suite. Preserve History,
named access and PRES-01/PRES-02 behavior. Update the presence delta and label
shell/unsupported/partial paths explicitly. No complete release matrix or shared
SDK replacement is implied; BIND-01 remains release/deployment verified until a
later grouped delivery.
