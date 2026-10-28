# M11-9 Delivery Report — Event Cancellation

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-28`)**

Part of the five-slice M11-6..M11-10 verification batch on branch
`m11-6-event-subclasses`; final verified commit `35c85fe`.

## Evidence

- Linux `release-linux` run `40661574145` — cp311 / cp312 / cp313 / cp314,
  each `1352 passed, 2 skipped`.
- Windows `release-windows` run `40661580891` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1352 passed, 2 skipped`.
- `test/test_event_cancellation.py` ran in every lane (9 items).
- 1352 = the M11-5 baseline 1265 + the batch's 87 new items.

## Fixes during verification

- Three older tests still asserted that cancellation members were absent; they
  were refreshed for the M11-9 surface (`35c85fe`): `test_event_bubbling.py`,
  `test_event_subclasses.py`, and `test_m4_a_contract.py`.

## Scope landed

`cancelable` / `defaultPrevented` / `preventDefault()` /
`stopImmediatePropagation()` on the flat Event model.
`stopImmediatePropagation` stops later same-target listeners and all later
bubbling hops; `dispatchEvent` returns `false` for a canceled cancelable event.
Capture phase, default actions, and listener options remain out of scope. See
`docs/m11_9_event_cancellation.md`.

## Outcome

M11-9 is verified and included in the five-slice merge to fork `master`.
