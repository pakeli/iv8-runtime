# CLAUDE.md

**Page source split stage 1.1 implemented (`2026-11-14`).** Source `a3dcb20` (tree `ca1c0c7d0618a8c88b9191b67159de18add43206`) extracts crypto, Base64 globals and encoding, with shared ASCII and trace helpers in `internal`. `page_state.cpp` is **12,637 lines**. The move audit reports **431 identical definitions, 0 differences**. Local real-V8 tests and required Linux CI passed **2090 tests, 2 skipped, 1 xfailed**, plus 56 audit checks. The full stage 1 release matrix follows the remaining leaf-module slices. Next is stage 1.2 (URL and structured clone). Evidence is in `docs/page_state_split.md`.

**Page source split preparation verified (`2026-11-14`).** Stage 0 source `973ac99` (tree `6751ff9c9f419d9b67054677744eef8a3d4f4646`) passed 56 audit checks and all 12 native wheel/test lanes, each **2090 passed, 2 skipped, 1 xfailed**. Linux and Windows used hosted release CI. macOS arm64 used a local four-version release matrix after hosted runners reported capacity exhaustion. Stage 1 continues with the approved leaf-module slices. Evidence and artifacts are in `docs/page_state_split.md`. M11-53 remains the next numbered feature.

**Browser compatibility development rule (`2026-11-07`).** Match each API's
presence, implementation kind, shape and behavior to the reference browser.
Browser-native methods in approved API slices require native bindings. Derive
names, arities, native source, construction, own `prototype`, descriptors,
inheritance, receivers and identity from per-API browser evidence, and test those
expectations. The combination **anonymous native + constructable + own
`prototype`** reproduced Customs HTTP 400 for the affected methods. Follow
`AGENTS.md` "Browser Compatibility Rules" and `docs/native_binding_regression.md`
under "Native method binding attribution".

**Latest verified open exposure regression repair (`2026-11-07`).** The default
Window open stub is withdrawn. The other Web IDL corrections remain available.
Source `f882344` (tree `c4ba8bee511316e69d1f8f58e865c036a6cec56c`)
passed Linux `42350637889`, Windows `42350641803` and macOS arm64
`42350648456` (macos-15), Python 3.11–3.14 each **2050 passed, 2 skipped,
1 xfailed**. The immutable Chrome probe has 74 matches and one declared
unavailable-open profile gap. macOS focused checks each passed 172 tests with
that one expected failure. Installed cp311 acceptance returned NMPA document
200 and data business code 200 with 10 rows. The original Customs caller
returned 200 in three independent runs after adding its missing second-hop
Referer. A fourth run confirmed the business title. Scope, causal evidence and release records are in
`docs/native_binding_regression.md`.

**Previous verified Web IDL environment repair (`2026-11-07`).** ENV-01 through
ENV-06 now cover native method metadata, receiver brands, shared interface
inheritance and defining-prototype descriptors. Native standalone object state,
document-root mutation notifications and explicit offline capability behavior
are included. Source `f42acae` (tree
`3d2d9351d45815e85f53f0f91fa4ad805fff49f8`) passed Linux `42326713519`,
Windows `42326717311` and macOS arm64 `42326726715` (macos-15), Python
3.11–3.14 each **2050 passed, 2 skipped** (2,052 collected). macOS focused
checks each **189 passed**. V8 CI `42326682563` / `42326787518` passed.
The installed cp311 wheel matched the user's original capture **75/75** and
the previous browser report **27/27**. Boundary, artifact IDs, helper restoration
and deployment evidence are in `docs/webidl_environment_semantics.md` and
`docs/webidl_environment_report.md`. M11-53 remains the next numbered feature.

**Previous verified browser compatibility repair (`2026-11-06`):** reflected
meta/script/anchor properties, stable node wrappers, live tag/class collections,
IDL accessors/brands, trailing-body recovery, document tree edits, document.all,
bounded capability behavior, offline navigation intents and opt-in async errors.
Source `15edd82` (tree `905d1f671bc31fd25b2aee78c13258a6318ec36a`) passed
Linux `42064333308`, Windows `42064342679`, and macOS arm64 `42064322136`
(macos-14), Python 3.11–3.14 each **1931 passed, 2 skipped** (1,933 collected).
V8 CI `42064100130` passed; macOS focused runs each **53 passed**. The original
independent report probe passed **27/27** against the installed final wheel.
Contract, artifact IDs, source/driver restoration and remaining limits:
`docs/browser_compatibility_repair.md`. M11-53 focus/visibility remains the next
numbered feature milestone; actual navigation and networking remain absent.

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

## Git and Pull Request Workflow

Configured for local Claude Code on `2026-11-08`.

This setup applies to the terminal-based Claude Code CLI and Desktop's Code tab
with a Local environment opened on `/Users/lil/Desktop/iv8-runtime`. In Desktop,
select the `</>` Code entry, choose Local, and open that project folder before
starting a session. Chat/Cowork sessions and cloud sessions have their own command
execution environments and GitHub access. A folder attached to a Chat/Cowork
session provides that session's configured file access, while shell execution
and repository authorization depend on its execution environment.

1. Read `AGENTS.md` and inspect `git status` and the existing diff before editing.
   Keep unrelated work separate and stage the specific files belonging to the task.
2. Work on a task branch or an isolated worktree. Run focused tests first and the
   remaining verification required by the approved task and repository contracts.
3. When the user requests publishing, commit the reviewed task files, push the
   branch to `origin`, and create a pull request targeting `master` with the change
   summary and actual verification results.
4. Check the PR's latest head and required CI. The protected `master` branch
   requires `Linux x64 / Python 3.13 / V8` to pass. Complete any additional release
   lanes required by the task before claiming the work verified.
5. When task authorization includes merging, merge the verified PR head using
   `--match-head-commit` and the repository's normal protected-branch workflow.
6. Report the branch, commit SHA, PR URL, test results and CI state to the user.

The local `.claude/settings.local.json` grants the corresponding commit, push and
PR tool permissions and allows GitHub network access in the sandbox. Git uses
the authenticated GitHub CLI credential helper for `github.com`. Local settings
are excluded through `.git/info/exclude`.

## Project Reality Check

- Previous monitoring presentation addition, verified (`2026-11-04`):
  `Page.read_access_trace(clear=True, format="text")` renders concise events and
  captured values. Default raw dictionaries and step-7 aggregation are unchanged.
  Source `1599fd5` passed Linux `41848358166`, Windows `41848373389`, and macOS
  arm64 fallback `41850244647` (macos-14), Python 3.11–3.14 each
  `1878 passed, 2 skipped` (1,880 collected). V8 CI `41848183724` passed;
  focused macOS checks each `79 passed`, including all five native integrations.
  See `docs/access_trace_text.md` for evidence and driver restoration. M11-53
  remains the next browser feature milestone.
- Previous C++ extraction maintenance, verified (`2026-11-04`): HTML entity
  decoding and text/attribute escaping are extracted into `html_entities`;
  DOM resources, parse contexts, and traversal stay in Page. Source `85b2d20`
  passed Linux `41828654174`, Windows `41828670556`, and macOS arm64
  `41828851448`, Python 3.11–3.14 each `1822 passed, 2 skipped` (1,824 collected).
  macOS focused runs each `63 passed`; V8 CI `41828571144` passed. Source/run/
  artifact and 20,070-case differential/UBSan evidence is in
  `docs/page_html_entities_refactor.md`. M11-53 remains next.
- Previous internal maintenance, verified (`2026-11-04`): Base64 byte codec
  is extracted into `base64`; host coercion/Latin-1 validation/error conversion
  stay in place. Source `10151d4` passed Linux `41797209223`, Windows
  `41797219218`, and macOS arm64 `41797389405`, Python 3.11–3.14 each
  `1816 passed, 2 skipped` (1,818 collected). macOS focused runs each `32 passed`;
  V8 CI `41797126420` passed. Independent source/artifact and 14,955-case
  differential/UBSan/reference-encoding evidence is in
  `docs/page_base64_refactor.md`. M11-53 remains the next feature milestone.
- Previous internal maintenance, verified (`2026-11-04`): query byte codec
  and ordered parse/serialize are extracted into `query_parameters`; canonical
  V8 private-slot storage and host coercion/mutation stay in place. Focused
  query/URL set passed all 54 items in each macOS interpreter before the full
  suite. Source `f017a01` passed Linux `41791203775`, Windows `41791210361`, and
  macOS arm64 `41791328195`, Python 3.11–3.14 each `1806 passed, 2 skipped`
  (1,808 collected). V8 CI `41791168585` also passed. Independent source/run/
  artifact evidence and 20,271-case native differential/UBSan results are in
  `docs/page_query_refactor.md`. M11-53 stays the next feature milestone.
- Previous internal maintenance, verified (`2026-11-04`): minimal URL
  decomposition/resolution is extracted into `url_parser`; the host layer keeps
  JS coercion/tracing and secure-context policy. The focused URL/consumer set
  passed all 76 items in each macOS interpreter before the full suite. Source
  `2e30d48` passed Linux `41783823684`, Windows `41783830832`, and macOS arm64
  `41783905526`, Python 3.11–3.14 each `1799 passed, 2 skipped` (1,801 collected).
  V8 CI `41783767829` also passed. See `docs/page_url_refactor.md` for artifacts
  and the 15,224-case native differential/UBSan results. M11-53 stays next.
- Previous internal maintenance, verified (`2026-11-04`): the second Page
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
