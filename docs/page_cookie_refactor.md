# Page refactor — Cookie module

Scope agreed: `2026-11-04`. Status: **implemented; local checks passed;
real-V8/release verification pending**.
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

Pending. Record source commit/tree, CI/release run IDs, artifact IDs, and actual
passed/skipped counts after all release lanes succeed. Previous selector
extraction verification does not cover this change.
