# M11-3 Report — `requestIdleCallback` / `cancelIdleCallback`

Date: `2026-08-06` · Status: **landed and verified on real V8 (Linux + Windows)**

> **Postscript (`2026-10-27`).** The "natural neighbour" suggested in §8 below,
> `queueMicrotask`, has since landed and been verified as **M11-4** on real V8
> (Linux + Windows) — see `docs/m11_4_queue_microtask.md` and
> `docs/m11_4_report.md`. The rest of this page remains the historical M11-3
> delivery record.

Boundary document: `docs/m11_3_idle_callbacks.md` (authoritative for behavior and
non-goals). This page is the delivery record: what was asked, what shipped, how it was
verified, and what remains open.

## 1. Goal

Add the third compatibility slice under the M11 line: a minimal, deterministic
`requestIdleCallback` / `cancelIdleCallback` implementation driven by an explicit
Python pump, matching the M11-2 frame-pump model rather than inventing a new one.

Explicit constraints from the task: no real clock, no background thread, no automatic
event loop; one pump call runs one round; a re-request lands in the next round;
cancellation must prevent a queued callback; the callback must receive a minimal
`IdleDeadline` (`didTimeout` + `timeRemaining()`) kept deterministic; `options.timeout`
type-validated but **not** implemented; stay consistent with `load()` / `dispose()` /
busy-disposed paths; no `performance.now()`, unified scheduler, `scheduler.postTask`,
network, navigation, storage, or canvas/webgl.

## 2. What was implemented

**JS surface** (per generation, installed by `install_page`):

- `requestIdleCallback(callback[, options])` → numeric handle; FIFO by registration
  order. A non-function / missing first argument queues nothing and returns
  `undefined`.
- `cancelIdleCallback(handle)` → drops a queued, not-yet-run callback. Unknown /
  already-run / already-cancelled handles are silent no-ops.

**Python surface**: `Page.run_idle_callbacks()` — the only trigger.

**Round semantics.** The pump snapshots the handles queued when the call begins, but
leaves the entries in the live queue and re-looks-up each handle immediately before
invoking it. This is the same design as M11-2 and it is load-bearing for two
requirements at once: only snapshot handles run (so a callback that re-requests lands
in the **next** round, bounding the pump), while a callback can still cancel a later
one **in the same round** (a wholesale detach would have broken this). Entries are
dequeued before the call, so a callback runs at most once per registration.

**IdleDeadline.** A plain object rebuilt per callback, with two read-only
(`ReadOnly | DontDelete`) members: `didTimeout` (always `false`) and `timeRemaining()`
(always `50`). Deterministic on purpose — with no clock, a "real" remaining time would
be fabricated. Consequences: `timeRemaining()` does not decrease while a callback runs,
and each callback gets its own deadline object. `50`ms matches the browser's typical
idle cap so feature-detecting scripts stay on a familiar path.

**`options.timeout` — validated, not implemented.** `options` may be absent /
`undefined` / `null` / an object; a non-object `options`, or a present `timeout` that is
not a non-negative finite Number, throws a JS `TypeError` **before anything is queued**.
A valid value is clamped to int32 and recorded only.

Two deliberate judgement calls here, both documented in the boundary doc:

- *Reject loudly rather than coerce.* A malformed timeout is far more likely a caller
  bug than an intentional value; silently reinterpreting it would hide the bug. This
  follows the M8-1 `TextDecoder` precedent (invalid label → throw; `options` → captured
  but inert).
- *Do not implement the timeout.* A real timeout is a **deadline**, which requires the
  clock this runtime deliberately lacks. `{timeout: 0}` does not run a callback earlier
  and a large timeout does not delay it — registration order alone decides.

**Queue independence.** The idle queue is separate from the M2-4 timers, the M11-2
animation frames, and the microtask queue, with its own handle space. No pump runs
another's queue, and `cancelIdleCallback` / `clearTimeout` / `cancelAnimationFrame`
never cross-cancel. The runtime now has **four** manual pumps, each draining exactly one
queue.

## 3. Files changed

Runtime / binding / facade (7):

| File | Change |
|---|---|
| `include/iv8/context_host.h` | `idle_callbacks_` FIFO queue (`std::vector`, same rationale as `animation_frames_`), handle counter, three method declarations |
| `src/core/context_host.cpp` | `register_idle_callback` / `cancel_idle_callback` / `run_idle_callbacks`; `idle_deadline_time_remaining` + `kIdleBudgetMs`; teardown handle release |
| `src/core/page_state.cpp` | the two JS globals incl. `options.timeout` validation; installation in `install_page`; `PageState::run_idle_callbacks`; added `<cmath>` |
| `include/iv8/page_state.h` | `run_idle_callbacks` declaration |
| `src/binding/page_binding.cpp` | binds `run_idle_callbacks` |
| `python/iv8/page.py` | `Page.run_idle_callbacks()` facade + docstring |
| `python/iv8/__init__.py` | module docstring |

Tests (1): `test/test_idle_callbacks.py` — 44 tests.

Docs (6): new `docs/m11_3_idle_callbacks.md` and this report; updated
`docs/runtime_surface.md`, `docs/current_status.md`, `docs/next_tasks.md`,
`docs/decision_log.md`, and `docs/m11_2_animation_frames.md` (its non-goal list stated
`requestIdleCallback` was out of scope — it now points here).

## 4. Test results

**Local (skeleton build, `_v8_linked == False`).** Full suite:
**156 passed, 1030 skipped, 0 failed**. The 3 new passes are the shape guards; the
runtime tests are `@on_only` and skip here. This is the pre-change baseline plus exactly
the new shape tests — no regression to any earlier milestone's guards.

Additionally, the 23 `options.timeout` validation branches were modelled in Python and
checked one-by-one against the test expectations before any CI run — all matched.

**CI (real V8).** Both platforms green, `1184 passed, 2 skipped, 0 failed`:

| Platform | Run | Jobs | Result |
|---|---|---|---|
| Linux (manylinux_2_28, cp311–cp314) | `31077011236` | `linux-wheels` success | `1184 passed, 2 skipped` × 4 wheels |
| Windows (cp311–cp314) | `31077012724` | `build-v8` + 4 × `wheels` success | `1184 passed, 2 skipped` |

Evidence that this slice's tests actually ran (not skipped) — from the Linux log:

```
test/test_idle_callbacks.py ............................................ [ 56%]
```

44 dots = the 44 tests in the file, all passing, zero skips. Windows shows the same
file passing. `FAILED` / `error:` count in both logs: **0**.

Baseline comparison: M11-2 measured `1140 passed`; M11-3 measures `1184` — exactly +44,
matching the new file. Nothing was silently dropped.

The V8 monolith cache **hit** on both platforms (the ~40-minute rebuild step was
skipped), so no build-config change leaked in.

## 5. Non-goals (confirmed absent)

Not added by this slice, and asserted by tests where observable:

- **real `options.timeout` firing semantics** — the headline non-goal;
- a real clock, `performance.now()`, or elapsed-time measurement (`timeRemaining()` is
  a constant);
- a global `IdleDeadline` constructor — the argument is a plain object;
- a unified `tick()` / `drain()` / `run_event_loop()` / `run_until_idle()` scheduler, or
  a "run N rounds" helper;
- `scheduler.postTask` / `scheduler.yield` / task priorities / `queueMicrotask` /
  `requestPostAnimationFrame`;
- background threads or an automatic event loop;
- network, navigation, storage, `canvas` / WebGL.

`JSContext` is untouched. M11-1, M11-2, the first-layer DOM / script / lifecycle / form
/ M8 surfaces, and the M9–M10 monitoring channels are unchanged. `test_idle_callbacks.py`
includes explicit guards that `performance` / `scheduler` / `queueMicrotask` /
`IdleDeadline` / `requestPostAnimationFrame` remain `undefined`.

## 6. Verification limits and remote paths

Per the task's requirement to state what could not be reproduced locally:

**What could not be verified locally: the C++ compile and every runtime test.** This
machine has no C++ toolchain (no cmake, no MSVC/clang-cl), WSL has no distro installed,
and the Windows `v8_monolith.lib` does not exist here. The Linux `libv8_monolith.a` in
`data/v8` is an ELF archive built under WSL2 Ubuntu-24.04 (per its `BUILD_INFO.txt`) and
cannot be linked by a Windows toolchain. Locally, therefore, only the skeleton build
runs, which is why 1030 tests skip.

**Remote verification path used** (and recommended for future slices):

- `.github/workflows/release-linux.yml` — real-V8 Linux wheels + full suite;
- `.github/workflows/release-windows.yml` — real-V8 Windows wheels + full suite;
- both are `workflow_dispatch`, so they can be triggered on a branch without a tag.
  With the V8 cache warm they finish in minutes rather than the ~40-minute cold build.
- `.github/workflows/ci.yml` runs automatically on push across 16 os × Python
  combinations, but it is a **skeleton** (V8 not linked) lane — it validates compilation
  and shape guards only, so it cannot substitute for the two release workflows for
  runtime behavior.

This matches how the project has always worked: `WINDOWS_BUILD_NOTES.md` §5 recommends
CI iteration over local rebuilds, and commit `6efbc12` is labelled "Windows,
CI-driven" — real-V8 Windows has never been built on this machine.

Note: GitHub connectivity dropped for roughly a minute during this slice (four
consecutive `443` timeouts on push). It was transient and unrelated to the change;
retrying succeeded.

## 7. Open questions

1. **Should the four manual pumps gain a composed driver?** The runtime now exposes
   `run_timers()`, `run_jobs()`, `run_animation_frame()`, and `run_idle_callbacks()`,
   each draining one queue. A caller wanting "advance everything" must call all four in
   an order they choose. A single driver would need an opinionated ordering (browsers
   interleave tasks → microtasks → frames → idle), which is a real design decision, not
   a mechanical addition — so it was deliberately **not** invented here. Recorded in
   `docs/decision_log.md` §A.
2. **`options.timeout` is a known divergence from browsers.** Scripts that rely on the
   timeout to guarantee eventual execution behave differently here: the callback still
   runs, but only on the next pump, and `didTimeout` is never `true`. Implementing it
   would require introducing a clock — a larger decision than this slice.
3. **The ranked compatibility matrix still does not exist.** M11-1, M11-2 and M11-3
   were all chosen by direction rather than from a source-backed survey of the target
   community surface. `docs/next_tasks.md` Priority 4 and `docs/decision_log.md` §A
   remain open. Note that producing that survey needs access to the target repository,
   which is currently blocked from this environment by network policy.
4. **`IdleDeadline` is a plain object, not a branded class.** `d instanceof
   IdleDeadline` is impossible (there is no such global) and
   `Object.prototype.toString.call(d)` reports `[object Object]`. Fine for the minimal
   model; worth revisiting if a compatibility target inspects the deadline's type.

## 8. Recommended next step

Do **Priority 4** before another feature slice: build the ranked compatibility backlog
so `M11-4` is chosen from evidence rather than direction. Three slices have now landed
without it, and the gap is compounding.

If a feature slice is preferred instead, the natural neighbours of this work are
`queueMicrotask` (small, and `run_jobs()` already exists to drain it) or a decision on
question 1 above. Both should still be ranked against the target surface first.
