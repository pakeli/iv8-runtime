# Next Tasks

Last reviewed: `2026-10-28`

This file is intentionally short and should be kept current. It is the preferred
task queue for Claude after chat-history loss.

## Priority 1: Reconcile contracts with implementation reality — DONE (`2026-08-06`)

### Goal

Reduce confusion between the original M1 contract and the real runtime surface now
present in `HEAD`.

### What was delivered

- **`docs/runtime_surface.md`** — a new overview index: the three-layer model, the
  complete public Python surface (`JSContext` + every `Page` member with its
  milestone), the JS-side surface, the standing exclusions, a milestone map linking
  every summary, and the build/platform reality.
- **Scope notes** on the four M1-era documents — `api_contract.md` (M1 `JSContext`
  only), `architecture.md` (ownership/lifecycle still authoritative, §2 Non-Goals
  historical), `implementation_plan.md` (completed historical plan), `test_plan.md`
  (M1 suite; later milestones add collar suites).
- **`AGENTS.md`** — the Project Scope rules no longer read as a live ban on
  DOM/timers/DevTools; they are marked as the M1 kernel boundary, with a pointer to
  the milestone-scoped working rule and to the index.
- **`CLAUDE.md`** — reading order now includes the index; the Reality Check names it.
- **`docs/current_status.md`** — §5 rewritten from "mismatch to remember" to
  "layering reconciled"; open questions now cite their `decision_log.md` entries;
  a new §8 records the stale local venv.

### Acceptance — met

- a new contributor can identify the current milestone and surface from
  `docs/runtime_surface.md` without reading commit history;
- no document now implies the repository exposes only M1 behavior.

### Deliberately not done

`runtime_surface.md` is an **index, not a contract** — it does not freeze `Page`
behavior. Whether `Page` should gain a frozen contract document remains Priority 6 /
`decision_log.md` §B.

## Priority 2: Choose the post-M10 direction -- DONE (`2026-08-06`)

### Goal

Pick the next milestone direction instead of continuing ad hoc.

### What was decided

- The repository now adopts a **compatibility-first** post-M10 direction.
- `M11` is defined as the first milestone line focused on reproducing the
  community-edition capability surface of `HanZzzzz000/iv8`, then extending beyond
  it deliberately.
- The immediate deliverable is `docs/m11_compatibility_plan.md`, which frames the
  goal, gap categories, API-shape question, rollout model, and risks.
- This resolves the old "continue M10 breadth vs begin M11" ambiguity.

### Acceptance -- met

- one approved direction is written down in `docs/decision_log.md`;
- `docs/current_status.md` and this file reflect the choice.

### Deliberately not done

This planning step does **not** yet choose the exact `M11-1` implementation slice.
That remains the next planning-to-execution handoff.

## Priority 3: First M11 compatibility slice — DONE (`2026-08-06`)

### Goal

Turn the compatibility-first direction into one bounded implementation target.

### What landed

`M11-1`: **configurable host-environment baseline**. `Page(environment=None)` plus the
static `Page.get_default_environment()`; eleven keys drive the read-only `navigator`
identity values, the new `screen` global, and `window.devicePixelRatio` /
`innerWidth` / `innerHeight`. Defaults reproduce the previous fixed `navigator`
constants (an unconfigured page is unchanged), values persist across `load()`
generations, the surface is read-only from JS, and validation is strict and
Python-side so a bad environment never partially installs a page.

Boundary and non-goals: `docs/m11_1_host_environment.md`. Tests:
`test/test_host_environment.py`.

### Acceptance — met

- one `M11-1` slice was selected and implemented;
- it has explicit scope, non-goals, files/modules, and focused tests.

### Deliberately not done

No compatibility **matrix** document was produced. `M11-1` was chosen by direction
rather than from a ranked source-backed survey, so the ranked-backlog part of this
priority is carried into Priority 4 below.

## Priority 3b: Second M11 compatibility slice — DONE (`2026-08-06`)

### What landed

`M11-2`: **`requestAnimationFrame` / `cancelAnimationFrame` with a manual frame pump**.
The two JS globals plus the Python-side `Page.run_animation_frame()`, which is the only
trigger. No real clock, background thread, or automatic event loop; one call runs one
frame (the callbacks queued when it began, in registration order), and a callback that
re-requests a frame lands in the next frame — so a self-rescheduling callback advances
one frame per call instead of spinning. `cancelAnimationFrame` prevents a queued
callback, including one cancelled by an earlier callback in the same frame. The queue is
independent of the M2-4 timers and the microtask queue and has its own handle space.

Boundary and non-goals: `docs/m11_2_animation_frames.md`. Tests:
`test/test_animation_frames.py`.

### Deliberately not done

Like `M11-1`, this slice was chosen by direction rather than from a ranked
source-backed survey. Priority 4 below is still outstanding and now covers `M11-5`.

## Priority 3c: Third M11 compatibility slice — DONE (`2026-08-06`)

### What landed

`M11-3`: **`requestIdleCallback` / `cancelIdleCallback` with a manual idle pump**. The
two JS globals plus the Python-side `Page.run_idle_callbacks()`, the only trigger. Same
manual model as M11-2: no clock, thread, or automatic loop; one call runs one round (the
callbacks queued when it began, in registration order), and a re-request lands in the
next round. Each callback receives a minimal `IdleDeadline`-style object — read-only
`didTimeout` (always false) and `timeRemaining()` (a fixed 50ms) — deterministic by
design. `options.timeout` is type-validated (malformed values throw `TypeError` before
anything is queued) but has **no firing semantics**, which is the slice's headline
non-goal. The idle queue is independent of the timers, animation frames, and microtasks,
with its own handle space.

Boundary and non-goals: `docs/m11_3_idle_callbacks.md`. Report:
`docs/m11_3_report.md`. Tests: `test/test_idle_callbacks.py`.

### Deliberately not done

Like M11-1 and M11-2, this slice was chosen by direction rather than from a ranked
source-backed survey. Priority 4 below is still outstanding and now covers `M11-5`.

## Priority 3d: Fourth M11 compatibility slice — DONE (`2026-10-27`)

### What landed

`M11-4`: **`queueMicrotask(callback)` on the existing manual `run_jobs()` microtask
pump**. The global enqueues callbacks on the generation's V8 microtask queue; there is
no new Python API and no fifth pump. A non-callable argument throws a JS `TypeError`
before enqueueing (standard semantics, unlike the permissive rAF/idle host calls).
Microtasks run FIFO, interleave with `Promise` reactions in enqueue order, and a
callback queued from inside a microtask appends to the same checkpoint. A throwing
callback is swallowed by V8's explicit checkpoint, so the page stays usable. `load()`
discards the old generation's pending microtasks; `run_jobs()` after `dispose()` raises
`JSContextDisposedError`. The microtask queue remains independent of timers, animation
frames, and idle callbacks.

Boundary and non-goals: `docs/m11_4_queue_microtask.md`. Tests:
`test/test_queue_microtask.py`. Local verification was static only (no C++ toolchain,
no supported Python runtime). Real-V8 CI on `2026-10-27`: Linux run `40471874328` and
Windows run `40483069548`, both on commit `0305b49`, each reported
`1213 passed, 2 skipped`; the Linux cold build required build-only bootstrap fixes
`a1869e9` / `0305b49`. See `docs/m11_4_report.md`.

### Deliberately not done

No scheduler API, automatic event loop, clock, `MutationObserver`, microtask
cancellation, or changes to the four existing pumps. Like the earlier M11 slices, this
was chosen before the ranked compatibility matrix existed; Priority 4 below now covers
`M11-5`.

## Priority 3e: Fifth M11 compatibility slice — DONE (`2026-10-28`)

### What landed

`M11-5`: **`MutationObserver`** — the minimal JS global with
`observe(target, options)` / `disconnect()` / `takeRecords()`. Records cover
`childList` and `attributes` (including `subtree`, `attributeOldValue`,
`attributeFilter`), are queued in mutation order, and are delivered only by the
existing `Page.run_jobs()` microtask checkpoint. Element and document targets are
accepted; `characterData` is accepted but inert because the DOM is element-only.
There is no new Python API, no fifth pump, no `MutationRecord` global, and no
cross-generation observer state.

Boundary: `docs/m11_5_mutation_observer.md`. Tests:
`test/test_mutation_observer.py` (52 collected items; the skeleton shape guard
passes locally, runtime tests require a rebuilt wheel).

### Verification — passed

Real-V8 CI on branch `m11-5-mutation-observer`, commit `4fa6469`:

- Linux `release-linux` run `40632597636` — cp311–cp314, each
  `1265 passed, 2 skipped`;
- Windows `release-windows` run `40632601387` — Python 3.11–3.14, each
  `1265 passed, 2 skipped`.

`test/test_mutation_observer.py` ran in every lane; 1265 is the M11-4 baseline
(`1213 passed, 2 skipped`) plus the 52 new items. Two fixes were needed after
the first runs: `ab1d507` (V8 15.0 `ExternalPointerTypeTag` on
`External::New` / `External::Value`) and `4fa6469` (the tests set ids via
`setAttribute`, since `.id` has no setter). See `docs/m11_5_report.md`.

### Next

`M11-6` is selected: Event subclass cluster — see Priority 3f.

### Deliberately not done

`characterData` records, `MutationRecord` global, `id`/`className` property
setters, document-node childList records without `subtree`, JS-finalizer observer
cleanup, `watch_apis` integration, and any scheduler/new pump.

## Priority 3f: Sixth M11 compatibility slice — SELECTED, IMPLEMENTATION IN PROGRESS (`2026-10-28`)

### What is being built

`M11-6`: **Event subclass cluster** — `CustomEvent` (`detail`), `MouseEvent`
(`clientX` / `clientY` / `button` / `buttons`), `KeyboardEvent` (`key` / `code`
/ `repeat`), and `FocusEvent` (`relatedTarget`) as minimal constructors on top
of the existing `Event` / `EventTarget` / bubbling model. Prototype chains make
each subclass `instanceof Event`; subclass fields are own read-only values; no
capture, cancellation, listener options, native event generation, or new pump.

Boundary: `docs/m11_6_event_subclasses.md`. Tests:
`test/test_event_subclasses.py` (pending).

### Verification pending

Real-V8 CI on branch `m11-6-event-subclasses` must run `release-linux` and
`release-windows` on the same commit. Expected total: the M11-5 baseline
(`1265 passed, 2 skipped`) plus the new items. Local checks are static only (no
C++ toolchain / Windows V8 monolith).

## Priority 4: Build the ranked compatibility backlog and pick M11-5 — DONE (`2026-10-28`)

### Goal

Replace per-slice intuition with a documented, ranked compatibility backlog.

### What was delivered

- `docs/m11_compatibility_backlog.md` — a source-backed survey (target README and
  demos vs. the current surface), scored candidates, and a ranked shortlist;
- `M11-5` selected from that backlog: `MutationObserver`;
- explicit scope, non-goals, delivery model, and tests frozen in
  `docs/m11_5_mutation_observer.md`.

### Why this matters

- `M11-1`, `M11-2`, `M11-3` and `M11-4` were all chosen without a source-backed gap
  matrix, so slice choice is still direction-driven;
- `docs/m11_compatibility_plan.md` §13 leaves open whether a long-term compatibility
  matrix should be maintained at all.

### Suggested output

- a source-backed compatibility survey or matrix against the target community surface;
- a ranked shortlist of remaining slices;
- one chosen `M11-5` slice with acceptance criteria.

### Acceptance — met

- the backlog is written down and ranked;
- `M11-5` is selected from it with explicit scope, non-goals, and tests.

## Priority 5: Refresh stale build/CI comments

### Goal

Update comments or docs that still describe a much earlier repository state.

### Why this matters

- some comments still mention “placeholder extension”, “no eval yet”, or
  M1-era constraints that no longer match `HEAD`;
- this slows future handoffs and code review.

### Suggested targets

- `src/module.cpp`
- `CMakeLists.txt`
- `.github/workflows/ci.yml`
- other docstrings/comments discovered during edits

### Acceptance

- comments describe the real current surface or clearly note that they are
  historical.

## Priority 6: Decide whether to publish a `Page`-level contract

### Goal

Choose whether the `Page` API should remain milestone-doc driven or gain a single
public contract document similar to M1.

### Why this matters

- `Page` is now central to the real product surface;
- behavior is currently spread across many milestone files and tests.

### Acceptance

- either a new contract doc is created, or a recorded decision explains why the
  milestone-doc approach remains preferred.

## Priority 7: Keep context files updated after every meaningful milestone

### Goal

Prevent another loss of working context after machine migration.

### Required discipline

After any milestone or multi-file feature lands, update:

- `docs/current_status.md`
- `docs/next_tasks.md`
- `docs/decision_log.md` when a real direction choice was made

### Acceptance

- future contributors can resume work from the repo alone.
