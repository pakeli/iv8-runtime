# Page refactor — Cookie module

Scope agreed: `2026-11-04`. Status: **VERIFIED on real V8 — Linux x64,
Windows x64, macOS arm64 (`2026-11-04`)**.
Source baseline: `f0cbc18` (the verified selector extraction, PR #1).

This is a second internal maintenance slice after M11-52. The frozen
`docs/m11_46_document_cookie.md` contract and Python/JS surface are unchanged;
M11-53 remains the next feature milestone.

## Boundary

- Extract Cookie parsing, fixed-epoch expiry decisions, serialization, and
  insertion-ordered storage into a V8-independent `CookieJar` module.
- Own the jar by value in `PageState` at the existing member location; it
  continues to outlive document generations and remains isolated per Page.
- Retain `PageState::document_cookie` / `set_document_cookie` as thin delegates.
  The DocumentHost getter/setter still owns JS coercion, exception propagation,
  and the existing access-trace path.
- Preserve malformed-input handling, supported date formats, numeric parsing,
  attribute precedence, and ignored origin/path/security attributes. This
  slice adds no clock, networking, persistence, or browser Cookie features.

## Verification plan

Compare the original and extracted implementations over fixed and generated
assignment sequences, checking the serialized jar after every write under
UBSan. Extend Python-facing regression tests for expiry precedence, supported
date forms, coercion failure, and delete/reinsert ordering; use the existing
load/isolation and access-trace tests. Run focused tests before full-suite
verification, then real-V8 CI and Linux/Windows/macOS arm64 release lanes.
Record actual source trees, run IDs, artifacts, and passed/skipped counts here.
Keep this slice pending until every release lane is green.

## Local results — `2026-11-04`

- Added `include/iv8/cookie_jar.h` / `src/core/cookie_jar.cpp` and registered
  the new translation unit in CMake. PageState owns a `CookieJar` value at the
  former vector member's position and delegates its existing Cookie methods.
- `page_state.cpp` decreased from 12,900 to 12,725 lines. Cookie-only date/
  stream headers moved with the implementation; `cstdlib` remains for other
  Page code's numeric parsing.
- Both original and extracted implementations compiled under C++20 with
  `-Wall -Wextra -Werror`, UBSan, and non-recovering UB diagnostics. All
  **30,033 writes across 1,001 assignment sequences** produced identical
  serialized jars after every write, with no UBSan diagnostics. Seed `110446`;
  cases include malformed input, whitespace, embedded NUL/Unicode, date formats,
  numeric boundaries, duplicate attributes, and insertion/update/deletion order.
- Cookie pytest collection: **11 items** (six added to the existing five).
  Full collection: **1,795 items**. Local focused pytest: **11 skipped**, because
  the local interpreter has a V8-free wheel. These skips are not runtime
  verification. The new acceptance tests still require real-V8 CI.
- Differential inputs/executables and local logs are in ignored
  `data/cookie-refactor/`; no environments or artifacts are committed.

## Real-V8 verification

Source commit: `347dfbf5e561c46d6dfd7a4cbc674bb6e5eb677f`, Git tree
`02a5fc1835bea8d162c4a140c6bfb7324aa519b4`. All three release runs succeeded;
the 1,795-item full suite reported **1793 passed, 2 skipped** in every lane.
Every release interpreter imported a V8-linked wheel reporting `15.0.245.19`.

| Lane | Run | Python versions | Result per interpreter |
|---|---|---|---|
| V8 CI | [41764857600](https://github.com/pakeli/iv8-runtime/actions/runs/41764857600) | 3.13 | 1793 passed, 2 skipped |
| release-linux | [41764895229](https://github.com/pakeli/iv8-runtime/actions/runs/41764895229) | 3.11–3.14 | 1793 passed, 2 skipped |
| release-windows | [41764904132](https://github.com/pakeli/iv8-runtime/actions/runs/41764904132) | 3.11–3.14 | 1793 passed, 2 skipped |
| release-macos | [41764990337](https://github.com/pakeli/iv8-runtime/actions/runs/41764990337) | 3.11–3.14 | focused 11 passed; full 1793 passed, 2 skipped |

Linux artifact: `13293010800` (`iv8-linux-manylinux2_28-wheels`, all four
interpreters).

Windows artifacts: `13292356664` (cp311), `13292745409` (cp312),
`13291882305` (cp313), `13292805339` (cp314).

macOS arm64 artifacts: `13292745566` (cp311), `13291807864` (cp312),
`13292441608` (cp313), `13292336874` (cp314).

macOS used the official macos-15 release workflow with the existing
`macos-release` monolith cache. Driver commit
`012f8983489ff12bef67ae05aa6721b9eee0308a` pinned both checkout steps to the
full `347dfbf` source SHA and ran the Cookie test file before the full suite.
Restore commit `738f001f06a4793965269806ccf2cabb10d594ea` restored the original
workflow blob `eb730d3e77a17d4d527e67646475385cebb2428c` without rewriting
history. The driver changes are not part of the source branch.

The existing `Linux x64 / Python 3.13 / V8` GitHub Actions check remains
mandatory on master, with up-to-date branches and administrator enforcement.
Handoff-only documentation edits after the verified source preserve the tested
runtime, build configuration, and tests. M11-53 remains the next feature
milestone; this slice adds no browser surface.
