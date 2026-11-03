# M11-52 — Window messaging (`postMessage` + `MessageEvent`)

Status: **IMPLEMENTED — real-V8 verification pending**

This slice continues the Window-first track with the single-context messaging
surface: `window.postMessage` and a `MessageEvent` constructor. It builds on the
M3-4 window event target, the M11-51 `onmessage` handler attribute, and the
M11-38 structured-clone implementation.

## Surface

```js
window.postMessage(message, targetOrigin?, transfer?);

new MessageEvent(type, {
  data, origin, lastEventId, source, ports
});
```

`postMessage` is a writable, enumerable, configurable global function.
`MessageEvent` inherits `Event` and is constructible only with `new`.

## Semantics

- `postMessage` requires at least one argument (`TypeError` otherwise).
- The message is cloned synchronously with the runtime's structured-clone
  implementation before it is scheduled. Mutating the original after the call
  does not affect the delivered event; uncloneable values (functions, symbols,
  the global object, and the existing structuredClone limitations) throw
  `TypeError` and schedule nothing.
- `targetOrigin` is bounded to the single-context window:
  - omitted / `undefined` behaves as `/` (the sender's own origin);
  - `*`, `/`, the page origin, and the page origin plus a trailing slash
    deliver;
  - any other value silently drops the message (no exception).
- `transfer` must be absent, `null`, or an empty array-like value. A non-empty
  transfer list throws `TypeError`; transferables/`MessagePort` are not
  implemented.
- Delivery is asynchronous and uses the existing timer queue: the message task
  runs on `Page.run_timers()` (or a facade turn that advances timers), **not**
  on `Page.run_jobs()`. Tasks are FIFO, and listeners/`onmessage` registered
  before the pump observe the event.
- The event is a real `MessageEvent` instance with:
  - `type === "message"`, `bubbles === false`, `cancelable === false`;
  - `data` equal to the clone;
  - `origin` equal to the page origin;
  - `lastEventId === ""`;
  - `source === window`;
  - `ports` a fresh empty array (copied, not a frozen `FrozenArray`).
- Dispatch uses the ordinary window event-target path: `addEventListener`
  listeners run in registration order, then the M11-51 `window.onmessage`
  handler; `event.target`, `currentTarget`, and `eventPhase` are set, and
  handler `return false` / listener cancellation work as for any window event.
- Overriding `structuredClone`, `MessageEvent`, or `setTimeout` on the page
  after bootstrap does not affect `postMessage`; the bootstrap captures the
  original functions.
- Calls are recorded by the M9-2 watch channel as `window.postMessage`.

## MessageEvent constructor

- Calls without `new` throw `TypeError`.
- `data` defaults to `null`, `origin` / `lastEventId` to `""`, `source` to
  `null`, and `ports` to a fresh empty array.
- `ports` is copied from the init array (element identity preserved), so later
  mutation of the init array does not change the event.
- `origin` / `lastEventId` use ordinary string conversion; `source` and `data`
  are stored as supplied.
- No cloning happens in the constructor; only `postMessage` clones.

## Explicit non-goals

- Iframes, workers, `MessageChannel`, `MessagePort`, transferables, or any
  real cross-window/cross-origin delivery. `source` is always the same window.
- Full target-origin URL parsing, opaque origins, and security enforcement
  beyond the bounded string comparison above.
- Frozen `ports`, structured-clone transfer lists, `postMessage(message,
  options)` / `windowPostMessageOptions`, and messageerror events.
- Automatic delivery through `run_jobs()` or a background loop.
- `window.open` / `window.close`, navigation, or opener-based messaging.
- Access-trace coverage of `postMessage` argument/return values; only the
  M9-2 watch hit is recorded.

## Tests

`test/test_m11_52_window_messaging.py` covers API shape and hidden-native
cleanup, MessageEvent defaults/fields/ports copying, async clone delivery,
event fields at dispatch time, the target-origin matrix, listener-then-handler
ordering with FIFO delivery, transfer/argument TypeErrors, uncloneable-message
rejection, captured originals surviving page overrides, and the watch hit.
