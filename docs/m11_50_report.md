# M11-50 Delivery Report — Window scroll / focus / UI completion

Status: **VERIFIED — real V8 green on Linux + Windows + macOS arm64 (`2026-11-03`)**

Branch `m11-50-window-completion`, verified tree `6d2250e`.

This slice completes the deterministic `Window` surface that page scripts
commonly probe or call without layout, real focus, navigation, or OS window
management: scroll position metadata, no-op scroll/focus/move/resize/stop
methods, writable `window.status`, BarProp metadata, and the
`clientInformation` alias.

## Evidence

- Linux `release-linux` run `41597535300` — cp311 / cp312 / cp313 / cp314,
  each `1766 passed, 2 skipped`.
- Windows `release-windows` run `41597541345` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1766 passed, 2 skipped`.
- macOS arm64 fallback run `41598354469` (`macos-14`, cached V8 monolith,
  `macosx_11_0_arm64` wheels) — cp311–cp314, each `1766 passed, 2 skipped`.
  Every lane imported a V8-linked build reporting V8 `15.0.245.19`.

`test/test_m11_50_window_completion.py` ran in every lane (10 items, shown as
`..........` in the suite output). The full suite collects 1768 items; 1766 pass
and 2 are skipped.

macOS wheel artifacts: `13214030365` (cp311), `13213530903` (cp312),
`13213421172` (cp313), `13213540902` (cp314).

## Scope landed

- Read-only, access-traced `scrollX` / `scrollY` (always `0`) and the legacy
  `pageXOffset` / `pageYOffset` aliases.
- No-op `scroll` / `scrollTo` / `scrollBy` accepting numeric or options-object
  arguments; no-op `focus` / `blur` (with `document.hasFocus()` still `false`),
  `moveBy` / `moveTo` / `resizeBy` / `resizeTo`, and `stop`.
- Writable `window.status` with ordinary `ToString` coercion, reset on every
  `load()`, isolated per `Page`, and access-traced `get`/`set`.
- Read-only BarProp globals (`locationbar` / `menubar` / `personalbar` /
  `scrollbars` / `statusbar` / `toolbar`), each a fixed `{visible: true}`
  metadata object.
- Read-only `window.clientInformation` alias of `navigator`.
- All ten methods participate in the M9-2 `watch_apis` channel as
  `window.<name>` calls.
- No Python API or environment key change.

## Verification notes

- Local `zig c++ -fsyntax-only` passed against the real V8 headers; the only
  diagnostic was the known driver `FileNotFound` artifact.
- All three release lanes passed on the first attempt; no runtime or
  test-expectation fixes were needed.
- The macOS fallback used the cached V8 monolith on `macos-release` with
  deployment target 11.0; the branch was force-restored to `3f01d7c` afterward.
  The official `macos-15` lane remains unavailable.
- BarProp objects and `clientInformation` are plain metadata and are not part of
  the access-trace HostObject surface; the scroll coordinates and `status` are
  traced.
- Focus, scrolling, OS window management, event handler attributes,
  `postMessage`, and `getComputedStyle` remain out of scope and are documented
  in `docs/m11_50_window_completion.md` and `docs/dom_bom_gaps.md`.

## Outcome

M11-50 is verified and ready to merge to fork `master`. The next common-API
slice is M11-51: `document.write`/`writeln`/`open`/`close` plus
`importNode`/`adoptNode`.
