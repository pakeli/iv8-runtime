# Page refactor — Base64 byte codec

Scope agreed: `2026-11-04`. Status: **VERIFIED on real V8 — Linux x64,
Windows x64, macOS arm64 (`2026-11-04`)**.
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

Source commit: `10151d44e0deb2bb5acfc4ab637ffbe923ccbb39`, Git tree
`0766c6deae5d2f5896881a4928bde46893f68016`. All release runs succeeded; each
interpreter reported **1816 passed, 2 skipped** (1,818 collected) and imported
a V8-linked build reporting `15.0.245.19`.

| Lane | Run | Python versions | Result |
|---|---|---|---|
| V8 CI | [41797126420](https://github.com/pakeli/iv8-runtime/actions/runs/41797126420) | 3.13 | 1816 passed, 2 skipped |
| release-linux | [41797209223](https://github.com/pakeli/iv8-runtime/actions/runs/41797209223) | 3.11–3.14 | 1816 passed, 2 skipped |
| release-windows | [41797219218](https://github.com/pakeli/iv8-runtime/actions/runs/41797219218) | 3.11–3.14 | 1816 passed, 2 skipped |
| release-macos | [41797389405](https://github.com/pakeli/iv8-runtime/actions/runs/41797389405) | 3.11–3.14 | focused 32 passed; full 1816 passed, 2 skipped |

Linux artifact: `13306835043` (`iv8-linux-manylinux2_28-wheels`, all four
interpreters).

Windows artifacts: `13306106645` (cp311), `13306101628` (cp312),
`13305888247` (cp313), `13305149150` (cp314).

macOS arm64 artifacts: `13305604105` (cp311), `13306730055` (cp312),
`13305679327` (cp313), `13305709218` (cp314).

The macOS cached driver commit `5119889f4bc0a8df58eb59f0b54824294844e5c9`
pins both checkout steps to the full `10151d4` source SHA and runs the 32-item
focused set before the full suite in each interpreter. Restore commit
`32d2cafc94cfb0235f9e721d165c2a55ca1eadaf` restored original workflow blob
`eb730d3e77a17d4d527e67646475385cebb2428c` without rewriting history. The
temporary driver is not part of the source branch.

The real-V8 merge gate remains mandatory with up-to-date branches and
administrator enforcement. Later handoff-only documentation edits preserve the
tested runtime/build/test files. M11-53 remains the next feature milestone.
