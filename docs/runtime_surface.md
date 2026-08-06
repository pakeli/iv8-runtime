# iv8-runtime Runtime Surface Index

Last reviewed: `2026-08-06` · Latest landed milestone: **M10-2** (`Page.watch_object_keys`)

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
| `run_jobs()` | M2-4 | drains the microtask queue |
| `load(html, base_url, scripts=None, resources=None)` | M2-5, M3-1, M3-5 | static load model — no network, no navigation, no history |
| `ready_state` | M3-2 | Python lifecycle (`"loading"` / `"complete"`), distinct from JS `document.readyState` |

`resources` (M3-5) is the **only** new Python parameter M3–M8 added. `Page` has no
`document` / DOM / event API on the Python side — the DOM is JS-side only.

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
  `hash` / `toString()`) — both static and read-only;
- **timers** — `setTimeout` / `clearTimeout` / `setInterval` / `clearInterval`
  (manual-pump only);
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
DOM or event API (`Page.document`), text/comment nodes, live collections, layout or
geometry, and fingerprint/profile simulation. `iv8` also exports no browser symbol at
module level — `test/test_scope_exclusions.py` guards this.

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
| M11 | **not started** — direction undecided | `docs/decision_log.md` §A |

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
