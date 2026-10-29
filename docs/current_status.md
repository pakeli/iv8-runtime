# Current Status

Last reviewed: `2026-10-29`

## 1. Summary

`iv8-runtime` started as an M1-scoped Python/V8 embedding project, but the actual
implementation in `master` has progressed well beyond the original M1 boundary.

As of `2026-10-29`, the latest implemented and verified milestone is **M11-34**
(CSS selector engine expansion). Real-V8 CI is green on both platforms — Linux
run `40855935052`, Windows run `40855940659`, both on commit `49f8582`, each
wheel lane reporting `1565 passed, 2 skipped`:

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
`master`. Upstream `investforever/iv8-runtime` remains at M11-3;
upstream sync is out of scope and the fork is the working master. See the
report documents (`docs/m11_6_report.md` … `docs/m11_34_report.md`).

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

### M9–M10 second-layer monitoring

Implemented and present at `HEAD`:

- `Page.devtools_url()` Inspector attach base;
- `Page.watch_apis()` / `read_watch_api_hits()`;
- `Page.watch_reflect_get()` / `read_reflect_get_hits()`;
- `Page.watch_reflect_has()` / `read_reflect_has_hits()`;
- `Page.watch_reflect_ownkeys()` / `read_reflect_ownkeys_hits()`;
- `Page.watch_object_keys()` / `read_object_keys_hits()`;
- optional `break_on_hit=True` pause behavior when a DevTools client is attached.

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
  existing `Page.run_jobs()` microtask checkpoint. Element and document targets are
  accepted; `characterData` is accepted but inert because the DOM is element-only.
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
  `querySelector[All]`, `matches()`, and `closest()`. Pseudo-classes are M11-35.
  Verified `2026-10-29`; see `docs/m11_34_selector_engine.md`.

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

## 4. Release / Distribution Status

Current known status from repository docs:

- Linux release strategy was chosen in `docs/phase9_release_strategy.md`.
- The repository records a green Linux wheel workflow result for cp311–cp314.
- Linux is the real release target.
- Windows and macOS exist in CI/build planning, but should still be treated as
  non-primary release targets unless explicitly updated.

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
- the next execution handoff is to choose `M11-35` — the planned follow-up is
  selector pseudo-classes; otherwise use `docs/m11_compatibility_backlog.md` or
  another gap-inventory slice with an explicit boundary.

## 7. Current Open Questions

These are the questions a new coding session should resolve before major new work:

- Which concrete compatibility slice should become `M11-35`? (`docs/decision_log.md`
  §A — `M11-1` through `M11-34` are settled, landed, and verified; the planned
  follow-up is selector pseudo-classes, or the ranked compatibility matrix at
  `docs/m11_compatibility_backlog.md` / `docs/dom_bom_gaps.md` can drive another
  choice.)
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
> hosted runtime and the M9–M10 monitoring extensions, and is executing the
> compatibility-first `M11` line — with `M11-34` (CSS selector engine expansion)
> landed and verified on real V8 (Linux + Windows) on `2026-10-29`.

For the surface behind that verdict, read `docs/runtime_surface.md`.

## 9. Local Environment Note

`data/review-venv` currently holds a **stale Phase-1 skeleton** install of `iv8` (its
`__init__.py` predates `Page`, and its `_core` extension has no `Page` binding), so
running `pytest test` against it reports widespread `AttributeError` failures rather
than the expected skips. That is a local install artifact, not a repository
regression. Rebuild and reinstall before trusting any local suite result, and check
`iv8._v8_linked` to know whether you are on a real-V8 or skeleton build.
