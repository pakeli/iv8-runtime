# M11-35 Delivery Report — BOM Metadata Completion

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-30`)**

Branch `m11-35-bom-metadata`, verified commit `aa23c57`.

## Evidence

- Linux `release-linux` run `40990275470` — cp311 / cp312 / cp313 / cp314,
  each `1599 passed, 2 skipped`.
- Windows `release-windows` run `40990279247` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1599 passed, 2 skipped`.
- `test/test_m11_35_bom_metadata.py` ran in every lane (12 items). Full suite
  collects 1601 items; 1599 pass and 2 are skipped.
- Both workflows were green on the first attempt; no guard refresh beyond the
  planned `window.name` historical guard was needed.

## Scope landed

- Window: `name` (mutable, Page-persistent across `load()`),
  `length` (0), `screenLeft` / `screenTop` aliases, and `isSecureContext`
  derived from the base URL.
- Screen: `availLeft` / `availTop` and a derived `orientation`
  (`landscape-primary`/0 or `portrait-primary`/90) with no events or lock.
- Navigator: `maxTouchPoints`, `pdfViewerEnabled`, `doNotTrack`, and a
  read-only `connection` object (`effectiveType` / `rtt` / `downlink` /
  `saveData`).
- `Page(environment=...)`: 9 new keys, 27 → 36 flat keys, with nested
  `navigator` / `screen` equivalents; strict type validation and updated
  `get_defaults()` / `get_default_environment()`.
- No navigation, networking, rendering, dialogs, performance marks,
  structuredClone, permissions/clipboard/geolocation, or Python BOM API.

See `docs/m11_35_bom_metadata.md`.

## Verification notes

- Local `zig c++ -fsyntax-only` passed for `page_state.cpp` and
  `page_binding.cpp`; the Python environment/tests passed `py_compile`; 1601
  tests collect locally.
- The V8 monolith cache was reused; both workflows completed from the warmed
  master cache.
- Environment round-trip/type tests were extended for all 36 keys; the nested
  defaults test and the historical `window.name` guard were refreshed.

## Outcome

M11-35 is verified and merged to fork `master`. The deterministic BOM metadata
surface is now substantially complete; history/location navigation, real
networking, rendering, dialogs, and the deferred Web APIs remain out of scope.
