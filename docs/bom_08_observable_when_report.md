# BOM-08 implementation & local validation report

Date: `2026-11-26`

**Status: Implemented, Codex-reviewed, and locally validated on macOS arm64.**

## Scope delivered

- Added `EventTarget.prototype.when(type, options)` across every `EventTarget`
  in the hosted `Page` runtime, raising BOM subobject inventory coverage against
  `docs/browser_api_inventory.json` to **100%** across all audited BOM objects:
  - `navigator`: **70/70** secure (`38/70` insecure/opaque)
  - `navigator.connection`: **9/9** (`8/9 -> 9/9`)
  - `screen.orientation`: **9/9** (`8/9 -> 9/9`)
  - `visualViewport`: **14/14** (`13/14 -> 14/14`)
- Removed the temporary `PRES-03` own `window.when` `NotSupportedError` stub so
  `window.when === EventTarget.prototype.when` (`Object.hasOwn(window, "when")
  === false`, `owner_depth == 3`), matching Chrome's `Window -> WindowProperties
  -> EventTarget` prototype inheritance while preserving `window.when` call
  watch/trace attribution.
- Added native `Observable`, `Subscriber`, `AbortController`, and `AbortSignal`
  in all contexts (`src/core/browser/web/observable.{h,cpp}`):
  - `Observable.from` supporting `Observable` passthrough, async iterables, sync
    iterables, and `Promise`s.
  - All nineteen `Observable.prototype` methods (`catch`, `drop`, `every`,
    `filter`, `finally`, `find`, `first`, `flatMap`, `forEach`, `inspect`,
    `last`, `map`, `reduce`, `some`, `subscribe`, `switchMap`, `take`,
    `takeUntil`, `toArray`) with Chrome-matching prototype key ordering and
    non-constructible native shapes.
  - `AbortController` (`signal`, `abort(reason)`) and `AbortSignal` (`aborted`,
    `reason`, `onabort`, `throwIfAborted()`, static `abort`, `any`, `timeout`).
  - Connected `AbortSignal` brand validation and cancellation to
    `navigator.credentials.get`, `navigator.credentials.create`, and
    `navigator.locks.request`.

## Verification & Codex review fixes

- Browser references: `test/fixtures/bom_08/{chrome,chrome-insecure,chrome-opaque}.json`
  (Google Chrome `158.0.8127.88`, fresh headless profiles).
- Independent Codex review (`codex exec review --uncommitted`) — all seven
  technical findings verified against headless Chrome `158.0.8127.88` and fixed:
  - Tracked `switchMap`'s single active inner `AbortSignal` directly instead of
    an un-decremented counter on replacement, completing downstream as soon as
    both the outer stream and the latest inner stream complete, and matching
    Chrome's 1-based mapper index.
  - Registered `AbortSignal` cancellation on queued `navigator.locks.request`
    entries so aborting before grant removes the entry from `pending_` and
    rejects immediately with `signal.reason` without invoking its callback or
    disturbing already-granted locks.
  - Shared a single active `Subscriber` across concurrent `Observable.subscribe`
    observers (multicasting emissions, detaching individual observers when their
    own `signal` aborts, closing the producer only when the last observer
    detaches, and preserving Chrome's pre-aborted and re-entrant `subscribe`
    semantics).
  - Queued `flatMap` outer emissions sequentially while an inner `Observable` is
    active (matching Chrome's 1-based mapper index and draining queued inputs
    iteratively without deep recursion).
  - Closed async iterators via `iterator.return(reason)` when an
    `Observable.from(asyncIterable)` subscription terminates early (`take` or
    `signal` abort), while skipping `return()` on normal exhaustion or iterator
    errors.
  - Clamped `AbortSignal.timeout(ms)` delays to `INT32_MAX` before passing to
    `ContextState::register_timer` so values `>= 2^31` do not wrap negative and
    fire immediately at `0ms`.
  - Registered `inspect({abort})` on the subscriber signal prior to upstream
    subscription and wrapped `inspect`/`Subscriber` callbacks in `v8::TryCatch`
    reporting uncaught errors to `window`'s `'error'` event and async error
    diagnostics without interrupting teardowns or `AbortController.abort()`.
- Focused suite: `test/test_bom_08_observable_when.py` — **5 passed** (plus
  `test_presence_03_window.py`, `test_presence_03_window_monitoring.py`,
  `test_presence_03_window_errors.py`, and `test_bom_05_permission_objects.py`
  all green).
- Full macOS arm64 suite (Python 3.13 wheel): **3483 passed, 2 skipped**.
