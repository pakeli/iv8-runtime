# DOM / BOM Gap Inventory

Last reviewed: `2026-10-29` · Baseline: **M11-33** (OS-backed Crypto random
verified `2026-10-29`)

> **Scope note.** This is a reference inventory of browser-surface features that
> are **not implemented** as of M11-33. It is not a contract and not a roadmap.
> Where a feature is partially present, only the missing parts are listed.
> Status labels:
>
> - **excluded** — deliberate standing exclusion or architecture boundary;
> - **deferred** — needs a separate design decision before implementation;
> - **candidate** — a viable near-term slice, ranked in
>   `docs/m11_compatibility_backlog.md`;
> - **absent** — not implemented and not yet ranked.
>
> Sources: `docs/runtime_surface.md`, the milestone boundary documents, and
> `docs/m11_compatibility_backlog.md`.

## 1. BOM gaps

### 1.1 Networking and navigation

| Missing | Status |
|---|---|
| `fetch` / `Request` / `Response` / `Headers` / `FormData` / `AbortController` | **excluded** (offline XHR/fetch is backlog rank 7) |
| `XMLHttpRequest`, `WebSocket`, `EventSource`, `navigator.sendBeacon` | **excluded** |
| `location.assign` / `replace` / `reload`; URL-driven navigation | **excluded** |
| `history` (`pushState` / `replaceState` / `back` / `forward` / `go`), `hashchange` / `popstate` | **excluded** |
| `window.open` / `close`, `frames` / `parent` / `top` / `opener`, `postMessage` | **excluded** |
| `beforeunload` / `unload` / `pageshow` / `pagehide` lifecycle events | **excluded** |

### 1.2 Storage and persistence

| Missing | Status |
|---|---|
| `storage` events and cross-page/cross-tab notification | **absent** — M11-7 provides per-Page in-memory stores only |
| `document.cookie` | **excluded** |
| IndexedDB, Cache API, `navigator.storage` / `StorageManager` | **absent** |
| Persistence across `Page` instances or processes | **excluded** — entries survive `load()` within one Page only |

### 1.3 Time, scheduling, event loop

| Missing | Status |
|---|---|
| Full Performance API (`mark` / `measure` / timelines / entries / timing) | **partially implemented** — M11-31 adds virtual `performance.now()` / `timeOrigin`; marks, measures, timelines, and resource/navigation timing are absent |
| `requestAnimationFrame` callback timestamp argument | **implemented** — M11-31 passes the current virtual timestamp |
| `requestIdleCallback` `options.timeout` firing semantics | **deferred** — validated but never fires |
| `scheduler.postTask`, microtask cancellation | **absent** |
| Full event-loop facade (rAF/idle + real sleep/clock) | **partially implemented** — M11-29/M11-30 add virtual `tick` / `advance` / `run_until_idle` for timers, microtasks, rAF, and idle; real sleep/clock remain absent |
| Automatic event loop / background timers | **excluded** — explicit Python pumps/facade only; no background execution |

### 1.4 Dialogs and UI

| Missing | Status |
|---|---|
| `alert` / `confirm` / `prompt`, `window.print` | **absent** |
| Fullscreen API, `screen.orientation`, `visualViewport` | **absent** |
| Scroll APIs (`scroll` / `scrollTo` / `scrollBy` / `scrollX` / `scrollY`) | **absent** — no layout |
| `matchMedia` | **absent** — no CSS/media-query engine |

### 1.5 Device and platform APIs

| Missing | Status |
|---|---|
| `crypto` (`getRandomValues`, SubtleCrypto) | **partially implemented** — M11-33 adds OS-backed `getRandomValues()` / `randomUUID()`; `crypto.subtle` (digest, AES, RSA, ECDH/ECDSA, HMAC, KDF) is absent |
| `Worker` / `SharedWorker` / `ServiceWorker` | **absent** |
| `navigator.clipboard`, `geolocation`, `Notification`, `Permissions` | **absent** |
| `navigator.connection` / `userAgentData` | **absent** |
| `navigator.maxTouchPoints` / permissions / clipboard / geolocation | **absent** |
| `structuredClone` and other transfer/serialization APIs | **absent** |

### 1.6 Screen and window metadata

| Missing | Status |
|---|---|
| `screen.orientation` / `availLeft` / `availTop` | **absent** |
| `window.name` / `length` | **absent** |

Implemented for contrast: `screen.width` / `height` / `availWidth` /
`availHeight` / `colorDepth` / `pixelDepth`, and `window.devicePixelRatio` /
`innerWidth` / `innerHeight` / `outerWidth` / `outerHeight` / `screenX` /
`screenY` (read-only reported values, M11-1/M11-25/M11-26).

### 1.7 Console

| Missing | Status |
|---|---|
| Console formatting, stack capture, table rendering, group nesting, real timing | **absent** — M11-28 provides deterministic method aliases |

Implemented for contrast: `console.log` / `info` / `warn` / `error`, plus
M11-28 `debug` / `trace` / `table` / `dir` / `group` / `groupEnd` / `time` /
`timeEnd` / `assert` / `count`, all routed to Python `logging`; no clock,
formatting, stack, or nesting state.

## 2. DOM gaps

### 2.1 Node model

| Missing | Status |
|---|---|
| Text, comment, and CDATA nodes; `createTextNode` / `createComment` / `createCDATASection` | **excluded** — the DOM is element-only |
| `DocumentFragment` and `createDocumentFragment` | **absent** |
| Live re-aggregation of `textContent` after structural edits | **excluded** — `textContent` is a parse-time/write-time aggregate |
| `importNode`, `adoptNode`, `normalize`, `compareDocumentPosition`, `getRootNode` | **absent** |
| `Range`, `Selection`, `TreeWalker`, `NodeIterator` | **absent** |

Implemented for contrast: M11-13 added raw `firstChild` / `lastChild` /
`previousSibling` / `nextSibling` / `hasChildNodes()` (element-only aliases);
M11-14 added `cloneNode`, `isSameNode`, and `isEqualNode`.

### 2.2 Collections

| Missing | Status |
|---|---|
| `HTMLCollection` / `NodeList`, `item()` / `namedItem()` | **excluded** — collections are plain JS `Array`s |
| Live collection objects and object-identity guarantees | **excluded** — collections are recollected per access; wrappers may be recreated |
| Iteration helpers specific to `NodeList` (`forEach`, entries/keys/values) | **absent** (plain arrays cover ordinary iteration) |

### 2.3 Selectors and queries

| Missing | Status |
|---|---|
| Combinators (descendant/child/sibling), attribute selectors, pseudo-classes/elements, selector lists | **absent** — only a single `#id` / `.class` / `tag` is supported |
| Complex selectors in `matches` / `closest` (currently `false` / `null`) | **absent** |
| `document.all` | **absent** |

Implemented for contrast: M11-19 added multi-token `getElementsByClassName`
(document + element, descendants only for the element form); M11-20 added
`document.getElementsByName`.

### 2.4 Element surface

| Missing | Status |
|---|---|
| `innerHTML` / `outerHTML` / `insertAdjacentHTML` / `insertAdjacentElement` / `insertAdjacentText` | **absent** |
| `dataset`, `style` / CSSOM, `attributes` (`NamedNodeMap`) | **absent** |
| `getAttributeNS` / `setAttributeNS` / `removeAttributeNS` | **absent** |
| Attribute-reflection properties (`.src`, `.href`, `.type`, `.name`, …) | **partially implemented (M11-18)** — form-control subset only; other reflections **absent** |
| `replaceWith`, `append`, `prepend`, `before`, `after`, `replaceChildren` | **absent** — need text-node/string semantics |
| Specialized `HTML*Element` classes | **excluded** |
| Shadow DOM (`attachShadow`, `ShadowRoot`, slots), Custom Elements, `<template>` | **absent** |
| SVG / MathML namespaces (`createElementNS`) | **absent** |
| Layout/geometry members (`getBoundingClientRect`, `offset*`, `client*`, `scroll*`) | **absent** — no layout engine |
| Resource-backed element behavior (`img` loading, `naturalWidth`, media elements) | **absent** |

Implemented for contrast: M11-12 added writable `id` / `className`,
`hasAttributes()`, and `getAttributeNames()`; M11-13 added `remove()` and
`replaceChild()`.

### 2.5 Events

| Missing | Status |
|---|---|
| `composed`, `timeStamp`, `isTrusted` | **absent** |
| More event classes (`UIEvent`, `InputEvent`, `WheelEvent`, …) | **absent** — M11-6 delivered `CustomEvent` / `MouseEvent` / `KeyboardEvent` / `FocusEvent` |
| `signal` listener option, `on*` handler properties | **absent** |
| Native event generation and default actions (`click()`, `submit` / `reset` / `input` / `change`, media events) | **excluded** |
| Additional lifecycle events (`beforeunload` / `unload` / `pageshow` / `pagehide` / `visibilitychange`) | **absent** |
| `document.createEvent` | **absent** |
| `MutationRecord` global; `characterData` mutation records | **excluded** — records are plain objects; the DOM has no text nodes |
| `document`-target `childList` records without `subtree` | **excluded** — no JS API mutates the document node itself |

Implemented for contrast: `Event(type, {bubbles})`, `target` / `currentTarget`,
`stopPropagation()`, bubbling up to `document` / `window`, and the
`readystatechange` / `DOMContentLoaded` / `load` lifecycle events. M11-9 added
cancellation (`cancelable` / `defaultPrevented` / `preventDefault` /
`stopImmediatePropagation`); M11-11 added the capture phase, `eventPhase`, and
`capture` / `once` / `passive` listener options.

### 2.6 Forms

| Missing | Status |
|---|---|
| Validation API (`checkValidity`, `reportValidity`, `validity`, `willValidate`, `setCustomValidity`) | **partially implemented** — M11-32 adds `required`, `minlength` / `maxlength`, email/URL `typeMismatch`, custom validity, form aggregation, and minimal `invalid` events; `pattern`, `min` / `max` / `step`, radio-group semantics, and native UI remain absent |
| Automatic `submit` / `reset` / `input` / `change` events and default submit action | **excluded** |
| `requestSubmit(submitter)` argument and `event.submitter` | **absent** |
| `FormData` and real form encoding | **absent** |
| Writable `select.length`, option collection methods, multi-select behavior | **absent** — minimal single-select only |
| Radio-group exclusivity; type-specific input behavior | **absent** |
| Browsing-context resolution of `target`; URL resolution of `action` | **absent** — stored verbatim |

Implemented for contrast: `form.elements` (plain array), `reset()`, no-op
`submit()` / `requestSubmit()`, the five form metadata properties,
control `value` / `checked` / `default*` / `form`, and the option surface.
M11-16 added `select.options`, writable `selectedIndex`, and read-only
`multiple` / `size`; M11-18 added input `type` / `disabled` / `readOnly` /
`placeholder`, button `type` / `disabled`, `form.encoding`, and
`option.label` / `index`; M11-32 added the bounded validation surface
(`required`, `minlength` / `maxlength`, email/URL `typeMismatch`, custom
validity, form aggregation, and `invalid` events); `URL.searchParams` (M11-15)
is a non-live snapshot.

### 2.7 Scripts and document loading

| Missing | Status |
|---|---|
| ES modules, import maps, dynamic `import()`, `nomodule` | **excluded** — classic scripts only |
| `async` / `defer` scheduling and real network loading | **excluded** |
| Dynamic script execution on tree insertion | **excluded** — inserted `<script>` is inert |
| `document.write` / `writeln` / `open` / `close` | **absent** |
| MIME sniffing and script load/error events (`onload` / `onerror`) | **absent** |
| Subresource loading: CSS, images, fonts, `<iframe>` | **excluded** |
| `DOMParser`, `XMLSerializer` | **absent** |
| Full HTML5 parser (foster parenting, tables, entities, error recovery) | **absent** — a minimal static element parser only |

Implemented for contrast: inline and classic external `<script>` execution,
`resources` pre-resolution, `document.currentScript`, `document.scripts`,
`document.readyState`, and the static `Page.load()` lifecycle.

### 2.8 Rendering and layout

| Missing | Status |
|---|---|
| CSSOM, `getComputedStyle`, style sheets, `matchMedia` | **absent** |
| Layout / geometry / scrolling / hit testing (`elementFromPoint`) | **absent** — no layout engine |
| Canvas / WebGL / SVG rendering | **absent** (backlog rank 9) |
| Image and media rendering (`HTMLImageElement`, `HTMLVideoElement`, …) | **absent** |

### 2.9 Document misc

| Missing | Status |
|---|---|
| `document.cookie` | **excluded** |
| `document.createRange` / `createTreeWalker` / `createNodeIterator` / `createEvent` | **absent** |
| `document.all` / `plugins` | **absent** |
| Browsing contexts, frames, `<iframe>` behavior | **absent** |

Implemented for contrast: M11-23 added read-only `characterSet` / `contentType`
/ `compatMode` / `referrer` / `domain` / `visibilityState` / `hidden` /
`activeElement` and `hasFocus()`; M11-22 added read-only `select.length`, plus
`select.item` / `namedItem`.

## 3. Cross-cutting architecture boundaries

These are not ordinary missing APIs; changing them is a project-level decision:

- No automatic event loop, clock, or background thread. Asynchronous work is
  driven only by the four explicit pumps (`run_timers` / `run_jobs` /
  `run_animation_frame` / `run_idle_callbacks`).
- No networking, navigation, or persistence in the core runtime.
- No JS-to-Python callback bridge (`expose()`); the DevTools surface is
  monitoring only.
- No Python-side DOM or event API (`Page.document`); the DOM exists only in JS.
- Element-only tree, plain-array collections, no wrapper-identity guarantee.
- No layout/rendering tree; geometry and CSS surfaces cannot exist without one.

## 4. Near-term candidates

The M11-6..M11-10 batch delivered event subclasses, Web Storage,
Blob/File/FileReader, event cancellation, and classList/toggleAttribute;
M11-11 delivered event capture and listener options; M11-12..M11-17 delivered
the element attribute/tree/clone/URL.searchParams/select extensions and the
nested environment + `get_defaults()`; M11-18 delivered form-control attribute
reflection; M11-19..M11-23 delivered query, form, select, and document-metadata
completeness; M11-24..M11-28 delivered navigator/screen/window metadata and the
console method cluster; M11-29 delivered the deterministic timer/microtask
event-loop facade, M11-30 integrated rAF/idle into it, M11-31/M11-32 added the
virtual performance clock and minimal form validation, and M11-33 added
OS-backed `crypto.getRandomValues()` / `randomUUID()`. Remaining candidates
(mapping to `docs/m11_compatibility_backlog.md`):

| Gap area | Backlog rank | Notes |
|---|---|---|
| Real clock / sleep semantics in the event-loop facade | 6 | M11-29/M11-31 expose virtual time to JS; real sleeping and a wall clock remain absent |
| Offline XHR/fetch | 7 | Requires approval of the networking exclusion |
| JS→Python `expose()` | 8 | Requires a separate architecture decision |
| Canvas / WebGL / CSSOM / Workers / full storage | 9 | Not a near-term slice |

## 5. Maintenance

- When a slice lands, remove the implemented rows here and add the feature to
  `docs/runtime_surface.md`.
- New rows should carry one of the four status labels; unranked items belong in
  the `absent` bucket until the backlog ranks them.
