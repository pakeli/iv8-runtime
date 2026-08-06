# Decision Log

Last reviewed: `2026-08-06`

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

As of `2026-08-06`, the latest landed milestone at `HEAD` is treated as `M10-2`.

This decision exists so future sessions do not restart from outdated M1-only
assumptions.

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

## Pending Decisions

These require an explicit human or project-level choice before large work begins.

### A. What is the next planned milestone after M10-2?

Not yet recorded in the repository.

### B. Should `Page` gain a unified public contract document?

Not yet decided. `docs/runtime_surface.md` (added `2026-08-06`) now indexes the `Page`
surface but is explicitly **not** a contract — it freezes nothing. So the choice
stands: keep the milestone-summary approach, or promote a single frozen `Page`
contract alongside `api_contract.md`.

### C. Should Windows become a first-class release target again?

Build and CI support exist in some form, but release priority should be confirmed
before major platform-specific investment.
