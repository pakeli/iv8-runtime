# M10-1 — `Page.watch_reflect_ownkeys` (minimal `Reflect.ownKeys` monitoring)

M10-1 adds a fourth, independent reflection-monitoring channel, mirroring the M9-4
(`Reflect.get`) and M9-5 (`Reflect.has`) lines, for **`Reflect.ownKeys(target)`**. It
is JS-side monitoring exposed as two Python `Page` methods; no new JS-visible global,
top-level object, or exception.

## Public API

- **`Page.watch_reflect_ownkeys(enabled=True, break_on_hit=False)`** — an on/off
  toggle (like `watch_reflect_get` / `watch_reflect_has`). When `enabled`, this
  page's `Reflect.ownKeys` is wrapped (per generation) with a recording hook that
  records each call and then delegates to the **real** `Reflect.ownKeys`, so behavior
  is unchanged. `break_on_hit` behaves exactly as elsewhere: with an **attached**
  DevTools session a hit pauses the Inspector (`breakProgram`), otherwise it records
  only — no pause, no error. Disabling stops recording; calling it again overrides the
  switch. Both arguments must be `bool` (else `TypeError`). Raises
  `JSContextDisposedError` after `dispose()`.
- **`Page.read_reflect_ownkeys_hits()`** — returns a `list[dict]` and **clears the log
  (read-and-clear)**. `Reflect.ownKeys(target)` has no property key, so each hit is
  `{"resource_name", "type": "Reflect.ownKeys"}` — **no `property` field**.
  `resource_name` reuses the existing script resource-name system (`<eval>`, HTML
  inline `"{base_url}#inline-script-{n}"`, or a host `scripts` name); `[]` if nothing
  was hit.

## Scope

- Monitors **only** `Reflect.ownKeys` calls. It does **not** attribute
  `Object.keys` / `Object.getOwnPropertyNames` / `Object.getOwnPropertySymbols` /
  `Object.defineProperty` / `Function.prototype.toString`, does not analyze
  Proxy/trap side effects, and takes no argument/return/stack snapshot. No wildcard /
  regex / filter, no `wrapNative` / `vconsole` / `vdebugger`, no `iframe` / `Worker`
  monitoring, no custom CDP domain, and no JS-visible watch API.

## Relationship to the existing surface

- The `Reflect.ownKeys` hit log is a **separate channel** from the host-method watch
  (`watch_apis`), `Reflect.get`, and `Reflect.has` logs — the four never cross-talk.
- Reuses the existing infrastructure unchanged: the Page-level `WatchRegistry`
  (config persists across `load()`), the M9-1 Inspector attach base + M9-3
  `breakProgram` pause, and the M9-3 pause-evidence path (V8 calls the client's
  `runMessageLoopOnPause` only when it stops; the internal `devtools_pause_count`
  probe is the reliable signal — no `Debugger.paused` frame scanning).
- Per generation, `install_page` rewraps `Reflect.ownKeys` (guarded against
  double-wrapping) when watching is still enabled; a `load()` starts from a pristine
  `Reflect.ownKeys`. `dispose()` / `stale` reuse the M1 error paths. `repeated load`
  keeps the configuration; a `failed load` does not roll back. `<script>` inert and
  all first-layer DOM / form / web-platform semantics are unchanged.

## Files changed

`src/core/page_state.cpp` (record helper + wrapper + hook installer + the two Page
methods + per-generation install), `include/iv8/watch_registry.h` (the fourth
channel), `include/iv8/page_state.h`, `src/binding/page_binding.cpp`,
`python/iv8/page.py` (facade + validation), `python/iv8/__init__.py` (docstring),
`test/test_watch_reflect_ownkeys.py`.
