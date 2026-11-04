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

- Latest internal work, verified (`2026-11-04`): the second Page
  extraction moves Cookie parsing/storage into `CookieJar`, owned by value in
  `PageState`; existing delegates preserve the DocumentHost coercion/trace path.
  Source `347dfbf` passed Linux `41764895229`, Windows `41764904132`, and macOS
  arm64 `41764990337`, Python 3.11–3.14 each `1793 passed, 2 skipped`. The
  Cookie file collects 11 tests (macOS focused runs all `11 passed`), full suite
  1,795. V8 CI `41764857600` also passed. See `docs/page_cookie_refactor.md`
  for source trees, artifacts, and the native differential/UBSan results.
  M11-53 remains the next feature milestone.
- Internal maintenance after M11-52: selector parsing is extracted from
  `page_state.cpp` into `selector_parser.h` / `selector_parser.cpp`; DOM matching
  and resource ownership stay unchanged. `.github/workflows/v8-ci.yml` adds
  automatic Linux/Python 3.13 real-V8 checks alongside skeleton CI. **Verified
  `2026-11-04`** on source `917cbfb`: Linux `41759793220`, Windows
  `41759812090`, macOS arm64 `41760140836`, each Python 3.11–3.14 lane
  `1787 passed, 2 skipped`; V8 CI `41674273069` also passed. The merge policy
  requires `Linux x64 / Python 3.13 / V8`. macOS used macos-15 with an explicit
  source checkout and the existing release-branch cache; temporary configuration
  was restored. See `docs/page_refactor.md`. M11-53 is the next feature slice.
- The repository started from an M1-only architecture and API contract.
- The implementation at `HEAD` is **well beyond M1**.
- Release lanes: Linux x64, Windows x64, and macOS arm64 (`macos-15` via
  `tools/build_v8_macos.sh` + `.github/workflows/release-macos.yml`) all build
  and test V8-linked wheels. macOS wheels are tagged `macosx_11_0_arm64`;
  Intel/x64 and universal2 are not built.
- As of `2026-11-03`, the execution order is **Window-first: Window is
  prioritized above Document** (`docs/next_tasks.md`). The latest verified
  milestone is **M11-52** (Window messaging — `postMessage` with structured
  clone and bounded target origins, async FIFO timer delivery, and
  `MessageEvent`), green on real V8 Linux + Windows + macOS arm64 (tree
  `bc00040`, merged to `master` on the fork; upstream sync out of scope). It
  follows **M11-51** (Window event handler attributes, tree `34b119b`),
  **M11-50** (Window scroll/focus/UI completion, tree `6d2250e`), **M11-49**
  (HTML string APIs and variadic tree methods, tree `52bafab`), **M11-48**
  (Node foundation, tree `3f4940f`), **M11-47** (Document identity, tree
  `cc1c47d`), **M11-46** (`document.cookie`, tree `18f1f4b`), and
  **M11-41..M11-45** (BOM viewport/media/device metadata, commit `61abc3b`).
  The M11 line is compatibility-first.
- A post-M11 monitoring slice is also verified: **host access trace steps 1–4
  plus the step-5 native BOM call, window metadata, nested metadata, static
  visualViewport, navigator.storage manager, navigator.permissions,
  matchMedia-result, legacy-navigator, and window-alias groups — the complete
  step-5 object coverage — plus steps 6a–6h (summaries, method references,
  delete/enumeration/define, explicit query wrappers, Reflect target metadata,
  native summaries, and exception metadata) and the step-7 aggregated
  ``Page.access_trace_report()`` workflow**
  (`Page.start_access_trace` / `stop_access_trace` / `read_access_trace`, native
  get/set/call, HostObject missing-string-key `get_probe`,
  performance/crypto/Storage callback calls, window metadata accessors,
  connection/orientation/timeOrigin reads, visualViewport metadata/methods,
  navigator.storage parent reads plus manager calls, permissions query plus
  PermissionStatus accesses, matchMedia result reads/method calls, legacy
  navigator metadata, window alias get/set events, query-result completions, and
  exception name/message/location/stack), green on real V8 at commit `bd148c2`;
  step 5 object coverage and steps 6a–6h are complete for the traceable
  HostObject surface; step 7 is verified at commit `c678ede` (release-tested
  tree `62af2e6`) with `Page.access_trace_report()` and the workflow in
  `docs/host_access_trace_workflow.md` (Linux/Windows release lanes plus a
  macos-14 fallback macOS arm64 release build). The `in` operator, non-HostObject query
  targets, cached wrappers, asynchronous exception metadata, and general
  call-site timing remain documented blind spots.
- Do not assume `docs/api_contract.md` describes the full current product surface;
  it is still the source of truth for the original M1 `JSContext` contract.
- `docs/runtime_surface.md` is the overview of what exists now, with a milestone map
  and links to every frozen boundary. Read it before concluding a feature is missing.
- The M1-era documents (`api_contract.md`, `architecture.md`, `implementation_plan.md`,
  `test_plan.md`) each carry a scope note at the top explaining what part of them is
  still authoritative and what is historical.

## Working Rules

- Keep changes small and milestone-aligned.
- Window work is prioritized above Document continuation: pick the next Window
  slice unless the user explicitly redirects to Document.
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
  - M11 compatibility slices through `M11-52`: host environment, rAF, idle
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
    single-context window metadata, `scheduler.postTask`), the BOM
    viewport/media/device batch (`visualViewport`, bounded `matchMedia`,
    `navigator.storage` / `permissions`, legacy navigator metadata), the
    deterministic `document.cookie` jar (M11-46), Document identity (M11-47),
    the M11-48 node foundation (Text/Comment/DocumentFragment, live text), the
    M11-49 HTML string/variadic DOM APIs, the M11-50 Window scroll/focus/UI
    stubs, the M11-51 Window event handler attributes, and the M11-52 Window
    messaging surface — verified on real V8 Linux + Windows + macOS arm64;
    latest verification `2026-11-03`;
  - latest implemented and verified compatibility feature: M11-52 (Window
    messaging);
  - latest verified post-M11 monitoring slice: host access trace steps 1–4 +
    step-5 native BOM calls, window metadata, nested metadata, static
    visualViewport, navigator.storage manager, navigator.permissions,
    matchMedia results, legacy navigator metadata, and window aliases (native
    HostObject get/set/call, missing-property probes, performance/crypto/Storage
    calls, window metadata/alias accessors, connection/orientation/timeOrigin,
    visualViewport metadata/methods, navigator.storage parent reads plus manager
    calls, permissions query/status accesses, matchMedia result accesses,
    plugins/mimeTypes/javaEnabled, parent/top/frames/opener get/set, steps 6a–6h
    (summaries, method references, delete/enumeration/define, explicit
    has/descriptor query wrappers, Reflect target metadata, native summaries,
    and synchronous exception metadata), and the step-7 aggregated
    `Page.access_trace_report()` workflow; see `docs/host_access_trace.md` and
    `docs/host_access_trace_workflow.md`).

## Handoff Discipline

After completing meaningful work, keep the handoff set coherent. A verified
milestone should update **all** of the applicable files below, not just one:

- the milestone boundary doc and `docs/m<major>_<n>_report.md` (verification
  evidence: trees, run IDs, artifact IDs, lane counts);
- `docs/current_status.md` (latest verified vs latest implemented/pending,
  test counts, run IDs, open questions, practical verdict);
- `docs/next_tasks.md` (current execution order and the completed priority
  section, including the next handoff);
- `docs/decision_log.md` (scope decision, priority changes, verification);
- `docs/runtime_surface.md` (surface index and milestone map);
- `CLAUDE.md` (reality check and “what stage is the project in” answers);
- `python/iv8/__init__.py` docstring plus `docs/dom_bom_gaps.md`,
  `docs/host_access_trace.md`, and `docs/dev_setup.md` when the slice touches
  those surfaces.

Rules:

- Only mark a milestone **verified** after every release lane is green; record
  the exact tree, run IDs, artifact IDs, and `passed/skipped` counts.
- Separate **latest verified** from **latest implemented, verification
  pending**; never let a pending slice read as verified.
- Update test-item counts only from actual collected/passed output.
- Before committing, sweep for stale `next`, `pending`, old milestone
  numbering, and superseded counts in the files above.
- Use path-limited commits and never include user-owned files
  (`test_dev_tool.py`, `.venv-iv8/`, `demo.py`, or untracked wheels).

This prevents the next machine or model from losing context again.
