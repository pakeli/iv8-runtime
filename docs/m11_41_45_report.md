# M11-41..M11-45 Delivery Report — BOM Viewport / Media / Device Metadata

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-30`)**

Branch `m11-41-45-bom-extras`, verified commit `61abc3b`.

## Evidence

- Linux `release-linux` run `41000182750` — cp311 / cp312 / cp313 / cp314,
  each `1626 passed, 2 skipped`.
- Windows `release-windows` run `41000186396` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1626 passed, 2 skipped`.
- `test/test_m11_41_45_bom_extras.py` ran in every lane (9 items). The full
  suite collects 1628 items; 1626 pass and 2 are skipped.

## Scope landed

- **M11-41 visual viewport and window flags** — read-only `visualViewport`
  geometry derived from `innerWidth` / `innerHeight`, plus `window.origin`,
  `crossOriginIsolated`, `closed`, and `frameElement`. No scroll, zoom, or
  resize events.
- **M11-42 matchMedia** — synchronous static MediaQueryList-shaped results for
  media types, comma lists, `not` / `only` / `and`, width/height, orientation,
  and a fixed desktop capability set. Listener methods are no-ops; there is no
  dynamic re-evaluation or `change` event.
- **M11-43 navigator.storage** — deterministic Promise-based `estimate()`
  (`usage: 0`, `quota: 10737418240`), `persisted()`, and `persist()` (both
  `false`). No quota accounting, persistence, buckets, or filesystem.
- **M11-44 navigator.permissions** — `query({name})` returns a static
  PermissionStatus with `state: "prompt"` for a bounded known-name set; unknown
  names reject with `TypeError`. No prompt UI, device access, or change events.
- **M11-45 legacy navigator metadata** — frozen empty `plugins` / `mimeTypes`
  arrays and `javaEnabled()` returning `false`.

`navigator.userAgentData` remains intentionally unimplemented. There are no new
Python APIs or environment keys.

## Verification notes

- The first verification attempt at `f3cebcb` failed one new test because its
  assertion joined `frameElement` (a JS `null`) without `String(...)`, producing
  an empty final field instead of `"null"`. `61abc3b` fixed the assertion.
- Local bootstrap validation ran the fixed JS under Node with viewport and
  navigator stubs; `zig c++ -fsyntax-only` passed for `page_state.cpp`; Python
  tests passed `py_compile`; 1628 tests collect locally.
- The `Uncaught Error: boom` stderr line after the pytest summary pre-dates this
  batch and does not affect the exit code.

## Outcome

M11-41..M11-45 are verified and merged to fork `master`. The remaining BOM
gaps are the deliberately excluded navigation/network surfaces, permission or
device APIs without deterministic backing, real layout/scroll/zoom, and
`navigator.userAgentData`. See `docs/m11_41_45_bom_extras.md`.
