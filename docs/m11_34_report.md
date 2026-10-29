# M11-34 Delivery Report — CSS Selector Engine Expansion

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-29`)**

Branch `m11-34-selector-engine`, final verified commit `49f8582` (feature
commit `a8fe60d`).

## Evidence

- Linux `release-linux` run `40855935052` — cp311 / cp312 / cp313 / cp314,
  each `1565 passed, 2 skipped`.
- Windows `release-windows` run `40855940659` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1565 passed, 2 skipped`.
- `test/test_m11_34_selector_engine.py` ran in every lane (12 items). The full
  suite collects 1567 items; 1565 pass and 2 are skipped.
- The first attempt (`a8fe60d`) failed one refreshed guard because
  `c.closest('div.box')` correctly returns the ancestor `a`, not `c`; `49f8582`
  fixed the test expectation. The engine code was unchanged.

## Scope landed

- Shared selector engine for document/element `querySelector[All]`, `matches()`,
  and `closest()`.
- Selector lists with document order and de-duplication.
- Compound selectors (type / id / class / attribute combined).
- Type selectors (case-insensitive) and universal `*`.
- Attribute selectors: existence, `=`, `~=`, `|=`, `^=`, `$=`, `*=`;
  quoted/unquoted values; attribute-name case-insensitivity; value
  case-sensitivity; dedicated `id` / `class` presence/value handling.
- Combinators: descendant, child, adjacent sibling, general sibling.
- Element-scoped queries keep the project's subtree-includes-self semantics.
- Malformed/unsupported selectors keep stable empty/null/false behavior;
  pseudo-classes/elements, escapes, namespaces, and `i`/`s` flags are deferred.

See `docs/m11_34_selector_engine.md`.

## Verification notes

- Local `zig c++ -fsyntax-only` passed for `page_state.cpp`; the new/refreshed
  tests passed `py_compile`; 1567 tests collect locally.
- The V8 monolith cache was reused; both workflows completed from the warmed
  master cache.
- Historical selector guards in `test_matches.py`, `test_closest.py`,
  `test_element_queries.py`, and `test_query_collections.py` were refreshed in
  the same change.

## Outcome

M11-34 is verified and merged to fork `master`. The query/match surface now
supports a practical CSS selector subset; pseudo-classes are the next planned
selector slice (M11-35).
