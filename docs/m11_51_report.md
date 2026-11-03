# M11-51 Delivery Report — Window event handler attributes

Status: **VERIFIED — real V8 green on Linux + Windows + macOS arm64 (`2026-11-03`)**

Branch `m11-51-window-event-handlers`, verified tree `34b119b`.

This slice starts the Window-first re-prioritization: Window work now precedes
Document continuation. It adds a bounded set of IDL `window.on*` handler
attributes on top of the existing event-target machinery.

## Evidence

- Linux `release-linux` run `41612087122` — cp311 / cp312 / cp313 / cp314,
  each `1777 passed, 2 skipped`.
- Windows `release-windows` run `41612092772` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1777 passed, 2 skipped`.
- macOS arm64 fallback run `41613300180` (`macos-14`, cached V8 monolith,
  `macosx_11_0_arm64` wheels) — cp311–cp314, each `1777 passed, 2 skipped`.
  Every lane imported a V8-linked build reporting V8 `15.0.245.19`.

`test/test_m11_51_window_event_handlers.py` ran in every lane (11 items, shown
as `...........` in the suite output). The full suite collects 1779 items; 1777
pass and 2 are skipped.

macOS wheel artifacts: `13219698134` (cp311), `13219987070` (cp312),
`13220615347` (cp313), `13219089638` (cp314).

## Scope landed

- Native accessor properties on `window` for a bounded handler set:
  `onload`, `onunload`, `onbeforeunload`, `onerror`, `onresize`, `onscroll`,
  `onfocus`, `onblur`, `onhashchange`, `onpopstate`, `onmessage`, `onstorage`,
  `ononline`, `onoffline`, `onpagehide`, `onpageshow`,
  `onunhandledrejection`, and `onrejectionhandled`.
- Function-or-null getters/setters: assigning a function installs/replaces the
  handler; `null`, `undefined`, or any non-callable clears it.
- Handlers run after regular `addEventListener` listeners for the same
  target/phase, at-target and while bubbling to `window`, with `this ===
  window` and the Event argument.
- Returning exactly `false` marks a cancelable event as `defaultPrevented`;
  handler exceptions are swallowed; `stopImmediatePropagation` suppresses the
  handler while `stopPropagation` does not.
- Handlers are generation-scoped and released with the ordinary listeners.
- Only the listed names are accessors; `window.onclick` and other unlisted
  names remain ordinary global properties.
- No Python API or environment key change.

## Verification notes

- The first attempt (`2924367`) failed 6 of the 11 new tests because handlers
  were stored under the accessor name (`onload`) while dispatch looked them up
  by the bare event type (`load`). `34b119b` normalized the key at the
  get/set boundary; all lanes then passed with no test changes.
- Local `zig c++ -fsyntax-only` passed against the real V8 headers; the only
  diagnostic was the known driver `FileNotFound` artifact.
- The macOS fallback used the cached V8 monolith on `macos-release` with
  deployment target 11.0; the branch was force-restored to `3f01d7c` afterward.
  The official `macos-15` lane remains unavailable.
- Handler property reads/writes and invocation are not access-traced; this is
  documented in `docs/host_access_trace.md`.
- Automatic `error` / `unhandledrejection` events and the legacy `onerror`
  five-argument signature remain absent, as do document/element handler
  attributes and unlisted `on*` names.

## Outcome

M11-51 is verified and ready to merge to fork `master`. The Window-first track
continues with M11-52: a `postMessage` / `MessageEvent` design for the
single-context window, or a deterministic focus/visibility model. Document
`write`/`importNode`/`adoptNode` remain lower priority.
