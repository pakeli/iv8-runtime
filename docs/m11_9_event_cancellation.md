# M11-9 — Event Cancellation (cancelable / preventDefault / stopImmediatePropagation)

M11-9 is the ninth implementation slice under the compatibility-first M11
line. It completes the flat event value model with cancellation and immediate
propagation stopping; the capture phase, default actions, and event listener
options remain out of scope.

The slice is part of the five-slice verification batch (M11-6 through M11-10).

## Public API

`new Event(type, init?)` now also exposes:

- **`cancelable`** — boolean from `init.cancelable` (truthy → `true`; a
  missing / non-object init → `false`).
- **`defaultPrevented`** — an ordinary own boolean field, initially `false`.
- **`preventDefault()`** — when `cancelable` is true, sets
  `defaultPrevented = true`; otherwise a no-op. Returns `undefined`.
- **`stopImmediatePropagation()`** — stops later listeners on the current
  target **and** all later targets. Returns `undefined`.

`stopPropagation()` keeps its M4-A-7 semantics: later targets are skipped, but
remaining listeners on the current target still run.

`dispatchEvent(event)` now returns `false` when a cancelable event has
`defaultPrevented === true` after dispatch, and `true` otherwise. This applies
to `window`, `document`, elements, and `FileReader`.

The `CustomEvent` / `MouseEvent` / `KeyboardEvent` / `FocusEvent` subclasses
inherit all of this through the shared `Event` setup.

## Propagation details

- `stopImmediatePropagation()` sets both an internal immediate-stop flag and the
  ordinary propagation-stop flag, so existing bubbling checks skip later
  targets without changes at every hop.
- `fire_at` checks the immediate-stop flag after each listener call and breaks
  out of the current target's listener snapshot. Listeners added after the
  snapshot never run in that dispatch.
- A throwing listener is still swallowed; it neither prevents later listeners
  (unless the event was explicitly stopped) nor changes `defaultPrevented`.
- `defaultPrevented` is a normal writable own data property, not a getter. This
  matches the runtime's existing writable-own-field Event model rather than the
  browser's accessor model.

## Non-goals

M11-9 deliberately does **not** add:

- the capture phase, `eventPhase`, `composed`, or `timeStamp`;
- default actions (`click`, submit, navigation, scrolling) or native event
  generation;
- `CustomEvent`-specific cancellation differences (none exist here) or new
  event classes;
- listener options (`once` / `capture` / `passive` / `signal`) or `on*`
  handler properties;
- a `DOMException` / `InvalidStateError` model;
- a Python API, a new pump, or DevTools `watch_apis` integration.

## Files changed

- `src/core/page_state.cpp` — cancelable field, cancellation methods, immediate
  propagation flag, `fire_at` break, and the `dispatchEvent` return value;
- `test/test_event_cancellation.py` — focused acceptance tests;
- `test/test_event_bubbling.py`, `test/test_event_subclasses.py`,
  `test/test_m4_a_contract.py` — refresh the old “cancellation absent”
  assertions;
- `python/iv8/__init__.py` — module-docstring paragraph;
- docs: this boundary note, `docs/m11_9_report.md` (created at verification),
  `docs/m11_6_event_subclasses.md` non-goal pointer,
  `docs/m11_compatibility_backlog.md`, `docs/dom_bom_gaps.md`,
  `docs/current_status.md`, `docs/next_tasks.md`, `docs/decision_log.md`, and
  `docs/runtime_surface.md`.

## Verification

Status: **implementation pending; real-V8 CI verification to follow in the
M11-6..M11-10 batch.**
