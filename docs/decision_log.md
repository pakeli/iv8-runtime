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

## Pending Decisions

These require an explicit human or project-level choice before large work begins.

### A. Which concrete compatibility slice becomes `M11-5`?

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

`M11-5` — **`MutationObserver`** — was selected on `2026-10-28` from the new ranked
backlog (`docs/m11_compatibility_backlog.md`); its boundary document is
`docs/m11_5_mutation_observer.md`. Implementation and real-V8 CI verification are in
progress on the `m11-5-mutation-observer` branch. The backlog now exists, so the
next choice (`M11-6`) should use it rather than direction alone.

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
