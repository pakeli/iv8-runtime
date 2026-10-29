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
`characterData` is accepted but inert because the DOM is element-only. The
missing `id` / `className` setters were later added by M11-12; those writes now
queue the same attribute records as `setAttribute`.

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

### 14. M11-12..M11-17 verified as one six-slice DOM/web batch

Decided/verified `2026-10-28`.

The batch (element attribute API, tree editing, cloneNode/equality,
`URL.searchParams` snapshot, select extension, nested environment +
`get_defaults()`) was verified on real V8 at commit `8d606e9`: Linux
`40695004440`, Windows `40695029530`, each wheel lane `1378 passed, 2 skipped`.
The first attempt at `0c47ecb` built every wheel but failed 15 historical
frozen-surface guards; refreshing those guards in `8d606e9` was the only
change needed. The full suite collects 1380 items (1370 previous + 8 batch
tests).

### 15. M11-18 delivered form-control attribute reflection

Decided/verified `2026-10-28`.

M11-18 adds a minimal reflected surface: input `type` / `disabled` /
`readOnly` / `placeholder`, button `type` / `disabled`, `form.encoding` (an
alias of the existing `.enctype` runtime slot), and `option.label` / read-only
`index`. Reflected writes are real attribute writes and feed the existing
MutationObserver attribute hooks; `form.encoding` changes only the runtime
slot. No Python API, validation, or form-submission behavior was added.

Verified on real V8 at commit `08350ce`: Linux `40698705892`, Windows
`40698728631`, each wheel lane `1386 passed, 2 skipped`. The first verification
run was green. The affected historical form guards were refreshed in the same
change. See `docs/m11_18_form_control_reflection.md` and
`docs/m11_18_report.md`.

### 16. M11-19..M11-23 verified as one five-slice DOM/form batch

Decided/verified `2026-10-28`.

The batch adds multi-token class queries and element-level
`getElementsByClassName`; `document.getElementsByName`; extended form reflection;
select `length` / `item` / `namedItem`; and deterministic document metadata
including `hasFocus()`. All five are JS-side only and add no architecture
surface. Boundary: `docs/m11_19_23_dom_form_batch.md`.

Verified on real V8 at commit `999c851`: Linux `40704330688`, Windows
`40704362549`, each wheel lane `1412 passed, 2 skipped`. The first attempt
(`9f3e459`) failed one historical element-query guard; refreshing it in
`999c851` was the only change needed. The full suite collects 1414 items
(1388 previous + 26 batch tests). See `docs/m11_19_23_report.md`.

### 17. M11-24..M11-28 verified as one host environment / console batch

Decided/verified `2026-10-28`.

The batch extends the configurable environment with navigator metadata and
identity fields, screen color/pixel depth, and outer window geometry, then adds
deterministic console methods. The flat environment grows from 11 to 27 keys;
existing keys/defaults are unchanged and no new Python method or setter exists.

Verified on real V8 at commit `a00e140`: Linux `40711526339`, Windows
`40711548915`, each wheel lane `1474 passed, 2 skipped`. The first attempt
(`f857d03`) failed one fresh-array test, fixed in `a00e140`. The full suite
collects 1476 items (1414 previous + 62 from the extended environment matrix
and 22 new tests). See `docs/m11_24_28_report.md`.

### 18. M11-29 delivered the deterministic event-loop facade

Decided/verified `2026-10-29`.

M11-29 adds `Page.tick()`, `advance(ms, max_steps)`, `run_until_idle(max_steps)`
and the read-only `event_loop_time_ms` over the existing timer and microtask
queues. Timers are ordered by virtual due time and registration sequence;
microtasks drain after each task; intervals use a 1ms virtual floor and bounded
steps. The legacy pumps and rAF/idle remain unchanged and are not run by the
facade in this phase.

Verified on real V8 at commit `b211481` (feature commit `0bc75a2`): Linux
`40820152813`, Windows `40820163797`, each wheel lane `1495 passed, 2 skipped`.
The first attempt (`0bc75a2`) failed three stale pump non-goal guards, refreshed
in `b211481`. The full suite collects 1497 items (1476 previous + 21 tests). See
`docs/m11_29_report.md`.

## Pending Decisions

These require an explicit human or project-level choice before large work begins.

### A. Which concrete compatibility slice becomes `M11-30`?

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
verification run. `M11-11` followed with the master cache warmed first.

`M11-12` through `M11-17` are a six-slice DOM/web completeness batch selected
on `2026-10-28`: element attribute API, tree-edit conveniences,
`cloneNode`/equality, `URL.searchParams` snapshot, select extension, and nested
environment + `get_defaults()`. Boundary: `docs/m11_12_17_dom_web_batch.md`.
The batch is delivered and verified (Linux `40695004440`, Windows `40695029530`,
commit `8d606e9`, each wheel lane `1378 passed, 2 skipped`). After it lands, the
next slice (`M11-18`) should again be chosen from this backlog rather than
direction alone.

- `M11-18` — **Form-control attribute reflection** — selected on `2026-10-28`
  from the gap inventory (the ranked backlog's remaining entries need separate
  architecture decisions): input `type` / `disabled` / `readOnly` /
  `placeholder`, button `type` / `disabled`, `form.encoding`, and
  `option.label` / `index`. Boundary:
  `docs/m11_18_form_control_reflection.md`. Verified on real V8 (Linux
  `40698705892`, Windows `40698728631`, commit `08350ce`, each wheel lane
  `1386 passed, 2 skipped`); see `docs/m11_18_report.md`.

- `M11-19` through `M11-23` — **DOM/form completeness batch** — selected
  `2026-10-28` from the gap inventory: multi-token class queries and
  element-level `getElementsByClassName`, `document.getElementsByName`,
  extended form reflection, select `length` / `item` / `namedItem`, and
  deterministic document metadata. Boundary:
  `docs/m11_19_23_dom_form_batch.md`. Verified on real V8 (Linux
  `40704330688`, Windows `40704362549`, commit `999c851`, each wheel lane
  `1412 passed, 2 skipped`); see `docs/m11_19_23_report.md`.

- `M11-24` through `M11-28` — **host environment / console batch** — selected
  `2026-10-28`: navigator metadata/identity, screen depth, outer window
  geometry, and console method extensions. Boundary:
  `docs/m11_24_28_host_environment_batch.md`. Verified on real V8 (Linux
  `40711526339`, Windows `40711548915`, commit `a00e140`, each wheel lane
  `1474 passed, 2 skipped`); see `docs/m11_24_28_report.md`.

- `M11-29` — **deterministic event-loop facade** — selected/frozen on
  `2026-10-29`: `tick` / `advance` / `run_until_idle` / `event_loop_time_ms`
  over timers and microtasks. Boundary:
  `docs/m11_29_event_loop_facade.md`. Verified on real V8 (Linux
  `40820152813`, Windows `40820163797`, commit `b211481`, each wheel lane
  `1495 passed, 2 skipped`); see `docs/m11_29_report.md`. The next slice
  (`M11-30`) is the natural rAF/idle integration or another gap-inventory item.

A related question this slice raises: the runtime still has **four** independent manual
pumps (`run_timers` / `run_jobs` / `run_animation_frame` / `run_idle_callbacks`);
M11-29 composes timers + microtasks only. Whether rAF/idle should join the same
facade is now the next design choice — see `docs/m11_3_report.md` "Open questions".

### B. Should `Page` gain a unified public contract document?

Not yet decided. `docs/runtime_surface.md` (added `2026-08-06`) now indexes the `Page`
surface but is explicitly **not** a contract — it freezes nothing. So the choice
stands: keep the milestone-summary approach, or promote a single frozen `Page`
contract alongside `api_contract.md`.

### C. Should Windows become a first-class release target again?

Build and CI support exist in some form, but release priority should be confirmed
before major platform-specific investment.
