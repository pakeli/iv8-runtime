# Decision Log

Last reviewed: `2026-11-04`

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

As of `2026-08-06`, the latest landed milestone at `HEAD` was treated as `M10-2`.

This decision exists so future sessions do not restart from outdated M1-only
assumptions. **Superseded:** the M11 compatibility line has since advanced through
`M11-4`; use `docs/current_status.md` for the current stage rather than this
historical entry.

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

### 10. Post-M10 work is compatibility-first, not ad hoc feature growth

Decided `2026-08-06`.

After `M10-2`, the repository's main forward-looking goal is no longer framed as
"continue adding whichever hosted-runtime feature seems most convenient next".
Instead, it is framed as:

> gradually reproduce the community-edition capability surface of
> `HanZzzzz000/iv8`, then extend beyond it deliberately.

Consequences of this decision:

- the next milestone line is `M11`, not an unbounded continuation of `M10`;
- `M11` starts with compatibility planning and gap ranking before new runtime code;
- future implementation slices should be justified as compatibility moves or as
  clearly-labeled enhancements beyond that compatibility baseline.

The planning artifact for this decision is `docs/m11_compatibility_plan.md`.

### 11. M11-5 delivered `MutationObserver` without a new pump

Decided/verified `2026-10-28`.

`M11-5` implements the JS global `MutationObserver` on the existing
`Page.run_jobs()` microtask checkpoint. It deliberately adds no Python API, no
fifth pump, no `MutationRecord` global, no `id` / `className` property setters,
and no cross-generation observer state. Records cover `childList` and
`attributes` (with `subtree` / `attributeOldValue` / `attributeFilter`);
`characterData` is accepted but inert because the DOM is element-only. (M11-48
later activated `characterData` for real Text/Comment nodes; see entry 45.) The
missing `id` / `className` setters were later added by M11-12; those writes now
queue the same attribute records as `setAttribute`.

Real-V8 verification on Linux run `40632597636` and Windows run `40632601387`
(commit `4fa6469`): all eight wheel lanes reported `1265 passed, 2 skipped`.
Two post-implementation fixes were required — `ab1d507` restored the V8 15.0
`ExternalPointerTypeTag` argument on `External::New` / `External::Value`, and
`4fa6469` corrected the tests to set ids through `setAttribute`. See
`docs/m11_5_mutation_observer.md` and `docs/m11_5_report.md`.

### 12. M11-6..M11-10 verified as one five-slice batch

Decided/verified `2026-10-28`.

To amortize the cold V8 build, M11-6 (event subclasses), M11-7 (Web Storage),
M11-8 (Blob/File/FileReader), M11-9 (event cancellation), and M11-10
(classList/toggleAttribute) were developed and committed on one branch
(`m11-6-event-subclasses`). The first run cold-built V8 and saved the branch
cache; the final commit `35c85fe` was then verified with cached runs: Linux
`40661574145`, Windows `40661580891`, each wheel lane `1352 passed, 2 skipped`.
All five new test files ran. Contract guards that had frozen the old surface
were refreshed as part of the batch.

### 13. M11-11 delivered event capture and listener options

Decided/verified `2026-10-28`.

The event model now has a capturing phase (window → document → ancestors),
`eventPhase` 0/1/2/3, and `capture` / `once` / `passive` listener options, with
`(type, callback, capture)` add/remove identity. Capture runs even for
non-bubbling events; `once` removes before invocation and `passive` suppresses
`preventDefault` for that listener. Verified on Linux `40683001239` and Windows
`40683010032` (commit `a269aa9`), each wheel lane `1370 passed, 2 skipped`.

### 14. M11-12..M11-17 verified as one six-slice DOM/web batch

Decided/verified `2026-10-28`.

The batch (element attribute API, tree editing, cloneNode/equality,
`URL.searchParams` snapshot, select extension, nested environment +
`get_defaults()`) was verified on real V8 at commit `8d606e9`: Linux
`40695004440`, Windows `40695029530`, each wheel lane `1378 passed, 2 skipped`.
The first attempt at `0c47ecb` built every wheel but failed 15 historical
frozen-surface guards; refreshing those guards in `8d606e9` was the only
change needed. The full suite collects 1380 items (1370 previous + 8 batch
tests).

### 15. M11-18 delivered form-control attribute reflection

Decided/verified `2026-10-28`.

M11-18 adds a minimal reflected surface: input `type` / `disabled` /
`readOnly` / `placeholder`, button `type` / `disabled`, `form.encoding` (an
alias of the existing `.enctype` runtime slot), and `option.label` / read-only
`index`. Reflected writes are real attribute writes and feed the existing
MutationObserver attribute hooks; `form.encoding` changes only the runtime
slot. No Python API, validation, or form-submission behavior was added.

Verified on real V8 at commit `08350ce`: Linux `40698705892`, Windows
`40698728631`, each wheel lane `1386 passed, 2 skipped`. The first verification
run was green. The affected historical form guards were refreshed in the same
change. See `docs/m11_18_form_control_reflection.md` and
`docs/m11_18_report.md`.

### 16. M11-19..M11-23 verified as one five-slice DOM/form batch

Decided/verified `2026-10-28`.

The batch adds multi-token class queries and element-level
`getElementsByClassName`; `document.getElementsByName`; extended form reflection;
select `length` / `item` / `namedItem`; and deterministic document metadata
including `hasFocus()`. All five are JS-side only and add no architecture
surface. Boundary: `docs/m11_19_23_dom_form_batch.md`.

Verified on real V8 at commit `999c851`: Linux `40704330688`, Windows
`40704362549`, each wheel lane `1412 passed, 2 skipped`. The first attempt
(`9f3e459`) failed one historical element-query guard; refreshing it in
`999c851` was the only change needed. The full suite collects 1414 items
(1388 previous + 26 batch tests). See `docs/m11_19_23_report.md`.

### 17. M11-24..M11-28 verified as one host environment / console batch

Decided/verified `2026-10-28`.

The batch extends the configurable environment with navigator metadata and
identity fields, screen color/pixel depth, and outer window geometry, then adds
deterministic console methods. The flat environment grows from 11 to 27 keys;
existing keys/defaults are unchanged and no new Python method or setter exists.

Verified on real V8 at commit `a00e140`: Linux `40711526339`, Windows
`40711548915`, each wheel lane `1474 passed, 2 skipped`. The first attempt
(`f857d03`) failed one fresh-array test, fixed in `a00e140`. The full suite
collects 1476 items (1414 previous + 62 from the extended environment matrix
and 22 new tests). See `docs/m11_24_28_report.md`.

### 18. M11-29 delivered the deterministic event-loop facade

Decided/verified `2026-10-29`.

M11-29 adds `Page.tick()`, `advance(ms, max_steps)`, `run_until_idle(max_steps)`
and the read-only `event_loop_time_ms` over the existing timer and microtask
queues. Timers are ordered by virtual due time and registration sequence;
microtasks drain after each task; intervals use a 1ms virtual floor and bounded
steps. The legacy pumps and rAF/idle remain unchanged and are not run by the
facade in this phase.

Verified on real V8 at commit `b211481` (feature commit `0bc75a2`): Linux
`40820152813`, Windows `40820163797`, each wheel lane `1495 passed, 2 skipped`.
The first attempt (`0bc75a2`) failed three stale pump non-goal guards, refreshed
in `b211481`. The full suite collects 1497 items (1476 previous + 21 tests). See
`docs/m11_29_report.md`.

### 19. M11-30 integrated animation frames and idle callbacks into the facade

Decided/verified `2026-10-29`.

M11-30 extends the M11-29 facade to drive `requestAnimationFrame` at fixed 16ms
virtual frame boundaries and `requestIdleCallback` one round at a time when no
timer/frame is due. Priority is due timer → due frame → idle; every task is
followed by a microtask checkpoint. `tick()` runs one current-time turn;
`advance()` and `run_until_idle()` cover all four queues with bounded steps.
The legacy one-queue pumps are unchanged and do not advance the facade clock.

The first verification attempt (`32f9289`) hung on every lane: the clock jump in
`advance()` / `run_until_idle()` used the timer id returned by
`next_timer_due()` as a due timestamp, so a future timer whose id was not
greater than the current clock spun the loop without counting a step.
`7dde955` looks up `due_ms` instead.

Verified on real V8 at commit `7dde955` (feature commit `32f9289`): Linux
`40826630371`, Windows `40826634489`, each wheel lane `1509 passed, 2 skipped`.
The full suite collects 1511 items (1497 previous + 14 tests). See
`docs/m11_30_report.md`.

### 20. M11-31/M11-32 added a virtual performance clock and minimal form validation

Decided/verified `2026-10-29`.

M11-31 exposes `performance.now()` / `performance.timeOrigin` over the
M11-29/M11-30 facade clock and passes the current virtual timestamp to
`requestAnimationFrame` callbacks. M11-32 adds a minimal constraint-validation
surface: `willValidate` / `validity` / `validationMessage` / `checkValidity()` /
`reportValidity()` / `setCustomValidity()` on controls, form-level aggregation,
and target-only `invalid` events. Supported constraints are `required`,
`minlength` / `maxlength`, email/URL `typeMismatch`, and custom errors; pattern,
min/max/step, radio-group semantics, and native UI remain deferred.

The first verification attempt (`faf5e28`) failed one new validation test
because the test helper omitted the `r` id; `a88b69d` fixed the test.

Verified on real V8 at commit `a88b69d` (feature commit `faf5e28`): Linux
`40831677507`, Windows `40831682182`, each wheel lane `1540 passed, 2 skipped`.
The full suite collects 1542 items (1511 previous + 31 tests). See
`docs/m11_31_32_report.md`.

### 21. M11-33 added OS-backed Crypto random APIs

Decided/verified `2026-10-29`.

M11-33 adds a read-only `crypto` global with `getRandomValues()` and
`randomUUID()`. `getRandomValues` accepts all integer TypedArrays, fills the
view's byte range, returns the same view, enforces the 65536-byte quota with a
`RangeError` (no `DOMException` in this runtime), and rejects
non-integer/missing/detached inputs with `TypeError`. `randomUUID()` returns a
lower-case RFC 4122 v4 UUID. `watch_apis` records both calls.

Randomness is the runtime's **one deliberate non-deterministic surface**: the
platform CSPRNG (advapi32 `SystemFunction036` on Windows, `getrandom` with a
`/dev/urandom` fallback on Linux) and no seed/replay mode. `crypto.subtle`,
`CryptoKey`, hash/AES/RSA/ECDH/HMAC/KDF/digest algorithms, deterministic
seeding, and a Python Crypto API remain out of scope.

Verified on real V8 at commit `c9b2fb5`: Linux `40834145493`, Windows
`40834153439`, each wheel lane `1553 passed, 2 skipped`. The full suite collects
1555 items (1542 previous + 13 tests). See `docs/m11_33_report.md`.

### 22. M11-34 expanded the CSS selector engine

Decided/verified `2026-10-29`.

M11-34 replaces the single-token selector matcher with a small CSS Selectors
engine shared by `querySelector[All]`, `matches()`, and `closest()`. It supports
selector lists, compound selectors, type/universal selectors, all four
combinators, and attribute selectors (`[a]`, `=`, `~=`, `|=`, `^=`, `$=`, `*=`)
with quoted/unquoted values. Attribute names are case-insensitive and values
case-sensitive; element-scoped queries keep the subtree-includes-self semantics.
Malformed/unsupported selectors retain stable empty/null/false behavior.
Pseudo-classes/elements, escapes, namespaces, and `i`/`s` flags are deferred
(M11-35 was subsequently used for BOM metadata; pseudo-classes remain a later
selector slice).

The first verification attempt (`a8fe60d`) failed one refreshed
`closest('div.box')` expectation (the engine correctly returned the ancestor
`a`, not `c`); `49f8582` fixed the test.

Verified on real V8 at commit `49f8582` (feature commit `a8fe60d`): Linux
`40855935052`, Windows `40855940659`, each wheel lane `1565 passed, 2 skipped`.
The full suite collects 1567 items (1555 previous + 12 tests). See
`docs/m11_34_report.md`.

### 23. M11-35 completed deterministic BOM metadata

Decided/verified `2026-10-30`.

M11-35 adds the remaining deterministic window/screen/navigator metadata:
`window.name` (Page-persistent mutable string), `window.length`,
`screenLeft` / `screenTop`, `isSecureContext` (base-URL-derived),
`screen.availLeft` / `availTop` / `orientation` (derived from dimensions), and
`navigator.maxTouchPoints` / `pdfViewerEnabled` / `doNotTrack` / a read-only
`connection` object. The flat environment grows from 27 to 36 keys, with nested
`navigator` / `screen` forms and strict type validation. No navigation,
networking, rendering, dialogs, performance marks, structuredClone, or Python
BOM API is added.

Verified on real V8 at commit `aa23c57`: Linux `40990275470`, Windows
`40990279247`, each wheel lane `1599 passed, 2 skipped`. The full suite collects
1601 items (1567 previous + 12 new tests + 22 environment parametrizations). See
`docs/m11_35_report.md`.

### 24. M11-36..M11-40 completed the deterministic BOM stubs

Decided/verified `2026-10-30`.

The batch adds deterministic `alert` / `confirm` / `prompt` / `print` stubs, a
virtual `performance` mark/measure timeline with entry queries and clears,
bounded `structuredClone` (including cycles, boxed primitives, and shared
cloned view buffers), single-context `window.parent` / `top` / `frames` /
`opener` metadata, and Promise-based `scheduler.postTask` over the existing
timer queue. `navigator.userAgentData` is intentionally not implemented; a
caller that wants a Chromium identity supplies one through
`Page(environment=...)`. `parent` and `frames` are replaceable, `top` remains
unforgeable/read-only, and `opener` is writable, matching browser
browsing-context metadata without adding child contexts.

The first verification attempt (`84a8850`) exposed three test-level collisions
and one stale expectation: `let frames` and `var parent` collided with the newly
installed globals, and a `structuredClone` assertion had the wrong formatting.
`512117c` corrected the semantics, refreshed the affected historical tests, and
tightened structuredClone view identity/offset handling.

Verified on real V8 at commit `512117c`: Linux `40997424174`, Windows
`40997427191`, each wheel lane `1617 passed, 2 skipped`. The full suite collects
1619 items (1601 previous + 18 new tests). See
`docs/m11_36_40_report.md`.

### 25. M11-41..M11-45 completed deterministic BOM viewport/media/device metadata

Decided/verified `2026-10-30`.

The batch adds a static `window.visualViewport` and window flags
(`origin` / `crossOriginIsolated` / `closed` / `frameElement`), a bounded
`matchMedia()` evaluator with no dynamic events, deterministic Promise-based
`navigator.storage` (`estimate` / `persisted` / `persist`), a bounded
`navigator.permissions.query()` that reports `"prompt"` for known permission
names, and empty legacy `navigator.plugins` / `mimeTypes` plus
`javaEnabled() === false`. No layout, real storage, OS permissions, network, or
new Python/environment surface is added. `navigator.userAgentData` remains
deliberately deferred.

The first verification attempt (`f3cebcb`) failed one test assertion because a
JS `null` joined into a string as an empty field instead of `"null"`; `61abc3b`
fixed only the test. Verified on real V8 at commit `61abc3b`: Linux
`41000182750`, Windows `41000186396`, each wheel lane `1626 passed, 2 skipped`.
The full suite collects 1628 items (1619 previous + 9 new tests). See
`docs/m11_41_45_report.md`.

### 26. Host access trace first slice (steps 1-3)

Decided/verified `2026-10-30`, following `docs/host_access_tracing_plan.md`
without assigning a new milestone number.

The first slice adds a separate Page-level `start_access_trace` /
`stop_access_trace` / `read_access_trace` channel for native HostObject
`property_getter` / `property_setter` / `method_callback` entries. Events are
`phase: "enter"`, carry sequence/generation/stable native object ID/type/label/
member/operation/resource/source kind, use earliest-N capacity with explicit
drops, support object-type filters, and do not add a JS-visible API or change
the five existing watch channels. The native hook fires before the concrete host
implementation; read-only writes and method-reference reads are not recorded.

Step 2/3 entry points are `property_getter`, `property_setter`, and
`method_callback` only; `window` data properties, plain BOM objects, user
objects, method-reference reads, and missing-property probes remain explicitly
out of scope. Step 4 (missing-property probes) is the next design gate.

The first verification attempt (`edb3dd7`) failed one test that assumed an
object-toString conversion would throw; `44fc847` switched the fixture to a
deterministic non-element `appendChild` throw. The runtime path was unchanged.
Verified on real V8 at commit `44fc847`: Linux `41015201477`, Windows
`41015206089`, each wheel lane `1646 passed, 2 skipped`. The full suite collects
1648 items (1628 previous + 20 new tests). See `docs/host_access_trace.md`.

### 27. Host access trace missing-property probes (step 4)

Decided/verified `2026-10-30`.

HostObject templates now install a named getter with `kNonMasking` +
`kOnlyInterceptStrings`, plus a no-op named query handler. Missing string-key
reads produce a `get_probe` event with `resolution: "missing"` and return
`Intercepted::kNo`; normal undefined/prototype lookup and descriptors are
unchanged. A real own/prototype property guard (`HasRealNamedProperty` chain
walk) suppresses false probes for existing properties, including value-
`undefined` own properties and prototype properties. Proxy/host prototypes are
treated conservatively as present. Numeric indices, Symbols, non-HostObject
objects, `in`, delete, enumeration, and descriptors remain outside this slice.
`resolution` currently emits only `"missing"`; no present/unknown classification
is emitted.

The first probe attempt (`76baaa2`) exposed query-path and existing-property
false probes; `8995b89` added the no-op query handler and real-property guard.
Verified on real V8 at commit `8995b89`: Linux `41017981567`, Windows
`41017987566`, each wheel lane `1656 passed, 2 skipped`. The full suite collects
1658 items (1648 previous + 10 probe tests). See `docs/host_access_trace.md`.

### 28. Host access trace native BOM callbacks (step 5a)

Decided/verified `2026-10-30`.

The first step-5 group records independent native BOM callback calls without
changing plain-object descriptors, prototypes, function identity, or `this`:
`performance` methods, `crypto` methods, and Storage methods plus the Storage
`length` getter. `performance`/`crypto` use stable native identity tokens;
`localStorage` and `sessionStorage` use their distinct backing store pointers as
object IDs and both report `object_type: "storage"` / label `"storage"`. Property
reads on these plain objects (other than Storage `length`) remain the next
step-5 group; value/argument/return summaries remain step 6.

Verified on real V8 at commit `ac80ee8`: Linux `41019395760`, Windows
`41019399775`, each wheel lane `1660 passed, 2 skipped`. The full suite collects
1662 items (1658 previous + 4 BOM-call tests). See
`docs/host_access_trace.md`.

### 29. Host access trace intrinsic window metadata (step 5b)

Decided/verified `2026-10-30`.

Read-only window metadata is now exposed through native accessors that record
`get` events with `object_type: "window"` and label `"window"`:
`devicePixelRatio`, `innerWidth`, `innerHeight`, `outerWidth`, `outerHeight`,
`screenX`, `screenY`, `screenLeft`, `screenTop`, `length`, `isSecureContext`,
`origin`, `crossOriginIsolated`, `closed`, and `frameElement`. `window.name`
records `get`/`set` through its existing accessor. Values, read-only assignment
behavior, and strict-mode errors are unchanged. The data-to-accessor descriptor
change is intentional for these fields; `parent` / `top` / `frames` / `opener`
keep their prior descriptors and remain untraced in this group.

Verified on real V8 at commit `e19f54d`: Linux `41020686185`, Windows
`41020689881`, each wheel lane `1664 passed, 2 skipped`. The full suite collects
1666 items (1662 previous + 4 window tests). See
`docs/host_access_trace.md`.

### 30. Host access trace nested metadata (step 5c)

Decided/verified `2026-10-30`.

`navigator.connection` and `screen.orientation` are now nested HostObjects, so
their property reads, missing string-key probes, and backing object identity use
the normal access-trace channel. Each nested property read also records the
parent getter (e.g. `navigator.connection` before `connection.effectiveType`).
`performance.timeOrigin` is now a traced read-only accessor on the otherwise
plain performance object. Values and read-only behavior are unchanged.

The first attempt (`9567761`) had two test expectations that assumed one event
per nested access; `924effd` corrected them to include repeated parent getters
and the new timeOrigin event. The runtime behavior was intended.

Verified on real V8 at commit `924effd`: Linux `41022649736`, Windows
`41022653765`, each wheel lane `1667 passed, 2 skipped`. The full suite collects
1669 items (1666 previous + 3 nested tests). See
`docs/host_access_trace.md`.

### 31. Host access trace static visualViewport (step 5d)

Decided/verified `2026-10-30`.

`visualViewport` is now a `VisualViewportHost` HostObject instead of a plain JS
object, so metadata reads, writable `onresize`/`onscroll`, no-op event-target
method calls, and missing string-key probes enter the normal trace channel. The
global property stays read-only; width/height still come from the Page
environment and method results are unchanged.

The conversion intentionally changes non-value characteristics, now documented
and tested: the instance prototype is the host-object template prototype; no-op
methods are writable/configurable; the writable handler descriptors are
non-configurable; read-only metadata descriptors keep their data shape but
descriptor reads evaluate the native getter and therefore emit a `get` event.
The fixed bootstrap was adjusted to read `innerWidth`/`innerHeight` once for the
static media evaluator, keeping genuine `internal` source attribution.

The first CI attempts failed only on test expectations (descriptor getter
invocation, `Array.join` of undefined, host-object descriptors/prototype); the
final commit `c87ffbe` carries the verified tests and runtime behavior.

Verified on real V8 at commit `c87ffbe`: Linux `41025891916`, Windows
`41025896654`, each wheel lane `1671 passed, 2 skipped`. The full suite collects
1673 items (1669 previous + 4 visualViewport tests). See
`docs/host_access_trace.md`.

### 32. Host access trace navigator.storage manager (step 5e)

Decided/verified `2026-10-30`.

`navigator.storage` is now exposed by `NavigatorHost` as a cached plain object
whose `estimate` / `persisted` / `persist` methods are native callbacks. The
parent read records a navigator `get`; each method call records a
`storage_manager` `call` using a stable native token. This keeps the M11-43
plain-object shape: `Object.prototype`, cached identity, read-only method
descriptors, unchanged results, and unchanged rejected read-only writes. No
manager descriptor/prototype change is needed.

Manager property reads/probes, method-reference reads, and Promise/estimate
values remain untraced and are documented as such. This slice passed on its
first CI attempt.

Verified on real V8 at commit `f91665f`: Linux `41027870583`, Windows
`41027874811`, each wheel lane `1675 passed, 2 skipped`. The full suite collects
1677 items (1673 previous + 4 storage-manager tests). See
`docs/host_access_trace.md`.

### 33. Host access trace navigator.permissions (step 5f)

Decided/verified `2026-10-30`.

`navigator.permissions` is exposed by `NavigatorHost` as a cached plain object
whose `query()` method is a native callback. The parent read is traced, each
query call is traced, and returned PermissionStatus objects keep their M11-44
plain shape while exposing `name` / `state` through traced native accessors and
the no-op event-target methods through traced native callbacks. Status identity
is grouped by the fixed per-permission descriptor; different permission names
get different IDs and IDs reset with a new generation.

The exact rejection messages, synchronous descriptor-getter exception, writable
`onchange`, and fresh status object per query are preserved. `onchange`
reads/writes, status missing-property probes, and Promise values remain
untraced.

The first CI attempt failed only on a test expectation (`Array.join` renders
`null` as an empty field); the final commit `18e674e` corrects the test.

Verified on real V8 at commit `18e674e`: Linux `41031830520`, Windows
`41031835192`, each wheel lane `1680 passed, 2 skipped`. The full suite collects
1682 items (1677 previous + 5 permissions tests). See
`docs/host_access_trace.md`.

### 34. Host access trace matchMedia results (step 5g)

Decided/verified `2026-10-30`.

`matchMedia` remains a JS function (so its non-native `toString` is unchanged)
and keeps the bounded JS query evaluator. A temporary native factory is captured
by the bootstrap in its closure and deleted from the global immediately. Each
result is a fresh `MediaQueryListHost` wrapper over a Page-owned backing cached
by (media string, matches); repeated queries share the backing identity, and
different queries do not. `media`/`matches` reads, the five no-op listener
methods, and missing probes are traced. `onchange` stays a plain per-wrapper own
property and is untraced.

Documented tradeoffs: HostObject template prototype, writable/configurable no-op
methods, descriptor reads that evaluate the media/matches getters, UTF-16 query
storage for exact text, and one backing per unique query/matches until the next
`load()`.

Verified on real V8 at commit `7d35a30`: Linux `41032945394`, Windows
`41032950281`, each wheel lane `1683 passed, 2 skipped`. The full suite collects
1685 items (1682 previous + 3 matchMedia tests). See
`docs/host_access_trace.md`.

### 35. Host access trace legacy navigator metadata (step 5h)

Decided/verified `2026-10-30`.

`navigator.plugins` / `mimeTypes` are cached frozen empty arrays returned by
`NavigatorHost`, so parent reads record navigator `get` events while array
element reads stay untraced. `navigator.javaEnabled` is a cached native function
exposed as a read-only property: the property read records `get`, the call
records `call` with its own stable token and returns `false`. Array identity,
frozen state, read-only assignment behavior, and data descriptors are preserved;
the function is native now, so its `toString()` differs.

### 36. Host access trace window aliases (step 5i)

Decided/verified `2026-10-30`.

`window.parent` / `top` / `frames` / `opener` are traced native accessors backed
by per-generation PageState overrides. `parent`/`frames` default to window,
`top` is always window, and `opener` is null. Reads record `get`;
`parent`/`frames`/`opener` writes record `set` and store a replacement that resets
on `load()`; `top` stays read-only. The writable aliases keep their configurable
attributes so delete behavior and top-level `let` declarations are unchanged.
The first attempt (`ac4e1f5`) used non-configurable accessors and broke
`let frames`; `b936307` fixed the attributes.

Verified on real V8 at commit `b936307`: Linux `41035577072`, Windows
`41035582564`, each wheel lane `1690 passed, 2 skipped`. The full suite collects
1692 items (1685 previous + 3 legacy-navigator + 4 window-alias tests). This
completes all planned step-5 object groups; remaining plain-property/returned-
collection gaps are documented non-goals. See `docs/host_access_trace.md`.

### 37. macOS arm64 V8-linked release wheels

Decided/verified `2026-10-30`.

macOS release support mirrors Linux/Windows: `tools/build_v8_macos.sh` builds the
pinned V8 monolith (arm64 by default) and `release-macos.yml` caches it, builds
cp311–cp314 wheels on `macos-15` (Apple Silicon), installs and tests each wheel,
and publishes on tags. macOS uses the platform libc++ with the allocator shim
disabled (SDK visibility conflict), keeps the V8 sandbox off, and sets
`mac_deployment_target=11.0`. CMake links Foundation/CoreFoundation for the
static monolith. Intel/x64 and universal2 remain unbuilt.

The first build (`41040030305`) failed in `allocator_shim_apple.cc`; disabling
`use_allocator_shim` / `use_partition_alloc_as_malloc` fixed it. Verified in run
`41043433081`: wheels
`iv8-0.0.1-cp3xx-cp3xx-macosx_11_0_arm64.whl`, V8 `15.0.245.19`, each lane
`1690 passed, 2 skipped` (full suite 1692).

### 38. Host access trace value/argument/return summaries (step 6a)

Decided/verified `2026-11-02`.

The generic HostObject get/set/call path now attaches completion metadata to the
existing enter event: `outcome` (`returned`/`threw`), `value_summary` for get/set,
`argument_count` plus up to eight `argument_summaries` for calls, and
`return_summary` for returned calls. Completion is keyed by `seq`, so nested
accesses during the implementation cannot steal it. Summaries inspect primitive
types only and never run user `toString`/`valueOf` or property Gets; strings are
UTF-8, capped at 64 display bytes with the original length, and skip conversion
above 4096 bytes. Event counts and `phase: "enter"` are unchanged.

The slice is intentionally partial for step 6: native-callback summaries,
method-reference reads, `in`/delete/enumeration/descriptors, Reflect target
identity, exception messages, stacks, and timing remain pending.

Verified on real V8 at commit `4d0800b`: Linux `41383785169`, Windows
`41383788221`, and macOS arm64 `41384345701`, each wheel lane `1696 passed,
2 skipped`. The full suite collects 1698 items (1692 previous + 6 value-summary
tests). See `docs/host_access_trace.md`.

### 39. Host access trace method-reference reads (step 6b)

Decided/verified `2026-11-02`.

HostObject method properties are now function-valued accessors. A read records
`get_method`; the function is created lazily and cached in a V8 private slot on
the wrapper, preserving identity, writability/configurability/enumerability, and
override/delete behavior. A normal `receiver.method(...)` read is folded into one
`call` when it is the last trace event; if another access intervenes, the
`get_method` remains and a separate call is recorded. Method assignment records
`set`, and non-function overrides read as ordinary properties rather than
`get_method`.

The folding rule is intentionally conservative and documented: descriptor reads
instantiate the accessor and may produce `get_method`; detached references are
not calls; no user `toString`/`valueOf` is executed. Coverage operations now
include `get_method`.

Verified on real V8 at commit `c13bdf5`: Linux `41395700932`, Windows
`41395704544`, and macOS arm64 `41396277098`, each wheel lane `1699 passed,
2 skipped`. The full suite collects 1701 items (1698 previous + 3
method-reference tests). See
`docs/host_access_trace.md`.

### 40. Host access trace delete/enumeration/define operations (step 6c)

Decided/verified `2026-11-02`.

HostObject named-property handlers now keep the interceptor active for existing
string keys (removed `kNonMasking`, kept `kOnlyInterceptStrings`) and record
three request events: `delete`, `own_keys` (empty member), and
`define_property`. Delete and define events are written before V8 applies the
operation, so a failed deletion still leaves evidence of the request. Every
callback returns `Intercepted::kNo` or an empty extra-key array, so lookup,
deletion results, enumeration order, descriptors, and definition behavior are
unchanged.

The named descriptor callback was implemented first (`d314256`) but removed
before verification. CI showed V8 invoking it for internal property bookkeeping
beyond explicit queries — for example, adding the per-wrapper matchMedia
`onchange` through native `Set` — which polluted ordinary traces with
indistinguishable `get_own_property_descriptor` events. Descriptor queries and
`in`/`hasOwnProperty` remain deferred; the latter has no reliable non-invasive
record point in the no-op query callback. A descriptor read may still evaluate a
native getter and emit `get`/`get_method`, but no descriptor operation event.

The operations currently carry only common fields, not completion summaries.
Coverage operations grow from five to eight entries.

Verified on real V8 against the tree of commit `96c7c00` (the release runs used
the identical tree `2addf62`, cleanly re-committed as `96c7c00`): Linux
`41399273630`, Windows `41399284773`, and macOS arm64 `41399294680`, each wheel
lane `1704 passed, 2 skipped`. The full suite collects 1706 items (1701 previous
+ 5 operation tests). See `docs/host_access_trace.md`.

### 41. Host access trace query wrappers, target metadata, native summaries, exception metadata (step 6d–6h)

Decided/verified `2026-11-02`.

The remaining step-6 surface is covered without changing the raw named
interceptor behavior:

- **Explicit query APIs use JS wrappers.** When tracing starts, the runtime
  wraps `Object.prototype.hasOwnProperty`, `Object.hasOwn`, `Reflect.has`,
  `Object.getOwnPropertyDescriptor`, and `Reflect.getOwnPropertyDescriptor`.
  They record `has` / `get_own_property_descriptor` only for HostObject targets,
  preserve the original receiver/arguments/results/throws, and are reinstalled
  after `load()`. A cached pre-trace reference, the `in` operator, and
  non-HostObject targets are documented blind spots. The named descriptor
  callback remains uninstalled because V8 invokes it for internal property
  bookkeeping (the lesson from step 6c).
- **Reflect watch hits gain target metadata.** `Reflect.get` / `has` / `ownKeys`
  hits carry `target_type` / `target_label`; unknown targets are marked
  explicitly. This is metadata only and does not merge the independent channels.
- **Native summaries share one implementation.** Primitive/type-only
  value/argument/return summaries now cover traced native BOM callbacks and
  getters as well as the generic HostObject path; no user `toString`/`valueOf`
  or property get runs.
- **Exception metadata is synchronous and safe.** A temporary stack `TryCatch`
  is installed only for retained events. On a throw, V8 message/stack metadata
  fills `exception_name`, `exception_message`, `exception_location`, and
  `exception_stack`, then the original exception is rethrown. User-defined
  `stack` properties are never read. Asynchronous exception metadata, general
  call-site locations, and timing remain out of scope.
- **Verification.** First full batch `8f8dc23` failed one query test because the
  `Reflect.getOwnPropertyDescriptor` wrapper lacked its completion scope. The
  fixed tree `bd148c2` (cleanly re-committed from the release-tested `72c00ed`)
  passed Linux `41406392044`, Windows `41406401506`, and macOS arm64
  `41406410844`, each reporting `1714 passed, 2 skipped` (1716 items).

See `docs/host_access_trace.md`.

### 42. Host access trace usage report/workflow (step 7)

Decided/verified `2026-11-02`.

Step 7 is implemented entirely in the Python facade: no new native event type,
JS-visible global, or top-level export. `Page.access_trace_report(clear=False,
progress=None)` builds an aggregated view from `read_access_trace`:

- Access groups are keyed by object type/label/object ID/member/operation and
  retain first/last `seq`, count, first resource/source, optional resolution,
  and outcome counts. Missing-property candidates are grouped separately.
- `trace` and `coverage` preserve capacity/filter/generation/coverage
  boundaries. Structured warnings report capacity overflow, active object-type
  filters, a stopped/never-started trace, and an empty snapshot.
- The default is non-destructive (`clear=False`); `clear=True` snapshots then
  clears the raw log. `progress` is a caller-supplied dict copied verbatim for
  event-loop budget/completion context, not interpreted by the runtime.
- Raw events remain the source of truth and are available through
  `read_access_trace`; the report is a dynamic-run view, not a static
  dependency list.

The boundary/report shape and recommended workflow are frozen in
`docs/host_access_trace_workflow.md`. Tests:
`test/test_access_trace_report.py` (6 items). The first attempt (`3fcc156`)
failed one stopped-report warning expectation because capacity warnings persist
with retained events; `c678ede` (identical tree to the tested `62af2e6`) corrected it. The full
suite then collected 1722 items; Linux `41426804187` and Windows `41426817304`
each reported `1720 passed, 2 skipped`. A macOS arm64 fallback release build
(`41466841016`, macos-14, cached V8 monolith, `macosx_11_0_arm64` wheels) also
reported `1720 passed, 2 skipped`; the official macos-15 runner run was
cancelled while queued and produced no test failure.

The next work item became `M11-46` on the object-oriented common-API track
(entry 43); the selector follow-up remains unassigned.

### 43. Document.cookie — first Document slice of the common-API track

Decided/verified `2026-11-02`.

The project direction changed from filling isolated BOM/DOM gaps to completing
commonly used browser objects one at a time. `Document` is the first object, and
`M11-46` is its first slice: `document.cookie`.

- **Numbering.** `M11-46` had been an unassigned placeholder ("the selector
  follow-up or another gap item"). It is now assigned to this slice. Selector
  pseudo-classes were never numbered and remain a deferred, unassigned
  follow-up. This avoids renumbering and records the choice explicitly.
- **Storage.** The jar is owned by `PageState`, not `DocumentHost`, so entries
  survive `load()` generations and are isolated per `Page`. It is never persisted
  or shared across processes.
- **Determinism.** There is no wall clock. `max-age <= 0` deletes; `expires` is
  parsed as an HTTP date and deletes only when at or before the Unix epoch.
  Positive/later expiries never fire during a Page's lifetime. This keeps the
  default environment deterministic.
- **Parsing.** Only the first `name=value` pair is processed; ASCII OWS is
  trimmed; names update in place and new names append; malformed inputs are
  ignored. Attributes are case-insensitive and `path` / `domain` / `secure` /
  `SameSite` are accepted but ignored. Non-string assignments use ordinary
  `ToString` coercion, and conversion exceptions propagate.
- **Descriptor.** The property uses the standard writable HostObject native data
  property path. `Object.getOwnPropertyDescriptor` materializes a data descriptor
  (`value`, `writable: true`, `enumerable: true`, `configurable: false`) and
  evaluates the getter; this matches other writable HostObject properties.
- **Trace.** Reads/writes use the generic getter/setter path, so the access
  trace records `document` / `cookie` / `get|set` with ordinary completion
  metadata. No new trace event type or Python/JS API was added.

The first verification attempt (`f92887c`) failed only one new descriptor test
that assumed `get`/`set` accessors; `18f1f4b` corrected the test to the
framework's descriptor shape without runtime changes. The full suite collects
1727 items. Linux `41479330002`, Windows `41479338268`, and macOS arm64 fallback
`41482460847` each reported `1725 passed, 2 skipped` on tree `18f1f4b`; the five
new tests ran in every lane. See `docs/m11_46_document_cookie.md` and
`docs/m11_46_report.md`.

### 44. Document identity — title and URL/view aliases

Decided/verified `2026-11-02`.

The second Document slice of the common-API track adds the remaining commonly
used identity members without changing architecture:

- **Writable title.** `document.title` uses the ordinary writable HostObject
  path. Assignment coerces with `ToString` (conversion errors propagate
  unchanged), updates the document string, and reflects the value into the
  first parsed `<title>` element's aggregate `textContent`. Element children
  are detached like `textContent = ...`; a missing `<title>` is never created.
  A new `Page.load()` rebuilds the title from the new HTML.
- **URL aliases.** `document.documentURI` and `baseURI` return the current
  generation base URL. The runtime has no `<base href>` resolution, so both
  equal `document.URL`.
- **Single-context view/location.** `document.defaultView` is the context
  global (identical to `window` / `globalThis`). `document.location` returns
  the same `LocationHost` wrapper installed as the global `location`; it is
  read-only on the Document and adds no navigation.
- **Descriptors and trace.** The four aliases use `ReadOnly | DontDelete`
  (strict writes throw `TypeError`); `title` is a writable native data property
  whose descriptor materializes the value. All five members use the generic
  HostObject path and appear in the access trace as `document` / member /
  `get`, with `title` writes recorded as `set` plus completion metadata.
- **Non-goals.** `lastModified`, `<base href>`, title-element creation, entity
  decoding, `document.location` navigation, and multi-window/null `defaultView`
  behavior.

The full suite collects 1733 items. Linux `41483937440`, Windows `41483943983`,
and macOS arm64 fallback `41485210986` each reported `1731 passed, 2 skipped` on
tree `cc1c47d`; the six new tests ran in every lane and all lanes passed on the
first attempt. See `docs/m11_47_document_identity.md` and
`docs/m11_47_report.md`.

### 45. Text-node architecture — M11-48 node foundation

Decided/verified `2026-11-03`.

The user approved replacing the element-only DOM boundary with a browser-style
node model before continuing the common-API track. This supersedes the M2-7
stored `textContent` aggregate and the M11-5 "characterData is inert" decision.

- **Node model.** `DomNode` gains a `NodeKind` (Element 1 / Text 3 / Comment 8 /
  DocumentFragment 11) and character `data`. The parser emits Text nodes for
  all character runs (including whitespace and top-level text) and Comment
  nodes; adjacent text merges. `script`/`style` are raw text and
  `title`/`textarea` are RCDATA. Entity decoding and full HTML5 parsing remain
  out of scope.
- **Live text.** `textContent` is computed from Text descendants in tree order
  (comments excluded) and assignment replaces children with zero or one Text
  node. The stored aggregate and source ranges are removed.
- **Hosts and wrappers.** `NodeHost` is the shared base; `ElementHost` is
  refit, and `CharacterDataHost` (Text/Comment) plus `DocumentFragmentHost` are
  added. The host cache/wrapper path dispatches by node kind; DOM argument
  helpers accept any node. Text/Comment expose nodeValue/data/length and raw
  navigation but not EventTarget or tree-editing methods. Fragment insertion
  moves children and empties the fragment.
- **Navigation/selectors.** `childNodes` includes all kinds; `children`,
  element child/sibling variants, document collections, and selector results
  stay element-only. `+`/`~` skip Text/Comment siblings.
- **Observer.** `characterData`/`characterDataOldValue` are active for
  Text/Comment targets and subtree/document observation; childList arrays wrap
  nodes by kind.
- **Trace.** New object types `text`, `comment`, `document_fragment` are valid
  filters and coverage entries.
- **Deviations.** Document is not a Node (`document.childNodes` absent);
  Text/Comment/Fragment event methods and CharacterData helpers are deferred;
  fragment mutation-record ordering is the documented runtime contract.
- **Title/textarea.** `document.title` uses a real title Text child and keeps
  the M11-47 no-title-element extension; textarea seeds and option text/value/
  label fallbacks use live text.

The full suite collects 1746 items. Linux `41562778133`, Windows `41562781320`,
and macOS arm64 fallback `41563750355` each reported `1744 passed, 2 skipped` on
tree `3f4940f`; the 13 new tests ran in every lane. The first attempt failed
only two new-test expectations (absent `document.contains`, int/str comparison)
and was corrected without runtime changes. See
`docs/m11_48_node_foundation.md` and `docs/m11_48_report.md`.

The next planned slice is M11-49: HTML string APIs (`innerHTML` / `outerHTML` /
`insertAdjacentHTML`) and variadic string tree methods, enabled by this
foundation.

### 46. HTML string APIs and variadic tree methods — M11-49

Decided/verified `2026-11-03`.

The common-API track added the HTML-string and variadic DOM manipulation
surface on top of the M11-48 node foundation.

- **innerHTML/outerHTML.** Writable Element properties. Fragment insertion uses
  the existing parser with scripts kept inert and parsed controls initialized
  (values, select normalization, reset baselines). outerHTML on a parentless
  element is a no-op. No document-level innerHTML/outerHTML or `<template>`
  content model.
- **Serialization.** Deterministic: lowercase tags, id/class first, remaining
  attributes ASCII-sorted, double-quoted escaped values, void elements without
  end tags, comments as `<!--data-->`, script/style text raw, other text escaped.
- **Entities.** Bounded decoding (`amp`, `lt`, `gt`, `quot`, `apos`, `nbsp`,
  decimal/hex numeric) in normal text, RCDATA, and attributes; unknown names
  stay literal and invalid numeric references become U+FFFD. The full HTML
  named-entity table and full HTML5 parsing remain out of scope.
- **Adjacent APIs.** `insertAdjacentHTML`/`Element`/`Text` with four positions;
  invalid position → SyntaxError; missing required parent → TypeError;
  `insertAdjacentElement` accepts Element only.
- **Variadic methods.** `append`/`prepend`/`replaceChildren` on Element and
  DocumentFragment; `before`/`after`/`replaceWith` on Element/Text/Comment.
  Node arguments preserve identity; other values are stringified to Text nodes;
  fragment arguments move their children. Adjacent runtime Text nodes are not
  auto-merged (documented deviation).
- **Mutation records.** Replace-all operations queue one childList record with
  removed+added; append/prepend/before/after queue one addition record;
  replaceWith/outerHTML queue removal + addition.

The full suite collects 1758 items. Linux `41577570846`, Windows `41577574466`,
and macOS arm64 fallback `41578427318` each reported `1756 passed, 2 skipped` on
tree `52bafab`; the 12 new tests ran in every lane. The first attempt
(`e9af2c6`) failed 7 new tests because innerHTML/outerHTML were missing from the
ElementHost setter's early name guard; `8739b1a` fixed the routing. `52bafab`
corrected two test expectations (innerHTML is children-only; Array.join renders
null as empty). Historical tests passed from the first run aside from the
intentional guard updates. See `docs/m11_49_html_strings.md` and
`docs/m11_49_report.md`.

At the user's direction the next slice became M11-50 Window completion (see
§A); `document.write`/`writeln`/`open`/`close` plus `importNode`/`adoptNode`
were deferred behind the Window-first slices that followed (M11-51/M11-52),
then the remaining headless-browser gaps. DOM prototype chains are deferred to
a later stage.

### 47. Incremental Page refactoring with a real-V8 PR check

Approved/implemented locally `2026-11-03`; real-V8 release-verified `2026-11-04`.

Keep the public Page API and browser-compatibility boundaries unchanged. Start
by extracting the V8-independent selector parser into its own translation unit;
leave DOM matching and V8/host-object lifetime ownership in place. This avoids a
large rewrite or introducing abstractions for unapproved features.

Add a Linux x64 / Python 3.13 V8-linked push/PR workflow using the existing
manylinux build helper, pinned image, and monolith cache. Require explicit V8
linking, fail on missing interpreters/wheels, and test outside the checkout.
Full release interpreter/platform coverage remains unchanged. Configuring this
check as merge-required is a separate GitHub setting. The merge policy uses
the GitHub Actions app's `Linux x64 / Python 3.13 / V8` check with up-to-date
branches and administrator enforcement.

20,119 parser comparisons were identical under UBSan; the newly built V8-free
wheel passed `225 passed, 1564 skipped` (focused: `4 passed, 25 skipped`). These
local results are distinct from runtime verification. Source `917cbfb` is now
verified by Linux `41759793220`, Windows `41759812090`, and macOS arm64
`41760140836`; all Python 3.11–3.14 lanes reported `1787 passed, 2 skipped`.
The macOS runner was macos-15 with a pinned source checkout and the release
branch cache; the temporary workflow configuration was restored. See
`docs/page_refactor.md` for source trees, artifacts, and evidence. M11-53 remains
the next Window feature milestone.

### 48. Second Page extraction — deterministic Cookie jar

Approved, implemented, and real-V8 release-verified `2026-11-04`.

Extract Cookie parsing, fixed-epoch expiry, serialization, and ordered storage
into a V8-independent `CookieJar` owned by value at the existing PageState member
location. Keep DocumentHost conversion/tracing and Page-owned lifetime intact.
Preserve the M11-46 contract, including permissive parsing and ignored origin/
path/security attributes; add no future Cookie features. Extend regression
coverage and use differential native validation plus all three real-V8 release
lanes. The original/extracted jars matched after all 30,033 writes under UBSan.
Source `347dfbf` passed Linux `41764895229`, Windows `41764904132`, and macOS
arm64 `41764990337`, Python 3.11–3.14 each `1793 passed, 2 skipped`; macOS
focused Cookie tests each `11 passed`. V8 CI `41764857600` passed. The temporary
macOS driver pinned the tested source and was restored afterward. See
`docs/page_cookie_refactor.md` for trees, artifacts, and verification evidence.
M11-53 stays the next feature slice.

### 49. Third Page extraction — shared minimal URL parser/resolver

Approved, implemented, and real-V8 release-verified `2026-11-04`.

Extract `UrlParts`, decomposition, and relative-reference resolution into a
string-only native module. Preserve M2-3/M8-2 bounded semantics and leave JS
coercion, URLSearchParams snapshots, secure-context policy, and all resource
ownership in the host layer. No WHATWG normalization upgrade or new browser
API. Verify original/extracted behavior differentially and run focused URL/
consumer tests plus the three real-V8 release matrices. Source `2e30d48`
passed Linux `41783823684`, Windows `41783830832`, and macOS arm64
`41783905526`, Python 3.11–3.14 each `1799 passed, 2 skipped`; macOS focused
runs each `76 passed`. V8 CI `41783767829` passed, as did 15,224 native
differential cases under UBSan. The pinned macOS driver was restored afterward.
See `docs/page_url_refactor.md` for trees and artifacts. M11-53 remains next.

### 50. Fourth Page extraction — query byte codec and pair serialization

Approved/implemented `2026-11-04`; real-V8/release verification pending.

Extract only byte-level form-URL encoding/decoding and ordered query pair
parsing/serialization. Leave the canonical V8 private-slot string, JS coercion,
mutation callbacks, validation, and URL snapshot behavior in the host layer.
Keep malformed escapes literal and preserve explicit ASCII encoding, duplicate
order, and empty keys/values. No new ownership or API surface. Use all-byte
native differential checks, focused query/URL tests, and the three real-V8
release matrices. See `docs/page_query_refactor.md`; M11-53 stays next.

## Pending Decisions

These require an explicit human or project-level choice before large work begins.

### A. Which concrete compatibility slice becomes the next numbered milestone?

Settled and implemented so far:

- `M11-1` — the **configurable host-environment baseline**
  (`Page(environment=...)` / `Page.get_default_environment()`, covering the `navigator`
  identity values, `screen`, and the `window` dimensions). See
  `docs/m11_1_host_environment.md`.
- `M11-2` — **`requestAnimationFrame` / `cancelAnimationFrame`** with the manual
  `Page.run_animation_frame()` pump. See `docs/m11_2_animation_frames.md`.
- `M11-3` — **`requestIdleCallback` / `cancelIdleCallback`** with the manual
  `Page.run_idle_callbacks()` pump. `options.timeout` is validated but deliberately
  not implemented. See `docs/m11_3_idle_callbacks.md`.
- `M11-4` — **`queueMicrotask(callback)`** on the existing manual `Page.run_jobs()`
  microtask pump. No new Python API and no fifth pump; a non-callable argument throws a
  JS `TypeError`; callbacks are FIFO and interleave with `Promise` reactions. Landed and
  verified on real V8 (Linux + Windows) on `2026-10-27`: Linux run `40471874328`,
  Windows run `40483069548`, commit `0305b49`, each `1213 passed, 2 skipped`. See
  `docs/m11_4_queue_microtask.md` and `docs/m11_4_report.md`.

- `M11-5` — **`MutationObserver`** — selected on `2026-10-28` from the ranked
  backlog (`docs/m11_compatibility_backlog.md`); boundary document
  `docs/m11_5_mutation_observer.md`. Landed and verified on real V8 (Linux +
  Windows) on `2026-10-28`: Linux run `40632597636`, Windows run `40632601387`,
  commit `4fa6469`, each `1265 passed, 2 skipped`. See `docs/m11_5_report.md`.
- `M11-6` — **Event subclass cluster** (`CustomEvent` / `MouseEvent` /
  `KeyboardEvent` / `FocusEvent`) — selected on `2026-10-28` from rank 2 of the
  backlog; boundary document `docs/m11_6_event_subclasses.md`. Verified in the
  five-slice batch: Linux run `40661574145`, Windows run `40661580891`, commit
  `35c85fe`, each `1352 passed, 2 skipped`. See `docs/m11_6_report.md`.
- `M11-7` — **Web Storage** (`localStorage` / `sessionStorage`, per-Page
  in-memory) — selected on `2026-10-28` from rank 3; boundary document
  `docs/m11_7_web_storage.md`. Verified in the same batch; see
  `docs/m11_7_report.md`.
- `M11-8` — **`Blob` / `File` / `FileReader`** — selected on `2026-10-28` from
  rank 4; boundary document `docs/m11_8_blob_file_reader.md`. Verified in the
  same batch; see `docs/m11_8_report.md`.
- `M11-9` — **Event cancellation** (`cancelable` / `defaultPrevented` /
  `preventDefault` / `stopImmediatePropagation`) — selected on `2026-10-28`
  from the gap inventory; boundary document
  `docs/m11_9_event_cancellation.md`. Verified in the same batch; see
  `docs/m11_9_report.md`.
- `M11-10` — **`classList` / `toggleAttribute`** — selected on `2026-10-28`
  from the gap inventory; boundary document `docs/m11_10_class_list.md`.
  Verified in the same batch; see `docs/m11_10_report.md`.
- `M11-11` — **Event capture phase + listener options** (`capture` / `once` /
  `passive`, `eventPhase`) — selected on `2026-10-28` from the gap inventory;
  boundary document `docs/m11_11_event_capture.md`. Verified on real V8
  (Linux + Windows) on `2026-10-28`: Linux run `40683001239`, Windows run
  `40683010032`, commit `a269aa9`, each `1370 passed, 2 skipped`. See
  `docs/m11_11_report.md`.

`M11-6` through `M11-10` were delivered as one five-slice batch on the
`m11-6-event-subclasses` branch, with one cold V8 build reused by the cached
verification run. `M11-11` followed with the master cache warmed first.

`M11-12` through `M11-17` are a six-slice DOM/web completeness batch selected
on `2026-10-28`: element attribute API, tree-edit conveniences,
`cloneNode`/equality, `URL.searchParams` snapshot, select extension, and nested
environment + `get_defaults()`. Boundary: `docs/m11_12_17_dom_web_batch.md`.
The batch is delivered and verified (Linux `40695004440`, Windows `40695029530`,
commit `8d606e9`, each wheel lane `1378 passed, 2 skipped`). After it lands, the
next slice (`M11-18`) should again be chosen from this backlog rather than
direction alone.

- `M11-18` — **Form-control attribute reflection** — selected on `2026-10-28`
  from the gap inventory (the ranked backlog's remaining entries need separate
  architecture decisions): input `type` / `disabled` / `readOnly` /
  `placeholder`, button `type` / `disabled`, `form.encoding`, and
  `option.label` / `index`. Boundary:
  `docs/m11_18_form_control_reflection.md`. Verified on real V8 (Linux
  `40698705892`, Windows `40698728631`, commit `08350ce`, each wheel lane
  `1386 passed, 2 skipped`); see `docs/m11_18_report.md`.

- `M11-19` through `M11-23` — **DOM/form completeness batch** — selected
  `2026-10-28` from the gap inventory: multi-token class queries and
  element-level `getElementsByClassName`, `document.getElementsByName`,
  extended form reflection, select `length` / `item` / `namedItem`, and
  deterministic document metadata. Boundary:
  `docs/m11_19_23_dom_form_batch.md`. Verified on real V8 (Linux
  `40704330688`, Windows `40704362549`, commit `999c851`, each wheel lane
  `1412 passed, 2 skipped`); see `docs/m11_19_23_report.md`.

- `M11-24` through `M11-28` — **host environment / console batch** — selected
  `2026-10-28`: navigator metadata/identity, screen depth, outer window
  geometry, and console method extensions. Boundary:
  `docs/m11_24_28_host_environment_batch.md`. Verified on real V8 (Linux
  `40711526339`, Windows `40711548915`, commit `a00e140`, each wheel lane
  `1474 passed, 2 skipped`); see `docs/m11_24_28_report.md`.

- `M11-29` — **deterministic event-loop facade** — selected/frozen on
  `2026-10-29`: `tick` / `advance` / `run_until_idle` / `event_loop_time_ms`
  over timers and microtasks. Boundary:
  `docs/m11_29_event_loop_facade.md`. Verified on real V8 (Linux
  `40820152813`, Windows `40820163797`, commit `b211481`, each wheel lane
  `1495 passed, 2 skipped`); see `docs/m11_29_report.md`.

- `M11-30` — **rAF/idle integration into the event-loop facade** — selected and
  frozen on `2026-10-29`: fixed 16ms virtual frames, idle rounds when no
  timer/frame is due, and microtask checkpoints after every task. Boundary:
  `docs/m11_30_frame_idle_facade.md`. Verified on real V8 (Linux `40826630371`,
  Windows `40826634489`, commit `7dde955`, each wheel lane
  `1509 passed, 2 skipped`); see `docs/m11_30_report.md`.

- `M11-31` / `M11-32` — **virtual performance clock + minimal form validation**
  — selected/frozen on `2026-10-29` as one batch. M11-31 adds
  `performance.now()` / `timeOrigin` over the facade clock and rAF timestamps;
  M11-32 adds the bounded constraint-validation surface. Boundary:
  `docs/m11_31_32_perf_validation_batch.md`. Verified on real V8 (Linux
  `40831677507`, Windows `40831682182`, commit `a88b69d`, each wheel lane
  `1540 passed, 2 skipped`); see `docs/m11_31_32_report.md`.

- `M11-33` — **OS-backed Crypto random** — selected/frozen on `2026-10-29`:
  `crypto.getRandomValues()` / `randomUUID()` over the platform CSPRNG, with the
  runtime's single deliberate non-deterministic surface. Boundary:
  `docs/m11_33_crypto_random.md`. Verified on real V8 (Linux `40834145493`,
  Windows `40834153439`, commit `c9b2fb5`, each wheel lane
  `1553 passed, 2 skipped`); see `docs/m11_33_report.md`.

- `M11-34` — **CSS selector engine expansion** — selected/frozen on
  `2026-10-29`: selector lists, compound selectors, type/universal selectors,
  all four combinators, and attribute selectors shared by query/matches/closest.
  Boundary: `docs/m11_34_selector_engine.md`. Verified on real V8 (Linux
  `40855935052`, Windows `40855940659`, commit `49f8582`, each wheel lane
  `1565 passed, 2 skipped`); see `docs/m11_34_report.md`.

- `M11-35` — **BOM metadata completion** — selected/frozen on `2026-10-30`:
  window name/length/geometry/secure-context metadata, screen avail/orientation,
  and navigator touch/PDF/DNT/connection metadata; the environment grows to 36
  keys. Boundary: `docs/m11_35_bom_metadata.md`. Verified on real V8 (Linux
  `40990275470`, Windows `40990279247`, commit `aa23c57`, each wheel lane
  `1599 passed, 2 skipped`); see `docs/m11_35_report.md`.

- `M11-36` through `M11-40` — **BOM completion batch** — selected/frozen on
  `2026-10-30`: deterministic dialogs, performance entry timeline,
  `structuredClone`, single-context window metadata, and
  `scheduler.postTask`. Boundary:
  `docs/m11_36_40_bom_completion.md`. Verified on real V8 (Linux
  `40997424174`, Windows `40997427191`, commit `512117c`, each wheel lane
  `1617 passed, 2 skipped`); see `docs/m11_36_40_report.md`.

- `M11-41` through `M11-45` — **BOM viewport/media/device metadata** —
  selected/frozen on `2026-10-30`: static `visualViewport` + window flags,
  bounded `matchMedia`, deterministic `navigator.storage` /
  `navigator.permissions`, and empty legacy navigator metadata. Boundary:
  `docs/m11_41_45_bom_extras.md`. Verified on real V8 (Linux `41000182750`,
  Windows `41000186396`, commit `61abc3b`, each wheel lane
  `1626 passed, 2 skipped`); see `docs/m11_41_45_report.md`.

- `M11-46` — **`document.cookie`** — selected on `2026-11-02` as the first slice
  of the object-oriented common-API track (Document first): a deterministic
  per-Page in-memory jar with insertion/update ordering, `max-age` / epoch
  `expires` deletion, and access-trace get/set events; no HTTP integration or
  origin/path filtering. Boundary: `docs/m11_46_document_cookie.md`. Verified on
  real V8 (Linux `41479330002`, Windows `41479338268`, macOS arm64 fallback
  `41482460847`, tree `18f1f4b`, each wheel lane `1725 passed, 2 skipped`); see
  `docs/m11_46_report.md`. The selector follow-up was never numbered and
  remains deferred; UA-CH remains deliberately deferred.

- `M11-47` — **Document identity** — selected on `2026-11-02` as the second
  Document slice: writable `document.title` reflected into the first parsed
  `<title>`, plus read-only `documentURI` / `baseURI` (base URL),
  `defaultView` (the single global window), and `location` (the same
  `LocationHost` as the global). Boundary:
  `docs/m11_47_document_identity.md`. Verified on real V8 (Linux `41483937440`,
  Windows `41483943983`, macOS arm64 fallback `41485210986`, tree `cc1c47d`,
  each wheel lane `1731 passed, 2 skipped`); see `docs/m11_47_report.md`.

- `M11-48` — **Node foundation** — selected/approved on `2026-11-03`: first-
  class Text/Comment/DocumentFragment nodes, live `textContent`, node-kind-aware
  navigation/tree editing/selectors, and active `characterData` MutationObserver
  records. Boundary: `docs/m11_48_node_foundation.md`. Verified on real V8
  (Linux `41562778133`, Windows `41562781320`, macOS arm64 fallback
  `41563750355`, tree `3f4940f`, each wheel lane `1744 passed, 2 skipped`); see
  `docs/m11_48_report.md`. M11-49 (HTML string APIs + variadic string tree
  methods) is next.

- `M11-49` — **HTML string APIs and variadic tree methods** — selected/approved
  on `2026-11-03`: writable `innerHTML`/`outerHTML`, `insertAdjacent*`,
  `append`/`prepend`/`before`/`after`/`replaceWith`/`replaceChildren`, bounded
  entity decoding, and deterministic serialization. Boundary:
  `docs/m11_49_html_strings.md`. Verified on real V8 (Linux `41577570846`,
  Windows `41577574466`, macOS arm64 fallback `41578427318`, tree `52bafab`,
  each wheel lane `1756 passed, 2 skipped`); see `docs/m11_49_report.md`.
  At the user's direction, `document.write` + import/adopt was deferred behind
  the Window slice below.

- `M11-50` — **Window scroll / focus / UI completion** — selected on
  `2026-11-03` after the user paused Document work and asked for Window API
  completion. Deterministic, layout-free surface only: read-only
  `scrollX`/`scrollY`/`pageXOffset`/`pageYOffset` (always `0`), no-op
  `scroll`/`scrollTo`/`scrollBy`, `focus`/`blur`, `moveBy`/`moveTo`/
  `resizeBy`/`resizeTo`, and `stop`, writable per-document `window.status`,
  read-only BarProp `{visible: true}` metadata, and the `clientInformation`
  alias of `navigator`. The scroll/stubs deliberately do not introduce a
  virtual scroll position or layout engine. Boundary:
  `docs/m11_50_window_completion.md`. Verified on real V8 (Linux `41597535300`,
  Windows `41597541345`, macOS arm64 fallback `41598354469`, tree `6d2250e`,
  each wheel lane `1766 passed, 2 skipped`); see `docs/m11_50_report.md`.

- **Priority rebalance — Window above Document (`2026-11-03`)** — at the user's
  direction, Window work is now the active common-API track and Document
  continuation is lower priority. Window slices come next; `document.write` /
  import-adopt and the remaining Document gaps move behind them. This reorders
  the roadmap in `docs/next_tasks.md` and `docs/current_status.md` without
  changing any delivered surface.

- `M11-51` — **Window event handler attributes** — selected on `2026-11-03` as
  the first Window slice after the priority rebalance: a bounded set of IDL
  `window.on*` accessors (`onload`, `onerror`, `onmessage`, lifecycle/messaging
  names), install/replace/clear semantics, post-listener invocation at target
  and during bubbling, `return false` cancellation, swallowed handler
  exceptions, and generation-scoped teardown. Boundary:
  `docs/m11_51_window_event_handlers.md`. Verified on real V8 (Linux
  `41612087122`, Windows `41612092772`, macOS arm64 fallback `41613300180`,
  tree `34b119b`, each wheel lane `1777 passed, 2 skipped`); see
  `docs/m11_51_report.md`. The first attempt (`2924367`) used `on<type>` as the
  handler key while dispatch looked up `<type>`; `34b119b` normalized the key.
  Document `write`/import-adopt remain later.

- `M11-52` — **Window messaging** — selected on `2026-11-03` as the next
  Window-first slice: `window.postMessage` with synchronous structured cloning,
  bounded target origins (own origin / `*` / `/`; other values drop),
  asynchronous FIFO delivery through the timer queue, and a `MessageEvent`
  subclass (`data` / `origin` / `lastEventId` / `source` / `ports`). The
  bootstrap captures the original `structuredClone` / `MessageEvent` /
  `setTimeout` and deletes its hidden natives; calls are watchable. No
  iframes, workers, transferables, or cross-window delivery. Boundary:
  `docs/m11_52_window_messaging.md`. Verified on real V8 (Linux `41620346617`,
  Windows `41620356968`, macOS arm64 fallback `41621413284`, tree `bc00040`,
  each wheel lane `1787 passed, 2 skipped`); see `docs/m11_52_report.md`.

A related question this slice raises: the runtime still has **four** independent
legacy manual pumps (`run_timers` / `run_jobs` / `run_animation_frame` /
`run_idle_callbacks`). M11-29 composed timers + microtasks, M11-30 added rAF/idle,
and M11-31 exposes the same virtual clock to JS; the legacy pumps remain valid
but are intentionally not a single coherent timeline with the facade. A **real**
clock, automatic loop, and background execution remain open design questions —
see `docs/m11_3_report.md` "Open questions".

### B. Should `Page` gain a unified public contract document?

Not yet decided. `docs/runtime_surface.md` (added `2026-08-06`) now indexes the `Page`
surface but is explicitly **not** a contract — it freezes nothing. So the choice
stands: keep the milestone-summary approach, or promote a single frozen `Page`
contract alongside `api_contract.md`.

### C. Should Windows become a first-class release target again?

Build and CI support exist in some form, but release priority should be confirmed
before major platform-specific investment.
