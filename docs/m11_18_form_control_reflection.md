# M11-18 — Form control attribute reflection (minimal)

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-28`)**

M11-18 is a bounded follow-up to M11-12 (attribute API) and the M5–M7 form
layers. It adds a small set of reflected properties to form controls so common
property-level code can read and write the underlying attributes without
changing any existing runtime slot or form behavior.

## Scope

Only these properties are added:

| Element | Property | Reflection |
|---|---|---|
| `<input>` | `type` | `type` attribute; normalized through the known input-type set; default `"text"` |
| `<input>` | `disabled` | presence of `disabled` |
| `<input>` | `readOnly` | presence of `readonly` |
| `<input>` | `placeholder` | `placeholder` attribute; default `""` |
| `<button>` | `type` | `type` attribute; normalized to `submit` / `reset` / `button`; default `"submit"` |
| `<button>` | `disabled` | presence of `disabled` |
| `<form>` | `encoding` | alias of the existing runtime `enctype` slot (normalized like `.enctype`) |
| `<option>` | `label` | `label` attribute; falls back to the option's text |
| `<option>` | `index` | read-only 0-based position among the nearest ancestor `<select>`'s options |

### Reflection rules

- `input.type` getter: ASCII-lowercases the attribute value; returns it only if
  it is a known HTML input type, otherwise `"text"` (including when absent).
  Setter writes the string verbatim to the `type` attribute.
- `button.type` getter: ASCII-lowercases; returns `"submit"`, `"reset"`, or
  `"button"`, otherwise `"submit"` (including when absent). Setter writes the
  string verbatim.
- `disabled` / `readOnly` getters test attribute presence. A truthy setter
  writes an empty attribute (always queues a record, like `setAttribute`); a
  falsey setter removes it only when present.
- `placeholder` / `label` setters write the string to the attribute and queue a
  record; the getters read the attribute or the documented default/fallback.
- `form.encoding` reads and writes the same runtime slot as `form.enctype`; it
  does **not** read or write the `enctype` attribute and produces no
  MutationObserver record, matching M7-5.
- `option.index` is read-only: the option's position in the nearest ancestor
  select's option list (document order), or `0` when there is no select.
- All reflected attribute writes queue `attributes` MutationObserver records
  with the lower-case attribute name (`type`, `disabled`, `readonly`,
  `placeholder`, `label`), using the same old-value semantics as
  `setAttribute` / `removeAttribute`.
- `getAttribute` / `hasAttribute` and the reflected getters stay consistent in
  both directions.

## Non-goals

- No `input.name` / `required` / `min` / `max` / `pattern`, no `textarea` or
  `select` disabled/readOnly/placeholder reflection.
- No `input.type` validation behavior, no `change` / `input` events, no form
  submission changes, no `FormData`, no validation API.
- No changes to `value` / `checked` / `default*` / `selected` runtime slots, or
  to `form.method` / `action` / `enctype` / `target` / `noValidate` semantics.
- No new Python API and no new DOM node types.
- No `option.index` mutation and no option-collection object.

## Tests

`test/test_m11_18_form_control_reflection.py` covers defaults, normalization,
attribute/property round-trips, boolean presence/removal, the `form.encoding`
alias, `option.label`/`index`, MutationObserver records, and shape guards.

## Verification

Verified on real V8 at commit `08350ce`:

- Linux `release-linux` run `40698705892` — cp311–cp314, each
  `1386 passed, 2 skipped`;
- Windows `release-windows` run `40698728631` — Python 3.11–3.14, each
  `1386 passed, 2 skipped`.

The new test file ran in every lane (8 items); the full suite collects 1388
items. See `docs/m11_18_report.md`.
