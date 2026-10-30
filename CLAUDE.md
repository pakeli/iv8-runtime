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
- As of `2026-10-30`, the latest verified milestone is **M11-41..M11-45** (BOM
  viewport/media/device metadata), green on real V8 Linux + Windows (commit
  `61abc3b`, merged to `master` on the fork; upstream sync out of scope). The
  M11 line is compatibility-first.
- A post-M11 monitoring slice is also verified: **host access trace steps 1–4
  plus the step-5 native BOM call, window metadata, nested metadata, static
  visualViewport, navigator.storage manager, navigator.permissions, and
  matchMedia-result groups**
  (`Page.start_access_trace` / `stop_access_trace` / `read_access_trace`, native
  get/set/call, HostObject missing-string-key `get_probe`,
  performance/crypto/Storage callback calls, window metadata accessors,
  connection/orientation/timeOrigin reads, visualViewport metadata/methods,
  navigator.storage parent reads plus manager calls, permissions query plus
  PermissionStatus accesses, and matchMedia result reads/method calls), green on
  real V8 at commit `7d35a30`; the remaining step-5 groups and
  value/argument/return summaries remain pending.
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
- When a slice expands the JS/Python surface, grep `test/` for every newly added
  member name and refresh the historical frozen-surface guards in the same
  change. Full runtime tests only run in CI, so a missed guard shows up as a red
  verification run even though the wheel build succeeded.

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
  - M11 compatibility slices through `M11-45`: host environment, rAF, idle
    callbacks, `queueMicrotask`, `MutationObserver`, event subclasses, Web
    Storage, Blob/File/FileReader, event cancellation,
    `classList`/`toggleAttribute`, event capture + listener options, element
    attribute/tree helpers, clone/equality, `URL.searchParams`, select
    extension, nested environment + `get_defaults()`, form-control attribute
    reflection, query/form/document completeness, host metadata/console
    extensions, the deterministic event-loop facade over timers, microtasks,
    animation frames, and idle callbacks, the virtual performance clock with
    rAF timestamps, minimal form validation, OS-backed Crypto random, the CSS
    selector engine, deterministic BOM metadata, the BOM completion batch
    (deterministic dialogs, performance entry timeline, `structuredClone`,
    single-context window metadata, `scheduler.postTask`), and the BOM
    viewport/media/device batch (`visualViewport`, bounded `matchMedia`,
    `navigator.storage` / `permissions`, legacy navigator metadata) — verified
    on real V8 Linux + Windows; latest verification `2026-10-30`;
  - latest implemented and verified compatibility feature: M11-41..M11-45
    (BOM viewport/media/device metadata);
  - latest verified post-M11 monitoring slice: host access trace steps 1–4 +
    step-5 native BOM calls, window metadata, nested metadata, static
    visualViewport, navigator.storage manager, navigator.permissions, and
    matchMedia results (native HostObject get/set/call, missing-property probes,
    performance/crypto/Storage calls, window accessors,
    connection/orientation/timeOrigin, visualViewport metadata/methods,
    navigator.storage parent reads plus manager calls, permissions query/status
    accesses, and matchMedia result accesses; see
    `docs/host_access_trace.md`).

## Handoff Discipline

After completing meaningful work, update at least one of:

- `docs/current_status.md`
- `docs/next_tasks.md`
- `docs/decision_log.md`

This prevents the next machine or model from losing context again.
