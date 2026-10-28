# M11-6 Delivery Report — Event Subclass Cluster

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-28`)**

Part of the five-slice M11-6..M11-10 verification batch on branch
`m11-6-event-subclasses`; final verified commit `35c85fe`.

## Evidence

- Linux `release-linux` run `40661574145` — cp311 / cp312 / cp313 / cp314,
  each `1352 passed, 2 skipped`.
- Windows `release-windows` run `40661580891` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1352 passed, 2 skipped`.
- `test/test_event_subclasses.py` ran in every lane (30 items; its own pytest
  progress line is present in each log).
- The full suite collects 1354 items; 1352 pass and 2 are skipped. 1352 is the
  M11-5 baseline (1265) plus the five-slice batch's 87 new items.

## Fixes during verification

- The first cold run (`ae0ab92`) built and saved the V8 monolith successfully,
  but failed three tests: two old contract guards still asserted `CustomEvent`
  was absent, and `test_dispatch_custom_event_bubbles` wrongly asserted
  `e.currentTarget === outer` (element wrappers are rebuilt per
  `make_host_object`, so identity is not guaranteed). The identity test now
  checks `e.currentTarget.id` (`7448749`); the contract guards were refreshed
  in `35c85fe`.

## Scope landed

`CustomEvent` / `MouseEvent` / `KeyboardEvent` / `FocusEvent` with the frozen
field set, prototype chains, `instanceof Event`, read-only subclass fields, and
existing dispatch/bubbling/`stopPropagation` reuse. See
`docs/m11_6_event_subclasses.md`.

## Outcome

M11-6 is verified and its implementation is included in the five-slice merge to
fork `master`.
