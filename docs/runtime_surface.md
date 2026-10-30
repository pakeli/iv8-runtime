# iv8-runtime Runtime Surface Index

Last reviewed: `2026-10-30` · Latest verified milestone: **M11-41..M11-45**
(BOM viewport/media/device metadata: `visualViewport`, `matchMedia`,
`navigator.storage`, `navigator.permissions`, and legacy navigator metadata),
verified on real V8 (Linux + Windows) on `2026-10-30`. The post-M11 host access
trace steps 1–4 plus the step-5 BOM-callback, window metadata, and nested
metadata groups are also verified on the same date; see
`docs/host_access_trace.md`.

## 0. What this document is

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
| `start_access_trace(max_events=10000, object_types=None)` / `stop_access_trace()` / `read_access_trace(clear=True)` | host access trace, steps 1–4 + step-5 BOM/window/nested metadata (verified 2026-10-30) | native HostObject property `get`/`set`, method `call`, missing string-key `get_probe`, independent performance/crypto/Storage callback entries, window metadata accessors, and nested connection/orientation/timeOrigin reads; structured events with generation, object identity, resource, and source kind; see `docs/host_access_trace.md` |

Shared semantics across the five watch channels:

- each `read_*` is **read-and-clear** and returns `list[dict]`; the five logs are
  independent and never cross-talk;
- a hit carries `resource_name` (the executing script's name, or `""`) and `type`;
  `watch_apis` hits also carry `path`, and the `Reflect.get`/`Reflect.has` hits carry
  `property`. `Reflect.ownKeys` / `Object.keys` take no key, so their hits have **no**
  `property` field;
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
calls plus Storage `length` reads, and records the traced window metadata
accessors (`innerWidth` / geometry / flags / `window.name`). It returns events
plus a coverage report. It does not cover window `parent`/`top`/`frames`/`opener`,
visualViewport/matchMedia/navigator storage/permission objects, other plain BOM
property reads, returned plain values, user-created objects, method-reference
reads, `in`, delete, enumeration, descriptors, or missing probes on non-HostObject
objects in this slice. See `docs/host_access_trace.md`.

Not public surface: `_core.Page.devtools_pause_count()` is an internal test probe for
pause evidence (M9-3).

## 3. JS-side surface (reachable only through `Page.eval`)

Installed per generation by `install_page`:

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
  `hostname` / `pathname` / `search` / `hash` / `toString()`) — both read-only;
  `navigator.connection` is a fresh read-only `{effectiveType, rtt, downlink,
  saveData}` object with no events; `location` still derives from the base URL
  and has no navigation methods;
- `screen` (`width` / `height` / `availWidth` / `availHeight` plus M11-25
  `colorDepth` / `pixelDepth` and M11-35 `availLeft` / `availTop` / `orientation`)
  and the `window` dimensions `devicePixelRatio` / `innerWidth` / `innerHeight`
  plus M11-26 `outerWidth` / `outerHeight` / `screenX` / `screenY` — read-only,
  configured via `Page(environment=...)`; M11-35 adds mutable `window.name`,
  read-only `window.length` (0), `screenLeft` / `screenTop`, and
  `isSecureContext` derived from the base URL. `screen.orientation` is a
  read-only `{type, angle}` derived from width/height with no events or lock; no
  scroll and these dimensions feed no layout engine. M11-39 adds
  the single-context browsing metadata: replaceable `window.parent` / `frames`,
  unforgeable read-only `window.top`, and writable `window.opener` (initially
  `null`), with no child frames, popups, or `postMessage`;
- **visual viewport + window flags** (M11-41) — read-only
  `visualViewport` (`width` / `height` from `innerWidth` / `innerHeight`,
  zero offsets, `scale=1`) with no-op listeners and no scroll/zoom/resize
  events; read-only `window.origin`, `crossOriginIsolated` (`false`),
  `closed` (`false`), and `frameElement` (`null`);
- **matchMedia** (M11-42) — a static MediaQueryList-shaped result for a bounded
  query language (media types, comma lists, `not` / `only` / `and`,
  width/height, orientation, and fixed desktop capabilities). Listener methods
  are no-ops; there are no dynamic viewport changes or `change` events;
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
  persistence, prompt UI, device access, or change events;
- **legacy navigator metadata** (M11-45) — frozen empty `navigator.plugins` /
  `mimeTypes` arrays and `navigator.javaEnabled()` returning `false`; no
  plugin enumeration or Java applet support;
- **microtasks** — `queueMicrotask(callback)` (M11-4), drained only by
  `Page.run_jobs()` (the same queue `Promise` reactions use). A non-callable
  argument throws a JS `TypeError`; callbacks run FIFO, may enqueue further
  microtasks in the same checkpoint, and a throwing callback is swallowed;
- **MutationObserver** (M11-5) — `new MutationObserver(callback)` with
  `observe(target, options)` / `disconnect()` / `takeRecords()`. Records cover
  `childList` + `attributes` (with `subtree` / `attributeOldValue` /
  `attributeFilter`), are queued in mutation order, and are delivered only by
  `Page.run_jobs()`; element and document targets are accepted. `characterData`
  is accepted but inert (element-only DOM); records are plain objects (no
  `MutationRecord` global); observer state is per generation;
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
- **tree editing + node navigation** (M11-13) — `replaceChild()`, `remove()`,
  `hasChildNodes()`, and raw `firstChild` / `lastChild` / `previousSibling` /
  `nextSibling` aliases. The tree is element-only, so the raw aliases equal the
  element variants;
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
  descendants-only), plus `document.getElementsByName`. Both return live plain
  arrays in document order;
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
  `removeEventListener` / `dispatchEvent` on `window`, `document`, and elements;
- **`document`** — `URL` / `title` / `readyState` / `documentElement` / `head` /
  `body` / `currentScript` / `scripts` / `children` / `forms` / `images` / `links` /
  `anchors` / `embeds` / `applets`, the M11-23 metadata (`characterSet` /
  `contentType` / `compatMode` / `referrer` / `domain` / `visibilityState` /
  `hidden` / `activeElement` / `hasFocus()`), and `getElementById` /
  `querySelector` / `querySelectorAll` / `getElementsByTagName` /
  `getElementsByClassName` / `getElementsByName` / `createElement`;
- **elements** — the minimal node surface (`tagName` / `nodeName` / `nodeType` /
  `id` / `className` / `textContent` / `parentNode` / `childNodes` / `children`),
  connectivity and structural navigation (`ownerDocument` / `isConnected` /
  `parentElement` / sibling and first/last child members / `childElementCount`,
  plus raw `firstChild` / `lastChild` / `previousSibling` / `nextSibling` and
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

Deliberate shape of this DOM: it is **element-only** (no text / comment / fragment
nodes), collections are plain JS `Array`s (not `HTMLCollection` / `NodeList`, no
`item()` / `namedItem()`, no identity guarantee), selectors support the M11-34
subset (lists, compound selectors, combinators, attribute selectors, `*`) but not
pseudo-classes/elements, escapes, namespaces, or `i`/`s` flags, there are no
specialized `HTML*Element` classes or
general attribute-reflection properties (only the M11-18/M11-21 form-control
subset), and a `<script>` inserted into the tree stays **inert**.
`form.submit()` / `requestSubmit()` are callable no-ops. Form validation is the
bounded M11-32 subset above, not the full HTML constraint-validation algorithm.
Crypto is the bounded M11-33 random subset; `crypto.subtle` is absent. Consult
the milestone summary before assuming any wider behavior.

## 4. Standing exclusions

Never implemented at any milestone so far: networking (`fetch` / XHR / WebSocket /
resource loading), navigation and history, JavaScript→Python callbacks, a Python-side
DOM or event API (`Page.document`), text/comment nodes, live collections, and layout
or geometry. `iv8` also exports no browser symbol at module level —
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
| Post-M11 host access trace (steps 1–4 + step-5 BOM/window/nested metadata) | native HostObject get/set/call trace + missing-property probes + performance/crypto/Storage callbacks + window/nested accessors (`start_access_trace` / `stop_access_trace` / `read_access_trace`) — verified 2026-10-30 | `docs/host_access_trace.md` |

Each milestone summary ends with an explicit *frozen out* list. That list is the
authoritative answer to "does this project support X?" — prefer it over inference
from the code.

The `python/iv8/__init__.py` module docstring carries the same milestone-by-milestone
narrative in prose and is kept in sync with these summaries.

## 6. Build and platform reality

- Linux x86-64 is the real release target; the repository records a green Linux wheel
  workflow for cp311–cp314. See `docs/phase9_release_strategy.md`.
- Windows builds with real V8 as well, but is **not** the primary release baseline.
  macOS is skeleton-only. Promoting Windows is an open decision
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
