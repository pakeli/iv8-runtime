# M11-18 Delivery Report — Form-Control Attribute Reflection

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-28`)**

Branch `m11-18-form-control-reflection`, final verified commit `08350ce`.

## Evidence

- Linux `release-linux` run `40698705892` — cp311 / cp312 / cp313 / cp314,
  each `1386 passed, 2 skipped`.
- Windows `release-windows` run `40698728631` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1386 passed, 2 skipped`.
- `test/test_m11_18_form_control_reflection.py` ran in every lane (8 items).
- Full suite collects 1388 items; 1386 pass and 2 are skipped. 1386 is the
  previous 1378 baseline plus the 8 new items.
- The first verification run was green; no post-implementation fixes were
  required.

## Scope landed

- `<input>`: `type` (known-type normalization, default `"text"`), `disabled`,
  `readOnly`, `placeholder`.
- `<button>`: `type` (`submit` / `reset` / `button`, default `"submit"`),
  `disabled`.
- `<form>`: `encoding`, an alias of the existing `.enctype` runtime slot.
- `<option>`: `label` (attribute, falling back to text) and read-only `index`.

Reflected writes are real attribute writes and feed the MutationObserver
attribute hooks; `form.encoding` changes only the runtime slot. The slice adds
no Python API, no form behavior, and no validation surface. See
`docs/m11_18_form_control_reflection.md` for the frozen boundary.

## Verification notes

- A local `zig c++ -fsyntax-only` pass against the real V8 headers was clean
  before pushing.
- The V8 monolith cache was reused; both workflows completed from the warmed
  master cache.
- Stale frozen-surface guards in the earlier form tests were refreshed in the
  same change (`test_button_value.py`, `test_document_forms.py`,
  `test_form_action.py`, `test_form_enctype.py`, `test_form_method.py`,
  `test_form_novalidate.py`, `test_form_request_submit.py`,
  `test_form_target.py`, `test_m5_contract.py`, `test_m7_contract.py`).

## Outcome

M11-18 is verified and merged to fork `master`. The next compatibility slice
(`M11-19`) should be chosen from `docs/m11_compatibility_backlog.md` or another
gap-inventory slice with an explicit boundary.
