# iv8-runtime Runtime Surface Index

**Latest verified open exposure regression repair (`2026-11-07`).** The default
Window open stub is withdrawn. The other Web IDL corrections remain available.
Source `f882344` (tree `c4ba8bee511316e69d1f8f58e865c036a6cec56c`)
passed Linux `42350637889`, Windows `42350641803` and macOS arm64
`42350648456` (macos-15), Python 3.11–3.14 each **2050 passed, 2 skipped,
1 xfailed**. The immutable Chrome probe has 74 matches and one declared
unavailable-open profile gap. macOS focused checks each passed 172 tests with
that one expected failure. Installed cp311 acceptance returned NMPA document
200 and data business code 200 with 10 rows. Customs matched browser-boundary
controls return 200, while complete original-caller consistency remains under
investigation. Scope, causal evidence and release records are in
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

Last reviewed: `2026-11-07` · Latest verified numbered milestone: **M11-52**
(Window messaging — `postMessage` / `MessageEvent`), verified on real V8 (Linux +
Windows + macOS arm64). It follows **M11-51** (Window event handler attributes),
**M11-50** (Window scroll/focus/UI completion), **M11-49** (HTML string APIs),
and **M11-48** (Node foundation — Text/Comment/
DocumentFragment nodes, live `textContent`, active `characterData` records),
**M11-47** (Document identity), **M11-46** (`document.cookie`), and
**M11-41..M11-45** (BOM viewport/media/device metadata). The post-M11 host
access
trace steps 1–4 plus the step-5 BOM-callback, window metadata, nested metadata,
static visualViewport, navigator-storage-manager, navigator-permissions,
matchMedia-result, legacy-navigator, and window-alias groups, plus step-6a
value/argument/return summaries, step-6b method-reference reads, step-6c
delete/enumeration/define operations, step-6d explicit has/descriptor queries,
step-6f Reflect target metadata, step-6g native callback/getter summaries, and
step-6h synchronous exception metadata, plus the step-7 aggregated
`Page.access_trace_report()` workflow, are also verified (Linux + Windows +
macOS arm64, `2026-11-02`); see `docs/host_access_trace.md` and
`docs/host_access_trace_workflow.md`.

## 0. What this document is

Readable monitoring addition, verified (`2026-11-04`):
`Page.read_access_trace(clear=True, *, format="raw")` keeps the default event
dict and accepts `format="text"` for a concise string log. No JS/native capture
changes. Source `1599fd5` passed Linux `41848358166`, Windows `41848373389`, and
macOS arm64 fallback `41850244647`, Python 3.11–3.14 each `1878 passed, 2 skipped`;
macOS focused checks each `79 passed`. Contract/artifacts/restoration are in
`docs/access_trace_text.md`.

Sixth internal extraction, verified (`2026-11-04`): bounded HTML
entity decoding and escaping now live in `html_entities`. DOM parsing/ownership,
raw/RCDATA decisions, serializer traversal, and the public surface are unchanged.
Source `85b2d20` passed Linux `41828654174`, Windows `41828670556`, and macOS
arm64 `41828851448`, Python 3.11–3.14 each `1822 passed, 2 skipped`; focused
macOS runs each `63 passed`. See `docs/page_html_entities_refactor.md` for
source trees, artifacts, and independent evidence.

Fifth internal extraction, verified (`2026-11-04`): Base64 byte
encoding/forgiving decoding is independent of V8 in `base64.h` / `base64.cpp`.
Host conversion, validation, exceptions, and the M8-4 surface are unchanged.
Source `10151d4` passed Linux `41797209223`, Windows `41797219218`, and macOS
arm64 `41797389405`, Python 3.11–3.14 each `1816 passed, 2 skipped`; focused
macOS runs each `32 passed`. See `docs/page_base64_refactor.md` for artifacts
and independent verification.

Fourth internal extraction, verified (`2026-11-04`): byte-level
query parsing/serialization now lives in `query_parameters`. URLSearchParams
still owns canonical text in a GC-managed private slot; JS semantics and
snapshot behavior are unchanged. Source `f017a01` passed Linux `41791203775`,
Windows `41791210361`, and macOS arm64 `41791328195`, Python 3.11–3.14 each
`1806 passed, 2 skipped`; focused macOS runs each `54 passed`. See
`docs/page_query_refactor.md` for source trees, artifacts, and evidence.

Third internal extraction, verified (`2026-11-04`): shared minimal
URL decomposition/resolution now lives in `url_parser.h` / `url_parser.cpp`.
There is no public-surface or normalization change; host-layer URL consumers
keep their existing semantics. Source `2e30d48` passed Linux `41783823684`,
Windows `41783830832`, and macOS arm64 `41783905526`, Python 3.11–3.14 each
`1799 passed, 2 skipped`; macOS focused URL/consumer runs each `76 passed`.
See `docs/page_url_refactor.md` for source trees and artifacts.

Second internal extraction, verified (`2026-11-04`): Cookie parsing,
fixed-epoch expiry, serialization, and storage are in `CookieJar`, owned by
`PageState`. The M11-46 public surface and DocumentHost trace/coercion path are
unchanged. Source `347dfbf` passed Linux `41764895229`, Windows `41764904132`,
and macOS arm64 `41764990337`, Python 3.11–3.14 each `1793 passed, 2 skipped`;
macOS focused Cookie tests each `11 passed`. See `docs/page_cookie_refactor.md`
for source trees, artifacts, and verification details.

Internal maintenance after M11-52 extracts selector parsing into
`selector_parser.h` / `selector_parser.cpp` and adds PR/push V8 CI. Verified on
`2026-11-04`: source `917cbfb`, Linux `41759793220`, Windows `41759812090`,
macOS arm64 `41760140836`, Python 3.11–3.14 each `1787 passed, 2 skipped`.
No public surface changes. See `docs/page_refactor.md` for full evidence and
the real-V8 merge policy. M11-53 remains the next feature milestone.

This is the **single overview page** for the runtime as it actually exists at `HEAD`.
It exists because the repository's oldest formal contract — `docs/api_contract.md` —
covers **only** the M1 `JSContext` kernel, which is a small fraction of the current
surface. Reading that file alone leads to the wrong conclusion that this project
exposes M1 behavior only.

Precedence when documents disagree:

1. the code (`python/iv8/`, `src/core/page_state.cpp`) and the test suite;
2. the milestone summary for the feature in question (§5 below);
3. this index;
4. `docs/api_contract.md` — authoritative for `JSContext` **only**.

This index is a **map, not a contract**. It does not freeze behavior; the milestone
summaries in §5 do that. When a new milestone lands, update §1/§2/§5 and the header
above.

## 1. Layer model

```text
M1 core                     JSContext — isolate/context ownership, eval, conversion,
                            structured errors, disposal, threading

first-layer hosted runtime  Page — host objects, browser-like globals, minimal DOM,
(M2–M8)                     scripts + lifecycle, forms, web-platform globals

second-layer monitoring     Page DevTools/Inspector attach base + opt-in watch
(M9–M10)                    channels
```

The layers stay separated on purpose: process-wide V8 ownership, per-context isolate
ownership, the `Page` hosted runtime, and the monitoring surface are distinct
concerns. See `docs/architecture.md` §4 (M1 core) and `docs/decision_log.md` §5.

## 2. Public Python surface

Everything exported from `iv8`:

```python
from iv8 import (
    JSContext, JSValue, JSUndefined, Page,
    JSError, JSConversionError, JSContextBusyError, JSContextDisposedError,
    __version__,
)
```

Module-level state (no `init` / `shutdown` API): `__version__`, `_v8_version`,
`_v8_commit`, `_v8_linked`, `_v8_runtime_version`.

### 2.1 `JSContext` — M1 kernel

`eval(source, *, to_py=False, name="<eval>")`, `dispose()`, `disposed`, `version`,
context-manager use. Frozen in `docs/api_contract.md`; unchanged by every later
milestone.

### 2.2 `Page` — first-layer runtime

| Member | Milestone | Notes |
|---|---|---|
| `eval(source, *, to_py=False, name="<eval>")` | M2-1 | same semantics as `JSContext.eval` |
| `dispose()` / `disposed` / `__enter__` / `__exit__` | M2-1 | delegates to the owned context |
| `run_timers()` | M2-4 | manual pump; timers never run in the background |
| `run_jobs()` | M2-4, M11-4, M11-5, M11-8 | drains the microtask queue (`Promise` reactions + `queueMicrotask` + MutationObserver + FileReader deliveries) |
| `run_animation_frame()` | M11-2 | manual pump; runs one frame of `requestAnimationFrame` callbacks |
| `run_idle_callbacks()` | M11-3 | manual pump; runs one round of `requestIdleCallback` callbacks |
| `tick()` | M11-29, M11-30 | one virtual-time turn: microtasks, then the earliest due timer, due frame, or one idle round |
| `advance(ms, max_steps=10000)` | M11-29, M11-30 | bounded virtual-time window over timers, frames, idle, and microtasks |
| `run_until_idle(max_steps=1000)` | M11-29, M11-30 | auto-advance through timers/frames/idle until all queues are empty |
| `event_loop_time_ms` | M11-29, M11-30 | read-only per-generation virtual clock advanced by timer/frame due times; reset by `load()` |
| `load(html, base_url, scripts=None, resources=None)` | M2-5, M3-1, M3-5 | static load model — no network, no navigation, no history |
| `ready_state` | M3-2 | Python lifecycle (`"loading"` / `"complete"`), distinct from JS `document.readyState` |
| `Page(environment=None)` | M11-1, M11-17, M11-24..M11-27, M11-35 | optional read-only host-environment baseline; accepts the flat 36-key form or nested `navigator` / `screen` / `window` sections |
| `get_default_environment()` | M11-1 | static; returns a fresh flat `dict` copy of the default template |
| `get_defaults()` | M11-17 | static; returns a fresh nested `{navigator, screen, window}` dict |
| `read_navigation_requests(clear=True)` | browser compatibility repair, verified 2026-11-06 | bounded offline assign/replace/reload intents; requests/dropped_requests/max_requests; never loads a document |
| `capture_async_errors(enabled=True)` / `read_async_errors(clear=True)` | browser compatibility repair, verified 2026-11-06 | opt-in bounded timer/job/Promise/frame/idle/listener diagnostics; errors/dropped_errors/max_errors; scheduling is unchanged |

`resources` (M3-5) was the only new Python parameter M3–M8 added; `environment`
(M11-1) is the next one. `Page` has no `document` / DOM / event API on the Python
side — the DOM is JS-side only.

The environment now accepts 36 flat keys. Navigator: `userAgent` / `platform` /
`language` / `appName` / `appCodeName` / `product` / `vendor` / `vendorSub` /
`productSub` / `doNotTrack` / `connectionEffectiveType` (`str`),
`webdriver` / `onLine` / `cookieEnabled` / `pdfViewerEnabled` /
`connectionSaveData` (`bool`), `languages` (non-empty `list[str]`), and
`hardwareConcurrency` / `maxTouchPoints` / `connectionRtt` (`int`),
`connectionDownlink` (`int` or `float`, stored `float`). Screen and window:
`screenWidth` / `screenHeight` / `availWidth` / `availHeight` /
`screenAvailLeft` / `screenAvailTop` / `screenColorDepth` / `screenPixelDepth` /
`innerWidth` / `innerHeight` / `outerWidth` / `outerHeight` / `screenX` /
`screenY` (`int`) and `devicePixelRatio` (`int` or `float`, stored `float`).
Omitted keys inherit the defaults, and the original values are unchanged.
Validation is strict and runs in the Python facade before any native page exists
(unknown key or wrong type → `TypeError`, nothing partially installed). Values
are re-applied to every `load()` generation. M11-17 additionally accepts the
nested form `{"navigator": {...}, "screen": {...}, "window": {...}}` (flattened
to the same keys; mixing nested and flat keys is rejected) and adds
`Page.get_defaults()` for the nested shape. See
`docs/m11_1_host_environment.md`, `docs/m11_12_17_dom_web_batch.md`,
`docs/m11_24_28_host_environment_batch.md`, and
`docs/m11_35_bom_metadata.md`.

### 2.3 `Page` — second-layer monitoring

All Python-side; none of these add a JS-visible global.

| Member | Milestone | Watches |
|---|---|---|
| `devtools_url()` | M9-1 | starts the per-`Page` CDP discovery + WS endpoint bridged to V8 Inspector |
| `watch_apis(paths, break_on_hit=False)` / `read_watch_api_hits()` | M9-2, M9-3 | host-method calls by dotted `"receiver.method"` path |
| `watch_reflect_get(enabled=True, break_on_hit=False)` / `read_reflect_get_hits()` | M9-4 | `Reflect.get` |
| `watch_reflect_has(...)` / `read_reflect_has_hits()` | M9-5 | `Reflect.has` |
| `watch_reflect_ownkeys(...)` / `read_reflect_ownkeys_hits()` | M10-1 | `Reflect.ownKeys` |
| `watch_object_keys(...)` / `read_object_keys_hits()` | M10-2 | `Object.keys` |
| `access_trace_report(*, clear=False, progress=None)` | step 7 (verified 2026-11-02) | pure-Python aggregated dynamic-run report: access groups with first/last seq and source, missing candidates, operation/object-type totals, coverage/capacity/filter/stopped warnings, and caller-supplied progress; see `docs/host_access_trace_workflow.md` |
| `start_access_trace(max_events=10000, object_types=None)` / `stop_access_trace()` / `read_access_trace(clear=True, *, format="raw")` | host access trace, steps 1–4 + the complete step-5 object groups + steps 6a–6h (verified 2026-11-02) | native HostObject property `get`/`set`, method `call`, missing string-key `get_probe`, `get_method` references with adjacent-call folding, `delete`/`own_keys`/`define_property` request events, explicit `has`/`get_own_property_descriptor` query events for HostObject targets, independent performance/crypto/Storage/permissions/matchMedia/window callback and getter entries, window metadata/alias accessors, nested connection/orientation/timeOrigin reads, visualViewport metadata/handler/method accesses, navigator.storage manager calls, navigator.permissions query/status accesses, matchMedia result reads/method calls, legacy navigator plugins/mimeTypes/javaEnabled accesses, generic/native get/set/call and query completion summaries, and synchronous exception name/message/location/stack; structured events with generation, object identity, resource, and source kind. Optional `format="text"` renders concise paths and captured values (see `docs/access_trace_text.md`); raw is the default. The separate step-7 report aggregates this log; see `docs/host_access_trace.md` and `docs/host_access_trace_workflow.md` |

Shared semantics across the five watch channels:

- each `read_*` is **read-and-clear** and returns `list[dict]`; the five logs are
  independent and never cross-talk;
- a hit carries `resource_name` (the executing script's name, or `""`) and `type`;
  `watch_apis` hits also carry `path`, and the `Reflect.get`/`Reflect.has` hits carry
  `property`. `Reflect.ownKeys` / `Object.keys` take no key, so their hits have **no**
  `property` field. The three Reflect channels also carry `target_type` /
  `target_label`; unrecognized targets are `"unknown"` with an empty label;
- the wrapper records and then delegates to the real method — observable behavior is
  unchanged;
- `break_on_hit=True` pauses the Inspector **only when a DevTools session is
  attached**; with none attached it records only — no pause, no error;
- configuration is `Page`-level and **persists across `load()`**; the hooks are
  re-installed per generation. Never enabling any of it leaves runtime paths at zero
  overhead.

The **host access trace** is a separate, later channel, not part of the five
watch channels above: it is default-off, has no `break_on_hit`, records every
entered generic `HostObject` getter/setter/method trampoline (subject to an
optional object-type filter), records `get_probe`/`resolution: "missing"` for
absent HostObject string keys, records performance/crypto/Storage native callback
calls plus Storage `length` reads, records the traced window metadata accessors
(`innerWidth` / geometry / flags / `window.name`), nested
connection/orientation/timeOrigin reads, static visualViewport
metadata/handler/method accesses, navigator.storage parent reads plus manager
method calls, navigator.permissions query calls plus PermissionStatus name/state/method
accesses, matchMedia result calls/gets/method calls, legacy navigator metadata,
window parent/top/frames/opener get/set events, generic/native get/set/call
and query completion metadata (returned/threw outcome plus primitive
value/argument/return summaries and synchronous exception
name/message/location/stack), `get_method` references (adjacent calls fold into
`call`), step-6c `delete`/`own_keys`/`define_property` request events
(enumeration events have an empty member; results and descriptors are
unchanged), and step-6d explicit `has`/`get_own_property_descriptor` events for
HostObject targets. It returns events plus a coverage report. It does not cover
MediaQueryList/PermissionStatus `onchange`/missing probes, array elements,
global binding reads, other plain BOM property reads, returned plain values,
user-created objects, the `in` operator, non-HostObject query targets, query
wrappers cached before tracing, asynchronous exception metadata, or missing
probes on non-HostObject objects in this slice. See
`docs/host_access_trace.md`.

Not public surface: `_core.Page.devtools_pause_count()` is an internal test probe for
pause evidence (M9-3).

## 3. JS-side surface (reachable only through `Page.eval`)

Installed per generation by `install_page`:

- **browser compatibility repair** (verified 2026-11-06) — reflected meta/script
  inputs, anchor/area URL components, shared IDL accessors with receiver checks,
  stable node identity, bounded trailing-body normalization, live query collections,
  document.all, inline CSSStyleDeclaration, unavailable Canvas contexts, and offline
  navigation/error diagnostics. Exact overrides to historical milestone contracts
  and remaining limitations: `docs/browser_compatibility_repair.md`;
- **global roots** — `window` / `globalThis` / `self` (all the same object);
- `console` (`log` / `info` / `warn` / `error` plus M11-28 `debug` / `trace` /
  `table` / `dir` / `group` / `groupEnd` / `time` / `timeEnd` / `assert` /
  `count` → Python `logging`, logger `iv8.console`; no clock, stack, formatting,
  or nesting);
- `navigator` (`userAgent` / `platform` / `language` / `webdriver` plus M11-24
  `languages` / `onLine` / `cookieEnabled` / `hardwareConcurrency`, M11-27
  `appName` / `appCodeName` / `product` / `vendor` / `vendorSub` /
  `productSub`, and M11-35 `maxTouchPoints` / `pdfViewerEnabled` / `doNotTrack` /
  `connection`) and `location` (`href` / `origin` / `protocol` / `host` /
  `hostname` / `port` / `pathname` / `search` / `hash` / `toString()`) — metadata remains read-only;
  `navigator.connection` is a fresh read-only `{effectiveType, rtt, downlink,
  saveData}` object with no events; `location` still derives from the base URL
  with assign/replace/reload recording offline intents. The compatibility repair
  also adds prototype webdriver, sendBeacon refusal, a secure-context deterministic
  getBattery Promise and bounded webkitPersistentStorage callbacks; see its contract;
- `screen` (`width` / `height` / `availWidth` / `availHeight` plus M11-25
  `colorDepth` / `pixelDepth` and M11-35 `availLeft` / `availTop` / `orientation`)
  and the `window` dimensions `devicePixelRatio` / `innerWidth` / `innerHeight`
  plus M11-26 `outerWidth` / `outerHeight` / `screenX` / `screenY` — read-only,
  configured via `Page(environment=...)`; M11-35 adds mutable `window.name`,
  read-only `window.length` (0), `screenLeft` / `screenTop`, and
  `isSecureContext` derived from the base URL. `screen.orientation` is a
  read-only `{type, angle}` derived from width/height with no events or lock;
  these dimensions feed no layout engine (see the M11-50 scroll stubs below).
  M11-39 adds
  the single-context browsing metadata: replaceable `window.parent` / `frames`,
  unforgeable read-only `window.top`, and writable `window.opener` (initially
  `null`), with no child frames or popups (same-window `postMessage` arrives in
  M11-52 below);
- **visual viewport + window flags** (M11-41) — read-only
  `visualViewport` (`width` / `height` from `innerWidth` / `innerHeight`,
  zero offsets, `scale=1`) with no-op listeners and no scroll/zoom/resize
  events; the access-trace step 5d now backs it with a HostObject (same values
  and read/write behavior, documented prototype/descriptor differences); see
  `docs/host_access_trace.md`. Read-only `window.origin`,
  `crossOriginIsolated` (`false`), `closed` (`false`), and `frameElement`
  (`null`). The window `parent`/`top`/`frames`/`opener` aliases are traced
  accessors under step 5i with unchanged replaceable/read-only behavior;
- **window scroll / focus / UI completion** (M11-50) — read-only, access-traced
  `scrollX` / `scrollY` (always `0`) and `pageXOffset` / `pageYOffset`
  aliases; no-op `scroll` / `scrollTo` / `scrollBy`, `focus` / `blur` (with
  `document.hasFocus()` still `false`), `moveBy` / `moveTo` / `resizeBy` /
  `resizeTo`, and `stop`; writable `status` (ToString, reset per `load()`);
  read-only BarProp metadata (`locationbar` / `menubar` / `personalbar` /
  `scrollbars` / `statusbar` / `toolbar`, each `{visible: true}`); and a
  read-only `clientInformation` alias of `navigator`. All ten methods are
  watchable through `watch_apis`. No layout, scroll events, focus machinery,
  navigation, or OS window management;
- **window messaging** (M11-52) — `window.postMessage(message, targetOrigin?,
  transfer?)` for the single window. The message is structured-cloned at send
  time; target origins are bounded to `*` / `/` / the page origin (other values
  drop); delivery is an asynchronous FIFO timer task observed by
  `Page.run_timers()` or a facade turn, not `run_jobs()`. `MessageEvent`
  extends `Event` with `data` / `origin` / `lastEventId` / `source` / `ports`;
  `source` is the same window and `ports` is empty. No iframes, workers,
  transferables, cross-window delivery, or `MessagePort`. Calls are watchable;
- **matchMedia** (M11-42) — a static MediaQueryList-shaped result for a bounded
  query language (media types, comma lists, `not` / `only` / `and`,
  width/height, orientation, and fixed desktop capabilities). Listener methods
  are no-ops; there are no dynamic viewport changes or `change` events. The
  access-trace step 5g backs returned lists with cached HostObjects while
  keeping values, query evaluation, `onchange`, and non-native
  `matchMedia.toString()` behavior unchanged;
- **dialogs** (M11-36) — deterministic no-op `alert()` / `confirm()` /
  `prompt()` / `print()` globals returning `undefined` / `false` / `null` /
  `undefined`; no UI, blocking, Python callback, or page mutation. Calls are
  watchable;
- **timers** — `setTimeout` / `clearTimeout` / `setInterval` / `clearInterval`
  (manual-pump only);
- **virtual performance clock** (M11-31) — `performance.now()` reports the
  M11-29/M11-30 facade clock (`Page.event_loop_time_ms`), and
  `performance.timeOrigin` is the virtual epoch `0`; both are read-only. There
  is no real clock; `Date` remains V8's wall clock;
- **performance timeline** (M11-37) — `performance.mark()` / `measure()` /
  `getEntries()` / `getEntriesByName()` / `getEntriesByType()` /
  `clearMarks()` / `clearMeasures()` return plain read-only entries over virtual
  timestamps; entries reset per `load()`. No `PerformanceObserver`, resource /
  navigation timing, or `performance.timeline`;
- **Crypto random** (M11-33) — read-only `crypto.getRandomValues(typedArray)`
  (integer TypedArrays, same-view return, view byte range, 65536-byte quota) and
  `crypto.randomUUID()` (v4). Bytes come from the platform CSPRNG and are the
  runtime's single deliberate non-deterministic surface. `crypto.subtle`,
  `CryptoKey`, digest, AES, RSA, ECDH/ECDSA, HMAC, and KDFs are absent;
- **structuredClone** (M11-38) — cycle/identity-preserving clones of
  primitives (including BigInt), plain objects, arrays, boxed
  Boolean/Number/String/BigInt objects, `Date`, `Map`, `Set`, `ArrayBuffer`,
  TypedArrays, and `DataView`; views keep their byte offset and share the
  cloned backing buffer. Functions, symbols, promises, weak collections,
  RegExp/Error objects, shared buffers, proxies, DOM/host objects, and the
  global object throw a JS `TypeError`; transfer lists are absent;
- **scheduler.postTask** (M11-40) — Promise-based `scheduler.postTask()`
  scheduled on the existing timer queue, supporting `options.delay`; tasks run
  only under the explicit timer/facade pumps. Priority queues, `AbortSignal`,
  and automatic execution are absent;
- **navigator storage/permissions** (M11-43, M11-44) — read-only
  `navigator.storage` with Promise-based `estimate()` (`usage: 0`,
  `quota: 10737418240`), `persisted()` / `persist()` (`false`), and
  `navigator.permissions.query({name})` returning a static
  `state: "prompt"` status for a bounded name set. No quota accounting,
  persistence, prompt UI, device access, or change events. The access-trace
  step 5e now backs the storage methods with native callbacks while keeping the
  plain manager object, its identity, and its descriptors unchanged; step 5f
  does the same for `navigator.permissions.query()` and traces returned
  PermissionStatus name/state/method accesses without changing their shape;
- **legacy navigator metadata** (M11-45) — frozen empty `navigator.plugins` /
  `mimeTypes` arrays and `navigator.javaEnabled()` returning `false`; no
  plugin enumeration or Java applet support. The access-trace step 5h traces
  the navigator property reads and the javaEnabled call while preserving array
  identity, frozen state, and read-only descriptors;
- **microtasks** — `queueMicrotask(callback)` (M11-4), drained only by
  `Page.run_jobs()` (the same queue `Promise` reactions use). A non-callable
  argument throws a JS `TypeError`; callbacks run FIFO, may enqueue further
  microtasks in the same checkpoint, and a throwing callback is swallowed;
- **MutationObserver** (M11-5) — `new MutationObserver(callback)` with
  `observe(target, options)` / `disconnect()` / `takeRecords()`. Records cover
  `childList` + `attributes` (with `subtree` / `attributeOldValue` /
  `attributeFilter`), are queued in mutation order, and are delivered only by
  `Page.run_jobs()`; element, Text/Comment, DocumentFragment, and document
  targets are accepted. M11-48 activates `characterData` /
  `characterDataOldValue`; records are plain objects (no `MutationRecord`
  global); observer state is per generation;
- **event subclasses** (M11-6) — `CustomEvent` (`detail`), `MouseEvent`
  (`clientX` / `clientY` / `button` / `buttons`), `KeyboardEvent` (`key` /
  `code` / `repeat`), and `FocusEvent` (`relatedTarget`), each inheriting
  `Event.prototype` and using the existing dispatch/bubbling model;
- **event cancellation** (M11-9) — `cancelable` / `defaultPrevented`,
  `preventDefault()`, and `stopImmediatePropagation()`; `dispatchEvent` returns
  `false` for a canceled cancelable event;
- **event capture + listener options** (M11-11) — the capturing phase
  (window → document → ancestors), `eventPhase` (0/1/2/3), and
  `addEventListener` `capture` / `once` / `passive` options. Capture runs even
  for non-bubbling events; `once` removes before the call and `passive`
  suppresses `preventDefault` for that invocation. Default actions, `on*`
  handlers, and `signal` options are still absent;
- **Web Storage** (M11-7) — `localStorage` / `sessionStorage` as independent,
  insertion-ordered, per-Page in-memory stores (`getItem` / `setItem` /
  `removeItem` / `clear` / `key` / read-only `length`); entries survive
  `Page.load()` of one Page but there is no disk persistence, origin keying,
  `storage` events, or cross-Page sharing;
- **Blob / File / FileReader** (M11-8) — immutable `Blob` / `File` byte value
  objects (`size` / `type` / `name` / `lastModified`, `slice`, Promise-returning
  `text` / `arrayBuffer`) and a minimal `FileReader` whose
  `readAsText` / `readAsArrayBuffer` complete on `Page.run_jobs()` with
  `loadstart` / `load` / `loadend` / `abort` listener events; no network,
  filesystem, object-URL, progress, or data-URL surface;
- **classList / toggleAttribute** (M11-10) — `element.classList`
  (`contains` / `add` / `remove` / `toggle` / `replace` / `item` / `length` /
  `value`, a fresh plain object per access) and
  `element.toggleAttribute(name, force?)`, both feeding the MutationObserver
  attribute hooks;
- **attribute API** (M11-12) — writable `id` / `className` property setters
  (they update the dedicated fields and queue attribute records like
  `setAttribute`), `hasAttributes()`, and `getAttributeNames()` (id/class first,
  then the remaining raw names ASCII-sorted);
- **tree editing + node navigation** (M11-13, extended M11-48/M11-49) —
  `replaceChild()`, `remove()`, `hasChildNodes()`, and raw `firstChild` /
  `lastChild` / `previousSibling` / `nextSibling` aliases. M11-48 makes the raw
  aliases span Element/Text/Comment nodes and adds `childNodes` (all kinds) vs
  `children` (elements); DocumentFragment insertion moves the fragment's
  children and empties it. M11-49 adds `append` / `prepend` / `replaceChildren`
  (Element + Fragment), `before` / `after` / `replaceWith` (Element/Text/
  Comment), node-or-string arguments, and pinned mutation records;
- **HTML strings** (M11-49) — writable Element `innerHTML` / `outerHTML`
  (`outerHTML` no-op on a parentless element), `insertAdjacentHTML` /
  `insertAdjacentElement` / `insertAdjacentText`, deterministic serialization
  (id/class first, remaining attributes sorted, escaped values, void elements,
  comments, raw script/style text), and bounded entity decoding in text/RCDATA/
  attributes. Fragment-inserted scripts stay inert; parsed controls are
  initialized; adjacent runtime Text nodes are not auto-merged;
- **node creation** (M11-48) — `document.createTextNode(data)` /
  `document.createComment(data)` return CharacterData nodes (`nodeType`
  3/8, `nodeName` `#text`/`#comment`, writable `data`/`nodeValue`/`textContent`,
  UTF-16 `length`, empty `childNodes`, raw sibling navigation);
  `document.createDocumentFragment()` returns a detached container with
  `childNodes` / `children` / first/last members, live `textContent`, tree
  editing, and `querySelector` / `querySelectorAll` / `getElementById`.
  `textContent` is live over Text descendants (comments excluded); assignment
  replaces children with zero or one Text node. Text/Comment/Fragment do not
  expose EventTarget methods in this slice;
- **clone / equality** (M11-14) — detached `cloneNode(deep?)`, identity
  `isSameNode()`, and structural `isEqualNode()`;
- **URL.searchParams** (M11-15) — a `URLSearchParams` snapshot of the URL's
  `search`; mutating the snapshot does not update the URL. URL fields remain
  read-only;
- **select extension** (M11-16, M11-22) — `select.options` (plain array),
  writable `selectedIndex` (single-select; `-1` clears), read-only
  `multiple` / `size`, read-only `length`, and `item(index)` / `namedItem(name)`
  lookups. No writable `length`, option-collection methods, or multi-select;
- **form-control attribute reflection** (M11-18, M11-21) — input `type` /
  `disabled` / `readOnly` / `placeholder` / `name` / `required`, button
  `type` / `disabled` / `name`, textarea `name` / `required` / `disabled` /
  `readOnly` / `placeholder`, select `name` / `required` / `disabled`,
  `form.encoding` (alias of the runtime `enctype` slot), and `option.label` /
  read-only `index`. Reflected writes queue MutationObserver attribute records;
- **query extensions** (M11-19, M11-20) — multi-token
  `getElementsByClassName` at document and element level (the element form is
  descendants-only), plus `document.getElementsByName`. The compatibility repair
  changes tag/class queries to live HTMLCollection with item/namedItem/iteration;
  getElementsByName retains its historical Array snapshot;
- **selector engine** (M11-34) — document/element `querySelector[All]`,
  `matches()`, and `closest()` share selector lists, compound selectors,
  descendant / child / adjacent-sibling / general-sibling combinators, attribute
  selectors, and `*`. Names are case-insensitive, values case-sensitive; the
  element-scoped queries include the root itself. Pseudo-classes/elements,
  escapes, namespaces, and `i`/`s` flags remain unsupported; pseudo-classes are
  deferred to a later selector slice;
- **document metadata** (M11-23) — read-only `characterSet` / `contentType` /
  `compatMode` / `referrer` / `domain` / `visibilityState` / `hidden` /
  `activeElement`, plus `hasFocus()` (always `false`). No focus, visibility, or
  navigation machinery;
- **document.cookie** (M11-46) — writable, access-traced property backed by a
  deterministic per-Page in-memory jar: first `name=value` pair per assignment,
  insertion/update ordering, ASCII OWS trimming, case-insensitive attributes,
  `max-age <= 0` / epoch `expires` deletion, and ordinary `ToString` coercion.
  `path` / `domain` / `secure` / `SameSite` are accepted and ignored. The jar
  survives `load()` and is isolated per `Page`; there is no HTTP integration,
  origin/path filtering, wall-clock expiry, persistence, or cookie store;
- **document identity** (M11-47) — writable `document.title` (coerced with
  `ToString`; reflected into the first parsed `<title>` element's
  `textContent`; no title-element creation, reset by `load()`), plus read-only
  `documentURI` / `baseURI` (the current base URL; no `<base href>`),
  `defaultView` (`=== window` / `globalThis`, single context), and `location`
  (the same `LocationHost` as the global `location`; no navigation). All are
  access-traced; strict assignment to the read-only aliases throws;
- **animation frames** — `requestAnimationFrame` / `cancelAnimationFrame` (M11-2,
  manual-pump only via `Page.run_animation_frame()`, and driven by the M11-30
  facade at fixed 16ms virtual frames). A separate FIFO queue from the timers,
  with its own handle space; M11-31 passes the current virtual timestamp as the
  callback's only argument, and a re-request lands in the next frame. No real
  clock;
- **idle callbacks** — `requestIdleCallback` / `cancelIdleCallback` (M11-3,
  manual-pump only via `Page.run_idle_callbacks()`, and driven by the M11-30
  facade when no timer/frame is due). Another separate FIFO queue with its own
  handle space; a re-request lands in the next round. The callback receives a
  minimal `IdleDeadline`-style object (`didTimeout` always `false`,
  `timeRemaining()` a fixed 50ms) — deterministic, since there is no real clock.
  `options.timeout` is type-validated but has **no** firing semantics;
- **events** — the `Event` constructor plus `addEventListener` /
  `removeEventListener` / `dispatchEvent` on `window`, `document`, and
  elements; M11-51 adds bounded Window IDL event handler attributes
  (`window.onload` / `onerror` / `onmessage` / lifecycle and messaging `on*`
  names) with function-or-null accessors, post-listener invocation at target
  and in bubbling, `return false` cancellation, and per-generation reset.
  `document.on*` / element `on*` attributes, automatic error/unhandled
  rejection events, the legacy `onerror` signature, and unlisted `on*` names
  remain absent;
- **`document`** — `URL` / `title` / `readyState` / `documentElement` / `head` /
  `body` / `currentScript` / `scripts` / `children` / `forms` / `images` / `links` /
  `anchors` / `embeds` / `applets`, the M11-23 metadata (`characterSet` /
  `contentType` / `compatMode` / `referrer` / `domain` / `visibilityState` /
  `hidden` / `activeElement` / `hasFocus()`), `getElementById` /
  `querySelector` / `querySelectorAll` / `getElementsByTagName` /
  `getElementsByClassName` / `getElementsByName` / `createElement`, and the
  M11-48 `createTextNode` / `createComment` / `createDocumentFragment`, plus the
  compatibility repair's appendChild/removeChild/insertBefore/replaceChild and
  V8-undetectable live document.all;
  `document.children` stays element-only. The Web IDL repair adds Document's
  Node identity, childNodes and Node operations, with the document as the parent
  of its root nodes. Release verification is recorded above.
- **elements** — the minimal node surface (`tagName` / `nodeName` / `nodeType` /
  `nodeValue` (null) / `id` / `className` / live `textContent` / `parentNode` /
  `childNodes` / `children`), connectivity and structural navigation
  (`ownerDocument` / `isConnected` / `parentElement` / element sibling and
  first/last element-child members / `childElementCount`, plus raw
  `firstChild` / `lastChild` / `previousSibling` / `nextSibling` and
  `hasChildNodes()` since M11-13), attributes (`getAttribute` /
  `hasAttribute` / `setAttribute` / `removeAttribute`, plus writable
  `id` / `className`, `hasAttributes()`, and `getAttributeNames()` since
  M11-12), tree editing (`appendChild` / `removeChild` / `insertBefore`, plus
  `replaceChild` / `remove` since M11-13), `cloneNode` / `isSameNode` /
  `isEqualNode` since M11-14, subtree queries, and `contains` / `matches` /
  `closest`;
- **form surface**, exposed only on the relevant tags — `<form>`: `elements` /
  `reset()` / `submit()` / `requestSubmit()` / `method` / `action` / `enctype` /
  `encoding` (M11-18 alias) / `target` / `noValidate`; controls: `form`,
  `value`, `checked`, `defaultValue`, `defaultChecked`; `<input>`: `type` /
  `disabled` / `readOnly` / `placeholder` / `name` / `required`
  (M11-18/M11-21); `<button>`: `type` / `disabled` / `name` (M11-18/M11-21);
  `<textarea>`: `name` / `required` / `disabled` / `readOnly` / `placeholder`
  (M11-21); `<select>`: `options` / `length` / `selectedIndex` / `multiple` /
  `size` / `item` / `namedItem` / `name` / `required` / `disabled`
  (M11-16/M11-21/M11-22); `<option>`: `value` / `selected` / `text` /
  `defaultSelected` / `label` / `index` (M11-18);
- **form validation** (M11-32) — on each control: `willValidate` (read-only),
  `validity` (fresh read-only flags), `validationMessage`, `checkValidity()` /
  `reportValidity()`, and `setCustomValidity(message)`; on `<form>`:
  `checkValidity()` / `reportValidity()` over `elements`. Supported constraints:
  `required`, `minlength` / `maxlength`, email/URL `typeMismatch`, and custom
  errors. Unsupported flags stay `false`; invalid controls receive a
  target-only, non-bubbling `invalid` event. No native UI, pattern/min/max/step,
  radio-group semantics, or `ValidityState` global;
- **web-platform globals (M8)** — `TextEncoder` / `TextDecoder` (UTF-8 only),
  `URL` (with the non-live `searchParams` snapshot since M11-15),
  `URLSearchParams`, `atob` / `btoa`.

`hostProbe` is M2-1 framework infrastructure, not a stable API.

Deliberate shape of this DOM (M11-48/M11-49): Element, Text, Comment, and
DocumentFragment nodes share one live tree; `textContent` is computed live and
whitespace text nodes are preserved; HTML fragments round-trip through a
deterministic serializer with bounded entity decoding; XML CDATA, doctype nodes,
and general XML parsing remain outside the bounded parser. Document now has
Node identity and the approved standalone native constructors are implemented.
Tag/class queries are live HTMLCollection objects. Other historical
collections retain their Array contracts. Native node wrappers have stable
identity per generation. Selectors match elements only and support the M11-34
subset (lists, compound selectors, combinators, attribute selectors, `*`) but not
pseudo-classes/elements, escapes, namespaces, or `i`/`s` flags, there are no
full attribute-reflection semantics. HTML interface prototypes now provide tag
identity and the implemented form-control and meta/script/anchor attributes.
A `<script>` inserted into the tree stays **inert**.
`form.submit()` / `requestSubmit()` are callable no-ops. Form validation is the
bounded M11-32 subset above, not the full HTML constraint-validation algorithm.
Crypto is the bounded M11-33 random subset; `crypto.subtle` is absent. Consult
the milestone summary before assuming any wider behavior.

## 4. Standing exclusions

Execution exclusions include HTTP/WebSocket transport, automatic resource
loading, committed navigation/history, JavaScript→Python callbacks, a Python-side
DOM or event API (`Page.document`), CDATA/doctype nodes, layout and geometry.
The Web IDL repair provides explicit offline fetch rejection and XHR/Worker
constructor failure. The follow-up repair keeps Window opening unavailable. Live tag/class collections
and offline navigation intents are available. `iv8` also exports no browser symbol at module level —
`test/test_scope_exclusions.py` guards this.

Nuance since M11-1: the page reports configurable `screen` / `window` **dimension
values**, but there is still no layout engine and nothing consumes those numbers
— they are reported values only. M11-42 adds a bounded `matchMedia` evaluator
over those reported dimensions; it is not a CSS/media-query engine. M11-1 also makes the `navigator`
identity values configurable, which is a *deterministic baseline* facility, not
fingerprint simulation or randomization: a page reports exactly what the caller
configured, and there is no UA-CH / canvas / webgl surface to spoof.

Also still excluded: a real clock, background threads, and any automatic event loop.
The M2–M11 queues remain available as explicit pumps — `run_timers()`,
`run_jobs()` (microtasks, including `queueMicrotask` since M11-4,
`MutationObserver` delivery since M11-5, and `FileReader` completion since
M11-8), `run_animation_frame()` (M11-2), and `run_idle_callbacks()` (M11-3).
M11-29 adds a **deterministic, synchronous facade** over timers + microtasks:
`tick()` / `advance()` / `run_until_idle()` advance a virtual clock and drain
microtasks after each task. M11-30 extends the same facade to drive
`requestAnimationFrame` at fixed 16ms virtual frame boundaries and
`requestIdleCallback` one round at a time when no timer/frame is due; every task
is followed by a microtask checkpoint. M11-31 exposes that same virtual clock to
JS as `performance.now()` and passes it to rAF callbacks. It is still only
driven by explicit Python calls: there is no background loop, no **real** clock,
and no vsync. The legacy one-queue pumps remain available and independent.
`IdleDeadline.timeRemaining()` remains a constant and `options.timeout` never
fires. M11-40 adds `scheduler.postTask` as a Promise wrapper over that same
explicit timer queue; it does not add an automatic loop. Crypto random (M11-33)
is the one intentional non-deterministic surface; it does not affect the virtual
clock.

## 5. Milestone map

| Milestone | Theme | Authoritative doc |
|---|---|---|
| M1 | `JSContext` execution kernel | `docs/api_contract.md`, `docs/architecture.md` |
| M2 | `Page`, globals, timers, load model, minimal document/element, targeted mutation | `docs/m2_1_*` … `docs/m2_8_*` |
| M3 | Scripts, lifecycle, events, ready-state, `currentScript`, attributes, script types | `docs/m3_summary.md` |
| M4-A | Query collections, `createElement`, tree editing, attribute writes, subtree queries, connectivity, bubbling | `docs/m4_a_summary.md` |
| M4-B | Structural navigation, `contains` / `matches` / `closest`, document collections, class query | `docs/m4_b_summary.md` |
| M5 | Form controls, owner form, control values, single-select model | `docs/m5_summary.md` |
| M6 | `form.reset()` and the `default*` reset baselines | `docs/m6_summary.md` |
| M7 | Submission entry points and submission metadata | `docs/m7_summary.md` |
| M8 | Web-platform globals — encoding, URL, query params, Base64 | `docs/m8_summary.md` |
| M9 | Inspector attach base, host-method + `Reflect.get`/`.has` monitoring | `docs/m9_summary.md` |
| M10-1 | `Reflect.ownKeys` monitoring | `docs/m10_1_watch_reflect_ownkeys.md` |
| M10-2 | `Object.keys` monitoring | `docs/m10_2_watch_object_keys.md` |
| M11 | compatibility-first line — planning | `docs/m11_compatibility_plan.md` |
| M11-1 | configurable host-environment baseline (`navigator` / `screen` / `window` dimensions) | `docs/m11_1_host_environment.md` |
| M11-2 | `requestAnimationFrame` / `cancelAnimationFrame` + `Page.run_animation_frame()` | `docs/m11_2_animation_frames.md` |
| M11-3 | `requestIdleCallback` / `cancelIdleCallback` + `Page.run_idle_callbacks()` | `docs/m11_3_idle_callbacks.md` |
| M11-4 | `queueMicrotask` (drained by `Page.run_jobs()`) — verified 2026-10-27 | `docs/m11_4_queue_microtask.md`, `docs/m11_4_report.md` |
| M11-5 | `MutationObserver` (childList/attributes/subtree; `run_jobs()` delivery) — verified 2026-10-28 | `docs/m11_5_mutation_observer.md`, `docs/m11_5_report.md` |
| M11-6 | event subclasses (`CustomEvent` / `MouseEvent` / `KeyboardEvent` / `FocusEvent`) — verified 2026-10-28 | `docs/m11_6_event_subclasses.md`, `docs/m11_6_report.md` |
| M11-7 | Web Storage (`localStorage` / `sessionStorage`, per-Page in-memory) — verified 2026-10-28 | `docs/m11_7_web_storage.md`, `docs/m11_7_report.md` |
| M11-8 | `Blob` / `File` / `FileReader` (`run_jobs()` delivery) — verified 2026-10-28 | `docs/m11_8_blob_file_reader.md`, `docs/m11_8_report.md` |
| M11-9 | event cancellation (`preventDefault` / `stopImmediatePropagation`) — verified 2026-10-28 | `docs/m11_9_event_cancellation.md`, `docs/m11_9_report.md` |
| M11-10 | `classList` / `toggleAttribute` — verified 2026-10-28 | `docs/m11_10_class_list.md`, `docs/m11_10_report.md` |
| M11-11 | event capture phase, `eventPhase`, `capture` / `once` / `passive` — verified 2026-10-28 | `docs/m11_11_event_capture.md`, `docs/m11_11_report.md` |
| M11-12 | writable `id` / `className`, `hasAttributes()` / `getAttributeNames()` — verified 2026-10-28 | `docs/m11_12_17_dom_web_batch.md`, `docs/m11_12_17_report.md` |
| M11-13 | `replaceChild` / `remove` / `hasChildNodes` + raw sibling aliases — verified 2026-10-28 | `docs/m11_12_17_dom_web_batch.md`, `docs/m11_12_17_report.md` |
| M11-14 | `cloneNode` / `isSameNode` / `isEqualNode` — verified 2026-10-28 | `docs/m11_12_17_dom_web_batch.md`, `docs/m11_12_17_report.md` |
| M11-15 | `URL.searchParams` snapshot — verified 2026-10-28 | `docs/m11_12_17_dom_web_batch.md`, `docs/m11_12_17_report.md` |
| M11-16 | `select.options` / `selectedIndex` / `multiple` / `size` — verified 2026-10-28 | `docs/m11_12_17_dom_web_batch.md`, `docs/m11_12_17_report.md` |
| M11-17 | nested `Page(environment=...)` + `Page.get_defaults()` — verified 2026-10-28 | `docs/m11_12_17_dom_web_batch.md`, `docs/m11_12_17_report.md` |
| M11-18 | form-control attribute reflection (`input` / `button` / `form.encoding` / `option.label` / `index`) — verified 2026-10-28 | `docs/m11_18_form_control_reflection.md`, `docs/m11_18_report.md` |
| M11-19 | multi-token class queries + element-level `getElementsByClassName` — verified 2026-10-28 | `docs/m11_19_23_dom_form_batch.md`, `docs/m11_19_23_report.md` |
| M11-20 | `document.getElementsByName` — verified 2026-10-28 | `docs/m11_19_23_dom_form_batch.md`, `docs/m11_19_23_report.md` |
| M11-21 | extended form reflection (`name` / `required`, textarea/select) — verified 2026-10-28 | `docs/m11_19_23_dom_form_batch.md`, `docs/m11_19_23_report.md` |
| M11-22 | select `length` / `item` / `namedItem` — verified 2026-10-28 | `docs/m11_19_23_dom_form_batch.md`, `docs/m11_19_23_report.md` |
| M11-23 | document metadata + `hasFocus()` — verified 2026-10-28 | `docs/m11_19_23_dom_form_batch.md`, `docs/m11_19_23_report.md` |
| M11-24 | navigator metadata (`languages` / `onLine` / `cookieEnabled` / `hardwareConcurrency`) — verified 2026-10-28 | `docs/m11_24_28_host_environment_batch.md`, `docs/m11_24_28_report.md` |
| M11-25 | screen `colorDepth` / `pixelDepth` — verified 2026-10-28 | `docs/m11_24_28_host_environment_batch.md`, `docs/m11_24_28_report.md` |
| M11-26 | window `outerWidth` / `outerHeight` / `screenX` / `screenY` — verified 2026-10-28 | `docs/m11_24_28_host_environment_batch.md`, `docs/m11_24_28_report.md` |
| M11-27 | navigator identity fields — verified 2026-10-28 | `docs/m11_24_28_host_environment_batch.md`, `docs/m11_24_28_report.md` |
| M11-28 | extended console methods — verified 2026-10-28 | `docs/m11_24_28_host_environment_batch.md`, `docs/m11_24_28_report.md` |
| M11-29 | deterministic event-loop facade (`tick` / `advance` / `run_until_idle` / `event_loop_time_ms`) — verified 2026-10-29 | `docs/m11_29_event_loop_facade.md`, `docs/m11_29_report.md` |
| M11-30 | rAF/idle integration into the event-loop facade (16ms virtual frames, idle rounds) — verified 2026-10-29 | `docs/m11_30_frame_idle_facade.md`, `docs/m11_30_report.md` |
| M11-31 | virtual `performance.now()` / `timeOrigin` + rAF timestamps — verified 2026-10-29 | `docs/m11_31_32_perf_validation_batch.md`, `docs/m11_31_32_report.md` |
| M11-32 | minimal form constraint validation (`willValidate` / `validity` / `checkValidity()` / `setCustomValidity()`, `invalid` events) — verified 2026-10-29 | `docs/m11_31_32_perf_validation_batch.md`, `docs/m11_31_32_report.md` |
| M11-33 | OS-backed `crypto.getRandomValues()` / `randomUUID()` (platform CSPRNG) — verified 2026-10-29 | `docs/m11_33_crypto_random.md`, `docs/m11_33_report.md` |
| M11-34 | CSS selector engine expansion (lists, compound/attribute selectors, combinators, `*`) — verified 2026-10-29 | `docs/m11_34_selector_engine.md`, `docs/m11_34_report.md` |
| M11-35 | deterministic BOM metadata completion (window/screen/navigator; 36-key environment) — verified 2026-10-30 | `docs/m11_35_bom_metadata.md`, `docs/m11_35_report.md` |
| M11-36..M11-40 | BOM completion batch (dialogs, performance timeline, structuredClone, single-context window metadata, scheduler.postTask) — verified 2026-10-30 | `docs/m11_36_40_bom_completion.md`, `docs/m11_36_40_report.md` |
| M11-41..M11-45 | BOM viewport/media/device metadata (visualViewport, matchMedia, navigator.storage/permissions, legacy navigator metadata) — verified 2026-10-30 | `docs/m11_41_45_bom_extras.md`, `docs/m11_41_45_report.md` |
| M11-46 | `document.cookie` — deterministic per-Page in-memory jar, access-traced, first Document slice of the common-API track — verified 2026-11-02 | `docs/m11_46_document_cookie.md`, `docs/m11_46_report.md` |
| M11-47 | Document identity — writable `document.title` + `documentURI` / `baseURI` / `defaultView` / `location` aliases — verified 2026-11-02 | `docs/m11_47_document_identity.md`, `docs/m11_47_report.md` |
| M11-48 | Node foundation — Text/Comment/DocumentFragment nodes, live `textContent`, `characterData` mutation records — verified 2026-11-03 | `docs/m11_48_node_foundation.md`, `docs/m11_48_report.md` |
| M11-49 | HTML string APIs and variadic tree methods — innerHTML/outerHTML/insertAdjacent*, append/prepend/before/after/replaceWith/replaceChildren, entity decoding — verified 2026-11-03 | `docs/m11_49_html_strings.md`, `docs/m11_49_report.md` |
| M11-50 | Window scroll/focus/UI completion — scrollX/scrollY/pageXOffset/pageYOffset (0), no-op scroll/focus/blur/move/resize/stop, writable `status`, BarProps, `clientInformation` alias — verified 2026-11-03 | `docs/m11_50_window_completion.md`, `docs/m11_50_report.md` |
| M11-51 | Window event handler attributes — bounded `window.on*` accessors, install/replace/clear, at-target/bubbling invocation, `return false` cancellation, per-generation reset — verified 2026-11-03 | `docs/m11_51_window_event_handlers.md`, `docs/m11_51_report.md` |
| M11-52 | Window messaging — `postMessage` with structured clone, bounded target origins, async FIFO timer delivery, `MessageEvent` (`data`/`origin`/`source`/`ports`) — verified 2026-11-03 | `docs/m11_52_window_messaging.md`, `docs/m11_52_report.md` |
| Post-M11 host access trace (steps 1–4 + complete step-5 object groups + steps 6a–7) | native HostObject get/set/call trace + missing-property probes + `get_method` references + `delete`/`own_keys`/`define_property` request events + explicit `has`/`get_own_property_descriptor` query events + Reflect target metadata + performance/crypto/Storage/permissions/matchMedia/window callbacks and getters + window metadata/alias accessors + nested/visualViewport accessors and methods + navigator.storage manager calls + navigator.permissions query/status accesses + matchMedia result accesses + legacy navigator metadata + primitive value/argument/return summaries + synchronous exception metadata + step-7 aggregated report (`start_access_trace` / `stop_access_trace` / `read_access_trace` / `access_trace_report`) — verified 2026-11-02 | `docs/host_access_trace.md`, `docs/host_access_trace_workflow.md` |

Each milestone summary ends with an explicit *frozen out* list. That list is the
authoritative answer to "does this project support X?" — prefer it over inference
from the code.

The `python/iv8/__init__.py` module docstring carries the same milestone-by-milestone
narrative in prose and is kept in sync with these summaries.

## 6. Build and platform reality

- Linux x86-64 is the real release target; the repository records a green Linux wheel
  workflow for cp311–cp314. See `docs/phase9_release_strategy.md`.
- Windows builds with real V8 as well, but is **not** the primary release baseline.
  macOS arm64 now also has a V8-linked release lane (`tools/build_v8_macos.sh` +
  `release-macos.yml`, `macos-15`, `macosx_11_0_arm64`); Intel/x64 and universal2
  are not built. Promoting Windows/macOS remains an open decision
  (`docs/decision_log.md` §C).
- The package exports the same public **shape** in a V8-free skeleton build, where
  `JSContext()` / `JSValue()` / `Page()` raise `RuntimeError`. Check `iv8._v8_linked`
  first: on a skeleton build the runtime tests skip, and only shape/boundary guards
  run. A stale skeleton install in a working venv shows up as widespread
  `AttributeError` failures rather than skips — rebuild before trusting a local
  suite result. See `docs/dev_setup.md`.

## 7. Where to look next

- current implementation status → `docs/current_status.md`
- task queue → `docs/next_tasks.md`
- browser-surface gaps (DOM / BOM) → `docs/dom_bom_gaps.md`
- project-level decisions and open questions → `docs/decision_log.md`
- local build / test setup → `docs/dev_setup.md`
- agent working rules → `AGENTS.md`, `CLAUDE.md`
