# M11-29 — Deterministic event-loop facade (timers + microtasks)

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-29`)**

M11-29 adds a higher-level, deterministic event-loop driver on top of the
existing timer and microtask queues. It does **not** add a background thread,
real sleeping, an automatic loop, or new JS globals. The existing four manual
pumps remain unchanged.

## Public API

```python
page.tick()                                  # one task turn -> bool
page.advance(ms, *, max_steps=10_000)        # bounded virtual-time window -> bool
page.run_until_idle(*, max_steps=1_000)      # auto-advance until no timers -> bool
page.event_loop_time_ms                      # read-only int, current virtual time
```

- `tick()`: drains microtasks, runs at most one timer whose `due_ms <= now`, then
  drains microtasks again. It never advances the clock. Returns `True` if a
  timer ran.
- `advance(ms)`: advances the virtual clock toward `now + ms`, executing every
  due timer in `(due_ms, registration seq)` order and draining microtasks after
  each callback. Timers scheduled inside the window are eligible. The clock is
  set to the target when the window completes. Returns `True` if the full window
  was processed; `False` if `max_steps` stopped it first.
- `run_until_idle()`: repeatedly runs the earliest timer, advancing virtual time
  to its due time, and drains microtasks after each callback, until no timers
  remain or `max_steps` is reached. Returns `True` only when no timers remain;
  an interval therefore normally returns `False` at the cap.
- `event_loop_time_ms`: the generation's virtual clock, initially `0`; reset when
  `load()` installs a new generation. Legacy pumps do not advance it.

All three methods use the existing operation guard, GIL-release and
callback-error rules: overlapping operations raise `JSContextBusyError`, after
`dispose()` they raise `JSContextDisposedError`, and a throwing timer callback is
swallowed so the page remains usable.

## Scheduling rules

- Timer due time is `registration_time + max(delay, 0)`; registration order is
  the tie-breaker for equal due times.
- Each timer callback is followed by a microtask checkpoint
  (`PerformMicrotaskCheckpoint`), so `Promise`, `queueMicrotask`,
  `MutationObserver`, and `FileReader` deliveries interleave with timers.
- A repeating timer is rescheduled at `previous_due + max(delay, 1)` before its
  callback runs. The 1ms floor prevents zero-delay intervals from spinning
  without virtual-time progress.
- `max_steps` bounds all three drivers. `advance` stops without jumping the clock
  to the target if the cap is hit; `run_until_idle` leaves the clock at the last
  processed due time and returns `False`.
- The legacy `run_timers()` keeps its original semantics: one snapshot, ordered
  by `(delay, seq)`, no clock, no automatic microtask checkpoint. Mixing legacy
  pumps and the facade is allowed but the two models are intentionally separate.

## Phase boundary

- `requestAnimationFrame` / `requestIdleCallback` remain independent manual
  pumps in M11-29. They are not run by `tick` / `advance` / `run_until_idle`.
- No real clock, `performance.now()`, real sleeping, background execution,
  Promise/timer cancellation, fetch/XHR, workers, or automatic loop.
- No new JS-visible global; no new top-level Python object or exception.
- The virtual clock is per generation; `load()` resets it and clears pending
  timers as before.

## Tests

`test/test_event_loop_facade.py` covers API shape, virtual time, due ordering,
microtask interleaving, nested timers, intervals and caps, `tick`, cancellation,
callback errors, load/dispose, legacy-pump compatibility, and rAF/idle isolation.

## Verification

Verified on real V8 at commit `b211481` (feature commit `0bc75a2`):

- Linux `release-linux` run `40820152813` — cp311–cp314, each
  `1495 passed, 2 skipped`;
- Windows `release-windows` run `40820163797` — Python 3.11–3.14, each
  `1495 passed, 2 skipped`.

The new test file ran in every lane (21 items); the full suite collects 1497
items. The first attempt (`0bc75a2`) failed three stale pump non-goal guards,
refreshed in `b211481`. See `docs/m11_29_report.md`.
