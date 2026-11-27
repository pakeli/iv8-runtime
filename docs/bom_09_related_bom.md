# BOM-09 boundary: Performance, Console extensions, and Scheduler

Date: `2026-11-27`

Status: Implemented locally on `codex/bom-09-related-bom` (cross-platform release verification pending with `BOM-10`; latest verified milestone remains `BOM-08`).

## Approved scope

`BOM-09` completes 17 of the final 19 missing members across the related BOM
subobjects in `docs/browser_api_inventory.json`:

1. **`performance` (`9/22 -> 22/22`)**:
   - Replaces the earlier `M11-31` / `M11-37` flat plain-object `performance`
     shim with a native `Performance` `EventTargetHost` instance
     (`performance instanceof Performance === true`,
     `performance instanceof EventTarget === true`, `Reflect.ownKeys(performance)`
     empty), inheriting `addEventListener`, `removeEventListener`,
     `dispatchEvent`, and `when` from `EventTarget.prototype`.
   - Exposes all 19 `Performance.prototype` members + `Symbol.toStringTag` in
     Chrome 158's exact key order: `timeOrigin`, `onresourcetimingbufferfull`,
     `clearMarks`, `clearMeasures`, `clearResourceTimings`, `getEntries`,
     `getEntriesByName`, `getEntriesByType`, `mark`, `measure`,
     `setResourceTimingBufferSize`, `toJSON`, `now`, `constructor`, `timing`,
     `navigation`, `memory`, `eventCounts`, `interactionCount`.
   - Adds global interfaces `Performance`, `PerformanceEntry`,
     `PerformanceMark` (constructible, inherits `PerformanceEntry`),
     `PerformanceMeasure` (inherits `PerformanceEntry`),
     `PerformanceNavigation` (`TYPE_NAVIGATE`, `TYPE_RELOAD`,
     `TYPE_BACK_FORWARD`, `TYPE_RESERVED`, `type`, `redirectCount`, `toJSON`),
     `PerformanceTiming` (21 deterministic offline timing accessors + `toJSON`),
     and `EventCounts` (read-only Maplike over Chrome's 36 event type keys
     initialized to `0`, with `[Symbol.iterator] === EventCounts.prototype.entries`).
   - `MemoryInfo` is NOT exposed as a global constructor on `window`;
     `performance.memory` and `console.memory` return fresh instances per read
     inheriting a shared private `MemoryInfo` prototype (`totalJSHeapSize`,
     `usedJSHeapSize`, `jsHeapSizeLimit`, `Symbol.toStringTag = "MemoryInfo"`)
     with deterministic offline heap constants (never host RAM / live V8 heap).
2. **`console` (`22/25 -> 25/25`)**:
   - Adds `context(name)` (`length: 1`), `createTask(name)` (`length: 0`), and
     the `memory` accessor (`get`/`set` anonymous native functions) in Chrome's
     exact own-key order alongside `Symbol.toStringTag = "console"`.
   - `console.createTask(name)` requires a non-empty string (`Error("First
     argument must be a non-empty string.")`) and returns a task object whose
     own `run(fn)` validates `typeof fn === "function"` (`Error("First argument
     must be a function.")`) and invokes `fn()` with zero arguments.
3. **`scheduler` (`1/2 -> 2/2`)**:
   - Replaces the earlier `M11-40` JS bootstrap `scheduler.postTask` shim with
     native `Scheduler`, `TaskController` (inherits `AbortController`),
     `TaskSignal` (inherits `AbortSignal`, with static `TaskSignal.any`), and
     `TaskPriorityChangeEvent` (inherits `Event`).
   - `Scheduler.prototype.postTask(callback, options)` is a WebIDL
     Promise-returning method (argument/option validation errors reject the
     returned Promise rather than throwing synchronously, superseding `M11-40`'s
     synchronous throw shim) supporting `delay`, `priority` (`"user-blocking"`,
     `"user-visible"`, `"background"`), and `signal` (`AbortSignal` /
     `TaskSignal` pre-abort and queued cancellation).
   - `Scheduler.prototype.yield()` returns a Promise resolving to `undefined` on
     the next deterministic task turn.

## Explicit non-goals

- No real network resource timing entries, real wall-clock timestamps, or host
  process memory inspection (`performance.now()` continues to read the
  deterministic Page virtual clock, and `performance.timing` / `memory` report
  deterministic profile constants).
- `crypto.subtle` and `navigator.storage.getDirectory` remain in `BOM-10`.
