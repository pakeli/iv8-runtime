# M11-3 — `requestIdleCallback` / `cancelIdleCallback` (manual idle pump)

M11-3 is the third implementation slice under the compatibility-first M11 line. It
mirrors the M11-2 frame pump exactly one level up: the JS API exists and behaves
normally, but **nothing runs until Python asks for it**.

## Public API

- **JS: `requestIdleCallback(callback[, options])`** — queues `callback` and returns a
  numeric handle. FIFO: registration order is run order. A non-function or missing
  first argument queues nothing and returns `undefined` (no throw).
- **JS: `cancelIdleCallback(handle)`** — drops a queued, not-yet-run callback. An
  unknown, already-run, or already-cancelled handle is a silent no-op.
- **Python: `Page.run_idle_callbacks()`** — the **only** trigger. Runs one round,
  then returns.

## Round model

One call to `run_idle_callbacks()` runs exactly **one round**: the callbacks queued at
the moment the call began, in registration order.

A callback that calls `requestIdleCallback()` again schedules into the **next** round
and cannot run during the current call. This is what bounds the pump — a callback that
unconditionally re-registers itself advances one round per call instead of spinning:

```python
page.eval("""
    globalThis.ticks = 0;
    function tick() { ticks++; requestIdleCallback(tick); }
    requestIdleCallback(tick);
""")
page.run_idle_callbacks()   # ticks == 1
page.run_idle_callbacks()   # ticks == 2
```

A callback runs **at most once per registration**. Pumping an empty queue is a
harmless no-op. A callback that throws is swallowed — the rest of the round still runs
and the page stays usable.

Mid-round cancellation works: if an earlier callback cancels a later one that has not
run yet, the later one does not run. As in M11-2, entries stay in the live queue and
each handle is re-looked-up immediately before it is invoked, so `cancelIdleCallback`
remains effective for the whole round.

## The IdleDeadline argument

Each callback receives **one** argument — a minimal `IdleDeadline`-style object,
rebuilt per callback:

| Member | Value | Notes |
|---|---|---|
| `didTimeout` | always `false` | read-only (`ReadOnly \| DontDelete`) |
| `timeRemaining()` | always `50` (ms) | read-only method; a fixed budget |

Both are **deterministic on purpose**. This runtime has no clock, so a "real"
remaining time would be a fabrication. Consequences worth knowing:

- `timeRemaining()` does **not** decrease while a callback runs — repeated calls
  within one callback return the same number;
- `didTimeout` is never `true`, because no timeout ever fires (see below);
- the budget is `50`ms to match the browser's typical idle deadline cap, which keeps
  feature-detecting scripts on a familiar code path;
- each callback gets its **own** deadline object (they are not `===`).

A sloppy-mode write to either member is a silent no-op; a strict-mode write is a
`TypeError`.

## `options.timeout` — validated, not implemented

`options` may be absent, `undefined`, `null`, or an object. Its optional `timeout` is
**type-validated but has no firing semantics this phase**:

- a non-object `options` → `TypeError`;
- a present `timeout` that is not a Number → `TypeError`;
- a `NaN` / `Infinity` / negative `timeout` → `TypeError`;
- a valid `timeout` is clamped to int32 and **recorded only**;
- unknown option keys are ignored.

Validation happens **before** the callback is queued, so a rejected `options` leaves
nothing half-registered.

The reason for rejecting loudly rather than coercing: a malformed timeout is much more
likely a caller bug than an intentional value, and silently reinterpreting it would
hide that. The reason for not implementing the timeout: a real timeout is a *deadline*,
which requires a clock this runtime deliberately does not have. **`{timeout: 0}` does
not make a callback run any earlier, and a large timeout does not delay it** —
registration order alone decides.

## Independence from the other queues

The idle queue is **separate** from the M2-4 timer registry, the M11-2 animation-frame
queue, and the microtask queue, and it has its **own handle space**:

- `run_idle_callbacks()` fires no timers, no frame callbacks, and drains no microtasks;
- `run_timers()` / `run_animation_frame()` / `run_jobs()` run no idle callbacks;
- `cancelIdleCallback` cancels neither a timer nor a frame callback, and
  `clearTimeout` / `cancelAnimationFrame` do not cancel an idle callback.

The runtime now has four independent manual pumps, each draining exactly one queue.

## Scope / non-goals

M11-3 deliberately does **not** add:

- real `options.timeout` firing semantics (see above) — the single most important
  non-goal of this slice;
- a real clock, `performance.now()`, or any elapsed-time measurement; `timeRemaining()`
  is a constant;
- a global `IdleDeadline` constructor / class — the argument is a plain object;
- background threads or an automatic event loop; nothing advances without a pump;
- a unified `tick()` / `drain()` / `run_event_loop()` / `run_until_idle()` scheduler
  that pumps several queues at once, or a "run N rounds" helper — one call is one
  round of one queue;
- `scheduler.postTask` / `scheduler.yield`, task priorities, or
  `requestPostAnimationFrame` / `queueMicrotask`;
- networking, navigation, storage, or `canvas` / WebGL;
- any relationship to `Page.ready_state` or the M3-4/M3-6 load lifecycle; a `load()`
  never pumps idle callbacks.

`JSContext` is untouched (the queue lives on `ContextState` but is reachable only
through the `Page` globals and pump). The first-layer DOM / script / lifecycle / form /
M8 web-platform semantics, the M9–M10 monitoring channels, and the M11-1 / M11-2
surfaces are unchanged.

## Monitoring interaction

The two globals record through the existing M9-2 `watch_record` path as
`window.requestIdleCallback` / `window.cancelIdleCallback`, exactly as
`window.setTimeout` and the M11-2 frame globals already do. This changes no default
behavior: `WatchRegistry::maybe_record` only records paths explicitly registered via
`Page.watch_apis([...])`, so an unwatched page is unaffected.

## Generation behavior

The globals are installed per generation by `install_page`, so a loaded document has
them. The queue belongs to the generation's `ContextState`: a `load()` tears down the
old context and its pending callbacks are discarded (never resurrected). Pending
callbacks hold V8 `Global` handles that `ContextState::teardown()` resets while the
isolate is still alive, in the same step that releases the timer and animation-frame
handles — the ordering requirement is identical.

## Files changed

- `include/iv8/context_host.h` — the `idle_callbacks_` FIFO queue (a `std::vector`,
  same rationale as `animation_frames_`), its handle counter, and the three method
  declarations;
- `src/core/context_host.cpp` — `register_idle_callback` / `cancel_idle_callback` /
  `run_idle_callbacks`, the `idle_deadline_time_remaining` callback and
  `kIdleBudgetMs`, plus the teardown handle release;
- `src/core/page_state.cpp` — the two JS global callbacks (including the
  `options.timeout` validation), their installation in `install_page`, the
  `PageState::run_idle_callbacks` forwarder, and `<cmath>` for the finite checks;
- `include/iv8/page_state.h` — the `run_idle_callbacks` declaration;
- `src/binding/page_binding.cpp` — binds `run_idle_callbacks`;
- `python/iv8/page.py` — the `Page.run_idle_callbacks()` facade + docstring;
- `python/iv8/__init__.py` — docstring;
- `test/test_idle_callbacks.py` — the acceptance suite;
- `docs/m11_2_animation_frames.md` — its non-goal list said `requestIdleCallback` was
  out of scope; now points here;
- `docs/runtime_surface.md`, `docs/current_status.md`, `docs/next_tasks.md`,
  `docs/m11_3_report.md`.

## Platforms

Unchanged: Linux and Windows build with real V8; macOS is skeleton-only. A skeleton
build exports the same public shape (`Page()` raises `RuntimeError`), so the runtime
tests are `@on_only` and only the shape guards run there.
