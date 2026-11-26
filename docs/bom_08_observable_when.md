# BOM-08 — Observable, Subscriber, AbortController/AbortSignal, and EventTarget.prototype.when

Selected on `2026-11-26` after BOM-07 completion (`3e8c198`). Implements native
`Observable`, `Subscriber`, `AbortController`, `AbortSignal`, and
`EventTarget.prototype.when`, bringing the remaining three BOM event-target
subobjects in `docs/browser_api_inventory.json` to full `100%` coverage:

- `navigator.connection`: `8/9 -> 9/9`
- `screen.orientation`: `8/9 -> 9/9`
- `visualViewport`: `13/14 -> 14/14`

## Contract update (supersedes PRES-03 `window.when` placeholder)

- In PRES-03 (`docs/window_presence.md` / `test/test_presence_03_window.py` /
  `test/test_presence_03_window_monitoring.py`), `when` was temporarily installed
  as an own `window` stub throwing `NotSupportedError` because `Observable` and
  `EventTarget.prototype.when` were out of scope for PRES-03.
- Per Chrome `157.0.8103.51` (`test/fixtures/presence_03_window/chrome.json`,
  `owner_depth: 3`) and Chrome `158.0.8127.88` (`test/fixtures/bom_08/chrome.json`),
  `window.when` is not an own property on `window` or `Window.prototype`; it is
  inherited from `EventTarget.prototype.when` (`window.when === EventTarget.prototype.when`)
  and returns an `Observable`.
- BOM-08 removes the temporary own-property stub in
  `src/core/browser/window/window_presence_functions.cpp`, installs `when` on
  `EventTarget.prototype`, preserves `window.when` read/call monitoring in
  `access_trace` and `watch_apis`, and updates the two PRES-03 `when('click')`
  expectations accordingly.

## Boundary to implement against browser evidence

- `AbortController` (constructible, `length = 0`, all contexts) and `AbortSignal`
  (non-constructible `EventTarget`, `length = 0`, all contexts):
  - `AbortController.prototype`: `[SameObject]` `signal`, `abort(reason?)`
    (default reason `DOMException("AbortError", "signal is aborted without reason")`).
  - `AbortSignal` statics: `abort(reason?)`, `any(signals)`, `timeout(ms)`
    (backed by the deterministic Page timer queue).
  - `AbortSignal.prototype`: `aborted`, `reason`, `onabort`, `throwIfAborted()`.
- `Observable` (constructible `new Observable(callback)`, `length = 1`, all
  contexts) and `Subscriber` (non-constructible, `length = 0`, all contexts):
  - `Observable.from(value)` (`length = 1`): returns `Observable` instances
    unchanged and converts async iterables, sync iterables, and Promises.
  - `Observable.prototype` exposes all 19 Chrome 158 methods in reference key
    order: `catch`, `drop`, `every`, `filter`, `finally`, `find`, `first`,
    `flatMap`, `forEach`, `inspect`, `last`, `map`, `reduce`, `some`,
    `subscribe`, `switchMap`, `take`, `takeUntil`, `toArray`.
  - `Subscriber.prototype` exposes `active`, `[SameObject]` `signal`
    (`AbortSignal`), `addTeardown`, `complete`, `error`, `next`; closing a
    subscription sets `active = false`, aborts `signal`, and runs LIFO teardowns
    before invoking terminal observer callbacks.
- `EventTarget.prototype.when(type, options?)` (`length = 1`):
  - Validates `type` (`DOMString`) and optional `ObservableEventListenerOptions`
    (`{passive}`).
  - Returns an `Observable` backed by native `EventTargetHost` listener
    registration/removal (immune to script mutation of `addEventListener` /
    `removeEventListener`) and automatically unsubscribes on teardown or abort.
