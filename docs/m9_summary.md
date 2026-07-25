# M9 Summary — approved second-layer DevTools / monitoring boundary

This is the closing (collar) document for milestone **M9** (phases M9-1 … M9-5),
consolidating the approved public boundary. It supersedes, for reference, the per-phase
notes, which remain as historical snapshots. M9 opens the project's **second layer**: a
minimal V8 Inspector attach base plus small, opt-in **monitoring** surfaces for host
method calls and two reflection entry points. Everything is a **Python `Page` method**
(no new JS-visible global, no new top-level object or exception); the first-layer
runtime (M1–M8) is unchanged.

M9-6 (this phase) adds **no** runtime capability — only this summary and the
`test/test_m9_contract.py` collar suite.

## 1. DevTools attach base (M9-1)

- **`Page.devtools_url()`** — lazily starts a per-`Page` localhost server (Python
  stdlib, daemon thread) providing the minimal CDP discovery endpoints
  (`/json/version`, `/json`, `/json/list`) plus a WebSocket endpoint bridged to this
  page's V8 Inspector, and returns a stable `ws://127.0.0.1:<port>/devtools/page/<id>`
  URL. Repeated calls on the same `Page` return the **same** URL; it stays stable
  **across `load()`** (each generation gets a fresh Inspector behind the same
  endpoint). Never calling it leaves the Inspector and server unstarted (zero runtime
  impact). After `dispose()` it raises `JSContextDisposedError`.
- It is an **attach base**, not a full CDP/DevTools platform: there is no message
  loop, so an external client's interactive session is best-effort; synchronous CDP
  request/response works (used by the break-on-hit pause probe below).

## 2. Host-method monitoring (M9-2, M9-3)

- **`Page.watch_apis(paths, break_on_hit=False)`** / **`Page.read_watch_api_hits()`**.
  `paths` is a `list[str]` of minimal dotted `"receiver.method"` paths naming a known
  host method — e.g. `"document.querySelector"`, `"script.getAttribute"`,
  `"window.setTimeout"`. Duplicates are deduped; an empty list is valid (enabled,
  watches nothing). It monitors **host-method calls only** — no `obj.x` access, no
  wildcard/regex/prototype paths, no property watches.
- `read_watch_api_hits()` returns a `list[dict]` — each `{"path", "resource_name",
  "type": "call"}` — and **clears the log (read-and-clear)**; `resource_name` reuses
  the existing script resource-name system (HTML inline: `"{base_url}#inline-script-
  {n}"`; host `scripts`: their name; `<eval>` otherwise).
- `break_on_hit` (M9-3): `False` = pure record; `True` = record **and** pause the
  Inspector on the hit (see §4).

## 3. Reflection-entry monitoring (M9-4, M9-5)

- **`Page.watch_reflect_get(enabled=True, break_on_hit=False)`** /
  **`Page.read_reflect_get_hits()`** — monitor **`Reflect.get(target, key[,
  receiver])`** calls.
- **`Page.watch_reflect_has(enabled=True, break_on_hit=False)`** /
  **`Page.read_reflect_has_hits()`** — monitor **`Reflect.has(target, key)`** calls.
- When enabled, that page's `Reflect.get` / `Reflect.has` is wrapped with a delegating
  hook (per generation) that records the call's stringified property key and then
  calls the **real** reflect method, so behavior is unchanged. Each `read_*` returns a
  `list[dict]` — `{"property", "resource_name", "type": "Reflect.get"|"Reflect.has"}` —
  and is **read-and-clear**. The two reflect logs and the host-method log are three
  **independent** channels.
- These monitor **only** `Reflect.get` / `Reflect.has` — not plain `obj.x` / `"x" in
  obj`, not `Reflect.ownKeys` / `Object.*`, not Proxy/getter/trap side-effect analysis.

## 4. break_on_hit semantics (shared by §2 and §3)

- With an **attached Inspector session** (a client connected via `devtools_url()` and
  enabled the Debugger domain), a hit records **and** pauses the Inspector at the hit
  (`breakProgram`). With **no session** attached, a hit **records only** — no pause,
  no error (the gentle 口径).
- Pause evidence is confirmed the way M9-3 verified it: V8 invokes the embedder
  client's `runMessageLoopOnPause` only when it actually stops, so the internal pause
  counter (`_core.Page.devtools_pause_count()`, a test probe — **not** public `Page`
  surface) is the reliable signal. The pause is instantaneous (no message loop) and
  non-blocking.
- This is **not** an automatic-breakpoint suite or a `pause()` / `resume()` platform;
  `debugger;` semantics are unchanged.

## 5. Relationship to M1 … M8

- M9 changes **none** of the first-layer runtime: DOM / script / lifecycle / form /
  reset / default / form-metadata / M8 web-platform globals all behave identically.
  `<script>` stays inert; the approved `textContent` boundary is untouched.
- `repeated load` / `failed load` (no rollback) / `dispose` / `stale` semantics are
  unchanged. The Inspector and the Reflect hooks are **per generation**: a `load()`
  recreates the Inspector (an old session is dropped — re-attach on the new
  generation) and re-wraps `Reflect.get`/`.has` if still enabled. The Inspector is
  destroyed (session first, then inspector) **before** its isolate is disposed.
- All M9 **configuration** (`watch_apis` paths + break mode, the reflect enable/break
  switches, and the devtools endpoint) is **Page-level** and persists across `load()`;
  the hit logs persist until read (read-and-clear). Never enabling any of it leaves
  runtime paths at zero overhead.

## 6. Explicitly NOT in M9 (frozen out)

- Automatic-breakpoint suites; wildcard / regex / prototype-path watches;
  `clear_watch_apis`; `Page.pause()` / `Page.resume()` / auto-resume APIs.
- Full reflection coverage — `Reflect.ownKeys` / `Reflect.getOwnPropertyDescriptor` /
  `Object.keys` / `getOwnPropertyNames` / `getOwnPropertySymbols` / `defineProperty` /
  `Function.prototype.toString` monitoring.
- `wrapNative`; `vconsole`; `vdebugger`; any `debugger;`-semantics change.
- `iframe` / `Worker` attach or monitoring; custom CDP domains; discovery/domain
  extensions; any JS-visible watch/monitor global.
- Trusted-input, geometry/layout, and profile/fingerprint systems. **M10 is not
  started.**

## Platforms

Linux and Windows build with real V8 (full runtime, V8 Inspector linked into the
monolith); macOS is skeleton-only. Behavior is verified identical on Linux/Windows
real-V8; skeleton builds export the same public **shape** (with `Page()` raising
`RuntimeError`) — `@on_only` tests skip there, and only shape/boundary guards run.
