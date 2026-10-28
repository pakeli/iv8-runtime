# M11-12..M11-17 Delivery Report — DOM / Web Platform Completeness Batch

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-28`)**

Branch `m11-12-17-dom-web-batch`, final verified commit `8d606e9`.

## Evidence

- Linux `release-linux` run `40695004440` — cp311 / cp312 / cp313 / cp314,
  each `1378 passed, 2 skipped`.
- Windows `release-windows` run `40695029530` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1378 passed, 2 skipped`.
- `test/test_m11_12_17_batch.py` ran in every lane (8 items).
- Full suite collects 1380 items; 1378 pass and 2 are skipped. 1378 is the
  previous 1370 baseline plus the 8 new items.
- The first verification attempt (`0c47ecb`, Linux `40693054124` / Windows
  `40693059780`) built every wheel successfully but failed 15 historical
  frozen-surface guards. Commit `8d606e9` refreshes those guards and the
  MutationObserver id/className expectation; the reruns above are the verified
  result.

## Scope landed

- **M11-12** — writable `id` / `className` property setters,
  `hasAttributes()`, and `getAttributeNames()`.
- **M11-13** — `replaceChild()`, `remove()`, `hasChildNodes()`, and the raw
  `firstChild` / `lastChild` / `previousSibling` / `nextSibling` aliases on the
  element-only tree.
- **M11-14** — detached `cloneNode(deep?)`, `isSameNode()`, and structural
  `isEqualNode()`.
- **M11-15** — `URL.searchParams` as a non-live `URLSearchParams` snapshot.
- **M11-16** — `<select>.options`, writable `selectedIndex`, and read-only
  `multiple` / `size`.
- **M11-17** — nested `Page(environment=...)` sections flattened to the native
  eleven-key environment, plus `Page.get_defaults()`.

See `docs/m11_12_17_dom_web_batch.md` for the frozen boundaries and non-goals.

## Verification notes

- The V8 monolith cache was reused; the Linux and Windows wheel jobs completed
  from the warmed master cache.
- The fix commit changed only tests and test comments; no C++ or Python runtime
  code changed after the first wheel build.
- Recurring maintenance rule: a surface-expanding slice must refresh the
  historical "member must be `undefined`" guards in the same change. Grep the
  new member names under `test/` before dispatching CI.
- A local `zig c++ -fsyntax-only` pass against the real V8 headers was clean
  before the first verification run.

## Outcome

M11-12 through M11-17 are verified and merged to fork `master`. The next
compatibility slice (`M11-18`) should be chosen from
`docs/m11_compatibility_backlog.md`.
