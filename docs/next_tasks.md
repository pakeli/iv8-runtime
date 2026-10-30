# Next Tasks

Last reviewed: `2026-10-30`

This file is intentionally short and should be kept current. It is the preferred
task queue for Claude after chat-history loss.

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
existing `Page.run_jobs()` microtask checkpoint. Element and document targets are
accepted; `characterData` is accepted but inert because the DOM is element-only.
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

Choose `M11-46` — selector pseudo-classes remain the planned selector
follow-up; `navigator.userAgentData` is deliberately deferred in this version.
Remaining candidates are inventoried in `docs/dom_bom_gaps.md`.

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
