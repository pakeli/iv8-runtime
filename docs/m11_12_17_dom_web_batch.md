# M11-12..M11-17 — DOM / Web Platform Completeness Batch

Six bounded slices developed on one branch and verified together against the
warmed master V8 cache. Each section freezes one milestone's scope.

## M11-12 — Element attribute API

- `element.id = value` and `element.className = value` become writable
  property setters. They update the dedicated id/class node fields, keep
  `getAttribute` / class queries consistent, and queue the same
  `attributes` MutationObserver record as `setAttribute`.
- `element.hasAttributes()` — true when id, class, or any raw attribute is
  present.
- `element.getAttributeNames()` — plain JS `Array`. Order: `"id"` then
  `"class"` when present, followed by the remaining raw attribute names in
  ASCII-sorted order (the runtime does not preserve source attribute order).
- No `attributes` NamedNodeMap, no attribute reflection properties, no
  namespace variants.

## M11-13 — Tree editing convenience (minimal)

- `parent.replaceChild(newChild, oldChild)` — replaces a direct child with an
  element node; validates parent/child relationships and rejects inserting a
  node into its own subtree. Queues MutationObserver childList records for the
  removal and addition (and for moving `newChild` out of an old parent).
- `element.remove()` — detaches from its parent when present; queues a removal
  record on the old parent.
- `element.hasChildNodes()` — children non-empty.
- Raw Node aliases on elements: `firstChild` / `lastChild` /
  `previousSibling` / `nextSibling`. Because the tree is element-only, they are
  the element variants (no text/comment nodes).
- **Deferred:** `append` / `prepend` / `before` / `after` / `replaceWith`
  (their string-to-text-node semantics need a text-node model).

## M11-14 — cloneNode / node equality

- `element.cloneNode(deep?)` — returns a detached clone (never linked into the
  document tree). Copies tag/id/class fields, raw attributes, `textContent`,
  and the form-control runtime slots (`value` / `checked` / `selected` and the
  form metadata / reset baselines). With `deep` truthy, recursively clones
  descendants and parent links within the detached subtree.
- `element.isSameNode(other)` — true only for the same element backing node.
- `element.isEqualNode(other)` — structural equality over tag, id/class fields,
  raw attributes, textContent, form-control slots, and children recursively.
- No `importNode` / `adoptNode`, no template cloning, no wrapper-identity
  guarantee.

## M11-15 — URL.searchParams (snapshot)

- `new URL(...).searchParams` returns a `URLSearchParams` built from the URL's
  `search` at construction time.
- It is a **snapshot**: mutating it does NOT update the URL (no live link in
  this slice). URL's existing fields stay read-only.
- Deferred: URL writable fields (`protocol` / `host` / `pathname` / `search` /
  `hash` setters), `canParse`, `port`, and live `searchParams` linkage.

## M11-16 — select extension

On `<select>`:

- `options` — plain JS `Array` of descendant `<option>` elements in document
  order.
- `selectedIndex` — read/write index of the first selected option, `-1` when
  none. Writing selects that option and clears every other option in the
  select; `-1` clears all. Out-of-range writes are ignored.
- `multiple` — read-only boolean reflecting the presence of the `multiple`
  attribute. Selection behavior stays single-select in this slice.
- `size` — read-only integer from the `size` attribute (0 when absent or
  non-numeric).

No `HTMLOptionsCollection`, `select.length`, `add` / `remove` collection
methods, or multi-select behavior.

## M11-17 — Nested environment + get_defaults()

- `Page(environment=...)` additionally accepts a nested mapping:

  ```python
  {
    "navigator": {"userAgent": ..., "platform": ..., "language": ..., "webdriver": ...},
    "screen": {"width": ..., "height": ..., "availWidth": ..., "availHeight": ...},
    "window": {"devicePixelRatio": ..., "innerWidth": ..., "innerHeight": ...},
  }
  ```

  It is flattened to the existing 11-key native environment; the flat form
  remains accepted. Mixing nested and flat keys in one mapping raises
  `TypeError`; unknown nested keys/types raise `TypeError`.
- `Page.get_defaults()` returns a fresh nested dict with the same three
  sections. `get_default_environment()` remains the flat template.
- WebGL / nested `location` / canvas stay out of scope.

## Verification

All six slices share `test/test_m11_12_17_batch.py`. Real-V8 CI will run the
full suite on Linux + Windows; the batch report is
`docs/m11_12_17_report.md`.
