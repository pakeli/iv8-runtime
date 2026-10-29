# M11-30 Delivery Report — Frame / Idle Facade Integration

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-29`)**

Branch `m11-30-frame-idle-facade`, final verified commit `7dde955` (feature
commit `32f9289`).

## Evidence

- Linux `release-linux` run `40826630371` — cp311 / cp312 / cp313 / cp314,
  each `1509 passed, 2 skipped`.
- Windows `release-windows` run `40826634489` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1509 passed, 2 skipped`.
- `test/test_m11_30_frame_idle_facade.py` ran in every lane (14 items). Full
  suite collects 1511 items; 1509 pass and 2 are skipped.
- The first attempt (`32f9289`, Linux `40825544237` / Windows `40825557162`)
  hung in every lane. `advance()` / `run_until_idle()` used the timer **id**
  returned by `next_timer_due()` as the virtual due time to jump to; when the
  id was not greater than the current clock, the loop made no progress and did
  not count a step. `7dde955` looks up the timer's `due_ms` instead; both reruns
  above are green.

## Scope landed

- rAF now runs at fixed **16ms** virtual frame boundaries inside the facade
  (first frame at 16, next at the following boundary; per-frame snapshot).
- Idle rounds run one at a time when no timer or frame is due at the current
  virtual time; self-rescheduling idle callbacks are bounded by `max_steps`.
- `tick()` priority: due timer → due frame → one idle round, with microtasks
  drained before and after.
- `advance(ms)` and `run_until_idle()` now include timers, animation frames,
  idle callbacks, and microtasks; timer/frame jumps use the timer due time.
- Every timer/frame/idle callback is followed by a microtask checkpoint; a
  missed frame boundary runs at the current virtual time and schedules the next
  frame 16ms later.
- Legacy `run_timers` / `run_jobs` / `run_animation_frame` /
  `run_idle_callbacks` keep their one-queue semantics and do not advance the
  facade clock.

See `docs/m11_30_frame_idle_facade.md`.

## Verification notes

- Local `zig c++ -fsyntax-only` passed for `context_host.cpp`; the Python facade
  and tests passed `py_compile`; 1511 tests collect locally.
- The V8 monolith cache was reused; both workflows completed from the warmed
  master cache.
- The hang was a native loop-progress defect, not a test timeout or platform
  issue; it reproduced identically on all eight wheel lanes.

## Outcome

M11-30 is verified and merged to fork `master`. The facade now composes
timers, microtasks, animation frames, and idle callbacks. A real clock,
`performance.now()`, automatic loops, and background execution remain out of
scope.
