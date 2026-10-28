# CLAUDE.md

This file is the primary handoff entrypoint for Claude (and other coding agents)
working on `iv8-runtime` after repository migration or chat-history loss.

## Start Here

Before making changes, read these files in order:

1. `AGENTS.md`
2. `docs/current_status.md`
3. `docs/runtime_surface.md` — the index of the surface that actually exists at `HEAD`
4. `docs/next_tasks.md`
5. `docs/api_contract.md` — M1 `JSContext` only
6. `docs/architecture.md`
7. `docs/decision_log.md`
8. `docs/dev_setup.md`

Then scan the latest 20 commits with:

```powershell
git log --oneline --decorate -n 20
```

## Project Reality Check

- The repository started from an M1-only architecture and API contract.
- The implementation at `HEAD` is **well beyond M1**.
- As of `2026-10-28`, the latest verified milestone is **M11-10**
  (`classList` / `toggleAttribute`), green on real V8 Linux + Windows as part
  of the M11-6..M11-10 five-slice batch (commit `35c85fe`, merged to `master`
  on the fork; upstream sync out of scope). The M11 line is compatibility-first.
- Do not assume `docs/api_contract.md` describes the full current product surface;
  it is still the source of truth for the original M1 `JSContext` contract.
- `docs/runtime_surface.md` is the overview of what exists now, with a milestone map
  and links to every frozen boundary. Read it before concluding a feature is missing.
- The M1-era documents (`api_contract.md`, `architecture.md`, `implementation_plan.md`,
  `test_plan.md`) each carry a scope note at the top explaining what part of them is
  still authoritative and what is historical.

## Working Rules

- Keep changes small and milestone-aligned.
- Do not expand scope casually.
- Do not add browser/runtime features that are not already planned or explicitly
  approved.
- Preserve the separation between:
  - process-wide V8 runtime ownership;
  - per-context isolate ownership;
  - `Page` host-object / DOM-like runtime layers;
  - second-layer DevTools / monitoring features.
- Treat `docs/current_status.md` as the best snapshot of real implementation
  status after the repo move.

## Expected Session Flow

When starting a fresh task:

1. Summarize the current milestone and affected subsystem.
2. State whether the task touches M1 core, first-layer `Page` runtime, or
   second-layer monitoring.
3. Check whether any public contract or milestone doc must be updated first.
4. Make the smallest code and test change that satisfies the requested task.
5. Run focused tests before broader validation.
6. Update `docs/current_status.md` and `docs/next_tasks.md` if the task changes
   project status or priorities.

## If Asked “What Stage Is The Project In?”

Answer roughly as follows:

- M1 contract still exists and matters for `JSContext`.
- Real implementation has progressed through M2–M11.
- Current `HEAD` includes:
  - `JSContext` lifecycle / eval / conversion / errors;
  - `Page` + minimal DOM / scripts / lifecycle / forms / URL utilities;
  - DevTools attach base and multiple watch/monitor APIs;
  - M11 compatibility slices through `M11-10`: host environment, rAF, idle
    callbacks, `queueMicrotask`, `MutationObserver`, event subclasses, Web
    Storage, Blob/File/FileReader, event cancellation, and
    `classList`/`toggleAttribute` (verified on real V8 Linux + Windows; latest
    verification `2026-10-28`);
  - latest implemented and verified feature: `classList` / `toggleAttribute`
    from M11-10.

## Handoff Discipline

After completing meaningful work, update at least one of:

- `docs/current_status.md`
- `docs/next_tasks.md`
- `docs/decision_log.md`

This prevents the next machine or model from losing context again.
