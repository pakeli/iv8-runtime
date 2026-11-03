# M11-50 — Window scroll / focus / UI completion

Status: **IMPLEMENTED — real-V8 verification pending**

This slice continues the object-oriented common-API track on `Window`. It adds
the remaining deterministic window surface that page scripts commonly probe or
call without requiring layout, real focus, navigation, or OS window management.
It follows M11-49 (HTML string APIs) and does not change the Document boundary.

## Surface

```js
window.scrollX;            // 0
window.scrollY;            // 0
window.pageXOffset;        // 0 (alias of scrollX)
window.pageYOffset;        // 0 (alias of scrollY)

window.scroll(x, y);       // no-op
window.scrollTo(x, y);     // no-op (also accepts {left, top, behavior})
window.scrollBy(dx, dy);   // no-op
window.focus();            // no-op
window.blur();             // no-op
window.moveBy(dx, dy);     // no-op
window.moveTo(x, y);       // no-op
window.resizeBy(dw, dh);   // no-op
window.resizeTo(w, h);     // no-op
window.stop();             // no-op

window.status;             // writable string, initially ""
window.locationbar;        // {visible: true}
window.menubar;            // {visible: true}
window.personalbar;        // {visible: true}
window.scrollbars;         // {visible: true}
window.statusbar;          // {visible: true}
window.toolbar;            // {visible: true}
window.clientInformation;  // === navigator
```

## Behavior

- `scrollX` / `scrollY` are read-only traced native accessors and always return
  `0`. The runtime has no layout engine, so there is no scrollable overflow and
  no virtual scroll position. `pageXOffset` / `pageYOffset` are read-only
  aliases with the same value and trace shape. Static `visualViewport` offsets
  remain `0`.
- `scroll` / `scrollTo` / `scrollBy` accept any arguments (numeric pairs or an
  options object) and return `undefined`; they never change the reported
  position. `scroll` and `scrollTo` are separate function objects.
- `focus` / `blur` are no-ops. `document.hasFocus()` remains `false` (M11-23)
  and no focus/blur events are fired: there is no focus or visibility
  machinery.
- `moveBy` / `moveTo` / `resizeBy` / `resizeTo` are no-ops. The configured
  `innerWidth` / `innerHeight` / `outerWidth` / `outerHeight` / `screenX` /
  `screenY` values do not change.
- `stop()` is a no-op; parsing is synchronous and there is no in-flight load to
  stop.
- `window.status` is a writable native accessor. Assignment uses ordinary
  `ToString` coercion (a coercion error leaves the stored value unchanged);
  reads return a string. It resets to `""` on every `load()` and is isolated
  per `Page`. `get`/`set` are access-traced like `window.name`.
- BarProp globals are fixed read-only objects with a read-only
  `visible: true` property. They are plain metadata, not HostObjects, and are
  not access-traced. The runtime reports a complete desktop browser window even
  though no browser UI exists.
- `window.clientInformation` is a read-only `DontDelete` alias of the same
  `navigator` object (`=== navigator`).
- All ten new methods participate in the M9-2 `watch_apis` channel as
  `window.<name>` calls.

## Explicit non-goals

- Any real scrolling, layout, scroll clamping, smooth-scroll animation,
  scroll events, or `visualViewport` scroll/zoom events.
- Focus/visibility machinery, `document.hasFocus()` changes, focus/blur events,
  or `activeElement` updates.
- OS window movement/resizing, real viewport geometry changes, or `resize`
  events.
- Navigation/loading control (`location` navigation, `window.open` / `close`,
  history), `postMessage` / `MessageEvent`, and `getComputedStyle`.
- Event handler attributes (`window.onload = ...` and friends); these remain
  plain properties and do not dispatch.
- A `Selection` implementation (`getSelection`), custom elements, or browser
  chrome behavior behind BarProp.
- Any new Python API or environment key.

## Tests

`test/test_m11_50_window_completion.py` covers the scroll metadata and method
arguments, focus/blur with unchanged `document.hasFocus()`, move/resize/stop,
status coercion/reset/per-Page isolation, BarProp shape and read-only behavior,
the `clientInformation` alias, access-trace records for the new scroll/status
members, and `watch_apis` hits for all ten methods.

No historical guard expects these members to be absent; the only intentional
change to existing behavior is that `window.status` now exists (it was an
ordinary global property before).
