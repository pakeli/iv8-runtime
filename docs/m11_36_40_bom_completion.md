# M11-36..M11-40 — BOM completion batch

Status: **IN PROGRESS — implementation and real-V8 verification pending
(`2026-10-30`)**

This batch finishes the deterministic BOM surfaces that do not need a real
window, network, layout engine, or host callback bridge. `navigator.userAgentData`
is intentionally **not** implemented in this version; callers that need a
Chromium identity can pass one through `Page(environment=...)`.

## M11-36 — deterministic dialogs

Bare globals:

- `alert(message?)` — no-op, returns `undefined`;
- `confirm(message?)` — returns `false`;
- `prompt(message?, default?)` — returns `null`;
- `print()` — no-op, returns `undefined`.

They never block, never render UI, and never mutate page state. Calls are
recordable through `watch_apis(["window.alert"])`, etc.

## M11-37 — performance entry timeline

Adds to the existing virtual `performance` object:

- `performance.mark(name)` — adds a mark at the current virtual time; returns a
  read-only entry object (`name` / `entryType` / `startTime` / `duration`);
- `performance.measure(name, startMark?, endMark?)` — adds a measure; omitted
  start defaults to `0`, omitted end defaults to the current virtual time;
  unknown mark names throw a JS `Error`;
- `performance.getEntries()` / `getEntriesByName(name)` /
  `getEntriesByType(type)`;
- `performance.clearMarks(name?)` / `clearMeasures(name?)`.

Entries are per generation (reset by `load()`). There is no `PerformanceObserver`,
resource/navigation timing, real clock, or `performance.timeline`.

## M11-38 — structuredClone

Global `structuredClone(value)` with cycle/identity preservation for:

- primitives (including BigInt), plain objects, arrays, `Date`, `Map`, `Set`,
  `ArrayBuffer`, TypedArrays, and `DataView`.

Functions, symbols, promises, weak collections, RegExp/Error objects, shared
buffers, proxies, DOM/host objects, and the global object throw a JS `TypeError`
(this runtime has no `DOMException`). Transfer lists and exotic objects are not
supported.

## M11-39 — single-context BOM metadata

This runtime has exactly one top-level browsing context:

- `window.parent === window`
- `window.top === window`
- `window.frames === window` (and `window.frames.length === 0`)
- `window.opener === null`

All are read-only. There are no child frames, popups, or `postMessage` delivery.

## M11-40 — scheduler.postTask

`window.scheduler.postTask(callback, options?)` returns a `Promise` and schedules
the callback on the existing timer queue:

- `options.delay` (non-negative finite milliseconds) is supported via the
  virtual timer queue;
- the callback result resolves the Promise; a throw rejects it;
- an invalid callback or delay throws a JS `TypeError`;
- there is no priority queue, `AbortSignal`, automatic loop, or real-time
  sleeping. `scheduler` and `scheduler.postTask` are read-only.

## Tests

`test/test_m11_36_40_bom_completion.py` covers all five slices: dialog return
values/watch hits, performance marks/measures/queries/clears/reset, structured
clone primitives/objects/cycles/Map/Set/Date/buffers and rejection paths,
single-context window metadata, and scheduler success/delay/rejection/validation.

Historical guards are refreshed: `test/test_idle_callbacks.py` no longer expects
`scheduler` to be absent, and `test/test_m11_31_virtual_performance_clock.py`
now expects the performance timeline methods.

## Non-goals

- `navigator.userAgentData` / UA-CH;
- `requestIdleCallback(options.timeout)` firing semantics;
- real `alert` / `confirm` / `prompt` UI or Python callbacks;
- `PerformanceObserver`, resource/navigation timing, real clocks;
- history/location navigation, fetch/XHR/WebSocket, storage events, IndexedDB;
- scroll/layout/matchMedia, workers, clipboard/geolocation/permissions;
- Python-side BOM APIs or a host callback bridge.

## Verification

Real-V8 Linux + Windows CI will run after implementation. This section will be
replaced with the run IDs and per-lane pytest summaries once green.
