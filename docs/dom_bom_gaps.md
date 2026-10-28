# DOM / BOM Gap Inventory

Last reviewed: `2026-10-28` · Baseline: **M11-5** (`MutationObserver`, verified
`2026-10-28`)

> **Scope note.** This is a reference inventory of browser-surface features that
> are **not implemented** as of M11-5. It is not a contract and not a roadmap.
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
| `performance.now()` and the Performance API (`mark` / `measure` / timelines) | **absent** — there is no clock |
| `requestAnimationFrame` callback timestamp argument | **absent** — manual frames are counted, not timed |
| `requestIdleCallback` `options.timeout` firing semantics | **deferred** — validated but never fires |
| `scheduler.postTask`, microtask cancellation | **absent** |
| Event-loop facade (`advance` / `sleep` / `tick` / `drain`) | **deferred** (backlog rank 6) |
| Automatic event loop / background timers | **excluded** — four explicit Python pumps only |

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
| `crypto` (`getRandomValues`, SubtleCrypto) | **absent** |
| `Worker` / `SharedWorker` / `ServiceWorker` | **absent** |
| `navigator.clipboard`, `geolocation`, `Notification`, `Permissions` | **absent** |
| `navigator.languages` / `onLine` / `cookieEnabled` / `hardwareConcurrency` / `connection` / `userAgentData` | **absent** |
| `structuredClone` and other transfer/serialization APIs | **absent** |

### 1.6 Screen and window metadata

| Missing | Status |
|---|---|
| `screen.colorDepth` / `pixelDepth` / `orientation` / `availLeft` / `availTop` | **absent** |
| `window.outerWidth` / `outerHeight` / `screenX` / `screenY` / `name` / `length` | **absent** |

Implemented for contrast: `screen.width` / `height` / `availWidth` /
`availHeight`, `window.devicePixelRatio` / `innerWidth` / `innerHeight` (read-only
reported values, M11-1).

### 1.7 Console

| Missing | Status |
|---|---|
| `console.debug` / `trace` / `table` / `group` / `groupEnd` / `time` / `timeEnd` / `assert` / `count` / `dir` | **absent** |

Implemented for contrast: `console.log` / `info` / `warn` / `error` only (routed
to Python `logging`).

## 2. DOM gaps

### 2.1 Node model

| Missing | Status |
|---|---|
| Text, comment, and CDATA nodes; `createTextNode` / `createComment` / `createCDATASection` | **excluded** — the DOM is element-only |
| `DocumentFragment` and `createDocumentFragment` | **absent** |
| Live re-aggregation of `textContent` after structural edits | **excluded** — `textContent` is a parse-time/write-time aggregate |
| Raw Node members on elements: `firstChild` / `lastChild` / `previousSibling` / `nextSibling` / `hasChildNodes` | **absent** (only the `*ElementSibling` / `*ElementChild` forms exist) |
| `cloneNode`, `importNode`, `adoptNode`, `normalize`, `isEqualNode`, `isSameNode`, `compareDocumentPosition`, `getRootNode` | **absent** |
| `Range`, `Selection`, `TreeWalker`, `NodeIterator` | **absent** |

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
| Multi-token `getElementsByClassName` (document-level takes exactly one token) | **absent** |
| `element.getElementsByClassName` | **absent** |
| `document.all` | **absent** |

### 2.4 Element surface

| Missing | Status |
|---|---|
| `innerHTML` / `outerHTML` / `insertAdjacentHTML` / `insertAdjacentElement` / `insertAdjacentText` | **absent** |
| `dataset`, `style` / CSSOM, `attributes` (`NamedNodeMap`) | **absent** |
| `hasAttributes`, `getAttributeNS` / `setAttributeNS` / `removeAttributeNS` | **absent** |
| Attribute-reflection properties (`.src`, `.href`, `.type`, `.name`, …) | **absent** — use `getAttribute` |
| `id` / `className` property setters | **excluded** — the getters are read-only |
| `remove()`, `replaceWith`, `append`, `prepend`, `before`, `after`, `replaceChildren`, `replaceChild` | **absent** — only `appendChild` / `insertBefore` / `removeChild` exist |
| `cloneNode` | **absent** |
| Specialized `HTML*Element` classes | **excluded** |
| Shadow DOM (`attachShadow`, `ShadowRoot`, slots), Custom Elements, `<template>` | **absent** |
| SVG / MathML namespaces (`createElementNS`) | **absent** |
| Layout/geometry members (`getBoundingClientRect`, `offset*`, `client*`, `scroll*`) | **absent** — no layout engine |
| Resource-backed element behavior (`img` loading, `naturalWidth`, media elements) | **absent** |

### 2.5 Events

| Missing | Status |
|---|---|
| Capture phase, `eventPhase`, `composed`, `timeStamp`, `isTrusted` | **absent** |
| More event classes (`UIEvent`, `InputEvent`, `WheelEvent`, …) | **absent** — M11-6 delivered `CustomEvent` / `MouseEvent` / `KeyboardEvent` / `FocusEvent` |
| Listener options (`once` / `capture` / `passive` / `signal`) | **absent** |
| `on*` handler properties | **absent** |
| Native event generation and default actions (`click()`, `submit` / `reset` / `input` / `change`, media events) | **excluded** |
| Additional lifecycle events (`beforeunload` / `unload` / `pageshow` / `pagehide` / `visibilitychange`) | **absent** |
| `document.createEvent` | **absent** |
| `MutationRecord` global; `characterData` mutation records | **excluded** — records are plain objects; the DOM has no text nodes |
| `document`-target `childList` records without `subtree` | **excluded** — no JS API mutates the document node itself |

Implemented for contrast: `Event(type, {bubbles})`, `target` / `currentTarget`,
`stopPropagation()`, bubbling up to `document` / `window`, and the
`readystatechange` / `DOMContentLoaded` / `load` lifecycle events.

### 2.6 Forms

| Missing | Status |
|---|---|
| Validation API (`checkValidity`, `reportValidity`, `validity`, `willValidate`, `setCustomValidity`) | **absent** |
| Automatic `submit` / `reset` / `input` / `change` events and default submit action | **excluded** |
| `requestSubmit(submitter)` argument and `event.submitter` | **absent** |
| `FormData` and real form encoding | **absent** |
| `select.options` / `selectedIndex` / `multiple` / `size` / `length` | **absent** — minimal single-select only |
| Radio-group exclusivity; type-specific input behavior | **absent** |
| `input.type` / `disabled` / `readOnly` / `placeholder`; `button.type` / `disabled` | **absent** |
| Browsing-context resolution of `target`; URL resolution of `action` | **absent** — stored verbatim |
| `form.encoding` (the `enctype` alias) | **absent** |

Implemented for contrast: `form.elements` (plain array), `reset()`, no-op
`submit()` / `requestSubmit()`, the five form metadata properties,
control `value` / `checked` / `default*` / `form`, and the option surface.

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
| `document.cookie`, `referrer`, `domain`, `characterSet`, `contentType`, `compatMode` | **absent** |
| `document.visibilityState` / `hidden` / `hasFocus` / `activeElement` | **absent** |
| `document.createRange` / `createTreeWalker` / `createNodeIterator` / `createEvent` | **absent** |
| `document.all` / `plugins` | **absent** |
| Browsing contexts, frames, `<iframe>` behavior | **absent** |

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
Blob/File/FileReader, event cancellation, and classList/toggleAttribute.
Remaining candidates (mapping to `docs/m11_compatibility_backlog.md`):

| Gap area | Backlog rank | Notes |
|---|---|---|
| Nested environment + `get_defaults()` | 5 | API-shape decision, not a pure feature |
| Event-loop facade | 6 | Deferred design decision |
| Offline XHR/fetch | 7 | Requires approval of the networking exclusion |
| JS→Python `expose()` | 8 | Requires a separate architecture decision |
| Canvas / WebGL / CSSOM / Workers / full storage | 9 | Not a near-term slice |

## 5. Maintenance

- When a slice lands, remove the implemented rows here and add the feature to
  `docs/runtime_surface.md`.
- New rows should carry one of the four status labels; unranked items belong in
  the `absent` bucket until the backlog ranks them.
