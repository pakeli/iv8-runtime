# BOM-09 implementation & local validation report

Date: `2026-11-27`

**Status: Implemented, Codex-reviewed, and locally validated on macOS arm64.**

## Scope delivered

- Completed 17 missing members across 3 related non-DOM BOM objects in
  `docs/browser_api_inventory.json`, raising each to **100%**:
  - `performance`: **22/22** (`9/22 -> 22/22`)
  - `console`: **25/25** (`22/25 -> 25/25`)
  - `scheduler`: **2/2** (`1/2 -> 2/2`)
- Upgraded `performance` (`src/core/browser/web/performance.{h,cpp}`) to a
  WebIDL `Performance` `EventTarget` host (`Performance -> EventTarget`) plus
  native `PerformanceEntry`, `PerformanceMark`, `PerformanceMeasure`,
  `PerformanceNavigation`, `PerformanceTiming`, `EventCounts`, and private
  `MemoryInfo`:
  - `Performance.prototype` exposes all 18 own string members plus
    `Symbol.toStringTag` in Chrome 158's exact key order, inheriting
    `addEventListener`, `dispatchEvent`, `removeEventListener`, and `when` from
    `EventTarget.prototype`.
  - `PerformanceMark` (`length = 1`) supports `new PerformanceMark(name, options)`
    (structured-cloned `detail` stored in a private JS wrapper slot without
    entering the `performance` timeline) and `performance.mark(name, options)`.
    `PerformanceMeasure` supports both legacy mark-name strings (including
    `PerformanceTiming` attribute resolution) and `PerformanceMeasureOptions`
    (`{detail, duration, end, start}`).
  - `PerformanceTiming`, `PerformanceNavigation`, `EventCounts` (36 deterministic
    event-type keys mapped to `0`, `EventCounts Iterator` prototype pre-created
    before user scripts run), and private `MemoryInfo` (`performance.memory` and
    `console.memory` returning fresh objects sharing one non-global prototype)
    expose deterministic constants with zero host wall-clock or RAM leakage.
- Upgraded `console` (`src/core/browser/bom/console.h`) to **25/25**:
  - Ordered all 24 `console` methods, `memory` accessor (anonymous native `get`
    and no-op `set`), and `Symbol.toStringTag = "console"` to match Chrome 158.
  - Added `console.context(name)` (22 contextual logging methods) and
    `console.createTask(name)` (`task.run(fn)` with receiver brand and constructor
    guards).
- Replaced the legacy JS `kSchedulerBootstrap` with native WebIDL `Scheduler`,
  `TaskController`, `TaskSignal`, and `TaskPriorityChangeEvent`
  (`src/core/browser/web/scheduler.{h,cpp}`):
  - `scheduler.postTask(callback, options)` and `scheduler.yield()` participate
    in priority-ranked scheduling (`user-blocking` / `yield`, `user-visible`,
    `background`) on `ContextState`'s deterministic timer queue, supporting
    dynamic `TaskSignal` priority promotion and immediate `AbortSignal` task
    cancellation.
  - `TaskController` (`AbortController` subclass) and `TaskSignal`
    (`AbortSignal` subclass with `TaskSignal.any(signals, init)`) propagate
    priority changes transitively (`A -> B -> C`) and enforce Chrome's
    `NotAllowedError` recursion guard.
- Fixed two pre-existing kernel/binding issues uncovered during review:
  - `to_python_deep` (`src/core/value_converter.cpp`) now extracts the V8 object
    graph with the Python GIL released before materializing `pybind11::object`s
    under the GIL, preventing slow JS getters in `eval(..., to_py=True)` or
    `JSValue.to_py()` from blocking other Python threads.
  - Keyed `install_host_interface` method lengths by WebIDL interface name
    (`src/core/host_object.cpp`, `src/core/web_method.cpp`) and reversed
    non-IDL `ObjectTemplate::SetNativeDataProperty` registration so own keys
    materialize in forward declaration order without cross-API `length`
    collisions (`console.error.length === 0` vs `Subscriber.prototype.error.length === 1`).

## Verification & review fixes

- Browser references: `test/fixtures/bom_09/{chrome,chrome-insecure,chrome-opaque}.json`
  (Google Chrome `158.0.8127.88`, fresh headless profiles).
- Verified and resolved all 7 review items (plus `IteratorStep` `done`-before-`value`
  ordering in `TaskSignal.any` and `AbortSignal.any`) with dedicated regressions in
  `test/test_bom_09_related_bom.py`:
  1. `[P1]` Released Python GIL during `to_py=True` / `JSValue.to_py()` JS
     property reads (`src/core/value_converter.cpp`).
  2. `[P1]` Scoped `v8::TryCatch` tightly in `clone_performance_detail` so
     `DOMException("DataCloneError")` propagates on `performance.mark`,
     `new PerformanceMark`, and `performance.measure`.
  3. `[P1]` Stored `PerformanceMark`/`PerformanceMeasure` `detail` on the JS
     wrapper's private slot with zero C++ `v8::Global` retainers on entry hosts,
     allowing V8 GC to reclaim `detail` buffers after `clearMarks()` /
     `clearMeasures()`.
  4. `[P2]` Added `priority_rank` scheduling and dynamic `TaskSignal` priority
     updates to `ContextState` (`user-blocking` / `yield` -> `user-visible` ->
     `background`).
  5. `[P2]` Propagated `TaskSignal` priority changes transitively across chained
     `TaskSignal.any` dependents while keeping the `NotAllowedError` recursion
     guard active across the full chain.
  6. `[P2]` Pre-initialized the `EventCounts Iterator` prototype during context
     bootstrap before user scripts execute (`0` calls to user-replaced
     `Array.prototype[Symbol.iterator]`).
  7. `[P2]` Separated WebIDL interface method lengths from `console` and fixed
     non-IDL `HostObject` own-key registration order.
  8. `[P2]` Enforced WebIDL `[EnforceRange] unsigned long long` `[0, 2^53 - 1]`
     bounds on `SchedulerPostTaskOptions.delay` before integer casting,
     preventing UB at `2 ** 64` and matching Chrome 158's `TypeError`.
  9. `[P2]` Implemented 6-level continuation priority ranking plus V8
     `SetContinuationPreservedEmbedderDataV2` task attribution so
     `scheduler.yield()` continuations inherit active `postTask` priority,
     dynamic `TaskSignal` updates, and `AbortSignal` state across `async`/`await`
     microtasks while defaulting to `user-visible` continuation outside `postTask`
     (and resetting across `setTimeout`).
  10. `[P2]` Replaced generation-adopted `PostTaskEntryHost` objects with
      `SchedulerHost`-owned `PendingSchedulerTask` entries that reclaim native
      allocations and unregister `AbortSignal` abort callbacks immediately upon
      task settlement or cancellation.
  11. `[P2]` Distinguished internal structured-clone failures via an explicit
      `out_data_clone_error` flag in `structured_clone_value` so user-thrown
      `detail` getter errors (including messages containing `"structuredClone:"`)
      propagate with exact error identity.
- Migrated superseded pre-`BOM-09` flat-object / JS-bootstrap expectations in
  `test_m11_31_virtual_performance_clock.py`, `test_m11_36_40_bom_completion.py`,
  `test_access_trace_bom_calls.py`, `test_access_trace_bom_references.py`,
  `test_access_trace_origins.py`, `test_access_trace_script_apis.py`, and
  `test_access_trace_values.py`.
- Focused suite: `test/test_bom_09_related_bom.py` — **8 passed**;
  `tools/test_audit_move.py` — **43 passed**.
- Full macOS arm64 suite (Python 3.13 wheel): **3491 passed, 2 skipped**.
