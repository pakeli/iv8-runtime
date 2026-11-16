# User-origin tracing acceptance report

Date: `2026-11-16`. **Implemented and locally accepted; release verification pending.**

Boundary: [access_trace_origins.md](access_trace_origins.md). The previous
release-verified source remains `aa72e50` in [PR #32](https://github.com/pakeli/iv8-runtime/pull/32)
until this repair completes its release lanes. Shared deployment remains BIND-01.

## Implemented behavior

An empty debug load is silent. `page.eval("window.location.href")` prints only
Window.window, Window.location and Location.href gets. Explicit user Event
construction is recorded, while synthetic construction and event field maintenance
are excluded before event sequence/identity/capacity accounting. Manual/raw traces
use the same native gate. Python mode parameters and event schemas are unchanged.

Page-generation origin scopes distinguish native implementation work from user
callbacks and input operations. User getters, setters, conversions, iterators,
listeners/handlers, timers, Promise/microtask/frame/idle callbacks and native
observer/FileReader/message deliveries preserve user API accesses. Runtime
bootstrap scripts use engine script IDs; their begin/end tokens restore origin
also when a call is filtered, capacity is full, or an exception is thrown.
Stack inspection is active only while tracing and occurs before the trace lock.

A pinned-V8 standalone experiment established that bound native callbacks have
no extra JS frame, so explicit user boundaries supplement stack-based reentry.
CSS Proxy/native-data forwarding retains visible method references, and descriptor
classification preserves custom native-bound accessors/method replacements without
running user Proxy traps for diagnostics. Normal CSS backend calls stay internal.
Independent watch channels retain their original contract.

## Actual local acceptance

- **40 new origin tests** plus the revised empty-load mode assertion. The first
  baseline run (18 origin cases plus the empty-load assertion) failed **10** and
  passed **9**, demonstrating the old internal-noise/capacity behavior.
- Final **24-module / 522-test installed-wheel release gate passes**.
- Full real-V8 macOS arm64/Python 3.13: **2812 passed, 2 skipped**.
- Existing descriptor/forwarding tests remove only internal duplicate event
  expectations; direct reads/writes, query events, original errors, native shapes
  and callback side effects retain positive assertions.
- The full suite also covers native binding metadata, History/named access,
  conversion/disposal/isolation/threading and monitoring-on/off behavior.

The tests run against the extracted final wheel outside the source root. Local
wheel SHA-256: `e80bab48427f143bc84abbaa92377d0143f8327498b78146ff51bcf15323b547`.
Build, gate, full logs and per-source/native hashes are under ignored
`data/access-trace-origins-2026-11-16/`; see `source-receipt.json`.

## Release and limits

The fix is being added to the existing PR #32 and its V8-linked builds. Exact
source/tree/run/artifact IDs and final platform counts will be recorded after
completion; local acceptance alone is not release verification. V8 remains pinned
to 15.0.245.19; no kernel/browser surface expansion, SDK install or version tag
is part of the repair. The separate prototype/invalid-receiver/symbol/query/
returned-object coverage gaps remain open: user-only logs do not imply complete
browser API coverage.
