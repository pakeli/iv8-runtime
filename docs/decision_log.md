# Decision Log

Last reviewed: `2026-10-27`

This file records project-level decisions that are easy to lose when work moves
between machines or chat sessions.

## Confirmed Decisions

### 1. Start from a narrow V8 kernel, not a full browser

- Original baseline is M1: `JSContext`, evaluation, conversion, exceptions,
  disposal, isolation.
- Browser APIs were intentionally excluded from the first contract.
- This remains important historical context even though implementation later grew.

### 2. Use one isolate per `JSContext`

- Preserve isolation and avoid a shared global JS runtime.
- Keep process-wide V8 initialization separate from context-owned resources.

### 3. Keep pybind11 bindings thin

- Native runtime logic belongs in dedicated C++ modules.
- Binding code should remain a narrow facade.

### 4. Build against a pinned V8 revision

- Never float against V8 `master` by default.
- Pinned V8 metadata is distinct from the Python package version.

### 5. Project evolved beyond M1 in layered form

- The repository did not stop at pure embedding.
- Later milestones added a layered `Page` runtime and then monitoring/devtools
  features.
- The right mental model is:
  - M1 core
  - first-layer hosted runtime (`Page`, DOM/forms/scripts/URL subset)
  - second-layer monitoring / Inspector attach

### 6. Linux release-first strategy was chosen

- Repository docs record Strategy A as selected.
- Linux wheels are the primary released/distributable target.
- Windows/macOS are not the main release baseline unless updated later.

## Context Recovery Decisions After Repo Migration

### 7. The repository itself must become the source of resume context

Because prior chat history was lost during machine migration, future continuity
must rely on repo-carried context rather than external conversation state.

Action taken:

- added `CLAUDE.md` as agent entrypoint;
- added `docs/current_status.md`;
- added `docs/next_tasks.md`;
- added this `docs/decision_log.md`;
- added `docs/dev_setup.md`.

### 8. Current implementation stage is recorded as M10-2

As of `2026-08-06`, the latest landed milestone at `HEAD` was treated as `M10-2`.

This decision exists so future sessions do not restart from outdated M1-only
assumptions. **Superseded:** the M11 compatibility line has since advanced through
`M11-4`; use `docs/current_status.md` for the current stage rather than this
historical entry.

### 9. Layering is reconciled by an index, not by rewriting the M1 documents

Decided `2026-08-06` while executing `next_tasks.md` Priority 1.

The M1-era documents (`api_contract.md`, `architecture.md`, `implementation_plan.md`,
`test_plan.md`) are **kept as written** and given scope notes, rather than being
edited to describe the current runtime. Reasons:

- `api_contract.md` is a frozen contract and `architecture.md` is still the source of
  truth for ownership/lifecycle — rewriting either would destroy a real boundary;
- the later milestones each already froze their own boundary in a summary document,
  so the missing piece was navigation, not content;
- the M1 documents' historical value (why the kernel is narrow) is worth preserving.

The navigation piece is `docs/runtime_surface.md`. It is explicitly an **index, not a
contract**: it maps the surface and links to the authoritative per-milestone
boundaries, and must not be treated as freezing behavior. This deliberately leaves
Pending Decision B (a unified `Page` contract) open.

### 10. Post-M10 work is compatibility-first, not ad hoc feature growth

Decided `2026-08-06`.

After `M10-2`, the repository's main forward-looking goal is no longer framed as
"continue adding whichever hosted-runtime feature seems most convenient next".
Instead, it is framed as:

> gradually reproduce the community-edition capability surface of
> `HanZzzzz000/iv8`, then extend beyond it deliberately.

Consequences of this decision:

- the next milestone line is `M11`, not an unbounded continuation of `M10`;
- `M11` starts with compatibility planning and gap ranking before new runtime code;
- future implementation slices should be justified as compatibility moves or as
  clearly-labeled enhancements beyond that compatibility baseline.

The planning artifact for this decision is `docs/m11_compatibility_plan.md`.

### 11. M11-5 delivered `MutationObserver` without a new pump

Decided/verified `2026-10-28`.

`M11-5` implements the JS global `MutationObserver` on the existing
`Page.run_jobs()` microtask checkpoint. It deliberately adds no Python API, no
fifth pump, no `MutationRecord` global, no `id` / `className` property setters,
and no cross-generation observer state. Records cover `childList` and
`attributes` (with `subtree` / `attributeOldValue` / `attributeFilter`);
`characterData` is accepted but inert because the DOM is element-only.

Real-V8 verification on Linux run `40632597636` and Windows run `40632601387`
(commit `4fa6469`): all eight wheel lanes reported `1265 passed, 2 skipped`.
Two post-implementation fixes were required — `ab1d507` restored the V8 15.0
`ExternalPointerTypeTag` argument on `External::New` / `External::Value`, and
`4fa6469` corrected the tests to set ids through `setAttribute`. See
`docs/m11_5_mutation_observer.md` and `docs/m11_5_report.md`.

### 12. M11-6..M11-10 verified as one five-slice batch

Decided/verified `2026-10-28`.

To amortize the cold V8 build, M11-6 (event subclasses), M11-7 (Web Storage),
M11-8 (Blob/File/FileReader), M11-9 (event cancellation), and M11-10
(classList/toggleAttribute) were developed and committed on one branch
(`m11-6-event-subclasses`). The first run cold-built V8 and saved the branch
cache; the final commit `35c85fe` was then verified with cached runs: Linux
`40661574145`, Windows `40661580891`, each wheel lane `1352 passed, 2 skipped`.
All five new test files ran. Contract guards that had frozen the old surface
were refreshed as part of the batch.

### 13. M11-11 delivered event capture and listener options

Decided/verified `2026-10-28`.

The event model now has a capturing phase (window → document → ancestors),
`eventPhase` 0/1/2/3, and `capture` / `once` / `passive` listener options, with
`(type, callback, capture)` add/remove identity. Capture runs even for
non-bubbling events; `once` removes before invocation and `passive` suppresses
`preventDefault` for that listener. Verified on Linux `40683001239` and Windows
`40683010032` (commit `a269aa9`), each wheel lane `1370 passed, 2 skipped`.

## Pending Decisions

These require an explicit human or project-level choice before large work begins.

### A. Which concrete compatibility slice becomes `M11-12`?

Settled and implemented so far:

- `M11-1` — the **configurable host-environment baseline**
  (`Page(environment=...)` / `Page.get_default_environment()`, covering the `navigator`
  identity values, `screen`, and the `window` dimensions). See
  `docs/m11_1_host_environment.md`.
- `M11-2` — **`requestAnimationFrame` / `cancelAnimationFrame`** with the manual
  `Page.run_animation_frame()` pump. See `docs/m11_2_animation_frames.md`.
- `M11-3` — **`requestIdleCallback` / `cancelIdleCallback`** with the manual
  `Page.run_idle_callbacks()` pump. `options.timeout` is validated but deliberately
  not implemented. See `docs/m11_3_idle_callbacks.md`.
- `M11-4` — **`queueMicrotask(callback)`** on the existing manual `Page.run_jobs()`
  microtask pump. No new Python API and no fifth pump; a non-callable argument throws a
  JS `TypeError`; callbacks are FIFO and interleave with `Promise` reactions. Landed and
  verified on real V8 (Linux + Windows) on `2026-10-27`: Linux run `40471874328`,
  Windows run `40483069548`, commit `0305b49`, each `1213 passed, 2 skipped`. See
  `docs/m11_4_queue_microtask.md` and `docs/m11_4_report.md`.

- `M11-5` — **`MutationObserver`** — selected on `2026-10-28` from the ranked
  backlog (`docs/m11_compatibility_backlog.md`); boundary document
  `docs/m11_5_mutation_observer.md`. Landed and verified on real V8 (Linux +
  Windows) on `2026-10-28`: Linux run `40632597636`, Windows run `40632601387`,
  commit `4fa6469`, each `1265 passed, 2 skipped`. See `docs/m11_5_report.md`.
- `M11-6` — **Event subclass cluster** (`CustomEvent` / `MouseEvent` /
  `KeyboardEvent` / `FocusEvent`) — selected on `2026-10-28` from rank 2 of the
  backlog; boundary document `docs/m11_6_event_subclasses.md`. Verified in the
  five-slice batch: Linux run `40661574145`, Windows run `40661580891`, commit
  `35c85fe`, each `1352 passed, 2 skipped`. See `docs/m11_6_report.md`.
- `M11-7` — **Web Storage** (`localStorage` / `sessionStorage`, per-Page
  in-memory) — selected on `2026-10-28` from rank 3; boundary document
  `docs/m11_7_web_storage.md`. Verified in the same batch; see
  `docs/m11_7_report.md`.
- `M11-8` — **`Blob` / `File` / `FileReader`** — selected on `2026-10-28` from
  rank 4; boundary document `docs/m11_8_blob_file_reader.md`. Verified in the
  same batch; see `docs/m11_8_report.md`.
- `M11-9` — **Event cancellation** (`cancelable` / `defaultPrevented` /
  `preventDefault` / `stopImmediatePropagation`) — selected on `2026-10-28`
  from the gap inventory; boundary document
  `docs/m11_9_event_cancellation.md`. Verified in the same batch; see
  `docs/m11_9_report.md`.
- `M11-10` — **`classList` / `toggleAttribute`** — selected on `2026-10-28`
  from the gap inventory; boundary document `docs/m11_10_class_list.md`.
  Verified in the same batch; see `docs/m11_10_report.md`.
- `M11-11` — **Event capture phase + listener options** (`capture` / `once` /
  `passive`, `eventPhase`) — selected on `2026-10-28` from the gap inventory;
  boundary document `docs/m11_11_event_capture.md`. Verified on real V8
  (Linux + Windows) on `2026-10-28`: Linux run `40683001239`, Windows run
  `40683010032`, commit `a269aa9`, each `1370 passed, 2 skipped`. See
  `docs/m11_11_report.md`.

`M11-6` through `M11-10` were delivered as one five-slice batch on the
`m11-6-event-subclasses` branch, with one cold V8 build reused by the cached
verification run. `M11-11` followed with the master cache warmed first. The
next slice (`M11-12`) should again be chosen from this backlog rather than
direction alone.

A related question this slice raises: the runtime still has **four** independent manual
pumps (`run_timers` / `run_jobs` / `run_animation_frame` / `run_idle_callbacks`);
`queueMicrotask` deliberately feeds `run_jobs` instead of adding a fifth. Whether the
four should eventually gain a single composed driver is a real design decision,
deliberately deferred — see `docs/m11_3_report.md` "Open questions".

### B. Should `Page` gain a unified public contract document?

Not yet decided. `docs/runtime_surface.md` (added `2026-08-06`) now indexes the `Page`
surface but is explicitly **not** a contract — it freezes nothing. So the choice
stands: keep the milestone-summary approach, or promote a single frozen `Page`
contract alongside `api_contract.md`.

### C. Should Windows become a first-class release target again?

Build and CI support exist in some form, but release priority should be confirmed
before major platform-specific investment.
