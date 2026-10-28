# M11-4 — `queueMicrotask` (manual microtask queue)

M11-4 is the fourth implementation slice under the compatibility-first M11 line.
It adds the standard `queueMicrotask(callback)` global and deliberately does **not**
add a new Python pump: the existing `Page.run_jobs()` is the only trigger.

The runtime already has a four-pump manual model (`run_timers`, `run_jobs`,
`run_animation_frame`, `run_idle_callbacks`). M11-4 does not add a fifth queue:
`queueMicrotask` feeds the existing V8 microtask queue that `run_jobs()` already
drains.

## Public API

- **JS: `queueMicrotask(callback)`** — enqueues `callback` on the current context's
  V8 microtask queue and returns `undefined`.
- **JS: no `cancelMicrotask`** — microtasks are not cancellable in the standard API.
- **Python: no new method.** `Page.run_jobs()` drains the queue.

`queueMicrotask` is a bare global function, installed per generation by
`install_page`; `window.queueMicrotask === queueMicrotask` holds because `window`
is the intrinsic global object.

## Callback validation

The first argument must be callable. Following the standard rather than the
permissive rAF/idle host-call style:

- `queueMicrotask()` / `queueMicrotask(1)` / `queueMicrotask({})` /
  `queueMicrotask(null)` / `queueMicrotask(undefined)` → JavaScript `TypeError`
  **before anything is enqueued**;
- a function is enqueued and the call returns `undefined`.

The error message is intentionally not part of the contract; tests assert the
JavaScript error name (`TypeError`), not the wording.

## Execution model

- Nothing runs at enqueue time. The callback runs only when Python calls
  `Page.run_jobs()`.
- Callbacks run in FIFO order (registration order) within one checkpoint.
- `queueMicrotask` interleaves with `Promise.then` reactions in the same FIFO
  microtask queue, in the order each was enqueued.
- A microtask queued **from inside** a microtask appends to the same checkpoint
  and runs after the already-queued microtasks.
- A callback runs at most once; there is no repeating form.
- Each callback receives **no arguments** (unlike rAF, which also has no clock
  timestamp).
- An exception thrown by a microtask is swallowed by V8's explicit microtask
  checkpoint (`PerformMicrotaskCheckpoint`), matching the existing timer/frame/idle
  error-isolation policy; the page stays usable and remaining microtasks continue.
- The queue belongs to the current generation. `load()` tears down that
  `ContextState` and its pending microtasks are discarded, never resurrected.
- `run_jobs()` after `dispose()` raises `JSContextDisposedError`.

`ContextState` already configures `v8::MicrotasksPolicy::kExplicit`; M11-4 adds
only the enqueue entry point, not a new scheduler.

## Independence from the other pumps

`queueMicrotask` uses the microtask queue, which is separate from the M2-4 timer
registry, the M11-2 animation-frame queue, and the M11-3 idle queue:

- `run_jobs()` drains microtasks and does not fire timers, frames, or idle
  callbacks;
- `run_timers()` / `run_animation_frame()` / `run_idle_callbacks()` do not drain
  microtasks;
- there is no cross-cancellation surface (there is no `cancelMicrotask`).

The runtime still has exactly **four** manual pumps.

## Relationship to `Promise`

V8 already enqueues `Promise.then` reactions on the same microtask queue.
`queueMicrotask` is a direct, Promise-free way to append to that queue; it does
not change Promise semantics, and `run_jobs()` drains both kinds of work.

## Scope / non-goals

M11-4 deliberately does **not** add:

- a new Python API or a fifth pump;
- an automatic event loop, background thread, or automatic microtask checkpoint;
- a real clock, `performance.now()`, or task deadlines;
- `MutationObserver` (landed separately in M11-5 — see
  `docs/m11_5_mutation_observer.md`);
- `scheduler.postTask` / `scheduler.yield` / task priorities;
- `queueMicrotask` cancellation (not part of the standard API);
- `requestPostAnimationFrame`, rendering, layout, networking, navigation,
  storage, canvas, or WebGL.

## Files changed

- `src/core/page_state.cpp` — `queue_microtask_callback` + installation in
  `install_page`;
- `test/test_queue_microtask.py` — focused acceptance tests;
- `python/iv8/page.py` — `run_jobs()` docstring;
- `python/iv8/__init__.py` — M11-4 module-docstring paragraph;
- docs: this boundary note plus `runtime_surface.md`, `current_status.md`,
  `next_tasks.md`, `decision_log.md`, and the M11-2/M11-3 non-goal pointers.

`JSContext` and the native `ContextState` API are unchanged: no new header,
binding, or Python symbol.

## Verification

Status: **landed and verified on real V8 (Linux + Windows, `2026-10-27`).**

- **Linux** — `release-linux` run `40471874328` on branch
  `m11-4-queue-microtask` (commit `0305b49`): `linux-wheels` success; cp311–cp314
  wheels built, installed, and tested. Result: **`1213 passed, 2 skipped in
  21.44s`**.
- **Windows** — `release-windows` run `40483069548` on the same commit:
  `build-v8` success with a V8 cache hit, and the four `wheels` jobs
  (cp311–cp314) all success. Result: **`1213 passed, 2 skipped in 20.52s`**.
- The total matches the M11-3 baseline (`1184 passed, 2 skipped`) plus the 29
  M11-4 test items in `test/test_queue_microtask.py`; the workflow's
  `_v8_linked` assertion makes the runtime tests run rather than skip.
- The manylinux cold build needed two build-only fixes on this branch:
  `a1869e9` (bootstrap depot_tools before `fetch`) and `0305b49` (detect
  bootstrap via `python3_bin_reldir.txt`, not the checked-in
  `python-bin/python3` wrapper). Neither changes runtime code. See
  `docs/m11_4_report.md`.
- Earlier local checks were static only: `git diff --check`, `bash -n`, and
  Python AST/`py_compile` pass. This machine has no C++ toolchain or supported
  Python 3.11–3.14 runtime, so the real-V8 evidence is the CI runs above.
- Real-V8 verification uses `.github/workflows/release-linux.yml` and
  `.github/workflows/release-windows.yml`, both `workflow_dispatch`-capable.
  `.github/workflows/ci.yml` is a skeleton lane and is not acceptance evidence.

## Open points

- Exception reporting is V8's explicit-checkpoint behavior: the exception is
  swallowed, not surfaced to Python. That matches the existing manual-pump error
  policy; a future error-reporting surface would need its own design.
- The four-pump composition question remains open (see `docs/decision_log.md` §A);
  M11-4 intentionally does not answer it.
- A ranked compatibility matrix is still the proper basis for choosing M11-5.
