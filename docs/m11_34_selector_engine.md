# M11-34 — CSS selector engine expansion

Status: **IN PROGRESS — implementation and real-V8 verification pending
(`2026-10-29`)**

M11-34 replaces the M2-era single-token selector matcher with a small CSS
Selectors engine shared by `querySelector`, `querySelectorAll`, `matches`, and
`closest`. It is JS-side only and adds no Python API.

## Supported syntax

- **Selector lists** — `h1, h2, .title`; results are de-duplicated and collected
  in document order.
- **Compound selectors** — `div.card#main[data-kind=container]` (type / id /
  class / attributes together).
- **Type selectors** — ASCII case-insensitive (`DIV` matches `div`); `*` matches
  every element.
- **IDs / classes** — `#id`, `.class`, multiple classes (`div.a.b`).
- **Attribute selectors**:
  - `[attr]`
  - `[attr=value]`
  - `[attr~=value]` (whitespace-separated token)
  - `[attr|=value]` (exact or `value-...`)
  - `[attr^=value]` / `[attr$=value]` / `[attr*=value]`
  - quoted values (single or double quotes) and unquoted values;
  - attribute **names** are ASCII case-insensitive; attribute **values** are
    case-sensitive; `[id]` / `[class]` consult the dedicated element fields.
- **Combinators**:
  - descendant: `div span`
  - child: `div > span`
  - adjacent sibling: `p + span`
  - general sibling: `p ~ span`
- **`matches` / `closest`** accept the same grammar. `closest` remains
  self-first; `matches` evaluates combinators against the live tree.

`document.querySelector[All]` searches the whole tree; element-level
`querySelector[All]` searches the element plus its descendants (the project's
existing subtree semantics, so the root itself may match). `getElementsByTagName`
is unchanged (it already accepts `*`).

## Matching rules

- Matching is over the live element-only tree; detached subtrees still support
  combinators through their current parent/sibling links.
- Attribute existence uses the dedicated `id` / `class` presence flags and the
  raw attribute table for everything else.
- Empty attribute values follow the CSS rules: `[attr=""]` can match an empty
  value; `^=` / `$=` / `*=` with an empty search value match nothing;
  `~=` with an empty/whitespace-containing value matches nothing.
- A malformed or unsupported selector matches nothing: `querySelectorAll` returns
  `[]`, `querySelector` returns `null`, `matches` returns `false`, `closest`
  returns `null` — no syntax error. This preserves the historical stable-empty
  behavior.

## Non-goals (M11-34)

- Pseudo-classes and pseudo-elements (`:first-child`, `:nth-child`, `:not`,
  `:checked`, `:hover`, `::before`, …) — planned for M11-35.
- CSS escapes, namespaces (`svg|a`), case-insensitivity flags (`[attr=value i]`),
  CSS comments, and the full Selectors Level 4 grammar (`:is` / `:where` /
  `:has`).
- `HTMLCollection` / `NodeList` types, live collection objects, selector
  specificity, or style/CSSOM integration.
- Styling, layout, or pseudo-element generation; the engine only matches nodes.

## Tests

`test/test_m11_34_selector_engine.py` covers lists, document order/dedup,
descendant/child/adjacent/general-sibling combinators, compound selectors, every
attribute operator, quoted values, name/value case rules, the universal selector,
`matches`/`closest`, element-scoped queries, live tree updates, detached
subtrees, and malformed/unsupported selectors. Historical guards in
`test_matches.py`, `test_closest.py`, `test_element_queries.py`, and
`test_query_collections.py` are refreshed in this change.

## Verification

Real-V8 Linux + Windows CI will run after implementation. This section will be
replaced with the run IDs and per-lane pytest summaries once green.
