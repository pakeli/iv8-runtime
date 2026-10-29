# M11-30 — Frame / idle integration into the event-loop facade

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-29`)**

M11-30 extends the M11-29 deterministic facade (`tick` / `advance` /
`run_until_idle`) to drive `requestAnimationFrame` and `requestIdleCallback` in
the same virtual event loop. Legacy pumps remain unchanged and independent.

## Scheduling model

- The virtual frame interval is fixed at **16ms**.
- A callback registered through `requestAnimationFrame` before a frame boundary
  runs at that boundary. A callback registered during a frame runs in the
  **next** frame. The frame callback list is snapshotted per frame, as in the
  legacy `run_animation_frame()` pump.
- At each turn, priority is:
  1. the earliest timer due at the current virtual time (ties: registration
     order);
  2. an animation frame due at the current virtual time;
  3. one idle round when no timer or frame is currently due.
- Every timer/frame/idle callback is followed by a microtask checkpoint.
- Idle callbacks run one round at a time when the current time has no due
  timer/frame. A callback that re-requests idle remains pending; `max_steps`
  bounds it.
- A repeating timer reschedules at `previous_due + max(delay, 1)`; a
  self-rescheduling rAF advances one frame (16ms) per turn.
- If a frame boundary is missed because legacy pumps advanced beyond it, the
  facade runs the pending frame at the current virtual time and schedules the
  next frame 16ms later.

## API semantics

- `tick()`: one current-time turn; a timer first, otherwise a due frame,
  otherwise one idle round. It never advances the virtual clock.
- `advance(ms)`: processes all timer/frame/idle work due in the window. It
  advances to the next timer/frame boundary when no work is due at the current
  time; idle callbacks run before advancing to future work when no timer/frame is
  currently due. Returns `True` when the window completes; `False` when
  `max_steps` stops it first.
- `run_until_idle()`: continues until no timers, animation-frame callbacks, or
  idle callbacks remain, or `max_steps` is reached. A self-rescheduling rAF or
  idle callback normally returns `False` at the cap.
- `event_loop_time_ms`: advances to timer/frame due times; idle callbacks do not
  move the clock. Reset by `load()`.

Legacy `run_timers` / `run_jobs` / `run_animation_frame` /
`run_idle_callbacks` keep their existing one-queue semantics and do not
participate in the virtual frame clock. Mixing legacy pumps and the facade is
allowed but intentionally not a single coherent timeline.

## Non-goals

- No real display refresh, vsync, paint, layout, or rendering.
- No `performance.now()` / real clock / background thread / automatic loop.
- No `IdleDeadline` deadline-based timeout semantics; `didTimeout` stays false
  and `timeRemaining()` stays the fixed budget.
- No new JS-visible global, no new exception type.

## Tests

`test/test_m11_30_frame_idle_facade.py` covers 16ms frame boundaries, rAF
snapshot/requeue behavior, idle priority, microtask checkpoints, advance/run-until
idle caps, legacy pump isolation, `load()` reset, and swallowed callback errors.

## Verification

Verified on real V8 at commit `7dde955` (feature commit `32f9289`):

- Linux `release-linux` run `40826630371` — cp311–cp314, each
  `1509 passed, 2 skipped`;
- Windows `release-windows` run `40826634489` — Python 3.11–3.14, each
  `1509 passed, 2 skipped`.

The new test file ran in every lane (14 items); the full suite collects 1511
items. The first attempt (`32f9289`) hung on all lanes because
`advance()` / `run_until_idle()` treated the timer id as the virtual due time;
`7dde955` fixed the clock jump. See `docs/m11_30_report.md`.
