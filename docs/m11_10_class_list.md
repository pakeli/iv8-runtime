# M11-10 — classList / toggleAttribute

M11-10 is the tenth implementation slice under the compatibility-first M11
line. It adds the `element.classList` token-list surface and
`element.toggleAttribute(name, force?)`, both backed by the existing class
attribute fields and MutationObserver attribute hooks. It adds no Python API,
no new pump, and no new node type.

The slice is part of the five-slice verification batch (M11-6 through M11-10).

## `element.classList`

`element.classList` returns a fresh plain JS object per access (the runtime's
usual no-identity-guarantee model). It is a minimal DOMTokenList-like surface:

- **`contains(token)`** — boolean.
- **`add(...tokens)`** — appends absent tokens; duplicates are ignored.
- **`remove(...tokens)`** — removes matching tokens.
- **`toggle(token, force?)`** — toggles, or applies explicit `force`; returns
  the resulting presence.
- **`replace(oldToken, newToken)`** — replaces `oldToken`; returns `false` when
  it is absent, `true` otherwise; if `newToken` already exists the old token is
  removed and the list is deduped.
- **`item(index)`** — token at `index`, or `null` when out of range.
- **`length`** — read-only live token count.
- **`value`** — read-write class string; writing splits on whitespace and keeps
  the class attribute present (an empty write yields `class=""`).

Rules:

- Tokens use `String(...)` then must be non-empty and whitespace-free; an empty
  or whitespace-containing token throws a JS `TypeError` (this runtime has no
  `DOMException`). This is a deliberate simplification.
- Tokens are stored in a `DomNode::classes` vector; class membership uses the
  existing tokenizer/`.class` selector semantics. No case folding.
- Removing the last token removes the class attribute (`className` becomes
  `""`, `hasAttribute("class")` becomes `false`).
- Adding / removing / replacing / toggling queue the same `attributes` /
  `attributeName: "class"` MutationObserver record as `setAttribute` /
  `removeAttribute`, including `oldValue` when requested.
- The class list object is a plain object; it is **not** an instance of a
  `DOMTokenList` constructor, and there is no module/global `DOMTokenList`.

## `element.toggleAttribute(name, force?)`

- `name` is `String(...)`-converted and ASCII-lowercased.
- With no `force`: an absent attribute is added with value `""`; a present
  attribute is removed. Returns the resulting presence (`true` / `false`).
- With `force`: `true` ensures presence, `false` ensures absence; the return is
  the resulting presence. No change is made (and no MutationObserver record is
  queued) when presence already matches.
- `id` / `class` keep their dedicated node fields and query behavior; other
  names live in the raw attribute table.
- Queues the same `attributes` record as the other attribute writers, with
  `oldValue` when requested and the attribute existed.

## Generation and lifetime

- Bindings are owned by the current generation's `DocumentHost`; `Page.load()`
  discards them with the rest of the generation.
- No V8 persistent handles are retained; nothing needs teardown beyond the
  generation.

## Non-goals

M11-10 deliberately does **not** add:

- a `DOMTokenList` constructor/global, `classList.supports()`, `relList`, or
  identity guarantees across accesses;
- attribute serialization, attribute order, `NamedNodeMap`, `dataset`,
  `classList` on `document`, or SVG/MathML namespace behavior;
- `classList.value` normalization beyond the token split (raw attribute text is
  preserved until the next token mutation);
- automatic CSS/layout effects — class changes update DOM queries only;
- a Python API, a new pump, or DevTools `watch_apis` integration.

## Files changed

- `src/core/page_state.cpp` — classList object/bindings, token operations,
  `length` / `value` accessors, `toggleAttribute`, MutationObserver hooks;
- `test/test_class_list.py` — focused acceptance tests;
- `python/iv8/__init__.py` — module-docstring paragraph;
- docs: this boundary note, `docs/m11_10_report.md` (created at verification),
  `docs/m11_compatibility_backlog.md`, `docs/dom_bom_gaps.md`,
  `docs/current_status.md`, `docs/next_tasks.md`, `docs/decision_log.md`, and
  `docs/runtime_surface.md`.

## Verification

Status: **implementation pending; real-V8 CI verification to follow in the
M11-6..M11-10 batch.**
