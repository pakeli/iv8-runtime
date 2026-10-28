# M11-19..M11-23 Delivery Report — DOM / Form Completeness Batch

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-28`)**

Branch `m11-19-23-dom-form-batch`, final verified commit `999c851`.

## Evidence

- Linux `release-linux` run `40704330688` — cp311 / cp312 / cp313 / cp314,
  each `1412 passed, 2 skipped`.
- Windows `release-windows` run `40704362549` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1412 passed, 2 skipped`.
- All five new test files ran in every lane: 5 + 4 + 6 + 6 + 5 = 26 items.
  Full suite collects 1414 items; 1412 pass and 2 are skipped.
- The first attempt (`9f3e459`, Linux `40703509647` / Windows `40703534083`)
  built every wheel but failed one stale guard,
  `test_element_queries.py::test_shape_guard`, which still asserted the
  element-level `getElementsByClassName` was absent. `999c851` refreshed it;
  both reruns above are green.

## Scope landed

- **M11-19** — multi-token `getElementsByClassName` at document and element
  level; the element form queries descendants only.
- **M11-20** — `document.getElementsByName(name)`, exact name-attribute match,
  live plain array.
- **M11-21** — extended form reflection: input `name` / `required`, textarea
  `name` / `required` / `disabled` / `readOnly` / `placeholder`, select
  `name` / `required` / `disabled`, button `name`.
- **M11-22** — select `length` (read-only), `item(index)`, `namedItem(name)`.
- **M11-23** — deterministic document metadata: `characterSet` / `contentType`
  / `compatMode` / `referrer` / `domain` / `visibilityState` / `hidden` /
  `activeElement`, plus `hasFocus()`.

All five are JS-side only; no Python API or architecture change. See
`docs/m11_19_23_dom_form_batch.md` for the frozen boundaries and non-goals.

## Verification notes

- A local `zig c++ -fsyntax-only` pass against the real V8 headers was clean
  before pushing; all tests and the module docstring passed `py_compile`.
- The V8 monolith cache was reused; both workflows completed from the warmed
  master cache.
- Historical query/reflection guards were refreshed in the same change; the one
  missed guard was caught by the first CI run and fixed in `999c851`.

## Outcome

M11-19 through M11-23 are verified and merged to fork `master`. The next
compatibility slice (`M11-24`) should be chosen from
`docs/m11_compatibility_backlog.md` or another gap-inventory slice with an
explicit boundary.
