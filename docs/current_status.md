# Current Status

**Latest implemented/validated candidate (`2026-11-26`): BOM-04..BOM-08 complete Navigator (`70/70`) and Observable/`EventTarget.prototype.when`.** Fixed runtime `3cc59b336676b5ce430f668646861eca5e359f8a` / tree `0fe758ea9a74679774280988b22597b4713a1c8e` completes all 35 remaining `Navigator` APIs (`35/70 -> 70/70` secure, `33/70 -> 38/70` insecure/opaque) across `BOM-04`..`BOM-07` and native `Observable`, `Subscriber`, `AbortController`, `AbortSignal`, and `EventTarget.prototype.when` in `BOM-08` (`navigator.connection` `9/9`, `screen.orientation` `9/9`, `visualViewport` `14/14`, inherited `window.when`). Each batch was independently reviewed by Codex CLI and verified against headless Chrome `158.0.8127.88`. All 12 selected Python 3.11–3.14 lanes pass: local macOS arm64 first, then hosted Linux x64 and Windows x64. Every lane passes the **42-module / 1257-test gate** (**32 new pytest cases**); full Mac/Linux are **3483 passed, 2 skipped**, Windows **3481 passed, 4 skipped**. All five hosted ZIPs (`15125369259`, `15125461652`, `15125761278`, `15126235377`, `15125606444`) and 12 wheels are collected and hash/CRC/ABI/native/RECORD verified. V8 `15.0.245.19` engines are reused; native extensions/wheels are rebuilt. Runs: Linux `45835448535`, Windows `45835458516`, push V8 CI `45835443187` (**83 audit / 1257 gate / 3483 passed, 2 skipped**). Shared SDK remains BIND-01; no shared installation, PR merge, or version tag publication is performed. Scope/evidence: `docs/bom_04_navigator_methods.md`..`docs/bom_08_observable_when.md`, `docs/bom_04_navigator_methods_report.md`..`docs/bom_08_observable_when_report.md`, `docs/bom_08_release_report.md`.

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

**Current development direction (`2026-11-15`): BOM first.** The user prioritizes
Window, Location, Navigator and History, then related BOM objects. Further DOM
node work and the proposed Web-data batch move behind this queue. Current local
counts, candidate batches and execution boundaries: `docs/bom_coverage_plan.md`.
This planning update leaves the local acceptance and release/deployment records
below unchanged.

**Previous Window monitoring batch, release-verified in the combined build (`2026-11-15`): WIN-MON-01.** The
PRES-03 monitoring extension maps all 49 added Window globals into structured
native get/get_method tracing and their supported writes into set completion.
All 22 callable globals enter both watch_apis and structured calls, including
safe argument/return summaries and synchronous exception completion. Native
shell methods enter both channels; shell/global and namespace accessors use
structured property hooks. Native data callbacks preserve data descriptors,
stored values and function identity. Local acceptance passes **550 focused** and
full macOS arm64/Python 3.13 **2547 passed, 2 skipped**. A native-shell sweep
records all **127 methods** in both channels. **Combined release verification is recorded above.**
No shared SDK replacement is performed; BIND-01 remains the verified shared deployment.
Boundary/report: `docs/window_monitoring.md` / `docs/window_monitoring_report.md`.

Descriptor queries may evaluate a native data getter and also record get.
Read-only writes or construction rejected before native callback entry do not
invent events; replacement with a user JS accessor ends that native data path.
Arbitrary nested data reads remain outside scope, and Promise calls record the
returned Promise without settlement tracing. watch_apis remains call-only.

**Previous local acceptance (`2026-11-15`): PRES-03.** The user-approved
Window presence batch exposes the 49 ordinary names from the fixed Chrome
inventory with observed native-function/object/constant/flag kinds. The separate
821 global interface entries are explicitly excluded. Native global methods use
browser-derived metadata; namespace shells retain canonical private prototypes
and stable generation-owned identities. Prefixed rAF operations reuse the
existing scheduler; window.event/reportError use bounded synthetic event
delivery. Missing backend operations explicitly throw NotSupportedError or
reject a Promise. The user separately authorizes native `open` exposure for
this candidate; creating a window is still unsupported. Local checks pass
**526 focused tests**, **28 profile/trace guards**, and full macOS arm64/Python
3.13 **2491 passed, 2 skipped**. Ordinary names are **128/128**, handlers
**125/125**; the 821 interface entries remain excluded. **Combined release verification is recorded above.** BIND-01 remains the release-verified and
deployed shared SDK, including its historical absent-open profile. Boundary:
`docs/presence_03_window.md` / `docs/presence_03_report.md`.

**Previous local acceptance (`2026-11-15`): PRES-02.** The DOM
logic/reflection batch adds live dataset/DOMStringMap and attributes/NamedNodeMap,
Attr nodes, namespace-aware creation and attribute operations, namespace-aware
live tag collections and Node root/normalization/position helpers,
and same-context Document import/adopt. Attr uses the Node prototype chain;
dataset and attribute views use their native defining interfaces. Adoption keeps
canonical wrappers, listeners, handlers and live views, with observer records
routed to the current owner. DOMParser text/html creates an inert independent
HTMLDocument with bounded html/head/body recovery. XML MIME types explicitly
fail as unsupported; XMLSerializer remains outside this batch. Local acceptance:
**489 focused passed, 1 xfailed**; full macOS arm64/Python 3.13:
**2454 passed, 2 skipped, 1 xfailed**. **Combined release verification is recorded above.** No shared SDK update
has been performed: BIND-01 remains the latest verified shared deployment
version below. Boundary, evidence and remaining limits:
`docs/presence_02_dom.md` / `docs/presence_02_report.md`.

**Previous local acceptance (`2026-11-15`): PRES-01.** The first
presence-first BOM/event batch adds configured Navigator identity and UAData,
inactive UserActivation, empty Location ancestorOrigins, UIEvent/Event fields
and dispatch paths, shared IDL handlers, Console additions and Screen metadata.
NavigatorUAData, UserActivation and DOMStringList use shared native interface
prototypes with illegal public constructors. The actual Event/UIEvent
constructor/prototype pairs are registered together, including native
Event → UIEvent → MouseEvent/KeyboardEvent/FocusEvent inheritance. Nested
objects, handler state and transient dispatch state are generation-owned and
cleaned before isolate disposal or after dispatch as appropriate. Local acceptance
passes **447 focused tests, 1 xfailed** and the full macOS arm64/Python 3.13
suite **2417 passed, 2 skipped, 1 xfailed**. The full release matrix has not run for this
batch and the shared SDK has not been replaced. **Latest shared deployment
remains BIND-01 below.** Scope and remaining differences:
`docs/presence_01_bom_events.md` / `docs/presence_01_report.md`.

**Latest shared deployment / previous release verified (`2026-11-15`): BIND-01.** Source `f015745` (tree `0914106d38d0c2c245b47e70daa7623b4ed04fc5`) restores 18 native operation installation paths and six Event constructor arities, preserving History/named access. All 12 lanes pass 87 focused tests: Linux `43675920281` / macOS `43675968125` each **2371 passed, 2 skipped, 1 xfailed**; Windows `43675928957` each **2369 passed, 4 skipped, 1 xfailed** after one retained startup-timeout retry. Backed-up shared iv8-test is installed with native `d2902411...` and passes 87 focused, full 2371/2/1 and cached EPUB offline. Mainline integration is PR #30. Known delegate/receiver/base-Event behavior gaps remain explicit; no live requests or server-pass claim. Boundary/source/run/artifact/deployment receipts: `docs/native_binding_restore_report.md`.

**Previous release verified (`2026-11-15`): NAMED-01 Window/form named access restoration.** Source `374c81b` (tree `d16031ea344e5ff182c9d986d2a9ca634c6388e0`) restores the private candidate's named access in the split modules while retaining History. All 12 release lanes pass 56 focused tests first: Linux `43616130342` / macOS `43616952704` each **2340 passed, 2 skipped, 1 xfailed**; Windows `43616142584` each **2338 passed, 4 skipped, 1 xfailed** (two extra Unix-only skips). Original 18 named cases/fixture and eight new integration cases are release-gated. Mainline integration is PR #29; source/run/artifact receipts are in `docs/named_access_restore_report.md`. No shared SDK replacement or live server-pass claim. M11-53 remains next; stage 4 remains held.

**Previous caller alignment (`2026-11-14`).** Shared HTTP/Page identity and per-request guards were locally accepted with **2314 passed, 2 skipped, 1 xfailed**. These tooling tests are now included in the NAMED-01 release above. Caller/runtime distinction and external deployment receipts remain in `docs/request_configuration_report.md`.

**Previous tooling (`2026-11-14`): unified offline scenarios and environment diagnostics.** The CLI retains pinned assets, isolated workers, bounded execution and separate redacted verdicts. Its tests are included in the NAMED-01 release above; historical local acceptance is in `docs/offline_scenarios_report.md`.

**JET-01 History native release verified; bounded live test still state -1 (`2026-11-14`).** Native source `dd67e49` passes all 12 platform lanes (**2120 passed, 2 skipped, 1 xfailed** each). After explicit user data-send approval, a same-session comparison with the unchanged original collector and identical JS test XHR adapter confirmed: old SDK fails at pushState with zero POSTs; the History SDK loads and emits two real POSTs, both HTTP201, but final `_abck` remains **-1**. The authorized two-POST budget is exhausted. This confirms the first execution blocker, with no server-pass or final root-cause claim. Stage 4 remains held. Evidence: `docs/jetstar_environment_repair.md`.

**Page split stage 3 verified (`2026-11-14`).** Source `2f02535` (tree `e0b9d47029c9ce78d37013d49ddcbd4368a7e817`) completes all seven host-object slices. `page_state.cpp` decreases from **8,774 to 2,852 lines**. Linux x64, Windows x64 and macOS arm64 passed all 12 Python 3.11–3.14 wheel/test lanes, each **2090 passed, 2 skipped, 1 xfailed**. The complete member-aware move audit matches **282 definitions, 0 differences**; **83 audit checks**, **29 object/prototype snapshots** and independent native compilation passed. Next is stage 4 monitoring extraction, outside this completed request. Evidence: `docs/page_state_split.md`.

**Page split stage 2 verified (`2026-11-14`).** Source `be5bdb4` (tree `cda6b32a807e3243a4ecb2b578e97d81eda92ded`) completes the pure C++ DOM data layer. `page_state.cpp` is **8,774 lines**. Linux x64, Windows x64 and macOS arm64 passed all 12 Python 3.11–3.14 wheel/test lanes, each **2090 passed, 2 skipped, 1 xfailed**. Seven pure translation units passed independent strict compilation; the move audit matched **317 definitions with 0 differences**, 20,000 DOM differential cases passed under UBSan, 56 audit checks passed, and 17 reflected object graphs matched. Stage 3 was authorized on `2026-11-14`. Evidence: `docs/page_state_split.md`.

**Page split stage 1 verified (`2026-11-14`).** Source `f5dbea5` (tree `f2cf965a2cc6037fb0933aeba03afe634f11e478`) completes all leaf-module slices. `page_state.cpp` is **10,065 lines**. Linux x64, Windows x64 and macOS arm64 passed all 12 Python 3.11–3.14 wheel/test lanes, each **2090 passed, 2 skipped, 1 xfailed**. Local syntax/link checks, 56 audit checks and 17 object-graph reflection comparisons passed; the move audit matched **403 definitions with 0 differences**. Stage 2 extracts the pure C++ DOM data layer. Evidence: `docs/page_state_split.md`.

**Page source split preparation verified (`2026-11-14`).** Stage 0 source `973ac99` (tree `6751ff9c9f419d9b67054677744eef8a3d4f4646`) passed 56 audit checks and all 12 native wheel/test lanes, each **2090 passed, 2 skipped, 1 xfailed**. Linux and Windows used hosted release CI. macOS arm64 used a local four-version release matrix after hosted runners reported capacity exhaustion. The complete stage 1 verification is recorded above. Evidence and artifacts are in `docs/page_state_split.md`. M11-53 remains the next numbered feature.

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

Last reviewed: `2026-11-15`

## 1. Summary

**Previous verified monitoring presentation (`2026-11-04`):**
`Page.read_access_trace(clear=True, format="text")` provides concise per-event
paths and captured values, including `GET document.cookie -> ""`. Default raw
dicts and aggregation remain compatible. Source `1599fd5` (tree
`9320586725f0034b1a33695771d04578f5c7ad91`) passed Linux `41848358166`, Windows
`41848373389`, and macOS arm64 fallback `41850244647` (macos-14), Python
3.11–3.14 each `1878 passed, 2 skipped` (1,880 collected). V8 CI `41848183724`
passed; macOS focused checks each `79 passed`. All five native integrations
executed. Local pure formatter checks and raw/aggregate compatibility also
passed. Evidence and driver restoration: `docs/access_trace_text.md`.

**Previous C++ extraction maintenance, verified (`2026-11-04`):** bounded
HTML entity decoding and text/attribute escaping are extracted into the
string-only `html_entities` module. DOM ownership, tokenization, raw/RCDATA
contexts, traversal, and mutation bookkeeping remain in Page. Source `85b2d20`
(tree `b904d7ff803b5acd5b0e7b7b7e8c2d70a7c18735`) passed Linux `41828654174`,
Windows `41828670556`, and macOS arm64 `41828851448`, Python 3.11–3.14 each
`1822 passed, 2 skipped` (1,824 collected). macOS focused runs each `63 passed`;
V8 CI `41828571144` passed. All 20,070 native comparisons matched under UBSan.
See `docs/page_html_entities_refactor.md` for artifact/restoration evidence.

**Previous internal maintenance, verified (`2026-11-04`):** Base64
byte encoding/forgiving decoding is extracted into a V8-independent module.
The host retains JS coercion, Latin-1 validation, exceptions, and binary-string
conversion. Source `10151d4` (tree `0766c6deae5d2f5896881a4928bde46893f68016`)
passed Linux `41797209223`, Windows `41797219218`, and macOS arm64
`41797389405`, Python 3.11–3.14 each `1816 passed, 2 skipped` (1,818 collected).
macOS focused runs each `32 passed`; V8 CI `41797126420` passed. All 14,955
native comparisons matched under UBSan and encoding matched Python's reference.
See `docs/page_base64_refactor.md` for artifacts and restoration evidence.

**Previous internal maintenance, verified (`2026-11-04`):** the
URLSearchParams byte codec and ordered query parsing/serialization are extracted
into `query_parameters`. Canonical state stays in its V8 private slot; host
coercion/mutation and URL snapshots are unchanged. Focused query/URL collection:
54 items, all passed before the full suite in every macOS interpreter. Source
`f017a01` (tree `f6b2075a905777714c2b0fa44562b4b49472490a`) passed Linux
`41791203775`, Windows `41791210361`, and macOS arm64 `41791328195`, Python
3.11–3.14 each `1806 passed, 2 skipped` (1,808 collected). V8 CI `41791168585`
also passed; 20,271 all-byte native comparisons matched under UBSan. See
`docs/page_query_refactor.md` for source/run/artifact and restoration evidence.

**Previous internal maintenance, verified (`2026-11-04`):** shared
minimal URL decomposition/resolution is extracted into the V8-independent
`url_parser` module. Host coercion/tracing, URLSearchParams snapshots, and
secure-context policy remain on their existing paths. The focused URL/consumer
set passed all 76 items in each macOS interpreter before the full suite. Source
`2e30d48` (tree `a95990e92f9e4dbc3d2caaeb23de6be5692ea2e0`) passed Linux
`41783823684`, Windows `41783830832`, and macOS arm64 `41783905526`, Python
3.11–3.14 each `1799 passed, 2 skipped` (1,801 collected). V8 CI `41783767829`
also passed. All 15,224 native differential cases matched under UBSan. See
`docs/page_url_refactor.md` for artifacts and temporary-driver restoration.

**Previous internal maintenance, verified (`2026-11-04`):** the
second Page refactor extracts deterministic Cookie parsing/storage into
`CookieJar`, owned by value in `PageState`. DocumentHost coercion/tracing stays
on its existing path; Python/JS contracts are unchanged. Source `347dfbf` (tree
`02a5fc1835bea8d162c4a140c6bfb7324aa519b4`) passed Linux `41764895229`, Windows
`41764904132`, and macOS arm64 `41764990337`, Python 3.11–3.14 each
`1793 passed, 2 skipped` (1,795 collected). V8 CI `41764857600` also passed;
macOS additionally ran the focused Cookie file first, `11 passed` in every
lane. The 30,033-write native differential check passed under UBSan. Full
source/run/artifact and temporary-driver restoration evidence is in
`docs/page_cookie_refactor.md`.

**Internal maintenance verified (`2026-11-04`):** the first Page refactor extracts the
V8-independent selector parser and adds automatic Linux/Python 3.13 V8 CI.
Source `917cbfb` (tree `b47aad7f9d1897d9cc390e6caf47a8edddd6e147`) is green on
Linux `41759793220`, Windows `41759812090`, and macOS arm64 `41760140836`
(macos-15, cached driver checkout pinned to `917cbfb`), each Python 3.11–3.14
lane `1787 passed, 2 skipped`. V8 CI `41674273069` is also green. The macOS
temporary configuration was restored without rewriting history. The merge
policy requires the real-V8 check; evidence, artifact IDs, and local
differential/UBSan results are in `docs/page_refactor.md`. Latest verified
feature remains M11-52; M11-53 is still next.

`iv8-runtime` started as an M1-scoped Python/V8 embedding project, but the actual
implementation in `master` has progressed well beyond the original M1 boundary.

As of `2026-11-03`, the active execution order is **Window-first: Window has
higher priority than Document** (see `docs/next_tasks.md`). The latest
implemented and verified M11 milestone is **M11-52** (Window messaging:
`window.postMessage` with synchronous structured cloning, bounded target
origins, asynchronous FIFO timer delivery, and a `MessageEvent` subclass with
`data` / `origin` / `lastEventId` / `source` / `ports`). Real-V8 CI is green on
Linux, Windows, and macOS arm64 — Linux run `41620346617`, Windows run
`41620356968`, macOS arm64 fallback run `41621413284`, each wheel lane
reporting `1787 passed, 2 skipped` on tree `bc00040`. It follows **M11-51**
(Window event handler attributes: `window.onload` and a bounded set of IDL
`on*` properties with install/replace/clear, at-target and bubbling invocation,
`return false` cancellation, and generation-scoped teardown; tree `34b119b`,
`1777 passed, 2 skipped`), **M11-50** (Window scroll / focus / UI completion:
read-only `scrollX` / `scrollY` / `pageXOffset` / `pageYOffset`, no-op
`scroll` / `scrollTo` / `scrollBy` / `focus` / `blur` / `move*` / `resize*` /
`stop`, writable per-document `window.status`, BarProp metadata, and the
`clientInformation` alias; tree `6d2250e`, `1766 passed, 2 skipped`), and
**M11-49** (HTML string APIs and variadic tree methods:
`innerHTML` / `outerHTML` / `insertAdjacent*`, `append` / `prepend` /
`before` / `after` / `replaceWith` / `replaceChildren`, bounded entity
decoding, and deterministic serialization; tree `52bafab`, `1756 passed,
2 skipped`). It builds on **M11-48** (Node foundation, tree `3f4940f`,
`1744 passed, 2 skipped`), **M11-47** (Document identity, tree `cc1c47d`),
**M11-46** (`document.cookie`, tree `18f1f4b`), and **M11-41..M11-45** (BOM
viewport / media / device metadata), verified on commit `61abc3b` with
`1626 passed, 2 skipped` per lane:

- `M10-1`: `Page.watch_reflect_ownkeys()` / `read_reflect_ownkeys_hits()`
- `M10-2`: `Page.watch_object_keys()` / `read_object_keys_hits()`
- `M11-1`: `Page(environment=...)` / `Page.get_default_environment()`
- `M11-2`: `requestAnimationFrame` / `cancelAnimationFrame` /
  `Page.run_animation_frame()`
- `M11-3`: `requestIdleCallback` / `cancelIdleCallback` / `Page.run_idle_callbacks()`
- `M11-4`: `queueMicrotask(callback)` (drained by the existing `Page.run_jobs()`)
- `M11-5`: `MutationObserver` (childList/attributes/subtree records delivered by
  the existing `Page.run_jobs()` microtask checkpoint)
- `M11-6`: `CustomEvent` / `MouseEvent` / `KeyboardEvent` / `FocusEvent`
- `M11-7`: `localStorage` / `sessionStorage` (per-Page in-memory stores)
- `M11-8`: `Blob` / `File` / `FileReader` (`run_jobs()` read delivery)
- `M11-9`: event cancellation (`cancelable` / `defaultPrevented` /
  `preventDefault` / `stopImmediatePropagation`)
- `M11-10`: `classList` / `toggleAttribute`
- `M11-11`: event capture phase, `eventPhase`, and `capture` / `once` /
  `passive` listener options
- `M11-12`: writable `id` / `className`, `hasAttributes()`,
  `getAttributeNames()`
- `M11-13`: `replaceChild()`, `remove()`, `hasChildNodes()`, raw sibling aliases
- `M11-14`: `cloneNode()`, `isSameNode()`, `isEqualNode()`
- `M11-15`: `URL.searchParams` snapshot
- `M11-16`: `select.options` / `selectedIndex` / `multiple` / `size`
- `M11-17`: nested `Page(environment=...)` + `Page.get_defaults()`
- `M11-18`: input/button `type` / `disabled` reflection, input `readOnly` /
  `placeholder`, `form.encoding`, `option.label` / `index`
- `M11-19`: multi-token class queries + element-level `getElementsByClassName`
- `M11-20`: `document.getElementsByName`
- `M11-21`: extended form reflection (`name` / `required`, textarea/select
  reflections, `button.name`)
- `M11-22`: select `length` / `item` / `namedItem`
- `M11-23`: deterministic document metadata + `hasFocus()`
- `M11-24`: navigator `languages` / `onLine` / `cookieEnabled` /
  `hardwareConcurrency`
- `M11-25`: screen `colorDepth` / `pixelDepth`
- `M11-26`: window `outerWidth` / `outerHeight` / `screenX` / `screenY`
- `M11-27`: navigator identity fields (`appName` / `appCodeName` / `product` /
  `vendor` / `vendorSub` / `productSub`)
- `M11-28`: extended console methods
- `M11-29`: deterministic event-loop facade — `tick()` / `advance(ms)` /
  `run_until_idle()` / `event_loop_time_ms` over timers + microtasks
- `M11-30`: rAF/idle integration into the facade — fixed 16ms virtual frames,
  idle rounds when no timer/frame is due, microtask checkpoints after every task
- `M11-31`: virtual performance clock — `performance.now()` / `timeOrigin` over
  the facade clock, plus rAF callback timestamps
- `M11-32`: minimal form validation — `willValidate` / `validity` /
  `validationMessage` / `checkValidity()` / `reportValidity()` /
  `setCustomValidity()`, form aggregation, and `invalid` events
- `M11-33`: OS-backed Crypto random — `crypto.getRandomValues()` and
  `crypto.randomUUID()`; the platform CSPRNG is the runtime's single deliberate
  non-deterministic surface
- `M11-34`: CSS selector engine expansion — selector lists, compound and
  attribute selectors, combinators, and `*` shared by query/matches/closest
- `M11-35`: deterministic BOM metadata — `window.name` / `length` /
  `screenLeft` / `screenTop` / `isSecureContext`, `screen.availLeft` /
  `availTop` / `orientation`, and navigator touch/PDF/DNT/connection metadata
- `M11-36`..`M11-40`: BOM completion batch — deterministic dialogs,
  `performance.mark` / `measure` / entry queries / clears, bounded
  `structuredClone`, replaceable single-context `window.parent` / `frames` /
  writable `opener` / read-only `top`, and `scheduler.postTask` over the timer
  queue
- `M11-41`..`M11-45`: BOM viewport/media/device metadata — static
  `visualViewport` + window flags, bounded `matchMedia`, deterministic
  `navigator.storage` and `navigator.permissions`, and empty legacy
  `navigator.plugins` / `mimeTypes` / `javaEnabled`
- `M11-46`: `document.cookie` — deterministic per-Page in-memory jar that
  survives `load()`, with insertion/update ordering, `max-age` / epoch
  `expires` deletion, ordinary `ToString` coercion, and access-trace get/set
  events; no HTTP integration or origin/path filtering
- `M11-47`: Document identity — writable `document.title` reflected into the
  first parsed `<title>`, plus read-only `documentURI` / `baseURI` (the current
  base URL), single-context `defaultView` (`=== window`), and `location`
  (the same object as the global `location`); all access-traced
- `M11-48`: Node foundation — parser-emitted text/comments (`nodeType` 3/8),
  `createTextNode` / `createComment` / `createDocumentFragment`, live
  `textContent`, white-space-preserving child order, `childNodes` vs element
  `children`, node-kind-aware tree editing/clone/equality/contains, fragment
  child-moving insertion, element-only selectors/collections, and active
  `characterData` MutationObserver records
- `M11-49`: HTML string APIs and variadic tree methods — writable `innerHTML` /
  `outerHTML`, `insertAdjacentHTML` / `Element` / `Text`, `append` / `prepend` /
  `before` / `after` / `replaceWith` / `replaceChildren` with node-or-string
  arguments, bounded entity decoding, deterministic serialization, and pinned
  mutation-record behavior
- `M11-50`: Window scroll/focus/UI completion — access-traced `scrollX` /
  `scrollY` / `pageXOffset` / `pageYOffset` (always `0`), no-op `scroll` /
  `scrollTo` / `scrollBy`, `focus` / `blur`, `moveBy` / `moveTo` /
  `resizeBy` / `resizeTo`, and `stop`, writable per-document `status`,
  read-only BarProp metadata, and the `clientInformation` alias of
  `navigator`
- `M11-51`: Window event handler attributes — bounded native `onload` /
  `onerror` / `onmessage` / lifecycle and messaging `on*` accessors;
  function-or-null getters/setters, handler invocation after regular listeners
  at target and in bubbling, `return false` cancellation, swallowed
  exceptions, and per-generation reset
- `M11-52`: Window messaging — `window.postMessage` with synchronous structured
  cloning, bounded target origins, asynchronous FIFO timer delivery, and a
  `MessageEvent` subclass (`data` / `origin` / `lastEventId` / `source` /
  `ports`)
- Post-M11 host access trace (steps 1–4, the complete step-5 object groups, steps 6a–6h, and step 7):
  `Page.start_access_trace` / `stop_access_trace` / `read_access_trace`,
  recording native HostObject get/set/call entries, missing string-key
  `get_probe` events, performance/crypto/Storage callback calls, intrinsic
  window metadata reads, nested connection/orientation/`performance.timeOrigin`
  reads, static `visualViewport` metadata/handler/method accesses,
  `navigator.storage` manager method calls, `navigator.permissions` query calls
  plus PermissionStatus accesses, matchMedia result reads/method calls, legacy
  navigator plugins/mimeTypes/javaEnabled accesses, window
  parent/top/frames/opener get/set events, step-6a value/argument/return
  summaries plus returned/threw outcomes for generic HostObject get/set/call,
  method-reference `get_method` events with adjacent-call folding, step-6c
  `delete`/`own_keys`/`define_property` request events, step-6d explicit
  has/descriptor query events, step-6f Reflect target type/label metadata,
  step-6g native callback/getter summaries, step-6h synchronous exception
  name/message/location/stack, and the step-7 `Page.access_trace_report()`
  aggregated report, with generation, object identity, resource, and source
  attribution; `in`, non-HostObject query targets, cached wrappers, and
  asynchronous exception metadata remain blind spots

M11-5 is verified and merged into `master` on the fork (`pakeli/iv8-runtime`,
merge point `6f4f6dc`). M11-6 through M11-10 were verified as one five-slice
batch on branch `m11-6-event-subclasses` (final commit `35c85fe`) and merged to
the fork's `master`. M11-11 was verified on `m11-11-event-capture` (commit
`a269aa9`) and merged to the fork's `master`. M11-12 through M11-17 were
verified as one six-slice DOM/web batch on branch
`m11-12-17-dom-web-batch` (final commit `8d606e9`) and merged to the fork's
`master`. M11-18 (form-control attribute reflection) was verified on
`m11-18-form-control-reflection` (final commit `08350ce`) and merged to the
fork's `master`. M11-19 through M11-23 (DOM/form completeness batch) were
verified on `m11-19-23-dom-form-batch` (final commit `999c851`) and merged to
the fork's `master`. M11-24 through M11-28 (host environment / console batch)
were verified on `m11-24-28-host-environment-batch` (final commit `a00e140`)
and merged to the fork's `master`. M11-29 (deterministic event-loop facade) was
verified on `m11-29-event-loop-facade` (final commit `b211481`) and merged to
the fork's `master`. M11-30 (rAF/idle facade integration) was verified on
`m11-30-frame-idle-facade` (final commit `7dde955`) and merged to the fork's
`master`. M11-31 / M11-32 (virtual performance clock + form validation) were
verified as one batch on `m11-31-32-perf-validation-batch` (final commit
`a88b69d`) and merged to the fork's `master`. M11-33 (OS-backed crypto random)
was verified on `m11-33-crypto-random` (commit `c9b2fb5`) and merged to the
fork's `master`. M11-34 (CSS selector engine) was verified on
`m11-34-selector-engine` (final commit `49f8582`) and merged to the fork's
`master`. M11-35 (BOM metadata completion) was verified on
`m11-35-bom-metadata` (commit `aa23c57`) and merged to the fork's `master`.
M11-36 through M11-40 (BOM completion batch) were verified on
`m11-36-40-bom-completion` (final commit `512117c`) and merged to the fork's
`master`. M11-41 through M11-45 (BOM viewport/media/device metadata) were
verified on `m11-41-45-bom-extras` (final commit `61abc3b`) and merged to the
fork's `master`. The post-M11 host access trace steps 1–4 were verified on
`host-access-probes` (commit `8995b89`) with Linux run `41017981567` and
Windows run `41017987566`, each wheel lane `1656 passed, 2 skipped`. The first
step-5 group (performance/crypto/Storage native calls) was verified on
`host-access-bom-calls` (commit `ac80ee8`) with Linux run `41019395760` and
Windows run `41019399775`, each wheel lane `1660 passed, 2 skipped`. The
intrinsic window metadata group was verified on `host-access-window` (commit
`e19f54d`) with Linux run `41020686185` and Windows run `41020689881`, each
wheel lane `1664 passed, 2 skipped`. The nested metadata/timeOrigin group was
verified on `host-access-nested` (commit `924effd`) with Linux run `41022649736`
and Windows run `41022653765`, each wheel lane `1667 passed, 2 skipped`. The
static visualViewport group was verified on `host-access-visual-viewport`
(commit `c87ffbe`) with Linux run `41025891916` and Windows run `41025896654`,
each wheel lane `1671 passed, 2 skipped`. The navigator.storage manager group
was verified on `host-access-storage-manager` (commit `f91665f`) with Linux run
`41027870583` and Windows run `41027874811`, each wheel lane `1675 passed,
2 skipped`. The navigator.permissions/PermissionStatus group was verified on
`host-access-permissions` (commit `18e674e`) with Linux run `41031830520` and
Windows run `41031835192`, each wheel lane `1680 passed, 2 skipped`. The
matchMedia result group was verified on `host-access-match-media` (commit
`7d35a30`) with Linux run `41032945394` and Windows run `41032950281`, each
wheel lane `1683 passed, 2 skipped`. The legacy navigator metadata and window
alias groups were verified together on `host-access-window-aliases` (final
commit `b936307`) with Linux run `41035577072` and Windows run `41035582564`,
each wheel lane `1690 passed, 2 skipped`. The step-6a value/argument/return
summary slice was verified on `host-access-values` (commit `4d0800b`) with
Linux run `41383785169`, Windows run `41383788221`, and macOS arm64 run
`41384345701`, each wheel lane `1696 passed, 2 skipped`. The method-reference
slice was verified on `host-access-method-reads` (commit `c13bdf5`) with Linux
run `41395700932`, Windows run `41395704544`, and macOS arm64 run `41396277098`,
each wheel lane `1699 passed, 2 skipped`. The step-6c delete/enumeration/define
operation slice was verified on `host-access-operations` against the tree of
commit `96c7c00` (release-tested tree `2addf62`) with Linux run `41399273630`,
Windows run `41399284773`, and macOS arm64 run `41399294680`, each wheel lane
`1704 passed, 2 skipped`. The remaining step-6d–6h batch (explicit queries,
Reflect target metadata, native summaries, and synchronous exception metadata)
was verified against the tree of commit `bd148c2` (release-tested tree
`72c00ed`) with Linux run `41406392044`, Windows run
`41406401506`, and macOS arm64 run `41406410844`, each wheel lane
`1714 passed, 2 skipped`. The step-7 aggregated report was verified against the tree of commit
`c678ede` (release-tested tree `62af2e6`) with Linux run `41426804187` and
Windows run `41426817304`, each wheel lane `1720 passed, 2 skipped`. macOS arm64
macOS arm64 fallback release run `41466841016` (macos-14, cached V8 monolith,
`macosx_11_0_arm64` wheels) also reported `1720 passed, 2 skipped`; the official
macos-15 run `41426828872` was cancelled while queued by runner availability.
M11-46 (`document.cookie`) was verified on `m11-46-document-cookie` (tree
`18f1f4b`) with Linux run `41479330002`, Windows run `41479338268`, and macOS
arm64 fallback run `41482460847` (macos-14, cached V8 monolith,
`macosx_11_0_arm64` wheels), each wheel lane `1725 passed, 2 skipped` and the
five new cookie tests present. The first attempt (`f92887c`) failed only one new
descriptor assertion that assumed native data properties expose `get`/`set`;
`18f1f4b` corrected the test to the framework's materialized data descriptor
(the descriptor read evaluates the getter) without changing runtime behavior.
M11-47 (Document identity) was verified on `m11-47-document-identity` (tree
`cc1c47d`) with Linux run `41483937440`, Windows run `41483943983`, and macOS
arm64 fallback run `41485210986` (macos-14, cached V8 monolith,
`macosx_11_0_arm64` wheels), each wheel lane `1731 passed, 2 skipped` and the
six new tests present; all lanes passed on the first attempt. M11-48 (Node
foundation) was verified on `m11-48-node-foundation` (tree `3f4940f`) with Linux
run `41562778133` and Windows run `41562781320`, each wheel lane `1744 passed,
2 skipped`; the first attempt failed only two new-test expectations (absent
`document.contains`, and a Python int/str comparison), corrected without runtime
changes. macOS arm64 fallback run `41563750355` (macos-14, cached V8 monolith,
`macosx_11_0_arm64` wheels) also reported `1744 passed, 2 skipped` per lane;
artifact IDs are in `docs/m11_48_report.md`. M11-49 (HTML string APIs) was
verified on `m11-49-html-strings` (tree `52bafab`) with Linux run `41577570846`,
Windows run `41577574466`, and macOS arm64 fallback run `41578427318`, each
wheel lane `1756 passed, 2 skipped`; the first attempts fixed a missing setter
name guard and two test expectations, with no remaining runtime issues. M11-50
(Window scroll/focus/UI completion) was verified on
`m11-50-window-completion` (tree `6d2250e`) with Linux run `41597535300`,
Windows run `41597541345`, and macOS arm64 fallback run `41598354469`, each
wheel lane `1766 passed, 2 skipped`; all lanes passed on the first attempt.
M11-51 (Window event handler attributes) was verified on
`m11-51-window-event-handlers` (tree `34b119b`) with Linux run `41612087122`,
Windows run `41612092772`, and macOS arm64 fallback run `41613300180`, each
wheel lane `1777 passed, 2 skipped`. The first attempt (`2924367`) failed 6 new
tests because handlers were keyed by `on<type>` instead of `<type>`;
`34b119b` normalized the key with no test changes. M11-52 (Window messaging) was
verified on `m11-52-window-messaging` (tree `bc00040`) with Linux run
`41620346617`, Windows run `41620356968`, and macOS arm64 fallback run
`41621413284`, each wheel lane `1787 passed, 2 skipped`; all lanes passed on
the first attempt.
Upstream `investforever/iv8-runtime` remains at M11-3;
upstream sync is out of scope and the fork is the working master. See the
report documents (`docs/m11_6_report.md` … `docs/m11_52_report.md`) and
`docs/host_access_trace.md`.

This means the project is no longer only a `JSContext` kernel. It now includes a
hosted `Page` runtime, a minimal DOM/form/web-platform layer, and a second-layer
DevTools/monitoring surface.

Post-M10 work is organized as **M11, a compatibility-first line**: it reproduces the
community-edition capability surface of `HanZzzzz000/iv8`, then extends beyond it
deliberately. `M11-1` is the first implementation slice under that line.

## 2. What Is Actually Implemented

### M1 core

Implemented and present at `HEAD`:

- process-wide V8 initialization;
- one isolate per `JSContext`;
- `JSContext` lifecycle and idempotent disposal;
- `eval()` with resource names;
- structured JavaScript exceptions;
- value conversion and `JSValue` fallback;
- context busy/disposed guards;
- focused threading behavior.

### M2–M8 first-layer runtime

Implemented and present at `HEAD`:

- `Page` host-object container;
- `window` / `globalThis` / `self` / `console`;
- `navigator` / `location`;
- manual timers and microtask pumping;
- `Page.load()` static load model;
- minimal `document` / `element` tree;
- targeted DOM mutation and query helpers;
- external scripts, inline scripts, lifecycle, ready-state, current-script;
- events and event bubbling subset;
- collections, structure navigation, matching/query APIs;
- form ownership and form control value/default/reset behavior;
- form submission metadata methods/properties;
- `TextEncoder` / `TextDecoder`;
- `URL` / `URLSearchParams`;
- `atob` / `btoa`.

### M9–M10 and post-M11 second-layer monitoring

Implemented and present at `HEAD`:

- `Page.devtools_url()` Inspector attach base;
- `Page.watch_apis()` / `read_watch_api_hits()`;
- `Page.watch_reflect_get()` / `read_reflect_get_hits()`;
- `Page.watch_reflect_has()` / `read_reflect_has_hits()`;
- `Page.watch_reflect_ownkeys()` / `read_reflect_ownkeys_hits()`;
- `Page.watch_object_keys()` / `read_object_keys_hits()`;
- optional `break_on_hit=True` pause behavior when a DevTools client is attached.

Post-M11 monitoring extension (verified `2026-11-02`):

- `Page.start_access_trace()` / `Page.stop_access_trace()` /
  `Page.read_access_trace()` — native HostObject and traced native get/set/call
  events, missing string-key `get_probe`/`resolution: "missing"` events,
  explicit has/descriptor queries, Reflect target metadata, completion
  summaries, and synchronous exception metadata, with generation/object
  identity, capacity/drop accounting, object-type filters, and source
  attribution. It is a separate channel from the five watch APIs; remaining
  plain-object property reads, the `in` operator, and other documented blind
  spots are not covered. See `docs/host_access_trace.md`.
- `Page.access_trace_report()` — a pure-Python aggregated dynamic-run report
  with access counts, missing candidates, first-sequence/source metadata,
  coverage boundaries, capacity/filter/stopped warnings, and caller-supplied
  event-loop progress. See `docs/host_access_trace_workflow.md`.

WIN-MON-01 extends the implemented monitoring surface to the 49 mapped PRES-03
Window globals, 22 callable globals and scoped shell calls/accessors. Local
acceptance has passed; combined release verification is recorded above. Its boundaries and unchanged native
data descriptors/identities are specified in `docs/window_monitoring.md`.

### M11 compatibility line

Implemented and present at `HEAD`:

- `M11-1` configurable host-environment baseline — the optional `Page(environment=...)`
  constructor parameter plus the static `Page.get_default_environment()`. Eleven keys
  configure the read-only `navigator` identity values, the new `screen` global, and
  the `window.devicePixelRatio` / `innerWidth` / `innerHeight` dimensions. Defaults
  reproduce the previous fixed `navigator` constants, so an unconfigured page is
  unchanged; values persist across `load()`; validation is strict and Python-side.
  See `docs/m11_1_host_environment.md`.
- `M11-2` animation frames — the JS globals `requestAnimationFrame` /
  `cancelAnimationFrame` plus the Python pump `Page.run_animation_frame()`. A manual
  frame model like the M2-4 timers: no clock, no background thread, no automatic loop;
  one call runs one frame (callbacks queued when it began, in registration order), and
  a re-request lands in the next frame. The queue is independent of the timers and
  microtasks, with its own handle space. See `docs/m11_2_animation_frames.md`.
- `M11-3` idle callbacks — the JS globals `requestIdleCallback` /
  `cancelIdleCallback` plus the Python pump `Page.run_idle_callbacks()`. Same manual
  model as M11-2: one call runs one round, a re-request lands in the next round. The
  callback receives a minimal `IdleDeadline`-style object (`didTimeout` always false,
  `timeRemaining()` a fixed 50ms) — deterministic, since there is no clock.
  `options.timeout` is type-validated but has **no** firing semantics. See
  `docs/m11_3_idle_callbacks.md`.
- `M11-4` microtasks — the JS global `queueMicrotask(callback)` feeding the existing
  V8 microtask queue; `Page.run_jobs()` is still the only trigger, so there is no new
  Python API and no fifth pump. A non-callable argument throws a JS `TypeError` before
  enqueueing; callbacks run FIFO, interleave with `Promise` reactions, and a throwing
  callback is swallowed by V8's explicit checkpoint. Landed and verified on real V8
  (Linux + Windows) on `2026-10-27`; see `docs/m11_4_queue_microtask.md` and
  `docs/m11_4_report.md`.
- `M11-5` mutation observers — the JS global `MutationObserver` with
  `observe(target, options)` / `disconnect()` / `takeRecords()`. Records cover
  `childList` and `attributes` (including `subtree`, `attributeOldValue`,
  `attributeFilter`), are queued in mutation order, and are delivered only by the
  existing `Page.run_jobs()` microtask checkpoint. Element and document targets
  are accepted; M11-5 accepted `characterData` but was inert in the element-only
  model (M11-48 later activated it for Text/Comment nodes).
  There is no new Python API, no fifth pump, and no `MutationRecord` global;
  observer state is per generation. Verified on real V8 (Linux + Windows) on
  `2026-10-28`; see `docs/m11_5_mutation_observer.md` and `docs/m11_5_report.md`.
- `M11-6` event subclasses — `CustomEvent` (`detail`), `MouseEvent`
  (`clientX` / `clientY` / `button` / `buttons`), `KeyboardEvent`
  (`key` / `code` / `repeat`), and `FocusEvent` (`relatedTarget`) on the
  existing Event / EventTarget / bubbling model. Subclass fields are own
  read-only values; constructors require `new`. Verified `2026-10-28`; see
  `docs/m11_6_event_subclasses.md`.
- `M11-7` Web Storage — `localStorage` / `sessionStorage` as independent,
  insertion-ordered, per-Page in-memory stores. Entries survive `Page.load()`
  generations of one Page; no disk, origin keying, `storage` events, or
  cross-Page sharing. Verified `2026-10-28`; see
  `docs/m11_7_web_storage.md`.
- `M11-8` Blob / File / FileReader — immutable byte value objects with
  `size` / `type` / `name` / `lastModified`, `slice`, and Promise-returning
  `text` / `arrayBuffer`, plus a minimal `FileReader` completed by
  `Page.run_jobs()` and listener events. Verified `2026-10-28`; see
  `docs/m11_8_blob_file_reader.md`.
- `M11-9` event cancellation — `cancelable` / `defaultPrevented` /
  `preventDefault()` / `stopImmediatePropagation()`, with `dispatchEvent`
  returning `false` for a canceled cancelable event. Capture phase and default
  actions remain out of scope. Verified `2026-10-28`; see
  `docs/m11_9_event_cancellation.md`.
- `M11-10` classList / toggleAttribute — `element.classList`
  (`contains` / `add` / `remove` / `toggle` / `replace` / `item` / `length` /
  `value`) and `element.toggleAttribute(name, force?)`, both wired into the
  MutationObserver attribute hooks. Verified `2026-10-28`; see
  `docs/m11_10_class_list.md`.
- `M11-11` event capture + listener options — the capture phase, `eventPhase`
  (0/1/2/3), and `capture` / `once` / `passive` registration options, with
  `(type, callback, capture)` listener identity. Capture runs from window
  through the tree even for non-bubbling events; `once` removes before the call
  and `passive` suppresses `preventDefault` for that invocation. Verified
  `2026-10-28`; see `docs/m11_11_event_capture.md`.
- `M11-12`..`M11-17` DOM/web completeness batch — writable `id` / `className`,
  `hasAttributes()` / `getAttributeNames()`, `replaceChild()` / `remove()` /
  `hasChildNodes()` / raw sibling aliases, `cloneNode()` / `isSameNode()` /
  `isEqualNode()`, non-live `URL.searchParams`, `select.options` /
  `selectedIndex` / `multiple` / `size`, and nested `Page(environment=...)` +
  `Page.get_defaults()`. Verified `2026-10-28`; see
  `docs/m11_12_17_dom_web_batch.md`.
- `M11-18` form-control attribute reflection — input `type` / `disabled` /
  `readOnly` / `placeholder`, button `type` / `disabled`, `form.encoding`
  (enctype alias), and `option.label` / read-only `index`; reflected writes
  feed the MutationObserver attribute hooks. Verified `2026-10-28`; see
  `docs/m11_18_form_control_reflection.md`.
- `M11-19`..`M11-23` DOM/form completeness batch — multi-token class queries
  and element-level `getElementsByClassName`; `document.getElementsByName`;
  extended form-control reflection; select `length` / `item` / `namedItem`;
  deterministic document metadata (`characterSet` / `contentType` /
  `compatMode` / `referrer` / `domain` / `visibilityState` / `hidden` /
  `activeElement` / `hasFocus()`). Verified `2026-10-28`; see
  `docs/m11_19_23_dom_form_batch.md`.
- `M11-24`..`M11-28` host environment / console batch — configurable
  navigator metadata/identity, screen color/pixel depth, outer window geometry,
  and deterministic console method extensions. Verified `2026-10-28`; see
  `docs/m11_24_28_host_environment_batch.md`.
- `M11-29` deterministic event-loop facade — `tick()` / `advance(ms)` /
  `run_until_idle()` / `event_loop_time_ms` over timers and microtasks, with
  virtual time, due ordering, microtask checkpoints after each task, and
  bounded interval handling. Verified `2026-10-29`; see
  `docs/m11_29_event_loop_facade.md`.
- `M11-30` rAF/idle facade integration — animation frames at fixed 16ms virtual
  boundaries, one idle round when no timer/frame is due, microtask checkpoints
  after every task, and bounded self-rescheduling work. Legacy pumps remain
  independent. Verified `2026-10-29`; see
  `docs/m11_30_frame_idle_facade.md`.
- `M11-31` virtual performance clock — `performance.now()` / `timeOrigin` over
  the facade clock and virtual rAF callback timestamps. Verified `2026-10-29`;
  see `docs/m11_31_32_perf_validation_batch.md`.
- `M11-32` minimal form validation — `willValidate` / `validity` /
  `validationMessage` / `checkValidity()` / `reportValidity()` /
  `setCustomValidity()`, `required` / length / email-URL constraints, form
  aggregation, and `invalid` events. Verified `2026-10-29`; see
  `docs/m11_31_32_perf_validation_batch.md`.
- `M11-33` OS-backed Crypto random — `crypto.getRandomValues()` fills integer
  TypedArrays, `crypto.randomUUID()` returns a v4 UUID, and the platform CSPRNG
  is the deliberate non-deterministic exception. No `crypto.subtle` yet.
  Verified `2026-10-29`; see `docs/m11_33_crypto_random.md`.
- `M11-34` CSS selector engine expansion — selector lists, compound selectors,
  descendant/child/sibling combinators, attribute selectors, and `*`, shared by
  `querySelector[All]`, `matches()`, and `closest()`. Verified `2026-10-29`; see
  `docs/m11_34_selector_engine.md`.
- `M11-35` deterministic BOM metadata — `window.name` / `length` /
  `screenLeft` / `screenTop` / `isSecureContext`; `screen.availLeft` /
  `availTop` / `orientation`; navigator `maxTouchPoints` /
  `pdfViewerEnabled` / `doNotTrack` / `connection`. The environment grows to 36
  keys. Verified `2026-10-30`; see `docs/m11_35_bom_metadata.md`.
- `M11-36`..`M11-40` BOM completion — deterministic dialogs;
  `performance.mark` / `measure` / entry queries / clears; bounded
  `structuredClone`; single-context `window.parent` / `top` / `frames` /
  `opener`; and `scheduler.postTask`. Verified `2026-10-30`; see
  `docs/m11_36_40_bom_completion.md`.
- `M11-41`..`M11-45` BOM viewport/media/device metadata — `visualViewport`
  and window flags; bounded `matchMedia`; deterministic `navigator.storage` /
  `navigator.permissions`; empty `navigator.plugins` / `mimeTypes` and
  `javaEnabled()`. Verified `2026-10-30`; see
  `docs/m11_41_45_bom_extras.md`.

## 3. Latest Known Milestone Sequence

Recent implementation sequence from Git history:

- `M7-1` through `M7-7`: form submission metadata and no-op submission methods
- `M8-1` through `M8-4`: text encoding, URL, URLSearchParams, Base64 globals
- `M9-1` through `M9-6`: Inspector attach base and watch API family
- `M10-1` and `M10-2`: reflection/key enumeration watch extensions
- `M11-1`: configurable host-environment baseline (first M11 compatibility slice)
- `M11-2`: `requestAnimationFrame` / `cancelAnimationFrame` manual frame pump
- `M11-3`: `requestIdleCallback` / `cancelIdleCallback` manual idle pump
- `M11-4`: `queueMicrotask` on the existing manual `run_jobs()` microtask pump
  (verified `2026-10-27`)
- `M11-5`: `MutationObserver` with `run_jobs()` delivery batches
  (verified `2026-10-28`)
- `M11-6` through `M11-10`: event subclasses, Web Storage,
  Blob/File/FileReader, event cancellation, and classList/toggleAttribute —
  verified as one five-slice batch (`2026-10-28`)
- `M11-11`: event capture phase and `capture` / `once` / `passive` listener
  options (verified `2026-10-28`)
- `M11-12` through `M11-17`: element attribute API, tree-edit conveniences,
  cloneNode/equality, `URL.searchParams` snapshot, select extension, and nested
  environment + `get_defaults()` — verified as one six-slice batch
  (`2026-10-28`)
- `M11-18`: form-control attribute reflection (verified `2026-10-28`)
- `M11-19` through `M11-23`: DOM/form completeness batch — query extensions,
  form reflection, select convenience, and document metadata (verified as one
  five-slice batch, `2026-10-28`)
- `M11-24` through `M11-28`: host environment / console batch — navigator
  metadata/identity, screen depth, outer window geometry, and console methods
  (verified as one five-slice batch, `2026-10-28`)
- `M11-29`: deterministic event-loop facade (verified `2026-10-29`)
- `M11-30`: rAF/idle integration into the facade (verified `2026-10-29`)
- `M11-31`: virtual performance clock + rAF timestamps (verified `2026-10-29`)
- `M11-32`: minimal form validation API (verified `2026-10-29`)
- `M11-33`: OS-backed Crypto random (`getRandomValues` / `randomUUID`;
  verified `2026-10-29`)
- `M11-34`: CSS selector engine expansion (verified `2026-10-29`)
- `M11-35`: deterministic BOM metadata completion (verified `2026-10-30`)
- `M11-36` through `M11-40`: BOM completion batch — dialogs, performance
  timeline, structuredClone, single-context window metadata, and
  scheduler.postTask (verified `2026-10-30`)
- `M11-41` through `M11-45`: BOM viewport/media/device metadata — visualViewport,
  matchMedia, navigator.storage/permissions, and legacy navigator metadata
  (verified `2026-10-30`)
- Post-M11 host access trace (steps 1–4, the complete step-5 object groups, steps 6a–6h, and step 7):
  native HostObject get/set/call, missing string-key probe,
  performance/crypto/Storage calls, window metadata accessors, nested
  connection/orientation/timeOrigin reads, static visualViewport
  metadata/handler/method accesses, navigator.storage manager method calls,
  navigator.permissions query calls plus PermissionStatus accesses, matchMedia
  result reads/method calls, legacy navigator metadata accesses, window
  parent/top/frames/opener get/set events, step-6a value/argument/return
  summaries with returned/threw outcomes, step-6b method-reference reads,
  step-6c `delete`/`own_keys`/`define_property` request events, step-6d explicit
  has/descriptor query events, step-6f Reflect target metadata, step-6g native
  summaries, and step-6h synchronous exception metadata (verified `2026-11-02`)

## 4. Release / Distribution Status

Current known status from repository docs:

- Linux release strategy was chosen in `docs/phase9_release_strategy.md`.
- Linux is the real release target; the repository records green Linux wheel
  results for cp311–cp314.
- Windows and macOS arm64 now have verified V8-linked release lanes as well:
  `release-windows.yml` and `release-macos.yml` (macos-15 arm64,
  `macosx_11_0_arm64` wheels). They remain non-primary release targets unless
  explicitly promoted.
- macOS arm64 verification (`2026-10-30`): Linux-independent run `41043433081`
  built the pinned V8 monolith on `macos-15` and produced cp311–cp314
  `macosx_11_0_arm64` wheels; every lane reported `1690 passed, 2 skipped`.
  Intel/x64 and universal2 macOS wheels are not built.

## 5. Documentation Layering (reconciled)

The documentation layering mismatch that used to mislead new sessions is now
addressed, not just recorded:

- `docs/runtime_surface.md` is the **overview index** of the surface that actually
  exists at `HEAD`, with a milestone map and links to every frozen boundary;
- the four M1-era documents — `api_contract.md`, `architecture.md`,
  `implementation_plan.md`, `test_plan.md` — each carry a **scope note** at the top
  stating what part of them is still authoritative and what is historical;
- `AGENTS.md` and `CLAUDE.md` point at the index and no longer read as M1-only
  constraints.

`docs/api_contract.md` remains the authoritative contract for `JSContext`. Do not
“downgrade” the repository mentally to M1 because that file is the oldest formal
contract. Cross-check with:

- `docs/runtime_surface.md` (start here)
- `python/iv8/__init__.py` and `python/iv8/page.py`
- the milestone summaries — `docs/m3_summary.md`, `docs/m4_a_summary.md`,
  `docs/m4_b_summary.md`, `docs/m5_summary.md`, `docs/m6_summary.md`,
  `docs/m7_summary.md`, `docs/m8_summary.md`, `docs/m9_summary.md`,
  `docs/m10_1_watch_reflect_ownkeys.md`, `docs/m10_2_watch_object_keys.md`,
  `docs/m11_1_host_environment.md`, `docs/m11_2_animation_frames.md`,
  `docs/m11_3_idle_callbacks.md`, `docs/m11_4_queue_microtask.md`,
  `docs/m11_5_mutation_observer.md`, `docs/m11_6_event_subclasses.md`,
  `docs/m11_7_web_storage.md`, `docs/m11_8_blob_file_reader.md`,
  `docs/m11_9_event_cancellation.md`, `docs/m11_10_class_list.md`,
  `docs/m11_11_event_capture.md`, `docs/m11_12_17_dom_web_batch.md`
- recent `git log`

## 6. Next planned phase

The repository is executing its post-M10 direction:

- `M11` is the current milestone line and is compatibility-first, not ad hoc runtime
  growth;
- the planning artifact is `docs/m11_compatibility_plan.md`;
- `M11-1` (configurable host-environment baseline) has **landed** — see
  `docs/m11_1_host_environment.md`;
- `M11-2` (`requestAnimationFrame` / `cancelAnimationFrame` manual frame pump) has
  **landed** — see `docs/m11_2_animation_frames.md`;
- `M11-3` (`requestIdleCallback` / `cancelIdleCallback` manual idle pump) has
  **landed** — see `docs/m11_3_idle_callbacks.md` and `docs/m11_3_report.md`;
- `M11-4` (`queueMicrotask` on the existing `run_jobs()` microtask pump) is
  **landed and verified on real V8 (Linux + Windows)** on `2026-10-27`; see
  `docs/m11_4_queue_microtask.md` and `docs/m11_4_report.md`;
- `M11-5` (`MutationObserver` on the existing `run_jobs()` microtask checkpoint) is
  **landed and verified on real V8 (Linux + Windows)** on `2026-10-28`; see
  `docs/m11_5_mutation_observer.md` and `docs/m11_5_report.md`;
- `M11-6` (Event subclasses), `M11-7` (Web Storage), `M11-8`
  (`Blob` / `File` / `FileReader`), `M11-9` (Event cancellation), and `M11-10`
  (`classList` / `toggleAttribute`) are **landed and verified on real V8
  (Linux + Windows)** on `2026-10-28` in one five-slice batch; see their
  boundary and report docs;
- `M11-11` (event capture phase + listener options) is **landed and verified
  on real V8 (Linux + Windows)** on `2026-10-28`; see
  `docs/m11_11_event_capture.md` and `docs/m11_11_report.md`;
- `M11-12` through `M11-17` (attribute API, tree editing, cloneNode/equality,
  `URL.searchParams` snapshot, select extension, nested environment +
  `get_defaults()`) are **landed and verified on real V8 (Linux + Windows)** on
  `2026-10-28` as one six-slice batch; see
  `docs/m11_12_17_dom_web_batch.md` and `docs/m11_12_17_report.md`;
- `M11-18` (form-control attribute reflection) is **landed and verified on
  real V8 (Linux + Windows)** on `2026-10-28`; see
  `docs/m11_18_form_control_reflection.md` and `docs/m11_18_report.md`;
- `M11-19` through `M11-23` (DOM/form completeness batch) are **landed and
  verified on real V8 (Linux + Windows)** on `2026-10-28`; see
  `docs/m11_19_23_dom_form_batch.md` and `docs/m11_19_23_report.md`;
- `M11-24` through `M11-28` (host environment / console batch) are **landed and
  verified on real V8 (Linux + Windows)** on `2026-10-28`; see
  `docs/m11_24_28_host_environment_batch.md` and
  `docs/m11_24_28_report.md`;
- `M11-29` (deterministic event-loop facade) is **landed and verified on real
  V8 (Linux + Windows)** on `2026-10-29`; see
  `docs/m11_29_event_loop_facade.md` and `docs/m11_29_report.md`;
- `M11-30` (rAF/idle integration into the facade) is **landed and verified on
  real V8 (Linux + Windows)** on `2026-10-29`; see
  `docs/m11_30_frame_idle_facade.md` and `docs/m11_30_report.md`;
- `M11-31` / `M11-32` (virtual performance clock + form validation) are
  **landed and verified on real V8 (Linux + Windows)** on `2026-10-29` as one
  batch; see `docs/m11_31_32_perf_validation_batch.md` and
  `docs/m11_31_32_report.md`;
- `M11-33` (OS-backed `crypto.getRandomValues` / `randomUUID`) is **landed and
  verified on real V8 (Linux + Windows)** on `2026-10-29`; see
  `docs/m11_33_crypto_random.md` and `docs/m11_33_report.md`;
- `M11-34` (CSS selector engine expansion) is **landed and verified on real V8
  (Linux + Windows)** on `2026-10-29`; see `docs/m11_34_selector_engine.md` and
  `docs/m11_34_report.md`;
- `M11-35` (deterministic BOM metadata completion) is **landed and verified on
  real V8 (Linux + Windows)** on `2026-10-30`; see
  `docs/m11_35_bom_metadata.md` and `docs/m11_35_report.md`;
- `M11-36` through `M11-40` (BOM completion batch) are **landed and verified on
  real V8 (Linux + Windows)** on `2026-10-30`; see
  `docs/m11_36_40_bom_completion.md` and `docs/m11_36_40_report.md`;
- `M11-41` through `M11-45` (BOM viewport/media/device metadata) are **landed
  and verified on real V8 (Linux + Windows)** on `2026-10-30`; see
  `docs/m11_41_45_bom_extras.md` and `docs/m11_41_45_report.md`;
- the post-M11 host access trace steps 1–4 (native get/set/call plus HostObject
  missing-property probes) are **landed and verified on real V8 (Linux +
  Windows)** on `2026-10-30`; see `docs/host_access_trace.md` and
  `docs/host_access_tracing_plan.md`;
- the first step-5 group (performance/crypto/Storage native method calls and
  Storage `length`) is **landed and verified on real V8 (Linux + Windows)** on
  `2026-10-30`;
- the intrinsic window metadata group is **landed and verified on real V8
  (Linux + Windows)** on `2026-10-30`;
- the nested connection/orientation/`performance.timeOrigin` group is **landed
  and verified on real V8 (Linux + Windows)** on `2026-10-30`;
- the static `visualViewport` HostObject group is **landed and verified on real
  V8 (Linux + Windows)** on `2026-10-30`;
- the `navigator.storage` manager group (parent reads plus
  estimate/persisted/persist calls, plain-object shape preserved) is **landed
  and verified on real V8 (Linux + Windows)** on `2026-10-30`;
- the `navigator.permissions` / PermissionStatus group (parent reads, query
  calls, status name/state gets and method calls) is **landed and verified on
  real V8 (Linux + Windows)** on `2026-10-30`;
- the matchMedia result group (call/get/method/probe events over cached
  query backings) is **landed and verified on real V8 (Linux + Windows)** on
  `2026-10-30`;
- the legacy navigator metadata and window parent/top/frames/opener groups are
  **landed and verified on real V8 (Linux + Windows)** on `2026-10-30`;
- all planned step-5 object groups are now complete;
- step 6a (primitive value/argument/return summaries and returned/threw
  outcomes for generic HostObject get/set/call) is **landed and verified on real
  V8 (Linux + Windows)** on `2026-11-02`;
- step 6b (method-reference reads with adjacent-call folding) is **landed and
  verified on real V8 (Linux + Windows + macOS arm64)** on `2026-11-02`;
- step 6c (delete/enumeration/define request events on HostObject members) is
  **landed and verified on real V8 (Linux + Windows + macOS arm64)** on
  `2026-11-02`;
- step 6d (explicit has/descriptor query wrappers), step 6f (Reflect target
  metadata), step 6g (native callback/getter summaries), and step 6h
  (synchronous exception metadata) are **landed and verified on real V8 (Linux +
  Windows + macOS arm64)** on `2026-11-02`;
- steps 6a–6h are complete for the traceable HostObject surface. The `in`
  operator, non-HostObject query targets, query wrappers cached before tracing
  starts, asynchronous exception metadata, and general call-site
  locations/timing are explicit blind spots;
- step 7 (aggregated usage report and workflow) is **landed and verified on real
  V8 (Linux + Windows release lanes; macOS arm64 macos-14 fallback release)** on
  `2026-11-02`; see `docs/host_access_trace_workflow.md`;
- M11-46 (`document.cookie`, the first Document slice of the object-oriented
  common-API track) is **landed and verified on real V8 (Linux + Windows +
  macOS arm64)** on `2026-11-02`; see `docs/m11_46_document_cookie.md` and
  `docs/m11_46_report.md`;
- M11-47 (writable `document.title` plus `documentURI` / `baseURI` /
  `defaultView` / `location` aliases) is **landed and verified on real V8
  (Linux + Windows + macOS arm64)** on `2026-11-02`; see
  `docs/m11_47_document_identity.md` and `docs/m11_47_report.md`;
- M11-48 (Node foundation: text/comment/fragment nodes, live `textContent`,
  `characterData` observers) is **landed and verified on real V8 (Linux +
  Windows + macOS arm64)** on `2026-11-03`; see
  `docs/m11_48_node_foundation.md` and `docs/m11_48_report.md`;
- M11-49 (HTML string APIs and variadic tree methods) is **landed and verified
  on real V8 (Linux + Windows + macOS arm64)** on `2026-11-03`; see
  `docs/m11_49_html_strings.md` and `docs/m11_49_report.md`;
- M11-50 (Window scroll/focus/UI completion) is **landed and verified on real
  V8 (Linux + Windows + macOS arm64)** on `2026-11-03`; see
  `docs/m11_50_window_completion.md` and `docs/m11_50_report.md`;
- M11-51 (Window event handler attributes) is **landed and verified on real
  V8 (Linux + Windows + macOS arm64)** on `2026-11-03`; see
  `docs/m11_51_window_event_handlers.md` and `docs/m11_51_report.md`;
- M11-52 (Window messaging) is **landed and verified on real V8 (Linux +
  Windows + macOS arm64)** on `2026-11-03`; see
  `docs/m11_52_window_messaging.md` and `docs/m11_52_report.md`;
- the latest implemented/release-tested batch is **ENV-MON-01 + Page debug/prod modes**;
  strict coverage gaps remain open in `docs/environment_monitoring_report.md`.
  The earlier WIN-MON-01 batch adds structured get/set/call coverage for
  the mapped globals and call-only watches for native globals/shell methods;
  `docs/window_monitoring.md` records the native-entry and data-property limits.
  **PRES-03 Window ordinary-member presence** has previous local acceptance;
  its combined release is verified. Its 49 names include native
  globals, deterministic flags/constants, private-prototype object shells,
  prefixed rAF and bounded event/reportError integration. The 821 missing global
  interface entries remain excluded. PRES-01/PRES-02 retain previous local
  acceptance; BIND-01 remains the release-verified and deployed SDK. M11-53
  focus/visibility remains a numbered candidate. Presence does not establish
  subsystem execution or business acceptance.
  Subsequent development is BOM-first: Window/Location/Navigator/History and
  related objects, with further node/Web-data work deferred. Current queue:
  `docs/bom_coverage_plan.md`; gaps: `docs/dom_bom_gaps.md`.

## 7. Current Open Questions

These are the questions a new coding session should resolve before major new work:

- Which bounded strict-use coverage repair should follow the verified ENV-MON-01 /
  Page-mode candidate? The remaining gaps are in `docs/environment_monitoring_report.md`.
  New Navigator/BOM breadth remains a separate family queue in
  `docs/bom_coverage_plan.md`.
  `M11-1` through `M11-52`
  remain release-verified; the combined build above is release-verified; BIND-01 is the deployed SDK.
  PRES-01/PRES-02 have prior local acceptance; PRES-03 now implements the 49
  ordinary Window names, with local acceptance passed; combined release verified above. The
  user excludes the 821 global interface entries. Document `write`, XML parsing/
  serialization and the selector follow-up remain separate work. Configured
  UAData presence does not complete browser device/profile behavior.
- Should the `in` operator, non-HostObject query targets, or wrapper-cached
  references ever be covered through a Proxy/AST design, or remain explicit
  out-of-scope blind spots? (Steps 6a–6h cover the explicit has/descriptor APIs
  and HostObject surface; see `docs/host_access_trace.md` §4.14/§7.)
- Should `Page` gain a single frozen contract document, or keep the milestone-doc
  approach? (`docs/decision_log.md` §B — still open. `docs/runtime_surface.md` is an
  *index*, not a contract, so it does not settle this.)
- Should Windows release support remain secondary, or be promoted again?
  (`docs/decision_log.md` §C — still open.)
- Should the stale build/CI comments that still mention early/M1-era limitations be
  refreshed? (`docs/next_tasks.md` Priority 5 — not yet done.)

## 8. Practical Status Verdict

If you need a one-line answer:

> The project has completed the original V8 embedding kernel, finished the M2–M8
> hosted runtime, the M9–M10 monitoring extensions, and the verified M11
> compatibility line through M11-52 (Document cookie/identity, node foundation,
> HTML string APIs, Window scroll/focus/UI completion, Window event handler
> attributes, and Window messaging). the combined build above is release-verified; BIND-01 remains
> the deployed SDK. PRES-01/PRES-02 have previous local acceptance; PRES-03
> exposes the user-selected 49 ordinary Window names with local acceptance passed;
> combined release verified above. WIN-MON-01 implements their scoped native monitoring
> with local acceptance passed; combined release verified above. Backend execution and the 821 global interface
> entries remain outside that presence batch.
> The project has also landed steps 1–4 plus the native-BOM-call,
> window-metadata, nested-metadata, static-visualViewport, navigator-storage
> manager, navigator-permissions, matchMedia-result, legacy-navigator, and
> window-alias groups of a post-M11 host access trace, plus step-6a value/
> argument/return summaries, step-6b method-reference reads, step-6c
> delete/enumeration/define request events, step-6d explicit has/descriptor
> queries, step-6f Reflect target metadata, step-6g native callback/getter
> summaries, and step-6h synchronous exception metadata, plus the step-7
> `Page.access_trace_report()` aggregated usage report — all verified on real
> V8 (Linux + Windows + macOS arm64) through `2026-11-03`. The step-5 object
> groups
> and steps 6a–6h are complete for the traceable HostObject surface; `in`,
> non-HostObject query targets, cached wrappers, and asynchronous exception
> metadata remain explicit blind spots.

For the surface behind that verdict, read `docs/runtime_surface.md`.

## 9. Local Environment Note

`data/review-venv` currently holds a **stale Phase-1 skeleton** install of `iv8` (its
`__init__.py` predates `Page`, and its `_core` extension has no `Page` binding), so
running `pytest test` against it reports widespread `AttributeError` failures rather
than the expected skips. That is a local install artifact, not a repository
regression. Rebuild and reinstall before trusting any local suite result, and check
`iv8._v8_linked` to know whether you are on a real-V8 or skeleton build.
