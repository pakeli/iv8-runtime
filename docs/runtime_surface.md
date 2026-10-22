# iv8-runtime Runtime Surface Index

Last reviewed: `2026-10-22` · Latest implemented milestone: **M11-4**
(`queueMicrotask`, working tree; real-V8 CI verification pending)

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
| `run_jobs()` | M2-4 | drains the microtask queue (`Promise` reactions + M11-4 `queueMicrotask`) |
| `run_animation_frame()` | M11-2 | manual pump; runs one frame of `requestAnimationFrame` callbacks |
| `run_idle_callbacks()` | M11-3 | manual pump; runs one round of `requestIdleCallback` callbacks |
| `load(html, base_url, scripts=None, resources=None)` | M2-5, M3-1, M3-5 | static load model — no network, no navigation, no history |
| `ready_state` | M3-2 | Python lifecycle (`"loading"` / `"complete"`), distinct from JS `document.readyState` |
| `Page(environment=None)` | M11-1 | optional read-only host-environment baseline; fixed for the page's lifetime |
| `get_default_environment()` | M11-1 | static; returns a fresh `dict` copy of the default template |

`resources` (M3-5) was the only new Python parameter M3–M8 added; `environment`
(M11-1) is the next one. `Page` has no `document` / DOM / event API on the Python
side — the DOM is JS-side only.

The M11-1 environment accepts exactly eleven keys — `userAgent` / `platform` /
`language` (`str`), `webdriver` (`bool`), `screenWidth` / `screenHeight` /
`availWidth` / `availHeight` / `innerWidth` / `innerHeight` (`int`), and
`devicePixelRatio` (`int` or `float`, stored `float`). Omitted keys inherit the
defaults, and the defaults reproduce the previous fixed `navigator` constants, so an
unconfigured page is unchanged. Validation is strict and runs in the Python facade
before any native page exists (unknown key or wrong type → `TypeError`, nothing
partially installed). Values are re-applied to every `load()` generation. See
`docs/m11_1_host_environment.md`.

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

Shared semantics across all five channels:

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

Not public surface: `_core.Page.devtools_pause_count()` is an internal test probe for
pause evidence (M9-3).

## 3. JS-side surface (reachable only through `Page.eval`)

Installed per generation by `install_page`:

- **global roots** — `window` / `globalThis` / `self` (all the same object);
- `console` (`log` / `info` / `warn` / `error` → Python `logging`, logger
  `iv8.console`);
- `navigator` (`userAgent` / `platform` / `language` / `webdriver`) and `location`
  (`href` / `origin` / `protocol` / `host` / `hostname` / `pathname` / `search` /
  `hash` / `toString()`) — both read-only; `navigator`'s four values are configurable
  per page since M11-1, `location` still derives from the base URL;
- `screen` (`width` / `height` / `availWidth` / `availHeight`) and the `window`
  dimensions `devicePixelRatio` / `innerWidth` / `innerHeight` — read-only, M11-1,
  configured via `Page(environment=...)`. No `colorDepth` / `orientation` /
  `outerWidth` / `screenX` / scroll / `matchMedia`, and these dimensions feed no
  layout engine;
- **timers** — `setTimeout` / `clearTimeout` / `setInterval` / `clearInterval`
  (manual-pump only);
- **microtasks** — `queueMicrotask(callback)` (M11-4), drained only by
  `Page.run_jobs()` (the same queue `Promise` reactions use). A non-callable
  argument throws a JS `TypeError`; callbacks run FIFO, may enqueue further
  microtasks in the same checkpoint, and a throwing callback is swallowed;
- **animation frames** — `requestAnimationFrame` / `cancelAnimationFrame` (M11-2,
  manual-pump only via `Page.run_animation_frame()`). A separate FIFO queue from the
  timers, with its own handle space; the callback receives no timestamp argument, and
  a re-request lands in the next frame. No clock and no `performance.now()`;
- **idle callbacks** — `requestIdleCallback` / `cancelIdleCallback` (M11-3,
  manual-pump only via `Page.run_idle_callbacks()`). Another separate FIFO queue with
  its own handle space; a re-request lands in the next round. The callback receives a
  minimal `IdleDeadline`-style object (`didTimeout` always `false`, `timeRemaining()`
  a fixed 50ms) — deterministic, since there is no clock. `options.timeout` is
  type-validated but has **no** firing semantics;
- **events** — the `Event` constructor plus `addEventListener` /
  `removeEventListener` / `dispatchEvent` on `window`, `document`, and elements;
- **`document`** — `URL` / `title` / `readyState` / `documentElement` / `head` /
  `body` / `currentScript` / `scripts` / `children` / `forms` / `images` / `links` /
  `anchors` / `embeds` / `applets`, and `getElementById` / `querySelector` /
  `querySelectorAll` / `getElementsByTagName` / `getElementsByClassName` /
  `createElement`;
- **elements** — the minimal node surface (`tagName` / `nodeName` / `nodeType` / `id`
  / `className` / `textContent` / `parentNode` / `childNodes` / `children`),
  connectivity and structural navigation (`ownerDocument` / `isConnected` /
  `parentElement` / sibling and first/last child members / `childElementCount`),
  attributes (`getAttribute` / `hasAttribute` / `setAttribute` / `removeAttribute`),
  tree editing (`appendChild` / `removeChild` / `insertBefore`), subtree queries, and
  `contains` / `matches` / `closest`;
- **form surface**, exposed only on the relevant tags — `<form>`: `elements` /
  `reset()` / `submit()` / `requestSubmit()` / `method` / `action` / `enctype` /
  `target` / `noValidate`; controls: `form`, `value`, `checked`, `defaultValue`,
  `defaultChecked`; `<option>`: `value` / `selected` / `text` / `defaultSelected`;
- **web-platform globals (M8)** — `TextEncoder` / `TextDecoder` (UTF-8 only), `URL`,
  `URLSearchParams`, `atob` / `btoa`.

`hostProbe` is M2-1 framework infrastructure, not a stable API.

Deliberate shape of this DOM: it is **element-only** (no text / comment / fragment
nodes), collections are plain JS `Array`s (not `HTMLCollection` / `NodeList`, no
`item()` / `namedItem()`, no identity guarantee), selectors support exactly one of
`#id` / `.class` / `tagname`, there are no specialized `HTML*Element` classes or
attribute-reflection properties, and a `<script>` inserted into the tree stays
**inert**. `form.submit()` / `requestSubmit()` are callable no-ops. Consult the
milestone summary before assuming any wider behavior.

## 4. Standing exclusions

Never implemented at any milestone so far: networking (`fetch` / XHR / WebSocket /
resource loading), navigation and history, JavaScript→Python callbacks, a Python-side
DOM or event API (`Page.document`), text/comment nodes, live collections, and layout
or geometry. `iv8` also exports no browser symbol at module level —
`test/test_scope_exclusions.py` guards this.

Nuance since M11-1: the page reports configurable `screen` / `window` **dimension
values**, but there is still no layout engine, no `matchMedia`, and nothing consumes
those numbers — they are reported values only. M11-1 also makes the `navigator`
identity values configurable, which is a *deterministic baseline* facility, not
fingerprint simulation or randomization: a page reports exactly what the caller
configured, and there is no UA-CH / canvas / webgl surface to spoof.

Also still excluded: a real clock, background threads, and any automatic event loop.
Every asynchronous queue is drained by an explicit Python pump — `run_timers()`,
`run_jobs()` (microtasks, including `queueMicrotask` since M11-4),
`run_animation_frame()` (M11-2), and `run_idle_callbacks()` (M11-3).
Nothing in this runtime advances on its own, and there is no `performance.now()` /
frame-rate / vsync notion; frames and idle rounds are counted queues, not timed ones.
There is also **no unified scheduler**: each pump drains exactly one queue, and no
`tick()` / `drain()` / `run_until_idle()` / `scheduler.postTask` surface exists.
Consequently `IdleDeadline.timeRemaining()` is a constant and `options.timeout` never
fires — a real deadline would need the clock this runtime deliberately lacks.

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
| M11-4 | `queueMicrotask` (drained by the existing `Page.run_jobs()`) | `docs/m11_4_queue_microtask.md` |

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
- project-level decisions and open questions → `docs/decision_log.md`
- local build / test setup → `docs/dev_setup.md`
- agent working rules → `AGENTS.md`, `CLAUDE.md`
