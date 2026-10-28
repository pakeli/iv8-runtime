# M11-6 — Event Subclass Cluster (manual value objects)

M11-6 is the sixth implementation slice under the compatibility-first M11 line.
It adds four minimal event value constructors — `CustomEvent`, `MouseEvent`,
`KeyboardEvent`, and `FocusEvent` — on top of the existing `Event` /
`EventTarget` / bubbling model. It adds no Python API, no new pump, and no
capture phase, cancellation, or native event generation.

The slice was selected from `docs/m11_compatibility_backlog.md` (rank 2:
"Event subclass cluster"); the target community edition claims 80+ event types
and its demos use click and lifecycle events.

## Public API

Installed per generation by `install_page` as globals:

- **`new CustomEvent(type, init?)`**
- **`new MouseEvent(type, init?)`**
- **`new KeyboardEvent(type, init?)`**
- **`new FocusEvent(type, init?)`**

Shared construction rules:

- Calling any of the four without `new` throws a JS `TypeError`.
- `type` is `String(firstArgument)`; a missing first argument yields `""`.
- `bubbles` is read from `init.bubbles` exactly like `Event` (truthy → `true`;
  a missing / non-object `init` → `false`).
- `target` / `currentTarget` start as `null`, and `stopPropagation()` is
  present, because each constructor initializes the object through the existing
  `Event` setup.
- `window.X === X` and `globalThis.X === X` hold for each constructor.
- **No new Python surface.** There is no module-level or `Page` method, and no
  `EventTarget` API change.

## Prototype chain

- `X.prototype` inherits `Event.prototype`, so
  `new X(type) instanceof X` and `new X(type) instanceof Event` are both true.
- The four subclasses are independent: `new CustomEvent('x') instanceof
  MouseEvent` is false.
- `Event` itself is unchanged: it remains a standalone constructor with the
  same fields (`type` / `bubbles` / `target` / `currentTarget`) and the same
  writable-own-property behavior.

## Frozen fields

All subclass-specific fields are **own read-only data properties** created by
the constructor. A sloppy-mode assignment is a silent no-op; a strict-mode
assignment throws `TypeError`.

| Constructor | Field | Source | Default | Conversion |
|---|---|---|---|---|
| `CustomEvent` | `detail` | `init.detail` | `null` | stored as-is (no copying); absent / `undefined` → `null` |
| `MouseEvent` | `clientX` | `init.clientX` | `0` | `ToNumber`; `NaN` / `±Infinity` → `0`; truncated toward zero |
| `MouseEvent` | `clientY` | `init.clientY` | `0` | same |
| `MouseEvent` | `button` | `init.button` | `0` | same |
| `MouseEvent` | `buttons` | `init.buttons` | `0` | same |
| `KeyboardEvent` | `key` | `init.key` | `""` | `ToString`; absent / `undefined` → `""` |
| `KeyboardEvent` | `code` | `init.code` | `""` | same |
| `KeyboardEvent` | `repeat` | `init.repeat` | `false` | `Boolean` |
| `FocusEvent` | `relatedTarget` | `init.relatedTarget` | `null` | stored as-is (not validated); absent / `undefined` → `null` |

Additional rules:

- A missing / non-object `init` (including `null`) yields all defaults; it does
  not throw.
- A throwing `ToString` / `ToNumber` conversion (for example `Symbol()`)
  propagates that error and aborts construction.
- `detail` object identity is preserved: `e.detail === passedObject` holds.
- `relatedTarget` is deliberately **not** validated as an `EventTarget`; it can
  hold an element, `document`, `window`, `null`, or any other JS value.
- Numeric fields do **not** emulate WebIDL 32-bit wrap-around; they are finite
  doubles truncated toward zero.

## Dispatch and lifecycle

- Subclass instances are plain JS event values dispatched through the existing
  `addEventListener` / `removeEventListener` / `dispatchEvent` on `window`,
  `document`, and elements.
- Bubbling and `stopPropagation()` use the existing `event.bubbles` /
  `stopPropagation()` machinery; no dispatch code changes are required.
- Constructors create values only. There is **no** automatic generation of
  `click`, `keydown`, `focus`, `blur`, or other native events, and no default
  actions.
- Constructors are installed per generation. `Page.load()` discards the old
  generation's constructors and instances like any other JS value; there is no
  cross-generation state.
- No native persistence handles or finalizer concerns are introduced: these are
  ordinary JS objects.

## Non-goals

M11-6 deliberately does **not** add:

- capture phase, `eventPhase`, `composed`, `timeStamp`, `isTrusted`
  (cancellation itself is added by M11-9:
  `docs/m11_9_event_cancellation.md`);
- listener options (`once` / `capture` / `passive` / `signal`) or `on*` handler
  properties;
- native event generation, default actions, or target validation;
- any additional event class (`UIEvent`, `InputEvent`, `WheelEvent`,
  `BeforeUnloadEvent`, …);
- `screenX` / `screenY` / `pageX` / `pageY` / `movementX` / `movementY`;
- keyboard modifier booleans (`ctrlKey` / `shiftKey` / `altKey` / `metaKey`),
  `keyCode` / `which` / `location` / `isComposing`;
- changes to `Event` itself (its fields stay writable, and the historical
  `Event` constructor behavior is untouched);
- a Python API, a new pump, DevTools `watch_apis` integration, or changes to
  `MutationObserver`, forms, or the DOM tree.

## Files changed

- `src/core/page_state.cpp` — four constructors, shared init-field helpers, and
  per-generation global installation;
- `test/test_event_subclasses.py` — focused acceptance tests;
- `python/iv8/__init__.py` — module-docstring paragraph;
- docs: this boundary note, `docs/m11_6_report.md` (created at verification),
  `docs/m11_compatibility_backlog.md`, `docs/dom_bom_gaps.md`,
  `docs/current_status.md`, `docs/next_tasks.md`, `docs/decision_log.md`, and
  `docs/runtime_surface.md`.

`JSContext`, the native `ContextState` API, and `Page` are unchanged.

## Verification

Status: **implementation pending; real-V8 CI verification to follow.**

- Focused tests: `test/test_event_subclasses.py`.
- Real-V8 CI uses `.github/workflows/release-linux.yml` and
  `.github/workflows/release-windows.yml`; `ci.yml` is the skeleton shape-guard
  lane.
- Expected total after the slice: the M11-5 baseline (`1265 passed, 2 skipped`)
  plus the new test file's item count, identical on both platforms.
