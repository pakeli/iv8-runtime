# M11-36..M11-40 — BOM completion batch

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-30`)**

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

- primitives (including BigInt), plain objects, arrays, boxed
  Boolean/Number/String/BigInt objects, `Date`, `Map`, `Set`, `ArrayBuffer`,
  TypedArrays, and `DataView`;
- TypedArray/DataView clones keep their byte offset and share the cloned
  backing buffer when the source views share a buffer.

Functions, symbols (including symbol wrappers), promises, weak collections,
RegExp/Error objects, shared buffers, proxies, DOM/host objects, and the global
object throw a JS `TypeError` (this runtime has no `DOMException`). Transfer
lists and other exotic objects are not supported.

## M11-39 — single-context BOM metadata

This runtime has exactly one top-level browsing context:

- `window.parent === window` — `[Replaceable]`; page scripts may assign it
- `window.top === window` — `[LegacyUnforgeable]`, read-only; assignment is
  ignored
- `window.frames === window` (and `window.frames.length === 0`) —
  `[Replaceable]`; page scripts may assign it
- `window.opener === null` — writable `any`; page scripts may assign it

There are no child frames, popups, or `postMessage` delivery. Because
`parent` / `frames` are replaceable and `opener` is writable (configurable
global properties), page scripts can declare their own `var parent` or
`let frames` bindings; `let top` remains a restricted declaration.

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

Verified on real V8 at commit `512117c`:

- Linux `release-linux` run `40997424174` — cp311 / cp312 / cp313 / cp314,
  each `1617 passed, 2 skipped`;
- Windows `release-windows` run `40997427191` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1617 passed, 2 skipped`.

`test/test_m11_36_40_bom_completion.py` ran in every lane (18 items). The full
suite collects 1619 items; 1617 pass and 2 are skipped. See
`docs/m11_36_40_report.md`.
