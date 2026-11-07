# Next Tasks

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

Last reviewed: `2026-11-07`

This file is intentionally short and should be kept current. It is the preferred
task queue for Claude after chat-history loss.

## Current execution order — Window-first (`2026-11-03`)

User-requested monitoring UX verified (`2026-11-04`): readable
`read_access_trace(format="text")`, preserving default raw logs and aggregation.
Source `1599fd5` passed V8 CI and all release lanes, Python 3.11–3.14 each
`1878 passed, 2 skipped`; macOS focused checks each `79 passed` on the macos-14
fallback. Merge through the existing V8 gate; evidence/artifacts/restoration are
in `docs/access_trace_text.md`. M11-53 remains the next browser feature.

Sixth maintenance extraction verified (`2026-11-04`): bounded HTML entity
decoding/escaping is independent of V8 and DOM ownership. Source `85b2d20`
passed V8 CI and all three release matrices, Python 3.11–3.14 each
`1822 passed, 2 skipped`; focused macOS runs each `63 passed`. Merge through
the existing V8 gate. Evidence is in `docs/page_html_entities_refactor.md`.
M11-53 remains next.

Fifth maintenance extraction verified (`2026-11-04`): Base64 byte algorithms
are independent of V8, preserving M8-4 behavior and host conversion/error paths.
Source `10151d4` passed V8 CI and all three release matrices, Python 3.11–3.14
each `1816 passed, 2 skipped`; focused macOS runs each `32 passed`. Merge via
the existing V8 gate. Evidence is in `docs/page_base64_refactor.md`.
M11-53 remains the next feature milestone.

Fourth maintenance extraction verified (`2026-11-04`): query codec and pair
parse/serialize functions are independent of V8; canonical private-slot state
and M8-3 semantics are preserved. Source `f017a01` passed V8 CI and all three
release matrices, Python 3.11–3.14 each `1806 passed, 2 skipped`; focused macOS
runs each `54 passed`. Merge through the existing V8 gate. See
`docs/page_query_refactor.md` for source/run/artifact evidence. M11-53 stays next.

Third maintenance extraction verified (`2026-11-04`): shared minimal URL
decomposition/resolution is in `url_parser`, with M2-3/M8-2 behavior preserved.
Source `2e30d48` passed real-V8 CI and all three release matrices, Python
3.11–3.14 each `1799 passed, 2 skipped`; macOS focused URL/consumer runs each
`76 passed`. Merge through the existing V8 gate. See `docs/page_url_refactor.md`
for source/run/artifact evidence. M11-53 stays the next feature milestone.

Second maintenance extraction verified (`2026-11-04`): Cookie parsing/storage
now lives in the Page-owned `CookieJar`. V8 CI and all three release lanes
passed, Python 3.11–3.14 each `1793 passed, 2 skipped`; macOS focused tests
reported `11 passed` before the full suite. Source/run/artifact evidence is in
`docs/page_cookie_refactor.md`. Merge through the existing required V8 gate;
M11-53 remains next, with no new M11 feature scope in this maintenance slice.

Maintenance completed/verified `2026-11-04`: the first Page parser extraction
and automatic Linux/Python 3.13 V8 CI passed all three real-V8 release lanes
(Python 3.11–3.14, each `1787 passed, 2 skipped`). Merge via PR with the
real-V8 check required. See `docs/page_refactor.md` for source trees, run IDs,
artifact IDs, and local differential/UBSan results. Resume M11-53 afterward.

At the user's direction, **Window now has higher priority than Document**:

1. **Active — Window continuation.** M11-51 (Window event handler attributes;
   Priority 3aq below) and M11-52 (Window messaging; Priority 3ar below) are
   verified. M11-53 is a deterministic focus/visibility model
   (`getComputedStyle` only if a CSS model is approved) before returning to
   Document work.
2. **Later — Document continuation.** `document.write`/`writeln`/`open`/`close`
   and `importNode`/`adoptNode` move behind the Window slices. The bounded
   document.all repair is verified above. The approved Web IDL interface and
   descriptor batch is verified above.
   Remaining legacy collection overloads follow the later Document work.
3. Remaining headless-browser gaps and the open decisions in
   `docs/current_status.md` follow.

## Priority 1: Reconcile contracts with implementation reality — DONE (`2026-08-06`)

### Goal

Reduce confusion between the original M1 contract and the real runtime surface now
present in `HEAD`.

### What was delivered

- **`docs/runtime_surface.md`** — a new overview index: the three-layer model, the
  complete public Python surface (`JSContext` + every `Page` member with its
  milestone), the JS-side surface, the standing exclusions, a milestone map linking
  every summary, and the build/platform reality.
- **Scope notes** on the four M1-era documents — `api_contract.md` (M1 `JSContext`
  only), `architecture.md` (ownership/lifecycle still authoritative, §2 Non-Goals
  historical), `implementation_plan.md` (completed historical plan), `test_plan.md`
  (M1 suite; later milestones add collar suites).
- **`AGENTS.md`** — the Project Scope rules no longer read as a live ban on
  DOM/timers/DevTools; they are marked as the M1 kernel boundary, with a pointer to
  the milestone-scoped working rule and to the index.
- **`CLAUDE.md`** — reading order now includes the index; the Reality Check names it.
- **`docs/current_status.md`** — §5 rewritten from "mismatch to remember" to
  "layering reconciled"; open questions now cite their `decision_log.md` entries;
  a new §8 records the stale local venv.

### Acceptance — met

- a new contributor can identify the current milestone and surface from
  `docs/runtime_surface.md` without reading commit history;
- no document now implies the repository exposes only M1 behavior.

### Deliberately not done

`runtime_surface.md` is an **index, not a contract** — it does not freeze `Page`
behavior. Whether `Page` should gain a frozen contract document remains Priority 6 /
`decision_log.md` §B.

## Priority 2: Choose the post-M10 direction -- DONE (`2026-08-06`)

### Goal

Pick the next milestone direction instead of continuing ad hoc.

### What was decided

- The repository now adopts a **compatibility-first** post-M10 direction.
- `M11` is defined as the first milestone line focused on reproducing the
  community-edition capability surface of `HanZzzzz000/iv8`, then extending beyond
  it deliberately.
- The immediate deliverable is `docs/m11_compatibility_plan.md`, which frames the
  goal, gap categories, API-shape question, rollout model, and risks.
- This resolves the old "continue M10 breadth vs begin M11" ambiguity.

### Acceptance -- met

- one approved direction is written down in `docs/decision_log.md`;
- `docs/current_status.md` and this file reflect the choice.

### Deliberately not done

This planning step does **not** yet choose the exact `M11-1` implementation slice.
That remains the next planning-to-execution handoff.

## Priority 3: First M11 compatibility slice — DONE (`2026-08-06`)

### Goal

Turn the compatibility-first direction into one bounded implementation target.

### What landed

`M11-1`: **configurable host-environment baseline**. `Page(environment=None)` plus the
static `Page.get_default_environment()`; eleven keys drive the read-only `navigator`
identity values, the new `screen` global, and `window.devicePixelRatio` /
`innerWidth` / `innerHeight`. Defaults reproduce the previous fixed `navigator`
constants (an unconfigured page is unchanged), values persist across `load()`
generations, the surface is read-only from JS, and validation is strict and
Python-side so a bad environment never partially installs a page.

Boundary and non-goals: `docs/m11_1_host_environment.md`. Tests:
`test/test_host_environment.py`.

### Acceptance — met

- one `M11-1` slice was selected and implemented;
- it has explicit scope, non-goals, files/modules, and focused tests.

### Deliberately not done

No compatibility **matrix** document was produced. `M11-1` was chosen by direction
rather than from a ranked source-backed survey, so the ranked-backlog part of this
priority is carried into Priority 4 below.

## Priority 3b: Second M11 compatibility slice — DONE (`2026-08-06`)

### What landed

`M11-2`: **`requestAnimationFrame` / `cancelAnimationFrame` with a manual frame pump**.
The two JS globals plus the Python-side `Page.run_animation_frame()`, which is the only
trigger. No real clock, background thread, or automatic event loop; one call runs one
frame (the callbacks queued when it began, in registration order), and a callback that
re-requests a frame lands in the next frame — so a self-rescheduling callback advances
one frame per call instead of spinning. `cancelAnimationFrame` prevents a queued
callback, including one cancelled by an earlier callback in the same frame. The queue is
independent of the M2-4 timers and the microtask queue and has its own handle space.

Boundary and non-goals: `docs/m11_2_animation_frames.md`. Tests:
`test/test_animation_frames.py`.

### Deliberately not done

Like `M11-1`, this slice was chosen by direction rather than from a ranked
source-backed survey. Priority 4 below is still outstanding and now covers `M11-5`.

## Priority 3c: Third M11 compatibility slice — DONE (`2026-08-06`)

### What landed

`M11-3`: **`requestIdleCallback` / `cancelIdleCallback` with a manual idle pump**. The
two JS globals plus the Python-side `Page.run_idle_callbacks()`, the only trigger. Same
manual model as M11-2: no clock, thread, or automatic loop; one call runs one round (the
callbacks queued when it began, in registration order), and a re-request lands in the
next round. Each callback receives a minimal `IdleDeadline`-style object — read-only
`didTimeout` (always false) and `timeRemaining()` (a fixed 50ms) — deterministic by
design. `options.timeout` is type-validated (malformed values throw `TypeError` before
anything is queued) but has **no firing semantics**, which is the slice's headline
non-goal. The idle queue is independent of the timers, animation frames, and microtasks,
with its own handle space.

Boundary and non-goals: `docs/m11_3_idle_callbacks.md`. Report:
`docs/m11_3_report.md`. Tests: `test/test_idle_callbacks.py`.

### Deliberately not done

Like M11-1 and M11-2, this slice was chosen by direction rather than from a ranked
source-backed survey. Priority 4 below is still outstanding and now covers `M11-5`.

## Priority 3d: Fourth M11 compatibility slice — DONE (`2026-10-27`)

### What landed

`M11-4`: **`queueMicrotask(callback)` on the existing manual `run_jobs()` microtask
pump**. The global enqueues callbacks on the generation's V8 microtask queue; there is
no new Python API and no fifth pump. A non-callable argument throws a JS `TypeError`
before enqueueing (standard semantics, unlike the permissive rAF/idle host calls).
Microtasks run FIFO, interleave with `Promise` reactions in enqueue order, and a
callback queued from inside a microtask appends to the same checkpoint. A throwing
callback is swallowed by V8's explicit checkpoint, so the page stays usable. `load()`
discards the old generation's pending microtasks; `run_jobs()` after `dispose()` raises
`JSContextDisposedError`. The microtask queue remains independent of timers, animation
frames, and idle callbacks.

Boundary and non-goals: `docs/m11_4_queue_microtask.md`. Tests:
`test/test_queue_microtask.py`. Local verification was static only (no C++ toolchain,
no supported Python runtime). Real-V8 CI on `2026-10-27`: Linux run `40471874328` and
Windows run `40483069548`, both on commit `0305b49`, each reported
`1213 passed, 2 skipped`; the Linux cold build required build-only bootstrap fixes
`a1869e9` / `0305b49`. See `docs/m11_4_report.md`.

### Deliberately not done

No scheduler API, automatic event loop, clock, `MutationObserver`, microtask
cancellation, or changes to the four existing pumps. Like the earlier M11 slices, this
was chosen before the ranked compatibility matrix existed; Priority 4 below now covers
`M11-5`.

## Priority 3e: Fifth M11 compatibility slice — DONE (`2026-10-28`)

### What landed

`M11-5`: **`MutationObserver`** — the minimal JS global with
`observe(target, options)` / `disconnect()` / `takeRecords()`. Records cover
`childList` and `attributes` (including `subtree`, `attributeOldValue`,
`attributeFilter`), are queued in mutation order, and are delivered only by the
existing `Page.run_jobs()` microtask checkpoint. Element and document targets
are accepted; `characterData` was accepted but inert in M11-5's element-only
model (M11-48 later activated it for Text/Comment nodes).
There is no new Python API, no fifth pump, no `MutationRecord` global, and no
cross-generation observer state.

Boundary: `docs/m11_5_mutation_observer.md`. Tests:
`test/test_mutation_observer.py` (52 collected items; the skeleton shape guard
passes locally, runtime tests require a rebuilt wheel).

### Verification — passed

Real-V8 CI on branch `m11-5-mutation-observer`, commit `4fa6469`:

- Linux `release-linux` run `40632597636` — cp311–cp314, each
  `1265 passed, 2 skipped`;
- Windows `release-windows` run `40632601387` — Python 3.11–3.14, each
  `1265 passed, 2 skipped`.

`test/test_mutation_observer.py` ran in every lane; 1265 is the M11-4 baseline
(`1213 passed, 2 skipped`) plus the 52 new items. Two fixes were needed after
the first runs: `ab1d507` (V8 15.0 `ExternalPointerTypeTag` on
`External::New` / `External::Value`) and `4fa6469` (the tests set ids via
`setAttribute`, since `.id` has no setter). See `docs/m11_5_report.md`.

### Next

`M11-6` is delivered in the five-slice batch — see Priority 3g.

### Deliberately not done

`characterData` records, `MutationRecord` global, `id`/`className` property
setters, document-node childList records without `subtree`, JS-finalizer observer
cleanup, `watch_apis` integration, and any scheduler/new pump.

## Priority 3f: Sixth M11 compatibility slice — DONE (`2026-10-28`)

### What landed

`M11-6`: **Event subclass cluster** — `CustomEvent` (`detail`), `MouseEvent`
(`clientX` / `clientY` / `button` / `buttons`), `KeyboardEvent` (`key` / `code`
/ `repeat`), and `FocusEvent` (`relatedTarget`) as minimal constructors on top
of the existing `Event` / `EventTarget` / bubbling model. Prototype chains make
each subclass `instanceof Event`; subclass fields are own read-only values; no
capture, cancellation, listener options, native event generation, or new pump.

Boundary: `docs/m11_6_event_subclasses.md`. Tests:
`test/test_event_subclasses.py` (30 items).

### Verification — passed

Verified in the five-slice batch on commit `35c85fe`: Linux run `40661574145`
and Windows run `40661580891`, each wheel lane `1352 passed, 2 skipped`.
`test/test_event_subclasses.py` ran in every lane. The cold-run failures were
outdated contract guards and a wrapper-identity test assumption; fixed in
`7448749` / `35c85fe`. See `docs/m11_6_report.md`.

## Priority 3g: Five-slice batch — M11-6 through M11-10 — DONE (`2026-10-28`)

### What landed

- `M11-6`: **Event subclass cluster** — `CustomEvent` / `MouseEvent` /
  `KeyboardEvent` / `FocusEvent` on the existing Event/EventTarget model.
- `M11-9`: **Event cancellation** — `cancelable` / `defaultPrevented` /
  `preventDefault()` / `stopImmediatePropagation()`, and a `dispatchEvent`
  return value of `false` for canceled cancelable events.
- `M11-10`: **`classList` / `toggleAttribute`** — `contains` / `add` / `remove`
  / `toggle` / `replace` / `item` / `length` / `value`, plus boolean attribute
  presence toggling, both wired into the MutationObserver attribute hooks.

- `M11-7`: **Web Storage** — `localStorage` / `sessionStorage` as two
  independent, insertion-ordered, per-Page in-memory stores with
  `getItem` / `setItem` / `removeItem` / `clear` / `key` and a read-only
  `length`. Entries survive `Page.load()` generations of the same Page; no
  disk, origin keying, `storage` events, or cross-Page sharing.
- `M11-8`: **`Blob` / `File` / `FileReader`** — immutable byte value objects
  with `size` / `type` / `name` / `lastModified`, `slice`, and Promise-returning
  `text` / `arrayBuffer`, plus a minimal `FileReader` whose
  `readAsText` / `readAsArrayBuffer` complete on `Page.run_jobs()` and dispatch
  `loadstart` / `load` / `loadend` / `abort` listener events. No network,
  filesystem, object-URL, progress, or data-URL surface.

Boundaries: `docs/m11_6_event_subclasses.md`, `docs/m11_7_web_storage.md`,
`docs/m11_8_blob_file_reader.md`, `docs/m11_9_event_cancellation.md`,
`docs/m11_10_class_list.md`. Tests: `test/test_event_subclasses.py`,
`test/test_web_storage.py`, `test/test_blob_file_reader.py`,
`test/test_event_cancellation.py`, `test/test_class_list.py`.

### Verification — passed

Final commit `35c85fe` (branch `m11-6-event-subclasses`):

- Linux `release-linux` run `40661574145` — cp311–cp314, each
  `1352 passed, 2 skipped`;
- Windows `release-windows` run `40661580891` — Python 3.11–3.14, each
  `1352 passed, 2 skipped`.

All five new test files ran in every lane. 1352 is the M11-5 baseline (1265)
plus 87 new items (`30 + 13 + 22 + 9 + 13`). Fixes during verification:
`62ea507` (storage `Local<Name>`), `7448749` (wrapper-identity test),
`35c85fe` (contract guards and two test bugs). Reports:
`docs/m11_6_report.md` … `docs/m11_10_report.md`.

### Next

Choose `M11-11` from `docs/m11_compatibility_backlog.md`.

## Priority 3h: Eleventh M11 compatibility slice — M11-11 — DONE (`2026-10-28`)

### What landed

`M11-11`: **event capture phase + listener options** — `addEventListener`
now accepts `capture` / `once` / `passive` (boolean or options object), with
`(type, callback, capture)` identity for add/remove; dispatch runs
window → document → ancestors → target → ancestors → document → window, with
`event.eventPhase` 1/2/3 and 0 outside dispatch. `once` removes before the
call; `passive` suppresses `preventDefault` for that invocation.

Boundary: `docs/m11_11_event_capture.md`. Tests:
`test/test_event_capture.py` (15 items) plus the new
`test/test_m11_batch_integration.py` (3 items).

### Verification — passed

Final commit `a269aa9` (branch `m11-11-event-capture`):

- Linux `release-linux` run `40683001239` — cp311–cp314, each
  `1370 passed, 2 skipped`;
- Windows `release-windows` run `40683010032` — Python 3.11–3.14, each
  `1370 passed, 2 skipped`.

`test/test_event_capture.py` (15 items) and
`test/test_m11_batch_integration.py` (3 items) ran in every lane. The full
suite collects 1372 items. The first verification run was green. See
`docs/m11_11_report.md`.

### Next

Choose `M11-12` from `docs/m11_compatibility_backlog.md`.

## Priority 3i: M11-12..M11-17 DOM/web batch — DONE (`2026-10-28`)

### What landed

- `M11-12`: `id` / `className` setters, `hasAttributes()`,
  `getAttributeNames()`.
- `M11-13`: `replaceChild`, `remove`, `hasChildNodes`, raw
  `firstChild` / `lastChild` / `previousSibling` / `nextSibling` aliases.
- `M11-14`: `cloneNode(deep?)`, `isSameNode`, `isEqualNode`.
- `M11-15`: `URL.searchParams` snapshot (no live linkage; URL fields remain
  read-only).
- `M11-16`: `select.options`, `selectedIndex`, read-only `multiple` / `size`.
- `M11-17`: nested `Page(environment=...)` + `Page.get_defaults()`.

Boundary: `docs/m11_12_17_dom_web_batch.md`. Tests:
`test/test_m11_12_17_batch.py` (8 items). Full suite collects 1380 items;
each wheel lane: **`1378 passed, 2 skipped`**.

### Verification — passed

Final commit `8d606e9` (branch `m11-12-17-dom-web-batch`):

- Linux `release-linux` run `40695004440` — cp311–cp314, each
  `1378 passed, 2 skipped`;
- Windows `release-windows` run `40695029530` — Python 3.11–3.14, each
  `1378 passed, 2 skipped`.

`test/test_m11_12_17_batch.py` (8 items) ran in every lane. 1378 is the
M11-11 baseline (1370) plus the 8 new items. The first attempt (`0c47ecb`,
Linux `40693054124` / Windows `40693059780`) built all wheels but failed 15
stale frozen-surface guards; `8d606e9` refreshed them. Report:
`docs/m11_12_17_report.md`.

### Next

Choose `M11-18` from `docs/m11_compatibility_backlog.md` (or another
gap-inventory slice with an explicit boundary).

## Priority 3j: M11-18 form-control attribute reflection — DONE (`2026-10-28`)

### What landed

- `<input>`: `type` (normalized known set; default `"text"`), `disabled`,
  `readOnly`, `placeholder`.
- `<button>`: `type` (normalized `submit` / `reset` / `button`; default
  `"submit"`), `disabled`.
- `<form>`: `encoding`, an alias of the existing `.enctype` runtime slot.
- `<option>`: `label` (attribute, falling back to text) and read-only `index`.

Property writes are real attribute writes and feed the MutationObserver
attribute hooks; `form.encoding` changes only the runtime slot. No Python API,
no form behavior, and no validation surface.

Boundary: `docs/m11_18_form_control_reflection.md`. Tests:
`test/test_m11_18_form_control_reflection.py` (8 items). Full suite collects
1388 items; each wheel lane: **`1386 passed, 2 skipped`**.

### Verification — passed

Final commit `08350ce` (branch `m11-18-form-control-reflection`):

- Linux `release-linux` run `40698705892` — cp311–cp314, each
  `1386 passed, 2 skipped`;
- Windows `release-windows` run `40698728631` — Python 3.11–3.14, each
  `1386 passed, 2 skipped`.

The new test file ran in every lane; the first verification run was green. The
affected form frozen-surface guards were refreshed in the same change. Report:
`docs/m11_18_report.md`.

### Next

Choose `M11-19` from `docs/m11_compatibility_backlog.md` or another
gap-inventory slice with an explicit boundary.

## Priority 3k: M11-19..M11-23 DOM/form completeness batch — DONE (`2026-10-28`)

### What landed

- `M11-19`: multi-token `getElementsByClassName` at document and element level;
  the element form queries descendants only.
- `M11-20`: `document.getElementsByName(name)`.
- `M11-21`: extended form reflection — input `name` / `required`, textarea
  `name` / `required` / `disabled` / `readOnly` / `placeholder`, select
  `name` / `required` / `disabled`, button `name`.
- `M11-22`: select `length` (read-only), `item(index)`, `namedItem(name)`.
- `M11-23`: document metadata (`characterSet` / `contentType` / `compatMode` /
  `referrer` / `domain` / `visibilityState` / `hidden` / `activeElement`) and
  `hasFocus()`.

Boundary: `docs/m11_19_23_dom_form_batch.md`. Tests:
`test/test_m11_19_class_query.py`, `test/test_m11_20_getelementsbyname.py`,
`test/test_m11_21_form_reflection_extension.py`,
`test/test_m11_22_select_convenience.py`,
`test/test_m11_23_document_metadata.py` (26 items). Full suite collects 1414
items; each wheel lane: **`1412 passed, 2 skipped`**.

### Verification — passed

Final commit `999c851` (branch `m11-19-23-dom-form-batch`):

- Linux `release-linux` run `40704330688` — cp311–cp314, each
  `1412 passed, 2 skipped`;
- Windows `release-windows` run `40704362549` — Python 3.11–3.14, each
  `1412 passed, 2 skipped`.

All five new test files ran in every lane. The first attempt (`9f3e459`)
failed one stale `test_element_queries.py` guard; `999c851` refreshed it.
Report: `docs/m11_19_23_report.md`.

### Next

Choose `M11-24` from `docs/m11_compatibility_backlog.md` or another
gap-inventory slice with an explicit boundary.

## Priority 3l: M11-24..M11-28 host environment / console batch — DONE (`2026-10-28`)

### What landed

- `M11-24`: navigator `languages` / `onLine` / `cookieEnabled` /
  `hardwareConcurrency`.
- `M11-25`: screen `colorDepth` / `pixelDepth`.
- `M11-26`: window `outerWidth` / `outerHeight` / `screenX` / `screenY`.
- `M11-27`: navigator identity (`appName` / `appCodeName` / `product` /
  `vendor` / `vendorSub` / `productSub`).
- `M11-28`: console `debug` / `trace` / `table` / `dir` / `group` /
  `groupEnd` / `time` / `timeEnd` / `assert` / `count`.

The flat environment grows from 11 to 27 keys; the nested form and
`get_defaults()` carry the same keys. Existing keys/defaults are unchanged; no
new Python method or setter. Boundary:
`docs/m11_24_28_host_environment_batch.md`. Tests:
`test/test_m11_24_navigator_metadata.py`,
`test/test_m11_25_screen_metadata.py`,
`test/test_m11_26_window_metadata.py`,
`test/test_m11_27_navigator_identity.py`,
`test/test_m11_28_console_methods.py` (22 items). Full suite collects 1476
items; each wheel lane: **`1474 passed, 2 skipped`**.

### Verification — passed

Final commit `a00e140` (branch `m11-24-28-host-environment-batch`):

- Linux `release-linux` run `40711526339` — cp311–cp314, each
  `1474 passed, 2 skipped`;
- Windows `release-windows` run `40711548915` — Python 3.11–3.14, each
  `1474 passed, 2 skipped`.

All five new test files ran in every lane. The first attempt (`f857d03`)
failed one fresh-array test; `a00e140` fixed it. Report:
`docs/m11_24_28_report.md`.

### Next

Choose `M11-29` from `docs/m11_compatibility_backlog.md` or another
gap-inventory slice with an explicit boundary.

## Priority 3m: M11-29 deterministic event-loop facade — DONE (`2026-10-29`)

### What landed

- `Page.tick()` — one virtual-time turn (microtasks, one due timer, microtasks).
- `Page.advance(ms, max_steps=10000)` — bounded virtual-time window with due-time
  ordering and microtask checkpoints.
- `Page.run_until_idle(max_steps=1000)` — auto-advance through future timers.
- `Page.event_loop_time_ms` — per-generation virtual clock, reset by `load()`.

Timers use `(due_ms, registration seq)` ordering; intervals reschedule with a
1ms virtual floor and are bounded by `max_steps`. Legacy pumps and rAF/idle
semantics are unchanged; rAF/idle are not driven by this facade in this phase.

Boundary: `docs/m11_29_event_loop_facade.md`. Tests:
`test/test_event_loop_facade.py` (21 items). Full suite collects 1497 items;
each wheel lane: **`1495 passed, 2 skipped`**.

### Verification — passed

Final commit `b211481` (feature commit `0bc75a2`, branch
`m11-29-event-loop-facade`):

- Linux `release-linux` run `40820152813` — cp311–cp314, each
  `1495 passed, 2 skipped`;
- Windows `release-windows` run `40820163797` — Python 3.11–3.14, each
  `1495 passed, 2 skipped`.

The new test file ran in every lane. The first attempt (`0bc75a2`) failed three
historical pump non-goal guards; `b211481` refreshed them. Report:
`docs/m11_29_report.md`.

### Next

Choose `M11-30` — the natural follow-up is integrating rAF/idle into the
facade; otherwise use `docs/m11_compatibility_backlog.md` or another
gap-inventory slice with an explicit boundary.

## Priority 3n: M11-30 rAF/idle facade integration — DONE (`2026-10-29`)

### What landed

- rAF callbacks run at fixed 16ms virtual frame boundaries inside the facade,
  with a per-frame snapshot; callbacks registered during a frame run in the
  next frame.
- Idle rounds run one at a time when no timer/frame is due; self-rescheduling
  idle callbacks remain pending and are bounded by `max_steps`.
- `tick` / `advance` / `run_until_idle` cover timers, microtasks, animation
  frames, and idle callbacks; every task is followed by a microtask checkpoint.
- Legacy pumps keep their one-queue semantics and do not advance the facade
  clock.

Boundary: `docs/m11_30_frame_idle_facade.md`. Tests:
`test/test_m11_30_frame_idle_facade.py` (14 items). Full suite collects 1511
items; each wheel lane: **`1509 passed, 2 skipped`**.

### Verification — passed

Final commit `7dde955` (feature commit `32f9289`, branch
`m11-30-frame-idle-facade`):

- Linux `release-linux` run `40826630371` — cp311–cp314, each
  `1509 passed, 2 skipped`;
- Windows `release-windows` run `40826634489` — Python 3.11–3.14, each
  `1509 passed, 2 skipped`.

The new test file ran in every lane. The first attempt (`32f9289`) hung in all
lanes because the clock jump used timer ids as due times; `7dde955` fixed it.
Report: `docs/m11_30_report.md`.

### Next

Choose `M11-31` from `docs/m11_compatibility_backlog.md` or another
gap-inventory slice with an explicit boundary.

## Priority 3o: M11-31/32 performance clock + form validation — DONE (`2026-10-29`)

### What landed

- M11-31: `performance.now()` reports the M11-29/30 virtual facade clock;
  `performance.timeOrigin` is the virtual epoch `0`; rAF callbacks receive the
  current virtual timestamp.
- M11-32: minimal constraint validation on input/button/select/textarea —
  `willValidate` / `validity` / `validationMessage` / `checkValidity()` /
  `reportValidity()` / `setCustomValidity()`; form-level aggregation; `required`,
  `minlength` / `maxlength`, email/URL `typeMismatch`, custom errors, and
  target-only `invalid` events.
- Historical clock/validation guards refreshed in the same change.

Boundary: `docs/m11_31_32_perf_validation_batch.md`. Tests:
`test/test_m11_31_virtual_performance_clock.py` (14 items) and
`test/test_m11_32_form_validation.py` (17 items). Full suite collects 1542
items; each wheel lane: **`1540 passed, 2 skipped`**.

### Verification — passed

Final commit `a88b69d` (feature commit `faf5e28`, branch
`m11-31-32-perf-validation-batch`):

- Linux `release-linux` run `40831677507` — cp311–cp314, each
  `1540 passed, 2 skipped`;
- Windows `release-windows` run `40831682182` — Python 3.11–3.14, each
  `1540 passed, 2 skipped`.

Both new test files ran in every lane. The first attempt (`faf5e28`) failed the
readOnly test because the test helper omitted the `r` id; `a88b69d` fixed the
test. Report: `docs/m11_31_32_report.md`.

### Next

Choose `M11-33` from `docs/m11_compatibility_backlog.md` or another
gap-inventory slice with an explicit boundary.

## Priority 3p: M11-33 OS-backed Crypto random — DONE (`2026-10-29`)

### What landed

- Read-only `crypto` global with `getRandomValues()` and `randomUUID()`.
- `getRandomValues` accepts all integer TypedArrays, fills the view's byte
  range, returns the same view, enforces the 65536-byte quota with `RangeError`,
  and rejects non-integer/absent/detached inputs with `TypeError`.
- `randomUUID` returns a lower-case RFC 4122 v4 UUID.
- Randomness comes from the platform CSPRNG (advapi32 `SystemFunction036` on
  Windows, `getrandom`/`/dev/urandom` on Linux); it is the runtime's single
  deliberate non-deterministic surface. `crypto.subtle` is deferred.

Boundary: `docs/m11_33_crypto_random.md`. Tests:
`test/test_m11_33_crypto_random.py` (13 items). Full suite collects 1555 items;
each wheel lane: **`1553 passed, 2 skipped`**.

### Verification — passed

Commit `c9b2fb5` (branch `m11-33-crypto-random`):

- Linux `release-linux` run `40834145493` — cp311–cp314, each
  `1553 passed, 2 skipped`;
- Windows `release-windows` run `40834153439` — Python 3.11–3.14, each
  `1553 passed, 2 skipped`.

The new test file ran in every lane. Both runs were green on the first attempt.
Report: `docs/m11_33_report.md`.

### Next

Choose `M11-34` — candidates include selector-engine expansion, `innerHTML`,
Crypto `subtle.digest`, event-surface completion, or another gap-inventory
slice with an explicit boundary.

## Priority 3q: M11-34 CSS selector engine — DONE (`2026-10-29`)

### What landed

- Shared selector engine for document/element `querySelector[All]`, `matches()`,
  and `closest()`.
- Selector lists with document order/dedupe, compound selectors, type selectors,
  and universal `*`.
- Descendant / child / adjacent-sibling / general-sibling combinators.
- Attribute selectors (`[a]`, `=`, `~=`, `|=`, `^=`, `$=`, `*=`) with quoted or
  unquoted values; names case-insensitive, values case-sensitive.
- Malformed/unsupported selectors keep stable empty/null/false behavior;
  pseudo-classes remain a later selector slice.

Boundary: `docs/m11_34_selector_engine.md`. Tests:
`test/test_m11_34_selector_engine.py` (12 items). Full suite collects 1567
items; each wheel lane: **`1565 passed, 2 skipped`**.

### Verification — passed

Final commit `49f8582` (feature commit `a8fe60d`, branch
`m11-34-selector-engine`):

- Linux `release-linux` run `40855935052` — cp311–cp314, each
  `1565 passed, 2 skipped`;
- Windows `release-windows` run `40855940659` — Python 3.11–3.14, each
  `1565 passed, 2 skipped`.

The new test file ran in every lane. The first attempt (`a8fe60d`) failed one
refreshed `closest('div.box')` expectation; `49f8582` fixed the test. Report:
`docs/m11_34_report.md`.

### Next

The planned selector pseudo-class follow-up is deferred; M11-35 was used for
BOM metadata (see Priority 3r below).

## Priority 3r: M11-35 BOM metadata completion — DONE (`2026-10-30`)

### What landed

- Window: mutable `window.name` (Page-persistent across `load()`),
  `window.length` (0), `screenLeft` / `screenTop` aliases, and
  `isSecureContext` derived from the base URL.
- Screen: `availLeft` / `availTop` and a derived `orientation`
  (`landscape-primary`/0 or `portrait-primary`/90); no events or lock.
- Navigator: `maxTouchPoints`, `pdfViewerEnabled`, `doNotTrack`, and a
  read-only `connection` object (`effectiveType` / `rtt` / `downlink` /
  `saveData`).
- `Page(environment=...)` grows from 27 to 36 keys, with flat and nested
  `navigator` / `screen` forms and strict type validation.

Boundary: `docs/m11_35_bom_metadata.md`. Tests:
`test/test_m11_35_bom_metadata.py` (12 items). Full suite collects 1601 items;
each wheel lane: **`1599 passed, 2 skipped`**.

### Verification — passed

Commit `aa23c57` (branch `m11-35-bom-metadata`):

- Linux `release-linux` run `40990275470` — cp311–cp314, each
  `1599 passed, 2 skipped`;
- Windows `release-windows` run `40990279247` — Python 3.11–3.14, each
  `1599 passed, 2 skipped`.

The new test file ran in every lane. Both runs were green on the first attempt.
Report: `docs/m11_35_report.md`.

### Next

M11-36..M11-40 completed the BOM completion batch (below); the following
M11-41..M11-45 batch completed the remaining deterministic viewport/media/device
metadata. Selector pseudo-classes remain the planned selector follow-up.

## Priority 3s: M11-36..M11-40 BOM completion batch — DONE (`2026-10-30`)

### What landed

- Deterministic dialogs: `alert()` / `confirm()` / `prompt()` / `print()`
  stubs with fixed returns and watch-API visibility.
- Performance timeline: `performance.mark()` / `measure()` /
  `getEntries()` / `getEntriesByName()` / `getEntriesByType()` /
  `clearMarks()` / `clearMeasures()` over virtual time.
- Bounded `structuredClone()` with cycle/identity preservation, boxed
  primitives, shared cloned view buffers, and explicit rejection of unsupported
  values.
- Single-context `window.parent` / `top` / `frames` / `opener` metadata:
  replaceable `parent` / `frames`, unforgeable `top`, writable `opener`.
- `scheduler.postTask(callback, options?)` over the existing timer queue with
  `delay`, Promise settlement, and explicit pumps.

Boundary: `docs/m11_36_40_bom_completion.md`. Tests:
`test/test_m11_36_40_bom_completion.py` (18 items). Full suite collects 1619
items; each wheel lane: **`1617 passed, 2 skipped`**.

### Verification — passed

Final commit `512117c` (feature commit `84a8850`, branch
`m11-36-40-bom-completion`):

- Linux `release-linux` run `40997424174` — cp311–cp314, each
  `1617 passed, 2 skipped`;
- Windows `release-windows` run `40997427191` — Python 3.11–3.14, each
  `1617 passed, 2 skipped`.

The first attempt (`84a8850`) failed three tests: a `let frames` collision with
the global `frames`, a wrong structuredClone expectation, and a `var parent`
collision with the global `parent`. `512117c` corrected the browsing-context
semantics (`parent`/`frames` replaceable, `opener` writable, `top` read-only),
refreshed the affected tests, and tightened structuredClone. Report:
`docs/m11_36_40_report.md`.

### Next

M11-41..M11-45 completed the next BOM metadata batch (below); selector
pseudo-classes remain the planned selector follow-up.

## Priority 3t: M11-41..M11-45 BOM viewport/media/device metadata — DONE (`2026-10-30`)

### What landed

- Static `window.visualViewport` derived from `innerWidth` / `innerHeight`, plus
  `window.origin`, `crossOriginIsolated`, `closed`, and `frameElement`.
- Bounded `window.matchMedia()` with static MediaQueryList-shaped results and
  no-op listeners; no dynamic viewport events.
- Deterministic Promise-based `navigator.storage.estimate()` /
  `persisted()` / `persist()`.
- `navigator.permissions.query()` with a bounded known-name set, static
  `"prompt"` statuses, and `TypeError` rejection for unknown names.
- Frozen empty `navigator.plugins` / `mimeTypes` and
  `navigator.javaEnabled() === false`.

Boundary: `docs/m11_41_45_bom_extras.md`. Tests:
`test/test_m11_41_45_bom_extras.py` (9 items). Full suite collects 1628 items;
each wheel lane: **`1626 passed, 2 skipped`**.

### Verification — passed

Final commit `61abc3b` (feature commit `f3cebcb`, branch
`m11-41-45-bom-extras`):

- Linux `release-linux` run `41000182750` — cp311–cp314, each
  `1626 passed, 2 skipped`;
- Windows `release-windows` run `41000186396` — Python 3.11–3.14, each
  `1626 passed, 2 skipped`.

The first attempt (`f3cebcb`) failed one new assertion because a JS `null`
frameElement joined to an empty field rather than `"null"`; `61abc3b` fixed the
test. Report: `docs/m11_41_45_report.md`.

### Next

M11-41..M11-45 completed the BOM metadata batch. The post-M11 host access trace
became the next work item (Priority 3u). The unassigned selector follow-up and
`navigator.userAgentData` remain deferred.

## Priority 3u: Host access trace first slice — DONE (`2026-10-30`)

### What landed

- Page-level `start_access_trace(max_events=10000, object_types=None)` /
  `stop_access_trace()` / `read_access_trace(clear=True)`.
- Native HostObject `property_getter` / `property_setter` / `method_callback`
  events at `phase: "enter"`, with sequence, generation, stable native object
  ID, object type/label, member, operation, resource name, and source kind.
- Earliest-N capacity with explicit `dropped_events`, object-type filters,
  read-and-clear, stop/start, generation/load handling, and per-Page isolation.
- Source attribution: `eval`, `html_script`, `internal`, `unknown`; existing
  `watch_apis` / Reflect / Object.keys channels remain unchanged.

Boundary: `docs/host_access_trace.md`. Tests:
`test/test_access_trace_api.py` (9 items) and
`test/test_access_trace_runtime.py` (11 items). Full suite collects 1648 items;
each wheel lane: **`1646 passed, 2 skipped`**.

### Verification — passed

Final commit `44fc847` (feature commits `95461c6`, `0176ebe`, `edb3dd7`; branch
`host-access-trace`):

- Linux `release-linux` run `41015201477` — cp311–cp314, each
  `1646 passed, 2 skipped`;
- Windows `release-windows` run `41015206089` — Python 3.11–3.14, each
  `1646 passed, 2 skipped`.

The first attempt (`edb3dd7`) failed one test that assumed an object-toString
conversion would throw; `44fc847` switched the fixture to a deterministic
non-element `appendChild` call. Boundary/verification: `docs/host_access_trace.md`;
roadmap: `docs/host_access_tracing_plan.md`.

### Next

Step 4 was completed as Priority 3v below. Step 5 began as Priority 3w below;
its remaining property/nested-object groups are next. The unassigned selector
follow-up remains deferred.

## Priority 3v: Host access trace missing-property probes — DONE (`2026-10-30`)

### What landed

- A named-property getter interceptor on `HostObject` templates with
  `kNonMasking` + `kOnlyInterceptStrings`.
- A no-op named query handler so `in`, `hasOwnProperty`, `Object.keys`, and
  descriptor queries do not masquerade as reads.
- A real own/prototype existence guard (`HasRealNamedProperty` chain walk) that
  prevents existing properties, including value-`undefined` own properties and
  prototype properties, from being reported missing. Proxy/host prototypes are
  treated conservatively as present.
- Missing HostObject string-key reads now emit exactly one
  `get_probe`/`resolution: "missing"` event and return `Intercepted::kNo` so the
  original lookup and value are unchanged.

Boundary/verification: `docs/host_access_trace.md`. Tests:
`test/test_access_trace_probes.py` (10 items). Full suite collects 1658 items;
each wheel lane: **`1656 passed, 2 skipped`**.

### Verification — passed

Final commit `8995b89` (initial probe commit `76baaa2`; branch
`host-access-probes`):

- Linux `release-linux` run `41017981567` — cp311–cp314, each
  `1656 passed, 2 skipped`;
- Windows `release-windows` run `41017987566` — Python 3.11–3.14, each
  `1656 passed, 2 skipped`.

The initial attempt (`76baaa2`) exposed query-path and existing-property false
probes; `8995b89` added the query no-op and real-property guard. Symbols and
numeric indices remain out of scope, and `resolution` currently emits only
`"missing"` (no present/unknown classification).

### Next

Step 5 began as Priority 3w below. Remaining step-5 groups (plain BOM property
reads and intrinsic window metadata) are next; value/argument/return summaries
remain step 6. The unassigned selector follow-up remains deferred.

## Priority 3w: Host access trace native BOM calls — DONE (`2026-10-30`)

### What landed

- `performance` method calls: `now` / `mark` / `measure` / `getEntries*` /
  `clearMarks` / `clearMeasures`.
- `crypto` method calls: `getRandomValues` / `randomUUID`.
- Storage method calls: `getItem` / `setItem` / `removeItem` / `clear` / `key`,
  plus the `length` property getter.
- A new `performance`, `crypto`, and `storage` object-type family with stable
  native identities; `localStorage` and `sessionStorage` share the `storage`
  label but get distinct backing object IDs.
- No plain-object property descriptors, prototypes, function identities, or
  `this` behavior were changed.

Boundary/verification: `docs/host_access_trace.md`. Tests:
`test/test_access_trace_bom_calls.py` (4 items). Full suite collects 1662 items;
each wheel lane: **`1660 passed, 2 skipped`**.

### Verification — passed

Commit `ac80ee8` (branch `host-access-bom-calls`):

- Linux `release-linux` run `41019395760` — cp311–cp314, each
  `1660 passed, 2 skipped`;
- Windows `release-windows` run `41019399775` — Python 3.11–3.14, each
  `1660 passed, 2 skipped`.

### Next

The intrinsic window metadata group was completed as Priority 3x below.
Remaining step-5 groups: plain property reads on performance/Storage and nested
BOM objects. Value/argument/return summaries remain step 6. The unassigned selector
follow-up remains deferred.

## Priority 3x: Host access trace window metadata reads — DONE (`2026-10-30`)

### What landed

- Read-only window metadata globals are now native accessors recording
  `get`: `devicePixelRatio`, `innerWidth`, `innerHeight`, `outerWidth`,
  `outerHeight`, `screenX`, `screenY`, `screenLeft`, `screenTop`, `length`,
  `isSecureContext`, `origin`, `crossOriginIsolated`, `closed`, and
  `frameElement`.
- `window.name` records `get` and `set` through its existing accessor.
- Values, read-only assignment behavior, strict-mode errors, and
  `parent`/`top`/`frames`/`opener` behavior are unchanged. The geometry/flag
  data properties are now accessors (descriptor change documented as
  intentional); `parent`/`top`/`frames`/`opener` remain untraced in this group.

Boundary/verification: `docs/host_access_trace.md`. Tests:
`test/test_access_trace_window.py` (4 items). Full suite collects 1666 items;
each wheel lane: **`1664 passed, 2 skipped`**.

### Verification — passed

Final commit `e19f54d` (branch `host-access-window`):

- Linux `release-linux` run `41020686185` — cp311–cp314, each
  `1664 passed, 2 skipped`;
- Windows `release-windows` run `41020689881` — Python 3.11–3.14, each
  `1664 passed, 2 skipped`.

### Next

The nested metadata/timeOrigin group was completed as Priority 3y below, the
static visualViewport group as Priority 3z, the navigator.storage manager as
Priority 3aa, navigator.permissions/PermissionStatus as Priority 3ab, and
matchMedia results as Priority 3ac. Priorities 3ad/3ae cover the final step-5
groups (legacy navigator metadata and window aliases); all planned step-5 object
groups are now complete. Value/argument/return summaries remain step 6. The unassigned
selector follow-up remains deferred.

## Priority 3y: Host access trace nested metadata — DONE (`2026-10-30`)

### What landed

- `navigator.connection` and `screen.orientation` are now nested HostObjects, so
  their property reads and missing string-key probes use the normal trace
  channel; repeated parent getters are visible before each nested read.
- `performance.timeOrigin` is a traced accessor.
- Nested object identity is stable across fresh wrappers; values and read-only
  behavior are unchanged.

Boundary/verification: `docs/host_access_trace.md`. Tests:
`test/test_access_trace_nested.py` (3 items). Full suite collects 1669 items;
each wheel lane: **`1667 passed, 2 skipped`**.

### Verification — passed

Final commit `924effd` (feature commit `9567761`; branch `host-access-nested`):

- Linux `release-linux` run `41022649736` — cp311–cp314, each
  `1667 passed, 2 skipped`;
- Windows `release-windows` run `41022653765` — Python 3.11–3.14, each
  `1667 passed, 2 skipped`.

The first attempt (`9567761`) had two test expectations that assumed one event
per nested access; `924effd` corrected them to include repeated parent getters
and the new timeOrigin event. The runtime behavior was intended.

### Next

Continue step 5 with the static visualViewport group (Priority 3z), then
`matchMedia` result objects, `navigator.storage` / `permissions` / `plugins` /
`mimeTypes`, and other plain property reads. Value/argument/return summaries
remain step 6. The unassigned selector follow-up remains deferred.

## Priority 3z: Host access trace static visualViewport — DONE (`2026-10-30`)

### What landed

- `visualViewport` is now a `VisualViewportHost` HostObject, so its
  metadata reads, writable `onresize`/`onscroll`, no-op event-target method
  calls, and missing string-key probes use the normal trace channel.
- Width/height still come from the Page environment; offsets/scale and method
  results are unchanged. The global property remains read-only.
- Intentional non-value changes are documented: the instance prototype is the
  host-object template prototype, the no-op methods are writable/configurable,
  and the writable handler descriptors are non-configurable. Read-only metadata
  descriptors keep their data shape, though reading a descriptor evaluates the
  native getter and emits a `get` event.
- The fixed internal bootstrap still reads `innerWidth`/`innerHeight` once for
  the static media evaluator, preserving real `internal` source attribution.

Boundary/verification: `docs/host_access_trace.md`. Tests:
`test/test_access_trace_visual_viewport.py` (4 items). Full suite collects 1673
items; each wheel lane: **`1671 passed, 2 skipped`**.

### Verification — passed

Final commit `c87ffbe` (branch `host-access-visual-viewport`):

- Linux `release-linux` run `41025891916` — cp311–cp314, each
  `1671 passed, 2 skipped`;
- Windows `release-windows` run `41025896654` — Python 3.11–3.14, each
  `1671 passed, 2 skipped`.

The initial attempts corrected test-only assumptions: native data-property
descriptor reads evaluate the getter (an extra `get`), undefined method results
do not survive `Array.join`, and host-object descriptors/prototypes differ as
documented. Runtime values and read/write behavior were intended.

### Next

Continue step 5 with the navigator.storage manager group (Priority 3aa below),
then `matchMedia` result objects, `navigator.permissions` / `plugins` /
`mimeTypes`, and other plain property reads. Value/argument/return summaries
remain step 6. The unassigned selector follow-up remains deferred.

## Priority 3aa: Host access trace navigator.storage manager — DONE (`2026-10-30`)

### What landed

- `navigator.storage` is exposed by `NavigatorHost` as a cached plain object
  whose `estimate` / `persisted` / `persist` methods are native callbacks, so
  the parent read records a navigator `get` and each call records a
  `storage_manager` `call`.
- The manager keeps `Object.prototype`, cached identity, read-only method
  descriptors, the same results (`{usage: 0, quota: 10737418240}` / `false` /
  `false`), and the read-only assignment behavior. No descriptor change is
  needed for the manager object itself.
- Manager property reads/probes, method-reference reads, and returned
  Promise/estimate values remain untraced.

Boundary/verification: `docs/host_access_trace.md`. Tests:
`test/test_access_trace_storage_manager.py` (4 items). Full suite collects 1677
items; each wheel lane: **`1675 passed, 2 skipped`**.

### Verification — passed

Final commit `f91665f` (branch `host-access-storage-manager`):

- Linux `release-linux` run `41027870583` — cp311–cp314, each
  `1675 passed, 2 skipped`;
- Windows `release-windows` run `41027874811` — Python 3.11–3.14, each
  `1675 passed, 2 skipped`.

This slice passed on the first CI attempt.

### Next

Continue step 5 with navigator.permissions/PermissionStatus (Priority 3ab
below), then `matchMedia` result objects, `navigator.plugins` / `mimeTypes`, and
other plain property reads. Value/argument/return summaries remain step 6. The
unassigned selector follow-up remains deferred.

## Priority 3ab: Host access trace navigator.permissions — DONE (`2026-10-30`)

### What landed

- `navigator.permissions` is a cached plain object exposed by `NavigatorHost`;
  its parent read is traced and `query()` is a traced native callback.
- Returned PermissionStatus objects stay fresh plain objects with
  `Object.prototype`, the original data descriptor shapes, writable `onchange`,
  and the exact M11-44 rejection messages. `name` / `state` reads and the no-op
  event-target method calls are traced through native accessors/callbacks with
  `object_type: "permission_status"`.
- Status identity is grouped by permission name (fixed per-name descriptor), so
  repeated statuses for one permission share an `object_id`; different
  permissions do not.
- `onchange` reads/writes, status missing-property probes, and Promise values
  remain untraced.

Boundary/verification: `docs/host_access_trace.md`. Tests:
`test/test_access_trace_permissions.py` (5 items). Full suite collects 1682
items; each wheel lane: **`1680 passed, 2 skipped`**.

### Verification — passed

Final commit `18e674e` (branch `host-access-permissions`):

- Linux `release-linux` run `41031830520` — cp311–cp314, each
  `1680 passed, 2 skipped`;
- Windows `release-windows` run `41031835192` — Python 3.11–3.14, each
  `1680 passed, 2 skipped`.

The first CI attempt failed only because a descriptor assertion used
`Array.join`, which stringifies `null` as an empty field; the test was corrected.

### Next

Continue step 5 with matchMedia result objects (Priority 3ac below), then
`navigator.plugins` / `mimeTypes` and the remaining plain/onchange reads.
Value/argument/return summaries remain step 6. The unassigned selector follow-up
remains deferred.

## Priority 3ac: Host access trace matchMedia results — DONE (`2026-10-30`)

### What landed

- `matchMedia` stays a JS function (non-native `toString` unchanged) and keeps
  the bounded JS evaluator. It captures a temporary native factory that is
  deleted from the global immediately.
- Each result is a fresh `MediaQueryListHost` wrapper over a Page-owned backing
  cached by (media string, matches):
  - `matchMedia()` calls record `media_query_list` `call` events (same query ->
    same backing ID);
  - `media` / `matches` record `get`, the five no-op listener methods record
    `call`, and missing string keys record `get_probe`;
  - `onchange` stays a plain per-wrapper own property and is untraced;
  - query text is stored as UTF-16 to preserve exact input.
- HostObject descriptor differences are documented: template prototype,
  writable/configurable no-op methods, and descriptor reads evaluating the
  media/matches getters. Backings live until the next `load()`.

Boundary/verification: `docs/host_access_trace.md`. Tests:
`test/test_access_trace_match_media.py` (3 items). Full suite collects 1685
items; each wheel lane: **`1683 passed, 2 skipped`**.

### Verification — passed

Final commit `7d35a30` (branch `host-access-match-media`):

- Linux `release-linux` run `41032945394` — cp311–cp314, each
  `1683 passed, 2 skipped`;
- Windows `release-windows` run `41032950281` — Python 3.11–3.14, each
  `1683 passed, 2 skipped`.

This slice passed on its first CI attempt.

### Next

Continue step 5 with the legacy navigator metadata (Priority 3ad below) and
window aliases (Priority 3ae), completing the planned object groups. Then move to
step 6 (starting with value/argument/return summaries, Priority 3ag). The unassigned
selector follow-up remains deferred.

## Priority 3ad: Host access trace legacy navigator metadata — DONE (`2026-10-30`)

### What landed

- `navigator.plugins` / `mimeTypes` are cached frozen empty arrays returned by
  `NavigatorHost`; reads record navigator `get` events, while array element reads
  stay untraced.
- `navigator.javaEnabled` is a cached native function exposed as a read-only
  property; the property read records `get` and the call records `call` with its
  own stable token, returning `false` (detached calls included).
- Values, array identity/frozen state, read-only assignment behavior, and
  descriptors are preserved; the function is now native, so `toString()` differs.

### Verification — passed

Final commit `b936307` (branch `host-access-window-aliases`):

- Linux `release-linux` run `41035577072` — cp311–cp314, each
  `1690 passed, 2 skipped`;
- Windows `release-windows` run `41035582564` — Python 3.11–3.14, each
  `1690 passed, 2 skipped`.

This slice passed with the window-alias slice on its final run.

## Priority 3ae: Host access trace window aliases — DONE (`2026-10-30`)

### What landed

- `window.parent` / `top` / `frames` / `opener` are traced native accessors.
  `parent`/`frames` default to window, `top` is always window, `opener` is null.
- Reads record `get`; `parent`/`frames`/`opener` writes record `set` and store a
  per-generation replacement; `top` stays read-only. Replacements reset on
  `load()`.
- Values, write results, delete behavior, descriptor shapes, and top-level `let`
  declarations are preserved by keeping the writable aliases configurable.

### Verification — passed

Final commit `b936307` (branch `host-access-window-aliases`):

- Linux `release-linux` run `41035577072` — cp311–cp314, each
  `1690 passed, 2 skipped`;
- Windows `release-windows` run `41035582564` — Python 3.11–3.14, each
  `1690 passed, 2 skipped`.

The first attempt (`ac4e1f5`) made the writable aliases non-configurable, which
broke top-level `let frames`; `b936307` preserves configurability and passes.

Boundary/verification docs: `docs/host_access_trace.md`. Tests:
`test/test_access_trace_legacy_navigator.py` (3 items) and
`test/test_access_trace_window_aliases.py` (4 items). Full suite collects 1692
items; each wheel lane: **`1690 passed, 2 skipped`**.

### Next

All planned step-5 object groups are complete. Step 6 then added
value/argument/return summaries (3ag), method-reference reads (3ah),
delete/enumeration/define operations (3ai), and the remaining query/target/
summary/exception batch (3aj). Step 6 is complete for the traceable HostObject
surface; `in`, non-HostObject targets, wrapper caching, asynchronous exception
metadata, and timing remain documented blind spots. The unassigned selector
follow-up remains deferred.

## Priority 3af: macOS arm64 V8-linked release wheels — DONE (`2026-10-30`)

### What landed

- `tools/build_v8_macos.sh` builds the pinned V8 monolith on macOS (arm64 by
  default, `V8_TARGET_CPU=x64` optional) and stages `data/v8` for CMake.
- `.github/workflows/release-macos.yml` mirrors the Linux/Windows release
  pattern: build/cached V8 monolith on `macos-15` (Apple Silicon), cp311–cp314
  wheel matrix, clean install, full pytest, artifact upload, tag publishing.
- macOS V8 args use the platform libc++ (`use_custom_libcxx=false`), disable the
  allocator shim/PA-as-malloc (SDK libc++ visibility conflict), keep the V8
  sandbox off, and set `mac_deployment_target=11.0`. CMake links Foundation and
  CoreFoundation for the static monolith.
- Intel/x64 and universal2 macOS wheels are intentionally not built.

### Verification — passed

Run `41043433081` (branch `macos-release`):

- `macos-15` arm64 V8 monolith build + cache succeeded;
- cp311/cp312/cp313/cp314 wheels built as
  `iv8-0.0.1-cp3xx-cp3xx-macosx_11_0_arm64.whl`, all importing V8
  `15.0.245.19`;
- each wheel lane: **`1690 passed, 2 skipped`** (full suite collects 1692).

The first build attempt (`41040030305`) failed in V8's
`allocator_shim_apple.cc` because the platform libc++ visibility attributes
conflict with the shim; `use_allocator_shim=false` +
`use_partition_alloc_as_malloc=false` fixed it.

## Priority 3ag: Host access trace value/argument/return summaries (step 6a) — DONE (`2026-11-02`)

### What landed

- Generic HostObject `get`/`set`/`call` events now carry completion metadata on
  the same enter event:
  - `outcome: "returned" | "threw"` after dispatch (exceptions are never
    swallowed);
  - `value_summary` for get/set;
  - `argument_count` plus up to 8 `argument_summaries` for calls;
  - `return_summary` for calls that return.
- Summaries are primitive/type-only and never execute user code: `undefined`,
  `null`, boolean, number, UTF-8 string (64-byte display cap with original byte
  length, 4096-byte conversion guard), bigint, symbol, function, array,
  host_object, object.
- Completion is attached by `seq`, so nested host accesses during the
  implementation cannot steal it. Allocation failures are best-effort and do
  not affect JS.
- Event count is unchanged; only optional fields are added. Independent native
  callbacks, get_probe, method references, `in`/`hasOwnProperty`, descriptor
  queries, and exception messages/stacks were still pending at this point;
  method references (3ah) and delete/enumeration/define (3ai) were added later.

Boundary/verification: `docs/host_access_trace.md`. Tests:
`test/test_access_trace_values.py` (6 items). Full suite collects 1698 items;
each wheel lane: **`1696 passed, 2 skipped`**.

### Verification — passed

Final commit `4d0800b` (branch `host-access-values`):

- Linux `release-linux` run `41383785169` — cp311–cp314, each
  `1696 passed, 2 skipped`;
- Windows `release-windows` run `41383788221` — Python 3.11–3.14, each
  `1696 passed, 2 skipped`;
- macOS arm64 `release-macos` run `41384345701` — cp311–cp314
  `macosx_11_0_arm64` wheels, each `1696 passed, 2 skipped`.

This slice passed on its first CI attempt.

### Next

Later slices completed method-reference reads (3ah), delete/enumeration/define
operations (3ai), and explicit queries, Reflect target metadata, native
summaries, and synchronous exception metadata (3aj). Step 6 is complete for the
traceable HostObject surface; the `in` operator and documented wrapper blind
spots remain. The unassigned selector follow-up remains deferred.

## Priority 3ah: Host access trace method references (step 6b) — DONE (`2026-11-02`)

### What landed

- HostObject method properties are function-valued accessors. Reading one records
  `get_method`; the function is cached in a private slot so identity and
  writable/configurable/enumerable behavior stay unchanged.
- A normal `receiver.method(...)` read folds into one `call` event when it is the
  last event; if another traced access intervenes, the `get_method` is kept and a
  separate `call` is recorded.
- Assignments record `set`; non-function overrides read as ordinary properties
  (not `get_method`). Descriptor reads materialize the accessor and can produce
  `get_method`.
- Coverage operations now include `get_method`. Detached references are not
  calls; no user `toString`/`valueOf` is executed.

Boundary/verification: `docs/host_access_trace.md`. Tests:
`test/test_access_trace_method_reads.py` (3 items). Full suite collects 1701
items; each wheel lane: **`1699 passed, 2 skipped`**.

### Verification — passed

Final commit `c13bdf5` (branch `host-access-method-reads`):

- Linux `release-linux` run `41395700932` — cp311–cp314, each
  `1699 passed, 2 skipped`;
- Windows `release-windows` run `41395704544` — Python 3.11–3.14, each
  `1699 passed, 2 skipped`;
- macOS arm64 `release-macos` run `41396277098` — cp311–cp314
  `macosx_11_0_arm64` wheels, each `1699 passed, 2 skipped`.

This slice passed on its first CI attempt.

### Next

Delete/enumeration/define (3ai) and the remaining query/target/summary/
exception batch (3aj) completed step 6. The `in` operator, non-HostObject
targets, cached wrappers, asynchronous exception metadata, and timing remain
documented blind spots. The unassigned selector follow-up remains deferred.

## Priority 3ai: Host access trace delete/enumeration/define operations (step 6c) — DONE (`2026-11-02`)

### What landed

- HostObject templates keep the named interceptor active for existing string
  properties (removed `kNonMasking`, kept `kOnlyInterceptStrings`).
- The named deleter, enumerator, and definer record `delete`, `own_keys` (with
  an empty member), and `define_property` request events. Delete/define events
  are emitted before V8 applies the operation; own-key enumeration emits one
  event per request.
- Every callback returns `Intercepted::kNo` or an empty extra-key array, so
  deletion results, enumeration order/result, descriptors, and definition
  behavior are unchanged.
- Coverage operations now list eight entries including delete, own_keys, and
  define_property.
- Descriptor queries and `in`/`hasOwnProperty` are deferred. The first attempt
  (`d314256`) installed the named descriptor callback and failed three existing
  matchMedia/visualViewport expectations because V8 also invoked it for internal
  property bookkeeping (matchMedia's per-wrapper `onchange`); that attempt is not
  part of the verified slice.

Boundary/verification: `docs/host_access_trace.md`. Tests:
`test/test_access_trace_operations.py` (5 items). Full suite collects 1706
items; each wheel lane: **`1704 passed, 2 skipped`**.

### Verification — passed

Final code commit `96c7c00` (branch `host-access-operations`; identical tree to
the release-tested `2addf62`):

- Linux `release-linux` run `41399273630` — cp311–cp314, each
  `1704 passed, 2 skipped`;
- Windows `release-windows` run `41399284773` — Python 3.11–3.14, each
  `1704 passed, 2 skipped`;
- macOS arm64 `release-macos` run `41399294680` — cp311–cp314
  `macosx_11_0_arm64` wheels, each `1704 passed, 2 skipped`.

### Next

Continue with Priority 3aj below: explicit has/descriptor query wrappers,
Reflect target metadata, native callback/getter summaries, and synchronous
exception metadata. The unassigned selector follow-up remains deferred.

## Priority 3aj: Host access trace queries, Reflect targets, native summaries, exception metadata (step 6d–6h) — DONE (`2026-11-02`)

### What landed

- Step 6d wraps `Object.prototype.hasOwnProperty`, `Object.hasOwn`,
  `Reflect.has`, `Object.getOwnPropertyDescriptor`, and
  `Reflect.getOwnPropertyDescriptor` when tracing starts and reinstalls them
  after `load()`. Events record `has`/`get_own_property_descriptor` only for
  HostObject targets, with outcome, argument, and return summaries; results,
  descriptors, and prototypes are unchanged.
- Step 6f adds `target_type`/`target_label` to the existing
  `watch_reflect_get` / `has` / `ownKeys` hits; unrecognized targets are marked
  `"unknown"` with an empty label. The channels remain independent.
- Step 6g applies the primitive/type-only completion summaries to traced native
  BOM callbacks and getters (performance, crypto, Storage, storage manager,
  permissions/status, matchMedia, javaEnabled, and window accessors).
- Step 6h attaches safe synchronous exception name/message/location/stack to a
  traced event while rethrowing the original exception unchanged. Collection
  uses V8 message/stack metadata and does not read a user-defined `stack`
  property.
- The `in` operator, non-HostObject query targets, query wrappers cached before
  tracing starts, asynchronous exception metadata, and general call-site
  locations/timing remain explicit blind spots. The raw named descriptor
  callback stays uninstalled because V8 invokes it for internal property
  bookkeeping.

Boundary/verification: `docs/host_access_trace.md`. Tests:
`test/test_access_trace_queries.py` (5 items),
`test/test_reflect_target_identity.py` (3 items), and two added
`test_access_trace_values.py` cases. Full suite collects 1716 items; each wheel
lane: **`1714 passed, 2 skipped`**.

### Verification — passed

Final code commit `bd148c2` (branch `host-access-queries`; identical tree to
the release-tested `72c00ed`):

- Linux `release-linux` run `41406392044` — cp311–cp314, each
  `1714 passed, 2 skipped`;
- Windows `release-windows` run `41406401506` — Python 3.11–3.14, each
  `1714 passed, 2 skipped`;
- macOS arm64 `release-macos` run `41406410844` — cp311–cp314
  `macosx_11_0_arm64` wheels, each `1714 passed, 2 skipped`.

The first full 6d–6h attempt (`8f8dc23`) failed one query test because the
`Reflect.getOwnPropertyDescriptor` wrapper was missing its completion scope;
`bd148c2` fixed it and the three platforms passed.

### Next

Continue with Priority 3ak below: the aggregated usage report/workflow (step 7).
The `in` operator, non-HostObject targets, wrapper caching, asynchronous
exception metadata, and timing remain documented blind spots rather than M11
scope. The unassigned selector follow-up remains deferred.

## Priority 3ak: Host access trace usage report/workflow (step 7) — DONE (`2026-11-02`)

### What landed

- `Page.access_trace_report(clear=False, progress=None)` is a pure-Python view
  over `read_access_trace`; it does not execute page code.
- The report groups accesses by object type/label/object ID/member/operation and
  preserves each group's first `seq`, first resource/source, last `seq`, count,
  and outcome counts. Missing-property candidates are grouped separately with
  the same first-source metadata.
- `trace`/`coverage` boundaries, operation/object-type totals, and structured
  warnings (`capacity_exceeded`, `object_type_filter_active`,
  `trace_not_recording`, `no_events`) are included.
- The default is non-destructive; `clear=True` snapshots then clears the raw
  log. `progress` is copied from the caller and never interpreted; it records
  event-loop budget/completion for the workflow.
- The exact report shape and recommended run sequence are frozen in
  `docs/host_access_trace_workflow.md`, including the dynamic-run/no-log and
  coverage blind-spot caveats.

Boundary/verification: `docs/host_access_trace_workflow.md`. Tests:
`test/test_access_trace_report.py` (6 items). Full suite collects 1722 items;
each wheel lane: **`1720 passed, 2 skipped`**.

### Verification — passed

Final code commit `c678ede` (branch `host-access-report`; identical tree to
the release-tested `62af2e6`):

- Linux `release-linux` run `41426804187` — cp311–cp314, each
  `1720 passed, 2 skipped`;
- Windows `release-windows` run `41426817304` — Python 3.11–3.14, each
  `1720 passed, 2 skipped`;
- macOS arm64 fallback run `41466841016` (`macos-14`) — built cp311–cp314
  `macosx_11_0_arm64` wheels from the cached V8 monolith and ran the full suite:
  each lane `1720 passed, 2 skipped`. The official macos-15 runner was
  unavailable (`release-macos` run `41426828872` cancelled while queued), so the
  release steps ran on macos-14 with deployment target 11.0.

The first attempt (`3fcc156`) failed one stopped-report warning expectation
because capacity warnings persist with retained events; `c678ede` corrected the
expectation.

### Next

Step 7 is complete. M11-46 (`document.cookie`) starts the object-oriented
common-API track (Priority 3al below). Continue with the next Document slice,
then the next commonly used object; the selector follow-up is unassigned and
`navigator.userAgentData` remains deliberately deferred.

## Priority 3al: Common-API track — Document.cookie (M11-46) — DONE (`2026-11-02`)

### Goal

Begin completing commonly used browser APIs one object at a time, starting with
`Document`, instead of continuing isolated gap slices.

### What landed

- `document.cookie` is a writable, access-traced Document property backed by a
  deterministic in-memory jar owned by `PageState`.
- The setter processes the first `name=value` pair, trims ASCII whitespace,
  updates existing names in place, and appends new names; malformed inputs are
  ignored. `max-age <= 0` and `expires` at or before the Unix epoch delete.
  Attributes are case-insensitive; `path` / `domain` / `secure` / `SameSite`
  are accepted and ignored.
- The jar survives `load()` generations and is isolated per `Page`; it is never
  persisted. Non-string assignments use ordinary `ToString` coercion.
- Reads/writes use the existing HostObject path, so the access trace records
  `document` / `cookie` / `get|set` with normal completion metadata. No new
  Python or JS API was added.
- The descriptor is the framework's writable native data property shape:
  `{value, writable: true, enumerable: true, configurable: false}`; the
  descriptor read evaluates the getter.

Boundary: `docs/m11_46_document_cookie.md`. Tests:
`test/test_m11_46_document_cookie.py` (5 items). The full suite collects 1727
items; each wheel lane: **`1725 passed, 2 skipped`**. The first attempt
(`f92887c`) failed only one descriptor assertion that assumed `get`/`set`
accessors; `18f1f4b` corrected the test to the materialized data descriptor
without runtime changes.

### Verification — passed

Tree `18f1f4b` (branch `m11-46-document-cookie`):

- Linux `release-linux` run `41479330002` — cp311–cp314, each
  `1725 passed, 2 skipped`;
- Windows `release-windows` run `41479338268` — Python 3.11–3.14, each
  `1725 passed, 2 skipped`;
- macOS arm64 fallback run `41482460847` (`macos-14`, cached V8 monolith,
  `macosx_11_0_arm64` wheels) — cp311–cp314, each `1725 passed, 2 skipped`.
  The official macos-15 lane remains unavailable.

### Next

The Document identity slice was completed as Priority 3am below. Later Document
work can revisit the excluded text-node/DocumentFragment architecture or a
scoped `document.write` decision, then move to the next browser object
(Window / Navigator / Element / Form / Event). The selector follow-up remains
unassigned.

## Priority 3am: Common-API track — Document identity (M11-47) — DONE (`2026-11-02`)

### Goal

Continue the object-oriented common-API track with the remaining commonly used
`Document` identity members after `document.cookie`.

### What landed

- `document.title` is writable: ordinary `ToString` coercion (conversion errors
  propagate unchanged), updates the document title string, and reflects the
  value into the first parsed `<title>` element's aggregate `textContent`
  (element children detached like `textContent = ...`; a missing title element
  is not created). A new `load()` rebuilds the title from the new HTML.
- `document.documentURI` / `baseURI` return the current generation base URL;
  `<base href>` is not resolved.
- `document.defaultView` is the context global, identical to `window` /
  `globalThis` (single browsing context).
- `document.location` is the same `LocationHost` wrapper as the global
  `location`; it is read-only on the Document and adds no navigation.
- The four read-only aliases use `ReadOnly | DontDelete`; strict-mode
  assignment throws a `TypeError`.
- All five members use the generic HostObject get/set path and are visible to
  the access trace with ordinary completion metadata. No new Python/JS API.

Boundary: `docs/m11_47_document_identity.md`. Tests:
`test/test_m11_47_document_identity.py` (6 items). The full suite collects 1733
items; each wheel lane: **`1731 passed, 2 skipped`**. All lanes passed on the
first attempt.

### Verification — passed

Tree `cc1c47d` (branch `m11-47-document-identity`):

- Linux `release-linux` run `41483937440` — cp311–cp314, each
  `1731 passed, 2 skipped`;
- Windows `release-windows` run `41483943983` — Python 3.11–3.14, each
  `1731 passed, 2 skipped`;
- macOS arm64 fallback run `41485210986` (`macos-14`, cached V8 monolith,
  `macosx_11_0_arm64` wheels) — cp311–cp314, each `1731 passed, 2 skipped`.
  The official macos-15 lane remains unavailable.

### Next

The text-node/DocumentFragment architecture was approved and landed as M11-48
(Priority 3an below). HTML string APIs and document.write build on it later.

## Priority 3an: Text-node architecture — M11-48 node foundation — DONE (`2026-11-03`)

### Goal

Replace the element-only DOM and stored `textContent` aggregate with
browser-style Text/Comment/DocumentFragment nodes and live text, unblocking the
remaining common DOM APIs.

### What landed

- `NodeKind` values (Element 1 / Text 3 / Comment 8 / DocumentFragment 11);
  the parser emits text (including whitespace and top-level text) and comments
  in source order, treats `script`/`style` as raw text and `title`/`textarea` as
  RCDATA, and no longer computes a stored aggregate.
- `document.createTextNode` / `createComment` / `createDocumentFragment`;
  CharacterData hosts expose `nodeValue` / `data` / UTF-16 `length`; fragments
  expose child/navigation/query/tree-edit surface.
- Live `textContent` (comments excluded; assignment replaces children with zero
  or one Text node), `nodeValue`/`data` writes, and `characterData`
  MutationObserver records (`characterDataOldValue` supported).
- `childNodes` vs element `children`, raw sibling/child aliases vs element
  variants, node-kind-aware clone/equality/contains/connectivity.
- Tree editing accepts all node kinds; fragment insertion moves children and
  empties the fragment with pinned mutation-record ordering.
- Selectors, `document.children`, and collections stay element-only; universal
  and sibling combinators ignore Text/Comment nodes.
- Access-trace object types `text`, `comment`, `document_fragment` are valid
  filters and appear in coverage.
- No Python API or environment change.

Boundary: `docs/m11_48_node_foundation.md`. Tests:
`test/test_m11_48_node_foundation.py` (13 items). The full suite collects 1746
items; each wheel lane: **`1744 passed, 2 skipped`**.

### Verification — passed

Tree `3f4940f` (branch `m11-48-node-foundation`):

- Linux `release-linux` run `41562778133` — cp311–cp314, each
  `1744 passed, 2 skipped`;
- Windows `release-windows` run `41562781320` — Python 3.11–3.14, each
  `1744 passed, 2 skipped`;
- macOS arm64 fallback run `41563750355` (`macos-14`, cached V8 monolith,
  `macosx_11_0_arm64` wheels) — cp311–cp314, each `1744 passed, 2 skipped`.
  Artifact IDs are in `docs/m11_48_report.md`.

The first attempt failed only two new-test expectations (document.contains is
intentionally absent; eval returned an int). Historical element-only boundary
tests were updated intentionally for live text and node creation.

### Next

The HTML string API slice was completed as Priority 3ao below.

## Priority 3ao: HTML string APIs and variadic tree methods — M11-49 — DONE (`2026-11-03`)

### Goal

Turn the M11-48 node foundation into the common DOM manipulation surface needed
for a usable headless browser.

### What landed

- Writable Element `innerHTML` / `outerHTML` with fragment parsing (scripts
  stay inert; parsed controls are initialized) and deterministic serialization
  (id/class first, remaining attributes sorted, escaping, void elements,
  comments, raw script/style text).
- Bounded entity decoding in text, RCDATA, and attributes (common named
  references plus decimal/hex numeric references).
- `insertAdjacentHTML` / `insertAdjacentElement` / `insertAdjacentText` with
  four positions, SyntaxError/TypeError validation, and Element-only insertion.
- `append` / `prepend` / `replaceChildren` on Element and DocumentFragment;
  `before` / `after` / `replaceWith` on Element, Text, and Comment. Node args
  keep identity; strings/non-nodes become Text nodes; fragments move and empty.
- Pinned mutation records: replace-all = one childList record with
  removed+added; append/prepend/before/after = one addition record;
  replaceWith/outerHTML = removal + addition.

Boundary: `docs/m11_49_html_strings.md`. Tests:
`test/test_m11_49_html_strings.py` (12 items). The full suite collects 1758
items; each wheel lane: **`1756 passed, 2 skipped`**.

### Verification — passed

Tree `52bafab` (branch `m11-49-html-strings`):

- Linux `release-linux` run `41577570846` — cp311–cp314, each
  `1756 passed, 2 skipped`;
- Windows `release-windows` run `41577574466` — Python 3.11–3.14, each
  `1756 passed, 2 skipped`;
- macOS arm64 fallback run `41578427318` (`macos-14`, cached V8 monolith,
  `macosx_11_0_arm64` wheels) — cp311–cp314, each `1756 passed, 2 skipped`.
  Artifact IDs are in `docs/m11_49_report.md`.

The first attempt (`e9af2c6`) failed 7 new tests because innerHTML/outerHTML
were writable but missing from the setter's early name guard; `8739b1a` fixed
the routing. `52bafab` corrected two test expectations (innerHTML is
children-only; `Array.join` renders null as empty). Historical tests passed
from the first run aside from intentional guard updates.

### Next

The Window slice was taken next at the user's direction and is Priority 3ap
below. `document.write`/`writeln`/`open`/`close` and import/adopt were deferred
behind the Window-first slices (M11-51/M11-52), then the remaining
headless-browser gaps (context-sensitive parsing, selector pseudo-classes,
additional document surfaces). DOM prototype chains are deferred to a later
stage.

## Priority 3ap: Window scroll / focus / UI completion — M11-50 — DONE (`2026-11-03`)

### Goal

Complete the deterministic `Window` surface that page scripts commonly probe
or call without layout, real focus, navigation, or OS window management.

### What landed

- Read-only `window.scrollX` / `scrollY` (always `0`) and the
  `pageXOffset` / `pageYOffset` aliases, all access-traced.
- No-op `scroll` / `scrollTo` / `scrollBy` accepting any arguments; no-op
  `focus` / `blur` (with `document.hasFocus()` still `false`), `moveBy` /
  `moveTo` / `resizeBy` / `resizeTo`, and `stop`.
- Writable `window.status` (ordinary `ToString`, reset on every `load()`,
  isolated per `Page`) with access-traced `get`/`set`.
- Read-only BarProp metadata (`locationbar`, `menubar`, `personalbar`,
  `scrollbars`, `statusbar`, `toolbar`), each `{visible: true}`.
- Read-only `window.clientInformation` alias of `navigator`.
- All ten new methods are visible to `watch_apis` as `window.<name>` calls.
- No Python API or environment change.

Boundary: `docs/m11_50_window_completion.md`. Tests:
`test/test_m11_50_window_completion.py` (10 items). The full suite collects 1768
items; each wheel lane: **`1766 passed, 2 skipped`**.

### Verification — passed

Tree `6d2250e` (branch `m11-50-window-completion`):

- Linux `release-linux` run `41597535300` — cp311–cp314, each
  `1766 passed, 2 skipped`;
- Windows `release-windows` run `41597541345` — Python 3.11–3.14, each
  `1766 passed, 2 skipped`;
- macOS arm64 fallback run `41598354469` (`macos-14`, cached V8 monolith,
  `macosx_11_0_arm64` wheels) — cp311–cp314, each `1766 passed, 2 skipped`.
  Artifact IDs are in `docs/m11_50_report.md`.

All lanes passed on the first attempt; the temporary macos-14 workflow's
`macos-release` branch was restored to `3f01d7c` afterward.

### Next

Window was raised above Document at the user's direction. M11-51 is Window
event handler attributes (Priority 3aq below); `document.write`/`writeln`/
`open`/`close` and `importNode`/`adoptNode` move to a later milestone.

## Priority 3aq: Window event handler attributes — M11-51 — DONE (`2026-11-03`)

### Goal

Raise Window compatibility further with the commonly used IDL event handler
properties, without changing Document or element priorities.

### What landed

- Native accessor properties on `window` for a bounded handler set:
  `onload`, `onunload`, `onbeforeunload`, `onerror`, `onresize`, `onscroll`,
  `onfocus`, `onblur`, `onhashchange`, `onpopstate`, `onmessage`, `onstorage`,
  `ononline`, `onoffline`, `onpagehide`, `onpageshow`,
  `onunhandledrejection`, and `onrejectionhandled`.
- Getter returns the assigned function or `null`; assigning a function
  installs/replaces it, and non-callable values clear it.
- Handlers run after regular listeners for the same target/phase, at-target
  and while bubbling to `window`, with `this === window` and the Event argument.
- Returning `false` cancels cancelable events; handler exceptions are
  swallowed; `stopImmediatePropagation` suppresses the handler while
  `stopPropagation` does not.
- Handlers are generation-scoped and released during teardown.
- Only the listed names are accessors; other `on*` names stay ordinary global
  properties.
- No Python API or environment key changes.

Boundary: `docs/m11_51_window_event_handlers.md`. Tests:
`test/test_m11_51_window_event_handlers.py` (11 items). The full suite collects
1779 items; each wheel lane: **`1777 passed, 2 skipped`**.

### Verification — passed

Tree `34b119b` (branch `m11-51-window-event-handlers`):

- Linux `release-linux` run `41612087122` — cp311–cp314, each
  `1777 passed, 2 skipped`;
- Windows `release-windows` run `41612092772` — Python 3.11–3.14, each
  `1777 passed, 2 skipped`;
- macOS arm64 fallback run `41613300180` (`macos-14`, cached V8 monolith,
  `macosx_11_0_arm64` wheels) — cp311–cp314, each `1777 passed, 2 skipped`.
  Artifact IDs are in `docs/m11_51_report.md`.

The first attempt (`2924367`) failed 6 of the 11 new tests because handlers
were keyed by `on<type>` while dispatch looked them up by `<type>`; `34b119b`
normalized the key with no test changes. The temporary macos-14 workflow's
`macos-release` branch was restored to `3f01d7c` afterward.

### Next

Window messaging was taken next as Priority 3ar below. A deterministic
focus/visibility model can follow; Document `write`/import work is explicitly
later.

## Priority 3ar: Window messaging (`postMessage` + `MessageEvent`) — M11-52 — DONE (`2026-11-03`)

### Goal

Add the single-context Window messaging surface without introducing iframes,
workers, transferables, or network behavior.

### What landed

- `window.postMessage(message, targetOrigin?, transfer?)` as a writable global
  function. The message is structured-cloned synchronously at send time;
  uncloneable values throw `TypeError`.
- Bounded target origins: omitted/`undefined`, `*`, `/`, the page origin, and
  the page origin with a trailing slash deliver; other values silently drop.
  Non-empty transfer lists throw `TypeError`.
- Asynchronous FIFO delivery through the timer queue (`Page.run_timers()` or a
  facade turn, not `run_jobs`). Delivery uses the ordinary window event path,
  so `addEventListener('message')` runs before the M11-51 `onmessage` handler.
- `MessageEvent` subclass of `Event` with `data` / `origin` / `lastEventId` /
  `source` / `ports`; constructor defaults, copied ports, and `new`-only
  construction.
- `event.origin` is the page origin, `source` is the same window, `ports` is a
  fresh empty array.
- Bootstrap captures the original `structuredClone`, `MessageEvent`, and
  `setTimeout`, so later page overrides do not affect `postMessage`; hidden
  natives are deleted. Calls are watchable as `window.postMessage`.
- No Python API or environment key changes.

Boundary: `docs/m11_52_window_messaging.md`. Tests:
`test/test_m11_52_window_messaging.py` (10 items). The full suite collects 1789
items; each wheel lane: **`1787 passed, 2 skipped`**.

### Verification — passed

Tree `bc00040` (branch `m11-52-window-messaging`):

- Linux `release-linux` run `41620346617` — cp311–cp314, each
  `1787 passed, 2 skipped`;
- Windows `release-windows` run `41620356968` — Python 3.11–3.14, each
  `1787 passed, 2 skipped`;
- macOS arm64 fallback run `41621413284` (`macos-14`, cached V8 monolith,
  `macosx_11_0_arm64` wheels) — cp311–cp314, each `1787 passed, 2 skipped`.
  Artifact IDs are in `docs/m11_52_report.md`.

All lanes passed on the first attempt; the temporary macos-14 workflow's
`macos-release` branch was restored to `3f01d7c` afterward.

### Next

Continue Window-first with M11-53: a deterministic focus/visibility model;
Document `write`/import work remains later.

## Priority 4: Build the ranked compatibility backlog and pick M11-5 — DONE (`2026-10-28`)

### Goal

Replace per-slice intuition with a documented, ranked compatibility backlog.

### What was delivered

- `docs/m11_compatibility_backlog.md` — a source-backed survey (target README and
  demos vs. the current surface), scored candidates, and a ranked shortlist;
- `M11-5` selected from that backlog: `MutationObserver`;
- explicit scope, non-goals, delivery model, and tests frozen in
  `docs/m11_5_mutation_observer.md`.

### Why this matters

- `M11-1`, `M11-2`, `M11-3` and `M11-4` were all chosen without a source-backed gap
  matrix, so slice choice is still direction-driven;
- `docs/m11_compatibility_plan.md` §13 leaves open whether a long-term compatibility
  matrix should be maintained at all.

### Suggested output

- a source-backed compatibility survey or matrix against the target community surface;
- a ranked shortlist of remaining slices;
- one chosen `M11-5` slice with acceptance criteria.

### Acceptance — met

- the backlog is written down and ranked;
- `M11-5` is selected from it with explicit scope, non-goals, and tests.

## Priority 5: Refresh stale build/CI comments

### Goal

Update comments or docs that still describe a much earlier repository state.

### Why this matters

- some comments still mention “placeholder extension”, “no eval yet”, or
  M1-era constraints that no longer match `HEAD`;
- this slows future handoffs and code review.

### Suggested targets

- `src/module.cpp`
- `CMakeLists.txt`
- `.github/workflows/ci.yml`
- other docstrings/comments discovered during edits

### Acceptance

- comments describe the real current surface or clearly note that they are
  historical.

## Priority 6: Decide whether to publish a `Page`-level contract

### Goal

Choose whether the `Page` API should remain milestone-doc driven or gain a single
public contract document similar to M1.

### Why this matters

- `Page` is now central to the real product surface;
- behavior is currently spread across many milestone files and tests.

### Acceptance

- either a new contract doc is created, or a recorded decision explains why the
  milestone-doc approach remains preferred.

## Priority 7: Keep context files updated after every meaningful milestone

### Goal

Prevent another loss of working context after machine migration.

### Required discipline

After any milestone or multi-file feature lands, update:

- `docs/current_status.md`
- `docs/next_tasks.md`
- `docs/decision_log.md` when a real direction choice was made

### Acceptance

- future contributors can resume work from the repo alone.
