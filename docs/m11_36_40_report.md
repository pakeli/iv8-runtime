# M11-36..M11-40 Delivery Report — BOM Completion Batch

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-30`)**

Branch `m11-36-40-bom-completion`, verified commit `512117c`.

## Evidence

- Linux `release-linux` run `40997424174` — cp311 / cp312 / cp313 / cp314,
  each `1617 passed, 2 skipped`.
- Windows `release-windows` run `40997427191` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1617 passed, 2 skipped`.
- `test/test_m11_36_40_bom_completion.py` ran in every lane (18 items). The
  full suite collects 1619 items; 1617 pass and 2 are skipped.

## Scope landed

- **M11-36 deterministic dialogs** — `alert()` / `confirm()` / `prompt()` /
  `print()` as no-op stubs with fixed return values; no UI, blocking, Python
  callback, or page mutation. Calls are visible through the watch APIs.
- **M11-37 performance entry timeline** — `performance.mark()` /
  `measure()` / `getEntries()` / `getEntriesByName()` / `getEntriesByType()` /
  `clearMarks()` / `clearMeasures()` over the M11-31 virtual clock. Entries are
  per load generation; unknown marks throw, and there is no
  `PerformanceObserver`, resource/navigation timing, or real clock.
- **M11-38 structuredClone** — cycle/identity-preserving clones of primitives,
  plain objects, arrays, boxed Boolean/Number/String/BigInt objects, `Date`,
  `Map`, `Set`, `ArrayBuffer`, TypedArrays, and `DataView`. Views keep their
  byte offset and share the cloned backing buffer when their sources share one.
  Functions, symbols (including wrappers), promises, weak collections,
  RegExp/Error objects, shared buffers, proxies, DOM/host objects, and the
  global object throw a JS `TypeError`. There are no transfer lists.
- **M11-39 single-context window metadata** — `parent`, `top`, `frames`, and
  `opener` for the runtime's one top-level browsing context. `parent` and
  `frames` are `[Replaceable]`, `top` is read-only/unforgeable, and `opener`
  starts `null` and is writable. There are no child frames, popups, or
  `postMessage` delivery.
- **M11-40 scheduler.postTask** — Promise-returning `scheduler.postTask()` over
  the existing timer queue, with a non-negative finite `delay` and the normal
  explicit `run_timers()` / facade/microtask pumps. Priority, `AbortSignal`,
  and automatic execution are out of scope.

`navigator.userAgentData` remains intentionally unimplemented; callers that
need a Chromium identity pass one through `Page(environment=...)`.

## Verification notes

- The first verification attempt at `84a8850` failed three tests:
  - the global `frames` binding collided with `let frames` in the M11-30
    self-rescheduling rAF test;
  - a `structuredClone` assertion encoded the wrong joined string;
  - the global `parent` binding collided with `var parent` in the
    MutationObserver detached-subtree test.
- `512117c` made `parent` / `frames` replaceable and `opener` writable to match
  browser browsing-context metadata, kept `top` unforgeable, refreshed the
  affected tests, and fixed the assertion. The same commit tightened
  `structuredClone` view identity/offset handling and added boxed-primitive
  clones.
- Local `zig c++ -fsyntax-only` passed for `page_state.cpp`; Python tests passed
  `py_compile`; 1619 tests collect locally.
- The `Uncaught Error: boom` stderr line after the pytest summary pre-dates this
  batch and does not affect the exit code.

## Outcome

M11-36..M11-40 are verified and merged to fork `master`. The deterministic BOM
surface now includes dialogs, the performance entry timeline,
`structuredClone`, single-context window metadata, and `scheduler.postTask`.
The standing exclusions remain: navigation/history, networking/workers,
rendering/layout, storage events/IndexedDB, `crypto.subtle`, UA-CH, and real
timing/background execution. See `docs/m11_36_40_bom_completion.md`.
