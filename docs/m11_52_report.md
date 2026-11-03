# M11-52 Delivery Report — Window messaging (`postMessage` + `MessageEvent`)

Status: **VERIFIED — real V8 green on Linux + Windows + macOS arm64 (`2026-11-03`)**

Branch `m11-52-window-messaging`, verified tree `bc00040`.

This slice continues the Window-first track with single-context messaging:
`window.postMessage` with synchronous structured cloning, bounded target
origins, asynchronous FIFO timer delivery, and a `MessageEvent` subclass.

## Evidence

- Linux `release-linux` run `41620346617` — cp311 / cp312 / cp313 / cp314,
  each `1787 passed, 2 skipped`.
- Windows `release-windows` run `41620356968` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1787 passed, 2 skipped`.
- macOS arm64 fallback run `41621413284` (`macos-14`, cached V8 monolith,
  `macosx_11_0_arm64` wheels) — cp311–cp314, each `1787 passed, 2 skipped`.
  Every lane imported a V8-linked build reporting V8 `15.0.245.19`.

`test/test_m11_52_window_messaging.py` ran in every lane (10 items, shown as
`..........` in the suite output). The full suite collects 1789 items; 1787 pass
and 2 are skipped.

macOS wheel artifacts: `13223344457` (cp311), `13224275433` (cp312),
`13222934177` (cp313), `13224335094` (cp314).

## Scope landed

- `window.postMessage(message, targetOrigin?, transfer?)` as a writable,
  enumerable, configurable global function.
- Synchronous structured cloning at send time using the existing clone
  implementation; uncloneable values throw `TypeError` and schedule nothing.
- Bounded target origins: omitted/`undefined`, `*`, `/`, the page origin, and
  the page origin plus a trailing slash deliver; other values silently drop.
  Non-empty transfer lists throw `TypeError`.
- Asynchronous FIFO delivery through the timer queue: messages run on
  `Page.run_timers()` or a facade turn, not `Page.run_jobs()`. Delivery uses the
  ordinary window event path, so `addEventListener('message')` runs before the
  M11-51 `onmessage` handler and `eventPhase` / `currentTarget` are set.
- `MessageEvent` extends `Event` with `data` / `origin` / `lastEventId` /
  `source` / `ports`; constructor defaults, copied ports, and `new`-only
  construction.
- Delivered events report the page origin, `source === window`, an empty
  `ports` array, and `lastEventId === ""`.
- The bootstrap captures the original `structuredClone`, `MessageEvent`, and
  `setTimeout`, deletes its hidden natives, and keeps working after page
  overrides.
- Calls are watchable as `window.postMessage`.
- No Python API or environment key change.

## Verification notes

- All release lanes passed on the first attempt; no runtime or test-expectation
  fixes were needed.
- Local `zig c++ -fsyntax-only` passed against the real V8 headers; the only
  diagnostic was the known driver `FileNotFound` artifact.
- The macOS fallback used the cached V8 monolith on `macos-release` with
  deployment target 11.0; the branch was force-restored to `3f01d7c` afterward.
  The official `macos-15` lane remains unavailable.
- `postMessage` arguments/returns and `MessageEvent` field reads are not
  access-traced; only the M9-2 watch hit is recorded. This is documented in
  `docs/host_access_trace.md`.
- No iframes, workers, transferables/`MessagePort`, cross-window delivery,
  `postMessage(message, options)`, messageerror, or full target-origin parsing.

## Outcome

M11-52 is verified and ready to merge to fork `master`. The Window-first track
continues with M11-53: a deterministic focus/visibility model. Document
`write`/`importNode`/`adoptNode` remain lower priority.
