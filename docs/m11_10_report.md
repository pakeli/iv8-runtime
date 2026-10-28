# M11-10 Delivery Report — classList / toggleAttribute

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-28`)**

Part of the five-slice M11-6..M11-10 verification batch on branch
`m11-6-event-subclasses`; final verified commit `35c85fe`.

## Evidence

- Linux `release-linux` run `40661574145` — cp311 / cp312 / cp313 / cp314,
  each `1352 passed, 2 skipped`.
- Windows `release-windows` run `40661580891` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1352 passed, 2 skipped`.
- `test/test_class_list.py` ran in every lane (13 items).
- 1352 = the M11-5 baseline 1265 + the batch's 87 new items.

## Fixes during verification

- Five older contract guards still asserted `classList` / `toggleAttribute` /
  `Blob` / `File` / `FileReader` were absent; they were refreshed for the new
  surface (`35c85fe`).
- `test_add_and_dynamic_length` expected a length of 4; the correct count after
  adding `c`, `d`, `e` to the initial `a b` list is 5 (`35c85fe`).

## Scope landed

`element.classList` (`contains` / `add` / `remove` / `toggle` / `replace` /
`item` / `length` / `value`) and `element.toggleAttribute(name, force?)`.
Class and boolean attribute changes share the existing dedicated fields, DOM
queries, and MutationObserver attribute hooks. classList is a fresh plain
object per access; there is no `DOMTokenList` global. See
`docs/m11_10_class_list.md`.

## Outcome

M11-10 is verified and included in the five-slice merge to fork `master`.
