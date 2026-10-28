# M11-19..M11-23 — DOM / Form Completeness Batch

Five bounded slices developed on one branch and verified together against the
warmed master V8 cache. Each section freezes one milestone's scope. All five are
JS-side only: no new Python API and no architecture change.

## M11-19 — Multi-token class queries

- `document.getElementsByClassName(names)` now accepts whitespace-separated
  class tokens and matches elements containing **all** tokens. An empty or
  whitespace-only argument returns `[]`. Matching is case-sensitive; classes
  are the existing token set used by `className` / `setAttribute('class')`.
- `element.getElementsByClassName(names)` is added with the same token rules,
  scoped to the element's **descendants** (the element itself is not matched).
- Both return plain JS arrays in document order over the live tree; detached
  subtrees are excluded. No `HTMLCollection` / `NodeList` methods, no
  identity guarantee.

## M11-20 — `document.getElementsByName`

- `document.getElementsByName(name)` returns a plain array of elements in the
  live document with a `name` attribute exactly equal to `String(name)`
  (case-sensitive). Empty name is allowed.
- Document order; detached elements excluded. Only the named attribute is
  matched (no `id` fallback, no form-control filtering). No `NodeList`
  methods or identity guarantee.

## M11-21 — Form reflection extension

Extends the M11-18 reflected surface:

- `<input>`: `name` (string), `required` (boolean).
- `<textarea>`: `name`, `required`, `disabled`, `readOnly`, `placeholder`.
- `<select>`: `name`, `required`, `disabled`.
- `<button>`: `name`.

Reflection rules match M11-18: string setters write the named attribute;
boolean getters test attribute presence; truthy boolean setters write an empty
attribute and queue a record (setAttribute-style), falsey setters remove only
an existing attribute. `readOnly` maps to the `readonly` attribute. All writes
queue MutationObserver attribute records. No validation behavior, no
`input.name` on non-controls, no `button.required`.

## M11-22 — Select convenience

On `<select>` only:

- `length` — read-only number of descendant `<option>` elements.
- `item(index)` — option at the 0-based index, or `null` when out of range;
  a missing/non-numeric index converts through `ToInt32` and is out of range.
- `namedItem(name)` — first option in document order whose `id` or `name`
  attribute equals `String(name)`, or `null`.

No writable `length`, no `add` / `remove` option-collection methods, no
multi-select behavior, and no `HTMLOptionsCollection`.

## M11-23 — Document metadata

Read-only deterministic document metadata:

- `characterSet` = `"UTF-8"`;
- `contentType` = `"text/html"`;
- `compatMode` = `"CSS1Compat"`;
- `referrer` = `""`;
- `domain` = the base URL's hostname (empty when the URL has no host);
- `visibilityState` = `"visible"`, `hidden` = `false`;
- `activeElement` = the current `<body>`, or `null` when there is no body;
- `hasFocus()` = `false`.

Assignments to the properties are no-ops. No focus/blur behavior, no
visibility events, no cookie or referrer machinery, and no navigation.

## Verification

All five slices share this batch boundary. Real-V8 Linux + Windows CI will run
after implementation; the batch report is `docs/m11_19_23_report.md`.
