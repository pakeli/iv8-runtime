# Page refactor — Base64 byte codec

Scope agreed: `2026-11-04`. Status: **implemented; local checks passed;
real-V8/release verification pending**.
Baseline: `52cbc2e` (verified query codec extraction, PR #4).

Extract standard Base64 byte encoding and the existing forgiving decoder into
`base64.h` / `base64.cpp`, independently of V8. Keep JS coercion, Latin-1/UTF-16
validation, exception translation, and byte-to-JS-string conversion in the
existing `btoa`/`atob` callbacks. No public API, ownership, or browser surface
changes; no Base64url, streaming, or automatic UTF-8 conversion.

Preserve padding/length validation, all six whitespace bytes currently ignored
by the decoder (space, tab, LF, CR, FF, VT), and discarded residual bits. The
native decoder appends to its output and can leave a decoded prefix on failure;
the host still supplies a fresh string and throws TypeError on failure.
M11-53 remains the next feature milestone.

## Verification plan

Compare original/extracted byte outputs and decoder acceptance/partial-output
behavior on explicit and generated inputs under UBSan, including all byte
values. Cross-check standard encoding with Python's Base64 implementation.
Extend Python tests for unpadded/padded inputs, whitespace, invalid padding,
URL-safe alphabet rejection, and coercion exceptions. Run focused tests, real-V8
CI, and all release matrices; record source trees, runs, artifacts, and actual
counts before marking the slice verified.

## Local results — `2026-11-04`

- Added `include/iv8/base64.h` / `src/core/base64.cpp`, registered in CMake.
  V8 callbacks retain the existing conversion and exception paths.
  `page_state.cpp` decreased from 12,522 to 12,438 lines.
- Original and extracted implementations compiled under C++20 with
  `-Wall -Wextra -Werror`, UBSan, and non-recovering diagnostics. **14,955 cases**
  produced identical encoding, decoder acceptance, and appended/partial-output
  bytes, with no UBSan findings; seed `110484`. The corpus covers all byte values
  and exhaustive short strings over an alphabet/padding/whitespace subset.
  Encoding also matched Python's standard Base64 implementation, and all
  canonical encoded byte strings decoded back to their original bytes.
- Added ten Python regression items. Focused Base64/M8 collection: **32**;
  full collection: **1,818**. Local V8-free focused pytest: **4 passed,
  28 skipped**; runtime skips are not functional verification.
- Scratch inputs, executables, and logs are in ignored `data/base64-refactor/`.

## Real-V8 verification

Pending. Record this slice's source tree, CI/release runs, artifacts, focused
results, and actual full-suite counts. Earlier maintenance runs do not cover it.
