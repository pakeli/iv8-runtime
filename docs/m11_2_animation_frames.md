# M11-2 — `requestAnimationFrame` / `cancelAnimationFrame` (manual frame pump)

> **Scope note (`2026-10-29`).** M11-31 supersedes the "no timestamp argument" line
> below: the callback now receives the current virtual timestamp, and a virtual
> `performance.now()` exists. The legacy pump still does not advance the clock, and
> there is still no real-time clock. The rest of this document remains the M11-2
> boundary.

M11-2 is the second implementation slice under the compatibility-first M11 line. It
adds the two animation-frame globals plus one Python pump, following the M2-4 timer
precedent exactly: the JS API exists and behaves normally, but **nothing runs until
Python asks for it**.

## Public API

- **JS: `requestAnimationFrame(callback)`** — queues `callback` and returns a numeric
  handle. FIFO: registration order is run order. The callback receives **no
  timestamp argument** (this runtime has no clock). A non-function or missing
  argument queues nothing and returns `undefined` (no throw).
- **JS: `cancelAnimationFrame(handle)`** — drops a queued, not-yet-run callback.
  Cancelling an unknown, already-run, or already-cancelled handle is a silent no-op.
- **Python: `Page.run_animation_frame()`** — the **only** trigger. Runs one frame,
  then returns.

## Frame model

One call to `run_animation_frame()` runs exactly **one frame**: the callbacks queued
at the moment the call began, in registration order.

A callback that calls `requestAnimationFrame()` again schedules into the **next**
frame and cannot run during the current call. This is what bounds the pump — a
callback that unconditionally re-registers itself advances one frame per call instead
of spinning forever:

```python
page.eval("""
    globalThis.ticks = 0;
    function tick() { ticks++; requestAnimationFrame(tick); }
    requestAnimationFrame(tick);
""")
page.run_animation_frame()   # ticks == 1
page.run_animation_frame()   # ticks == 2
```

A callback runs **at most once per registration** (there is no repeating form, unlike
`setInterval`). Pumping an empty queue is a harmless no-op. A callback that throws is
swallowed — the rest of the frame still runs and the page stays usable, matching
`run_timers()`.

Mid-frame cancellation works: if an earlier callback in the frame cancels a later one
that has not run yet, the later one does not run. The implementation keeps entries in
the live queue and re-looks-up each handle immediately before invoking it, so
`cancelAnimationFrame` remains effective for the whole frame.

## Independence from the other queues

The animation-frame queue is **separate** from the M2-4 timer registry and from the
microtask queue, and it has its **own handle space**:

- `run_animation_frame()` does not fire `setTimeout` / `setInterval` callbacks and
  does not drain microtasks;
- `run_timers()` does not run frame callbacks; `run_jobs()` does not either;
- `cancelAnimationFrame(timerId)` does not cancel a timer, and
  `clearTimeout(frameHandle)` does not cancel a frame callback.

This mirrors the browser split between the timer task queue and the animation-frame
callback list.

## Scope / non-goals

M11-2 deliberately does **not** add:

- a real clock, `performance.now()`, `Date`-based frame timing, a timestamp argument,
  or any frame-rate / vsync / 60fps notion — frames are counted, not timed;
- background threads or an automatic event loop; nothing advances without an explicit
  pump;
- a combined `run_event_loop()` / `run_until_idle()` / `tick()` driver, or a
  "run N frames" helper — one call is one frame;
- `queueMicrotask` or `requestPostAnimationFrame`. (`requestIdleCallback` /
  `cancelIdleCallback` were also out of scope for M11-2; they landed separately in
  **M11-3** — see `docs/m11_3_idle_callbacks.md`. `queueMicrotask` landed in
  **M11-4** — see `docs/m11_4_queue_microtask.md`.)
- rendering, layout, paint, compositing, or `<canvas>` / WebGL — a frame callback runs
  JavaScript and nothing else is observable;
- any relationship to `Page.ready_state` or the M3-4/M3-6 load lifecycle; a `load()`
  never pumps frames.
``
`JSContext` is untouched (the queue lives on `ContextState` but is reachable only
through the `Page` globals and pump). The first-layer DOM / script / lifecycle / form
/ M8 web-platform semantics and the M9–M10 monitoring channels are unchanged.

## Monitoring interaction``

The two globals record through the existing M9-2 `watch_record` path as
`window.requestAnimationFrame` / `window.cancelAnimationFrame`, exactly like
`window.setTimeout` / `window.setInterval` already do. This changes no default
behavior: `WatchRegistry::maybe_record` only records paths that were explicitly
registered via `Page.watch_apis([...])`, so an unwatched page is unaffected.

## Generation behavior

The globals are installed per generation by `install_page`, so a loaded document has
them. The queue belongs to the generation's `ContextState`: a `load()` tears down the
old context and its pending callbacks are discarded (never resurrected), which matches
the existing timer behavior. Pending callbacks hold V8 `Global` handles that
`ContextState::teardown()` resets while the isolate is still alive, in the same step
that releases timer handles — the ordering requirement is identical.

## Files changed

- `include/iv8/context_host.h` — the `animation_frames_` FIFO queue (a `std::vector`,
  since registration order is run order and needs no sort), its handle counter, and
  the three method declarations; added `<vector>`;
- `src/core/context_host.cpp` — `register_animation_frame` /
  `cancel_animation_frame` / `run_animation_frame`, plus the teardown handle release;
- `src/core/page_state.cpp` — the two JS global callbacks, their installation in
  `install_page`, and the `PageState::run_animation_frame` forwarder;
- `include/iv8/page_state.h` — the `run_animation_frame` declaration;
- `src/binding/page_binding.cpp` — binds `run_animation_frame`;
- `python/iv8/page.py` — the `Page.run_animation_frame()` facade + docstring;
- `python/iv8/__init__.py` — docstring;
- `test/test_animation_frames.py` — the acceptance suite;
- `docs/runtime_surface.md`, `docs/current_status.md`, `docs/next_tasks.md`.

## Platforms

Unchanged: Linux and Windows build with real V8; macOS is skeleton-only. A skeleton
build exports the same public shape (`Page()` raises `RuntimeError`), so the runtime
tests are `@on_only` and only the shape guards run there.
