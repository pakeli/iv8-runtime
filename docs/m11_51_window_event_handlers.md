# M11-51 — Window event handler attributes

Status: **VERIFIED — real V8 green on Linux + Windows + macOS arm64 (`2026-11-03`)**

This slice continues the Window-first common-API track after M11-50. It adds a
bounded set of IDL event handler attributes on `window` (`window.onload = fn`,
etc.) backed by the existing event-target machinery. Document work is
deliberately deprioritized; `document.write` / import-adopt are now later than
the Window slices.

## Surface

The following own properties are native accessor properties on the global
`window` object:

```text
onload onunload onbeforeunload onerror onresize onscroll onfocus onblur
onhashchange onpopstate onmessage onstorage ononline onoffline
onpagehide onpageshow onunhandledrejection onrejectionhandled
```

```js
window.onload;            // null when unset
window.onload = fn;       // installs/replaces the handler
window.onload = null;     // removes it
window.onload = 42;       // non-callable -> null
```

## Semantics

- The getter returns the currently assigned function, or `null`.
- The setter accepts a function (stored as a V8 global handle) and clears the
  handler for `null`, `undefined`, or any other non-callable value. Assignment
  uses ordinary property-set behavior (strict mode does not throw for a
  non-function).
- A handler is a non-capture event listener. It runs after the regular
  `addEventListener` listeners for the same target and phase. It runs in the
  at-target phase (`eventPhase === 2`) and when an event bubbles to `window`
  (`eventPhase === 3`), but not during the capture phase.
- The callback receives the event object as its only argument; `this`,
  `event.target` (for window-targeted events), and `event.currentTarget` are the
  window/global object.
- Returning exactly `false` marks a cancelable event as `defaultPrevented`, so
  `dispatchEvent` returns `false`; a non-cancelable event is unaffected.
- Exceptions thrown by a handler are swallowed, like listener exceptions.
  `stopImmediatePropagation()` in an earlier listener suppresses the handler;
  `stopPropagation()` does not suppress same-target handlers.
- Handlers are generation-scoped: they are released at teardown and a new
  `load()` starts with every handler `null`. The bounded `onload` handler is
  therefore installed by an inline script before the M3-4 lifecycle `load`
  dispatch and runs during that dispatch.
- Only the listed names are accessor properties. Other names such as
  `window.onclick` remain ordinary global properties and are never invoked by
  the handler mechanism.

## Explicit non-goals

- Event handler content attributes in HTML (`<body onload="...">`).
- `document.on*` and element `on*` properties; this slice is Window-only.
- The legacy `window.onerror` five-argument signature and automatic `error` /
  `unhandledrejection` events. `onerror` is installed for symmetry and fires
  only when an `error` event is explicitly dispatched, receiving the Event
  argument like any other handler.
- Automatic `resize`, `scroll`, `focus`, `blur`, `message`, storage, online,
  navigation, or unload events. The corresponding runtime features are absent
  or no-op.
- Capture-phase handlers, exact browser handler-vs-listener ordering, handler
  `handleEvent` objects, and `on*` properties outside the documented list.
- Access-trace coverage of handler property reads/writes; those accessors are
  not part of the traced window metadata group.
- Any new Python API or environment key.

## Tests

`test/test_m11_51_window_event_handlers.py` covers initial null values,
assignment/replacement/clearing, inline `onload` execution after regular
listeners with `this`/target/phase checks, replacement and removal before
dispatch, at-target and bubbling handler invocation, `false` return
cancellation, swallowed exceptions, `stopImmediatePropagation` versus
`stopPropagation`, generation resets, and the bounded-name behavior
(`window.onclick` stays inert).

## Verification

Real V8 is green for tree `34b119b` on Linux, Windows, and macOS arm64; every
wheel lane reports `1777 passed, 2 skipped` and all 11 new tests execute. The
first attempt (`2924367`) stored handlers under `on<type>` while dispatch looked
them up by `<type>`; `34b119b` normalized the key. Run IDs, artifact IDs, and
fallback details are in `docs/m11_51_report.md`.
