# M10-2 — `Page.watch_object_keys` (minimal `Object.keys` monitoring)

M10-2 adds a fifth, independent monitoring channel, mirroring M10-1
(`Reflect.ownKeys`), for **`Object.keys(target)`**. It is JS-side monitoring exposed as
two Python `Page` methods; no new JS-visible global, top-level object, or exception.

## Public API

- **`Page.watch_object_keys(enabled=True, break_on_hit=False)`** — an on/off toggle
  (like `watch_reflect_ownkeys`). When `enabled`, this page's `Object.keys` is wrapped
  (per generation) with a recording hook that records each call and then delegates to
  the **real** `Object.keys`, so behavior is unchanged. `break_on_hit` behaves exactly
  as elsewhere: with an **attached** DevTools session a hit pauses the Inspector
  (`breakProgram`), otherwise it records only — no pause, no error. Disabling stops
  recording; calling it again overrides the switch. Both arguments must be `bool` (else
  `TypeError`). Raises `JSContextDisposedError` after `dispose()`.
- **`Page.read_object_keys_hits()`** — returns a `list[dict]` and **clears the log
  (read-and-clear)**. `Object.keys(target)` has no property key, so each hit is
  `{"resource_name", "type": "Object.keys"}` — **no `property` field**.
  `resource_name` reuses the existing script resource-name system; `[]` if nothing was
  hit.

## Scope

- Monitors **only** `Object.keys` calls. It does **not** attribute
  `Object.getOwnPropertyNames` / `Object.getOwnPropertySymbols` /
  `Object.defineProperty` / `Reflect.ownKeys` (M10-1, unchanged — not re-worked here),
  does not analyze Proxy/trap side effects, and takes no argument/return/stack
  snapshot. No wildcard / regex / filter, no `wrapNative` / `vconsole` / `vdebugger`,
  no `iframe` / `Worker` monitoring, no custom CDP domain, and no JS-visible watch API.

## Relationship to the existing surface

- The `Object.keys` hit log is a **separate channel** from the host-method watch
  (`watch_apis`), `Reflect.get`, `Reflect.has`, and `Reflect.ownKeys` logs — the five
  never cross-talk.
- Reuses the existing infrastructure unchanged: the Page-level `WatchRegistry`
  (config persists across `load()`), the M9-1 Inspector attach base + M9-3
  `breakProgram` pause, and the M9-3 pause-evidence path (the internal
  `devtools_pause_count` probe / `runMessageLoopOnPause` count — no `Debugger.paused`
  frame scanning). The only difference from M10-1 is that the wrapped function lives on
  `globalThis.Object.keys` rather than `Reflect.ownKeys`.
- Per generation, `install_page` rewraps `Object.keys` (guarded against
  double-wrapping) when watching is still enabled; a `load()` starts from a pristine
  `Object.keys`. `dispose()` / `stale` reuse the M1 error paths. `repeated load` keeps
  the configuration; a `failed load` does not roll back. `<script>` inert and all
  first-layer DOM / form / web-platform semantics are unchanged.

## Files changed

`src/core/page_state.cpp` (record helper + wrapper + hook installer + the two Page
methods + per-generation install), `include/iv8/watch_registry.h` (the fifth channel),
`include/iv8/page_state.h`, `src/binding/page_binding.cpp`, `python/iv8/page.py`
(facade + validation), `python/iv8/__init__.py` (docstring),
`test/test_watch_object_keys.py`, plus a test-only un-freeze of `watch_object_keys` in
three prior surface/contract guards.
