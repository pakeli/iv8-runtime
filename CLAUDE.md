# CLAUDE.md

**Latest implemented candidate (`2026-11-28`, local macOS arm64 / Python 3.13 verified; cross-platform release pending): BOM-10 virtual storage plus digest.** Native secure StorageManager/FileSystem handle interfaces now support getDirectory, virtual directory/empty-file creation, lookup, removal, resolve/isSameEntry, permission methods, snapshot async iteration and internal File snapshots. Page-owned origin partitions preserve same-origin reload contents while invalidating old wrappers, isolate Pages/origins, and clear on disposal; ephemeral wrappers are GC-reclaimed. Browser comparisons cover path identity after recreation, USV/NUL names, errors, native shape and monitoring. Secure Crypto has **3/3** entry names and StorageManager **4/4**; name coverage is distinct from full execution. Writes/streams/move and the 16 non-digest SubtleCrypto operations remain explicit unsupported profiles. Fresh wheel: **96 focused passed; 3604 passed, 2 skipped full**. Three additional storage-tree receipts repeat **3/3**; all three release entry points share **46 focused modules**. Evidence, working-tree hashes and profile limits: `docs/bom_10_storage_report.md`; boundary: `docs/bom_10_storage.md`. Next: complete the selected 12-lane release validation (local macOS Python 3.11–3.14 first, then Linux/Windows) and separately scope writable streams/move or key algorithms. Latest cross-platform verified remains BOM-08; shared SDK remains BIND-01.

**Previous locally verified slice (`2026-11-27`): BOM-10 digest.** Native Crypto/SubtleCrypto/CryptoKey shapes, secure gating, real SHA-1/256/384/512 digest and native `SubtleCrypto.supports` now use pinned TF-PSA-Crypto 1.1.2 (portable SHA-only C build). Secure crypto membership is **3/3**, but only digest executes WebCrypto: the other 16 native operations reject an explicit unsupported profile; key instances and `navigator.storage.getDirectory` were unimplemented in that digest-only slice. Browser-derived random errors/receivers, argument conversion and snapshots, GIL, lifecycle and tracing are covered. Fresh wheel: **144 focused passed; 3577 passed, 2 skipped full**. Nine reference receipts include static supports and edge cases; V8-free smoke and backend hash/vector checks pass. Source hashes, wheel hashes and limits: `docs/bom_10_digest_report.md`; backend/pin: `docs/bom_10_digest_backend.md`; contract: `docs/bom_10_digest.md`. At that stage storage was still pending; the separate storage report now supersedes it for the latest local candidate. Cross-platform release verification remains pending.

**Previous locally validated candidate (`2026-11-27`, cross-platform release verification pending): BOM-09 completes Performance (`22/22`), Console (`25/25`), and Scheduler (`2/2`).** Branch `codex/bom-09-related-bom` upgrades `performance` (`9/22 -> 22/22` native `Performance` `EventTarget` host, `PerformanceEntry`/`PerformanceMark`/`PerformanceMeasure`/`PerformanceNavigation`/`PerformanceTiming`/`EventCounts`/private `MemoryInfo`, structured-cloned `detail`, and `adopt_ephemeral_host_object` weak-wrapper GC reclamation), `console` (`22/25 -> 25/25` with `context`, `createTask`, `memory`, `Symbol.toStringTag = "console"`, and Chrome 158 key order/arities), and `scheduler` (`1/2 -> 2/2` native `Scheduler`/`TaskController`/`TaskSignal`/`TaskPriorityChangeEvent`, 6-level priority-ranked scheduling, V8 continuation-preserved `yield()` attribution, and Chrome-matching `value`-then-`done` `AbortSignal.any`/`TaskSignal.any` iterator unpacking), plus GIL-free `to_python_deep` V8 traversal. Local macOS arm64 / Python 3.13 passes **8 BOM-09 focused**, **43 move-audit**, and **3491 passed, 2 skipped** full-suite tests. Cross-platform 12-lane release verification remains pending alongside `BOM-10` (`crypto.subtle` and `navigator.storage.getDirectory`); shared SDK remains `BIND-01`. Scope/evidence: `docs/bom_09_related_bom.md`, `docs/bom_09_related_bom_report.md`, `docs/bom_09_api_delta.json`.

**Latest cross-platform release-verified candidate (`2026-11-26`): BOM-04..BOM-08 complete Navigator (`70/70`) and Observable/`EventTarget.prototype.when`.** Fixed runtime `3cc59b336676b5ce430f668646861eca5e359f8a` / tree `0fe758ea9a74679774280988b22597b4713a1c8e` completes all 35 remaining `Navigator` APIs (`35/70 -> 70/70` secure, `33/70 -> 38/70` insecure/opaque) across `BOM-04`..`BOM-07` and native `Observable`, `Subscriber`, `AbortController`, `AbortSignal`, and `EventTarget.prototype.when` in `BOM-08` (`navigator.connection` `9/9`, `screen.orientation` `9/9`, `visualViewport` `14/14`, inherited `window.when`). Each batch was independently reviewed by Codex CLI and verified against headless Chrome `158.0.8127.88`. All 12 selected Python 3.11–3.14 lanes pass: local macOS arm64 first, then hosted Linux x64 and Windows x64. Every lane passes the **42-module / 1257-test gate** (**32 new pytest cases**); full Mac/Linux are **3483 passed, 2 skipped**, Windows **3481 passed, 4 skipped**. All five hosted ZIPs (`15125369259`, `15125461652`, `15125761278`, `15126235377`, `15125606444`) and 12 wheels are collected and hash/CRC/ABI/native/RECORD verified. V8 `15.0.245.19` engines are reused; native extensions/wheels are rebuilt. Runs: Linux `45835448535`, Windows `45835458516`, push V8 CI `45835443187` (**83 audit / 1257 gate / 3483 passed, 2 skipped**). Shared SDK remains BIND-01; no shared installation, PR merge, or version tag publication is performed. Scope/evidence: `docs/bom_04_navigator_methods.md`..`docs/bom_08_observable_when.md`, `docs/bom_04_navigator_methods_report.md`..`docs/bom_08_observable_when_report.md`, `docs/bom_08_release_report.md`.

**Previous implemented/validated candidate (`2026-11-21`): BOM-03 native legacy quota.** Fixed runtime `4cbab176ec8105d5d0545f61335c6600d9db8830` / tree `c76e926f74a7bcc1995ed697d0841a3950195a65` passes all 12 selected Python 3.11–3.14 lanes: local Mac first, then hosted Linux/Windows. Every lane passes the **37-module / 1225-test gate**; full Mac/Linux are **3451 passed, 2 skipped**, Windows **3449 passed, 4 skipped**. All five hosted ZIPs and 12 wheels are collected and hash/CRC/ABI/native/RECORD verified. V8 15.0.245.19 engines are reused; native extensions/wheels are rebuilt. Runs: Linux `44769697781`, Windows `44769706158`; push/PR V8 CI `44722569894` / `44722637832` also pass. Navigator remains **35/70** with **118 new tests**. PR #34 is ready for review above unmerged PR #33. This batch does not install the shared SDK or publish a version. Scope/evidence: `docs/bom_03_legacy_quota.md`, `docs/bom_03_legacy_quota_report.md`, `docs/bom_03_api_delta.json`, `docs/bom_03_release_report.md`.

**Previous implemented/validated candidate (`2026-11-19`): BOM-02.** Native secure getInstalledRelatedApps and inactive-profile vibrate raise Navigator secure-reference membership to **34/70**. Local Mac Python 3.11–3.14 wheels are validated before hosted Linux/Windows; all 12 selected lanes pass the **36-module / 1107-test gate**. Full Mac/Linux: **3333 passed, 2 skipped**; Windows: **3331 passed, 4 skipped**. There are **190 new tests**. Runtime code is `1dff793c5349206976dcb575d761c8331732da6e` / tree `3f7e18f50a7e17abf9fed27be7e57cc0aec6f01f`; Windows uses checkout-fix `d97412dcd97df4ababba40fa55b1c7c92888b3b4` / tree `57ad2e7f7a3c059c34cdc7fa8b2e8baa8a267d78`, differing only by .gitattributes with all 431 canonical runtime/test/build/workflow inputs unchanged. Both commits pass V8 source CI; all five remote ZIPs and 12 wheels are collected and verified. Generated diagnostic object-label differences under modified prototype metadata remain explicit. Baseline PR #32 is merged; BOM-02 is draft PR #33. Shared SDK remains BIND-01. Boundary/report/delta/release evidence: `docs/bom_02_navigator.md`, `docs/bom_02_navigator_report.md`, `docs/bom_02_api_delta.json`, `docs/bom_02_release_report.md`.

**Build order (`2026-11-17`, user direction): macOS wheels first.** Build and validate Python 3.11–3.14 wheels in the existing local macOS arm64 environments before dispatching Linux/Windows release builds. Hosted macOS is supplementary while runner capacity is unreliable; preserve its actual result separately. Reuse only matching pinned V8 caches, rebuild the extension/wheels, and record source/ABI/hash/test evidence.

**Current user direction (`2026-11-17`): complete BOM environment first; defer further Symbol.iterator monitoring.** Continue Navigator and existing BOM subobjects under `docs/bom_coverage_plan.md`. BOM-01 selects offline getGamepads/scheduling and connection/orientation/viewport native event foundations; boundary: `docs/bom_01_capabilities.md`. Existing monitoring regressions stay protected. This priority change does not mark remaining API observation or cross-platform release/deployment complete.

**Previous implemented/validated candidate (`2026-11-17`): BOM-01, source `6839c9ab4fec026cedf24b4de351c81c6e7d92e6` / tree `7d5f85f379b0f797b8512f8d318aa104183b4975`.** PR #32 includes native getGamepads/Scheduling and connection/orientation/viewport event foundations. All 12 Python 3.11–3.14 lanes pass the **917-test gate**: Linux/Windows are hosted; macOS arm64 uses local fallback after hosted runner acquisition failed before any code executed. Full Linux/macOS: **3143 passed, 2 skipped**; Windows: **3141 passed, 4 skipped**. Both source V8 CI checks pass **83 audit / 917 gate / 3143 passed, 2 skipped**; merge-snapshot identity remains separate. No merge, tag or shared SDK replacement. Hosted Mac failure stays explicit, and the latest fully hosted release matrix remains aa72e50. Scope/evidence: `docs/bom_01_capabilities.md`, `docs/bom_01_capabilities_report.md`, `docs/bom_01_release_report.md`.

**Previous implemented/local acceptance (`2026-11-17`): TAG-MON-01 native Symbol.toStringTag reads; full release pending.** Existing interface/collection/namespace/CSS/Battery/Quota tag definitions now record direct and Reflect.get reads through native data hooks, preserving values/data flags/owners/key order. Optional `member_kind="symbol"` makes real `[Symbol.toStringTag]` distinct from same-named strings. Ordinary object-toString tag lookup stays quiet; actual JS/native-bound callback reads retain provenance. Unknown dynamic Proxy metadata uses conservative attribution and can retain an extra GET, without extra getter/trap calls. **34 new cases**, **167 focused passed**, **823 release-focused passed**, full fresh-installed macOS arm64 / Python 3.11 wheel **3049 passed, 2 skipped**. PR #32 updated; no shared SDK replacement. Boundary/evidence: `docs/symbol_to_string_tag_monitoring.md` / `docs/symbol_to_string_tag_monitoring_report.md`.

**Previous local acceptance (`2026-11-17`): toString receiver metadata and explicit text invocation; full release pending.** Intrinsic CALL entries now carry raw `receiver_summary` and optional direct native-host `receiver_label`, captured before observer mapping. Text normalizes `.call(this, ...extraArgs)` to avoid confusing entrypoint and receiver; actual argument counts are unchanged. No function name/tag/constructor getters or user-Proxy unwrapping are used; saved reports without receiver fields retain their old text. **41 new cases**, **133 focused passed**, **789 release-focused passed**, full fresh-installed macOS arm64 / Python 3.11 wheel **3015 passed, 2 skipped**. PR #32 updated; no shared SDK replacement. Boundary/evidence: `docs/to_string_monitoring.md` / `docs/to_string_receivers_report.md`.

**Previous local acceptance (`2026-11-17`): TOSTRING-MON-01 intrinsic detection calls; full release pending.** Function.prototype.toString and Object.prototype.toString now emit completed host_object CALL entries, including cached/direct/call/apply/bind/Reflect.apply forms, full source strings and synchronous failures. Original V8 algorithms preserve strict receivers, user Proxy behavior and getter/error identity. Native Proxy interposers preserve standard JS shape; Inspector/internal-kind and pristine cross-Realm transparency are explicitly not claimed. **35 new cases**, **748 release-focused passed**, full installed macOS arm64/Python 3.11 wheel **2974 passed, 2 skipped**. PR #32 updated; shared SDK remains BIND-01. Boundary/evidence: `docs/to_string_monitoring.md` / `docs/to_string_monitoring_report.md`.

**Previous local acceptance (`2026-11-16`): complete trace strings and arguments; full release pending.** Script URLs and other string GET/SET/argument/return values are retained completely, including values beyond 4096 bytes; all argument positions are logged. Debug and manual text/raw reads share this capture, prod stays silent. A full-string coverage marker preserves saved legacy truncation semantics, including ambiguous literal ellipses. Objects remain type-only, exception/event limits remain unchanged, and no extra user getter/coercion runs. **6 new cases**, **154 focused passed**, **713 release-focused passed**, full macOS arm64/Python 3.13 **2939 passed, 2 skipped**. PR #32 is updated; shared SDK remains BIND-01. Boundary/evidence: `docs/access_trace_text.md` / `docs/full_trace_values_report.md`.

**Previous local acceptance (`2026-11-16`): XHR-01 native interface shape and monitoring; full release pending.** Native XMLHttpRequest now constructs with a stable Upload and browser-derived EventTarget inheritance, constants, descriptors, native functions and eager prototype order. Request methods are inert placeholders: `send` returns undefined and debug prints safe parameters; prod stays silent. No HTTP, state transitions, responses or automatic transfer events. **62 new tests**, **643 release-focused passed**, full macOS arm64/Python 3.13 **2933 passed, 2 skipped**. This slice updates PR #32; shared SDK remains BIND-01. Scope/evidence: `docs/xml_http_request_shape.md` / `docs/xml_http_request_shape_report.md`.

**Previous local acceptance (`2026-11-16`): MutationRecord field tracing; release pending.** The nine existing plain-object record fields now record completed GET/SET entries with per-record generation identity, including cached-before-start and async delivery. Original values/data flags/node/array identity and initialization setter behavior are preserved; internal materialization stays quiet. **23 new tests**, **581 release-focused passed**, full macOS arm64/Python 3.13 **2871 passed, 2 skipped**. This slice updates PR #32 without new browser APIs or SDK replacement. Array length/index, Symbol/reflection and other returned objects remain open. Boundary/report: `docs/mutation_record_monitoring.md` / `docs/mutation_record_monitoring_report.md`.

**Previous local acceptance (`2026-11-16`): generic binding references and receiver rejections; release pending.** Direct generic prototype method references now record completed GETs on their defining prototypes, while canonical instance METHOD/CALL identity and folding stay unchanged. Generic native method/getter/setter brand failures record failed CALL/GET/SET before the original Illegal invocation without parameter coercion. **36 new tests**, **558 release-focused passed**, full macOS arm64/Python 3.13 **2848 passed, 2 skipped**. Existing origin filtering and native shapes remain intact. This batch updates PR #32; prior release/fallback records below remain their verified fixed sources. Symbol/query, independent callback and returned-structure gaps remain separate, and shared SDK stays BIND-01. Boundary/report: `docs/generic_binding_monitoring.md` / `docs/generic_binding_monitoring_report.md`.

**Latest implemented/validated candidate (`2026-11-16`): user-origin tracing, source `4c777f78e535d54a003a297c029d9fc87ce20950` / tree `6184c309a03437de7bb13774a0bdf8008ba198cf`.** Empty debug loads are silent and `window.location.href` prints exactly three gets. **40 origin tests** are included in the **24-module / 522-test gate**. Linux/Windows hosted builds pass Python 3.11–3.14; macOS arm64 passes the same four-version build/installed-wheel gate/full suite locally after GitHub failed to acquire a macOS runner. Full Linux/macOS are **2812 passed, 2 skipped**, Windows **2810 passed, 4 skipped**. All 12 wheels are collected and verified. The hosted macOS attempt did not execute code and is not green; the last fully hosted release matrix remains `aa72e50` below. PR #32 remains unmerged; shared SDK deployment stays BIND-01 and strict API coverage gaps remain open. Scope/results/fallback evidence: `docs/access_trace_origins.md` / `docs/access_trace_origins_report.md`.

**Latest release-verified candidate (`2026-11-16`): ENV-MON-01 + Page debug/prod modes, PR #32.** Fixed build source `aa72e50f29bd2d1f472bad87a4038eb09086854b` (tree `3f07dc72a09b78f97f74924ecd3f15b2cb0624cd`) passes all 12 V8-linked release lanes, Python 3.11–3.14 on Linux x64, Windows x64 and macOS arm64. Every lane passes the **482-test focused gate**; full Linux/macOS are **2772 passed, 2 skipped**, Windows **2770 passed, 4 skipped**. Runs: Linux `43822383750`, Windows `43822388334`, macOS `43822440892`. Matching V8 15.0.245.19 engine caches were reused and extensions/wheels rebuilt. All 9 artifact archives and 12 wheels are downloaded and SHA-256/CRC/tag verified; the temporary macOS helper is restored and its original bytes verified. **Strict API-use observation remains incomplete**: documented prototype, invalid-receiver, symbol/query and returned-object gaps stay open. PR remains unmerged, with no tag publication or shared SDK replacement; deployed SDK remains BIND-01. Full evidence: `docs/monitoring_release_report.md`.

**Previous local acceptance (`2026-11-16`): Page load monitoring modes; now included in the verified candidate above.** `Page.load(..., mode="debug")` starts unfiltered access tracing before user scripts and automatically prints/consumes readable logs after load, eval and every event-loop pump. `mode="prod"` stops/clears access tracing; omission preserves existing manual use and active debug across reloads. A per-Page guard covers execution, output and mode/trace controls so competing threads cannot drain unfinished completion metadata. Debug uses `sys.maxsize` as the per-call ceiling instead of the manual 10,000-event default. macOS arm64/Python 3.13 acceptance: **57 new mode tests**, **328 focused passed**, full **2772 passed, 2 skipped**. This Python usability layer relies on the existing local ENV-MON-01 native candidate; the API coverage audit's prototype/invalid-receiver/symbol/query/returned-object gaps remain open. The user now authorizes a PR and V8-linked release builds; progress/source evidence is tracked in `docs/monitoring_release_report.md`. Shared SDK replacement remains outside this request. Boundary/report: `docs/page_monitoring_modes.md` / `docs/page_monitoring_modes_report.md`.

**Previous local acceptance (`2026-11-15`): History parent-entry tracing; release pending.** The user reproduction `start_access_trace(); eval("window.history"); stop; read(format="text")` exposed an untraced native global getter. The fix records Window/history/get and its safe return/exception completion without changing History behavior or getter shape. Local macOS arm64/Python 3.13 checks pass **94 focused** and **2552 passed, 2 skipped** full-suite tests. This is a new local fix beyond the combined release below; shared SDK and release artifacts are unchanged. Scope/evidence: `docs/history_entry_monitoring.md`.

**Previous release verified (`2026-11-15`): combined PRES-01/PRES-02/PRES-03 and WIN-MON-01.** Build source `40d1c1f5bd9c3a9d40f9ac9ba3e6930437dfddff` (tree `b7d518ecc9ab46a30812622041bd48f3c28ab1aa`) passes all 12 V8-linked release lanes, Python 3.11–3.14 on Linux x64, Windows x64 and macOS arm64. Each lane passes the combined **257-test focused gate**; full Linux/macOS are **2547 passed, 2 skipped**, Windows **2545 passed, 4 skipped**. Runs: Linux `43704599952`, Windows `43704600017`, macOS `43704687043`. Fixed V8 15.0.245.19 caches are reused; the host extension/wheels are rebuilt. Required V8 checks pass, macOS source pins/restore are verified, and branch protection is unchanged. **Latest shared SDK deployment remains BIND-01**; no environment installation or version-tag publication was performed. Source/run/artifact evidence and limits: `docs/bom_release_report.md`.

**Current user priority (`2026-11-15`): BOM first.** Prioritize Window, Location,
Navigator and History, then related BOM objects; defer further DOM-node work and
the proposed DOMException/Abort/Headers/Request/Response/FormData batch. Preserve
the completed PRES-01/PRES-02 work. Read `docs/bom_coverage_plan.md` and the top
of `docs/next_tasks.md` before choosing a task; these override older next-DOM or
next-Web-data recommendations. Retain shared native prototypes, configuration
consistency and meaningful local regressions. This priority change alone adds no
runtime API or release/deployment verification.

**Previous Window monitoring batch, release-verified in the combined build (`2026-11-15`): WIN-MON-01.** Read
`docs/window_monitoring.md` before changing the PRES-03 monitoring extension.
All 49 mapped globals have structured native get/get_method and supported set
completion; all 22 callable globals have watch_apis and structured call entries
with safe summaries and synchronous exceptions. Shell methods enter both
channels; shell/global and namespace accessors use structured property hooks.
Native data callbacks retain data descriptors, exact values and function
identity. Actual events/hits pass **550 focused** checks; full macOS arm64/Python
3.13 is **2547 passed, 2 skipped**. All **127 current native shell methods** emit
both channels in a sweep. Combined release verification is recorded above; the shared BIND-01
SDK is unchanged. Evidence: `docs/window_monitoring_report.md`.
Descriptor evaluation may record an extra get; read-only writes or
construction blocked before native entry have no invented event. User JS
accessor replacement, arbitrary nested data reads and Promise settlement remain
outside the native path. watch_apis stays call-only.

**Previous local acceptance (`2026-11-15`): PRES-03.** Read
`docs/presence_03_window.md` for the user-approved 49 ordinary Window names.
The separate 821 missing global interface entries are excluded. Preserve native
global kinds and metadata, canonical private shell prototypes, generation-owned
identity, prefixed rAF reuse and the bounded window.event/reportError model.
Backend operations explicitly throw NotSupportedError or reject a Promise;
typed presence is not subsystem execution. The user authorizes re-exposing
native `open` for this candidate while real window creation stays unsupported.
Keep the old absent-open regression as release history. Local checks pass
**526 focused tests**, **28 profile/trace guards**, and full macOS arm64/Python
3.13 **2491 passed, 2 skipped**. Ordinary names are **128/128**, handlers
**125/125**. **Release acceptance is pending**; shared BIND-01 SDK deployment is
unchanged. Evidence and limits: `docs/presence_03_report.md`.

**Previous local acceptance (`2026-11-15`): PRES-02.** Read
`docs/presence_02_dom.md` before extending the DOM logic/reflection batch.
Native dataset/DOMStringMap, attributes/NamedNodeMap and Attr share live backing
attribute state. Attr inherits Node; namespace-aware creation/attribute methods,
live namespace tag collections and root/normalize/position helpers
use defining native prototypes. Same-context import/adopt preserve the correct
owner and, for adoption, canonical wrappers, listeners, handlers, live views and
observer-record routing. DOMParser text/html returns an inert independent
HTMLDocument using the bounded parser; XML MIME types explicitly fail as
unsupported and XMLSerializer remains later work. Local acceptance:
**489 focused passed, 1 xfailed**; full macOS arm64/Python 3.13:
**2454 passed, 2 skipped, 1 xfailed**. **Combined release verification is recorded above.**
Read `docs/presence_02_report.md` for candidate identity and limits.
No new shared SDK installation has occurred;
BIND-01 remains the latest verified shared deployment below.

**Previous local acceptance (`2026-11-15`): PRES-01.** Read
`docs/presence_01_bom_events.md` before extending this presence-first offline
BOM/event batch. It adds configured Navigator identity/UAData, inactive
UserActivation, empty Location ancestorOrigins, UIEvent/Event fields and paths,
shared IDL handlers, Console additions and Screen metadata. NavigatorUAData,
UserActivation and DOMStringList use shared native defining prototypes with
illegal public constructors; the actual Event/UIEvent constructors and
prototypes are registered together, including Event → UIEvent →
MouseEvent/KeyboardEvent/FocusEvent inheritance. Nested instances and handler
state reset per generation, V8 handles release before isolate disposal, and
transient event dispatch state is cleared after delivery. Local acceptance is
**447 focused, 1 xfailed**; full macOS arm64/Python 3.13 is
**2417 passed, 2 skipped, 1 xfailed**. Read `docs/presence_01_report.md` for
candidate identity, evidence and limits. PRES-02 above now implements the next
DOM logic/reflection batch, with local acceptance passed and combined release verified above.
No complete release matrix or shared SDK update
has been performed for PRES-01; **latest shared deployment remains
BIND-01 below**. Do not turn configured UAData, inactive activation or synthetic
events into claims of real device observation, trusted input or server acceptance.

**Latest shared deployment / previous release verified (`2026-11-15`): BIND-01.** Source `f015745`, tree `0914106d38d0c2c245b47e70daa7623b4ed04fc5`, passes all 12 lanes: 87 focused, Linux/macOS **2371 passed, 2 skipped, 1 xfailed**, Windows **2369 passed, 4 skipped, 1 xfailed**. Shared iv8-test is backed up and upgraded from NAMED native `4b2f871d...` to `d2902411...`; actual shared interpreter passes focused/full and cached EPUB offline. Mainline integration is PR #30. Read `docs/native_binding_restore.md` / report for identities, artifact IDs, Windows retry, helper restoration and rollback. History/named access remain preserved; null-this/key-coercion/base-Event behavior gaps are separate. No live requests or server-pass claim. M11-53 remains next; stage 4 stays held.

**Previous release verified (`2026-11-15`): NAMED-01 named-access restoration.** Source `374c81b`, tree `d16031ea344e5ff182c9d986d2a9ca634c6388e0`, restores the private Epub named-access patch in the split tree while preserving History. All 12 release lanes pass 56 focused tests before the full suite: Linux/macOS **2340 passed, 2 skipped, 1 xfailed**, Windows **2338 passed, 4 skipped, 1 xfailed**. Mainline integration is PR #29. Read `docs/named_access_restore.md` and its report for source/run/artifact identity, helper restoration and delivery. This named-access slice originally excluded callback metadata; BIND-01 above restores it. Do not replace shared SDKs silently or claim live business acceptance. M11-53 remains next; stage 4 remains held.

**Previous local acceptance (`2026-11-14`): caller HTTP/Page configuration alignment.** Shared identity/guards and native-profile inspection remain as recorded in `docs/request_configuration_report.md`; their tests are covered by the NAMED-01 release above. Configuration findings do not prove a missing runtime API or server-rejection cause.

**Previous implemented repository tooling (`2026-11-14`): unified offline replay and environment diagnostics.** Read `docs/offline_scenarios.md` for manifest schema, CLI and evidence limits, and `docs/offline_scenarios_report.md` for local acceptance. Reports separate execution, checks, environment and server (`not_tested`); matched negative controls do not imply successful execution. These utilities do not change Page/native contracts. The subsequent caller configuration task takes precedence over speculative Navigator/profile/permissions repair; M11-53 remains next and stage 4 remains held.

**JET-01 History native release verified; bounded live test still state -1 (`2026-11-14`).** Native source `dd67e49` passes all 12 platform lanes (**2120 passed, 2 skipped, 1 xfailed** each). After explicit user data-send approval, a same-session comparison with the unchanged original collector and identical JS test XHR adapter confirmed: old SDK fails at pushState with zero POSTs; the History SDK loads and emits two real POSTs, both HTTP201, but final `_abck` remains **-1**. The authorized two-POST budget is exhausted. This confirms the first execution blocker, with no server-pass or final root-cause claim. Stage 4 remains held. Evidence: `docs/jetstar_environment_repair.md`.

**Page split stage 3 verified (`2026-11-14`).** Source `2f02535` (tree `e0b9d47029c9ce78d37013d49ddcbd4368a7e817`) completes all seven host-object slices. `page_state.cpp` decreases from **8,774 to 2,852 lines**. Linux x64, Windows x64 and macOS arm64 passed all 12 Python 3.11–3.14 wheel/test lanes, each **2090 passed, 2 skipped, 1 xfailed**. The complete member-aware move audit matches **282 definitions, 0 differences**; **83 audit checks**, **29 object/prototype snapshots** and independent native compilation passed. Next is stage 4 monitoring extraction, outside this completed request. Evidence: `docs/page_state_split.md`.

**Page split stage 2 verified (`2026-11-14`).** Source `be5bdb4` (tree `cda6b32a807e3243a4ecb2b578e97d81eda92ded`) completes the pure C++ DOM data layer. `page_state.cpp` is **8,774 lines**. Linux x64, Windows x64 and macOS arm64 passed all 12 Python 3.11–3.14 wheel/test lanes, each **2090 passed, 2 skipped, 1 xfailed**. Seven pure translation units passed independent strict compilation; the move audit matched **317 definitions with 0 differences**, 20,000 DOM differential cases passed under UBSan, 56 audit checks passed, and 17 reflected object graphs matched. Stage 3 was authorized on `2026-11-14`. Evidence: `docs/page_state_split.md`.

**Page split stage 1 verified (`2026-11-14`).** Source `f5dbea5` (tree `f2cf965a2cc6037fb0933aeba03afe634f11e478`) completes all leaf-module slices. `page_state.cpp` is **10,065 lines**. Linux x64, Windows x64 and macOS arm64 passed all 12 Python 3.11–3.14 wheel/test lanes, each **2090 passed, 2 skipped, 1 xfailed**. Local syntax/link checks, 56 audit checks and 17 object-graph reflection comparisons passed; the move audit matched **403 definitions with 0 differences**. Stage 2 extracts the pure C++ DOM data layer. Evidence: `docs/page_state_split.md`.

**Page source split preparation verified (`2026-11-14`).** Stage 0 source `973ac99` (tree `6751ff9c9f419d9b67054677744eef8a3d4f4646`) passed 56 audit checks and all 12 native wheel/test lanes, each **2090 passed, 2 skipped, 1 xfailed**. Linux and Windows used hosted release CI. macOS arm64 used a local four-version release matrix after hosted runners reported capacity exhaustion. The complete stage 1 verification is recorded above. Evidence and artifacts are in `docs/page_state_split.md`. M11-53 remains the next numbered feature.

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
  change. Run focused tests and the full suite against the installed real-V8
  wheel locally, then perform the required CI/release checks. A successful wheel
  build alone does not establish runtime acceptance.

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
  - ENV-MON-01 and Page debug/prod modes as the latest release-tested candidate;
    known strict-use gaps remain open in `docs/environment_monitoring_report.md`.
  - The earlier WIN-MON-01 batch is release-verified: mapped Window native get/set/call, call-only watches
    for 22 globals and shell methods, and scoped shell accessor tracing.
    Preserve native data descriptors/identity and the observation boundaries
    in `docs/window_monitoring.md`; the shared BIND-01 SDK remains unchanged;
  - PRES-03 with previous local acceptance: the user-selected 49
    ordinary Window names with native function/type exposure, canonical private
    shell prototypes, reused rAF and bounded event/reportError integration.
    Backend execution explicitly fails; the 821 global interface entries remain
    excluded. Native open presence is user-authorized but window creation stays
    unsupported. Local and combined-release acceptance have passed; the shared
    BIND-01 SDK is unchanged;
  - PRES-02 with previous local acceptance: native dataset/DOMStringMap,
    attributes/NamedNodeMap, Attr → Node inheritance, namespace-aware DOM
    operations, Node root/normalize/position helpers and constants, same-context
    import/adopt with canonical wrapper/listener/view/observer ownership, and
    inert HTML DOMParser. Local and combined-release acceptance have passed; the
    shared BIND-01 SDK is unchanged;
  - PRES-01 with previous local acceptance: configured Navigator identity,
    native UAData/UserActivation/DOMStringList prototypes, UIEvent inheritance,
    shared IDL handlers and the bounded Event/Console/Screen additions. Local
    acceptance passes (see report); it has not run a full release
    matrix and has not replaced the shared BIND-01 SDK;
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
  - latest verified numbered compatibility milestone: M11-52 (Window messaging);
    latest release-verified candidate: ENV-MON-01 + Page modes, PR #32;
    latest shared deployment: BIND-01. Strict API-use coverage remains incomplete.
    The earlier combined PRES/WIN build is recorded in `docs/bom_release_report.md`;
  - previous verified post-M11 monitoring foundation: host access trace steps 1–4 +
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
