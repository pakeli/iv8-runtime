# M11-11 — Event Capture Phase + Listener Options

M11-11 is the eleventh implementation slice under the compatibility-first M11
line. It adds the capture phase, `eventPhase`, and the `capture` / `once` /
`passive` listener options to the existing flat Event / EventTarget model. It
does not add new event classes, default actions, `on*` handlers, or a new pump.

## Listener registration

`addEventListener(type, callback, options?)`:

- `type` is `String(...)`; a non-function callback is ignored (as before).
- `options` may be:
  - a boolean — `true` means `capture`, `false` means the default;
  - an object — truthy `capture` / `once` / `passive` fields are honored;
  - missing / other values — all options default to `false`.
- Duplicate identity is `(type, callback, capture)`. Registering the same
  callback twice with the same capture flag is a no-op; registering it with a
  different capture flag creates a second listener.
- `removeEventListener(type, callback, options?)` matches the same identity;
  only the `capture` field participates (per DOM semantics). Removing the last
  listener removes the internal entry.

`once` removes the listener before its first invocation (a reentrant dispatch
during the callback does not call it again).

`passive` suppresses `preventDefault()` for that listener's invocation only:
`defaultPrevented` stays unchanged and `dispatchEvent` still returns `true`
unless a non-passive listener cancels the event.

## Dispatch phases

`event.eventPhase` is an own numeric field:

| Value | Meaning |
|---|---|
| `0` | NONE — before and after dispatch |
| `1` | CAPTURING_PHASE |
| `2` | AT_TARGET |
| `3` | BUBBLING_PHASE |

For a connected element dispatch:

1. **Capture**: `window` → `document` → ancestors from the topmost root down
   to the target's parent (capture listeners only).
2. **At target**: the target's capture and bubble listeners, in **registration
   order** (both run with `eventPhase === 2`).
3. **Bubble**: ancestors from the target's parent up to `document` → `window`
   (bubble listeners only), only when `event.bubbles === true`.

Capture runs even for `bubbles: false` events. A detached subtree captures and
bubbles only within that subtree; it never reaches `document` / `window` (the
existing M4-A-7 connectivity rule).

For `document.dispatchEvent`: `window` capture → document at-target → window
bubble (when `bubbles`). For `window.dispatchEvent` and `FileReader`, the target
is the only target (capture and bubble listeners both run at target, in
registration order). Native lifecycle / FileReader events fired from C++ are
at-target only with `eventPhase === 2`.

`stopPropagation()` stops later hops (including later capture ancestors and the
target/bubble phases); remaining listeners on the current target still run.
`stopImmediatePropagation()` additionally stops remaining listeners on the
current target. Both flags are reset at the start of each dispatch, and
`eventPhase` returns to `0` when dispatch ends (even when stopped).

## Non-goals

M11-11 deliberately does **not** add:

- `on*` handler properties, event delegation APIs, or `event.composedPath()`;
- `AbortSignal` / `signal` listener options;
- static `Event.CAPTURING_PHASE` / `AT_TARGET` / `BUBBLING_PHASE` constants
  (use the numeric `eventPhase` values above);
- default actions, `click()` synthesis, browser task scheduling, or new event
  classes;
- changes to the four manual pumps or any Python API.

## Files changed

- `src/core/page_state.cpp` — listener options storage, capture/bubble phase
  dispatch, `eventPhase`, passive suppression, once removal;
- `test/test_event_capture.py` — focused acceptance tests;
- existing event tests that asserted `eventPhase` was absent are refreshed;
- `python/iv8/__init__.py` — module-docstring paragraph;
- docs: this boundary note, `docs/m11_11_report.md` (created at verification),
  `docs/current_status.md`, `docs/next_tasks.md`, `docs/decision_log.md`,
  `docs/runtime_surface.md`, `docs/dom_bom_gaps.md`, and
  `docs/m11_compatibility_backlog.md`.

## Verification

Status: **implementation pending; real-V8 CI verification to follow.**
