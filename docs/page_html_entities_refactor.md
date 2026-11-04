# Page refactor — bounded HTML character references and escaping

Scope agreed: `2026-11-04`. Status: **VERIFIED on real V8 — Linux x64,
Windows x64, macOS arm64 (`2026-11-04`)**.
Baseline: `34d1d96` (verified Base64 extraction, PR #5).

Extract bounded character-reference decoding and text/attribute escaping into
`html_entities.h` / `html_entities.cpp`. Keep UTF-8 scalar emission and the six
named-reference lookup private to the module. Preserve the 32-byte lookahead
limit, case-sensitive names, numeric validation/replacement, literal malformed
references, single-pass decoding, and append-to-output escaping semantics.

The module operates on strings only. DOM node ownership, parsing/tokenization,
raw-text/RCDATA decisions, serialization traversal/attribute order, coercion,
and MutationObserver records stay in the host layer. No HTML5 parser upgrade,
new entity table, Python/JS API, or M11 feature is introduced; M11-53 stays next.

## Verification plan

Compare original/extracted decoding and both escapers under UBSan on fixed,
numeric-boundary, Unicode, arbitrary-byte, and generated inputs. Add Python
regressions for invalid/numeric references, literal unknown references,
single-pass behavior, text-versus-attribute escaping, and raw-text/RCDATA
consumers. Run focused DOM/HTML tests, real-V8 CI, and all release matrices.
Record actual source trees, runs, artifacts, and test counts before verification.

## Local results — `2026-11-04`

- Added `include/iv8/html_entities.h` / `src/core/html_entities.cpp`, registered
  in CMake. No DOM node or generation-owned resources moved.
  `page_state.cpp` decreased from 12,438 to 12,280 lines.
- Original and extracted code compiled under C++20 with `-Wall -Wextra -Werror`,
  UBSan, and non-recovering diagnostics. **20,070 byte-input cases** produced
  identical decoding, prefixed text/attribute escaping, and re-decoding, with
  no UBSan findings; seed `110649`. Scalar and lookahead boundaries, malformed
  references, Unicode, NUL, and all 256 byte values are covered.
- Added six Python regression items. Focused HTML/node/attribute/script
  collection: **63**; full collection: **1,824**. Local V8-free focused pytest:
  **2 passed, 61 skipped**. Runtime skips are not functional verification.
- Scratch inputs, executables, and logs live in ignored
  `data/html-entities-refactor/`.

## Real-V8 verification

Source commit: `85b2d2002582f7004c0e6868edf6f9b018067e36`, Git tree
`b904d7ff803b5acd5b0e7b7b7e8c2d70a7c18735`. All release runs succeeded; each
interpreter reported **1822 passed, 2 skipped** (1,824 collected) and imported
a V8-linked build reporting `15.0.245.19`.

| Lane | Run | Python versions | Result |
|---|---|---|---|
| V8 CI | [41828571144](https://github.com/pakeli/iv8-runtime/actions/runs/41828571144) | 3.13 | 1822 passed, 2 skipped |
| release-linux | [41828654174](https://github.com/pakeli/iv8-runtime/actions/runs/41828654174) | 3.11–3.14 | 1822 passed, 2 skipped |
| release-windows | [41828670556](https://github.com/pakeli/iv8-runtime/actions/runs/41828670556) | 3.11–3.14 | 1822 passed, 2 skipped |
| release-macos | [41828851448](https://github.com/pakeli/iv8-runtime/actions/runs/41828851448) | 3.11–3.14 | focused 63 passed; full 1822 passed, 2 skipped |

Linux artifact: `13323115499` (`iv8-linux-manylinux2_28-wheels`, all four
interpreters).

Windows artifacts: `13321346775` (cp311), `13322125432` (cp312),
`13321266841` (cp313), `13322930131` (cp314).

macOS arm64 artifacts: `13321782276` (cp311), `13322786154` (cp312),
`13323011099` (cp313), `13321397797` (cp314).

macOS used the existing release-branch cache through temporary workflow commit
`1de5fd67aef02311e9e731dcd2e64d7bff5f2246`; both checkout steps pinned the full
`85b2d20` source SHA, and the focused set ran before the full suite in each
interpreter. Restore commit `8b5c97d2515ad1ffdf9ebcf3f39765bad36e1324` restored
original workflow blob `eb730d3e77a17d4d527e67646475385cebb2428c` without
rewriting history. A dispatch response experienced a transport EOF; the run
was confirmed received before further action, so no duplicate was dispatched.
The temporary driver is not part of the source branch.

The real-V8 merge gate remains mandatory with up-to-date branches and
administrator enforcement. Later handoff-only documentation edits preserve
the tested runtime/build/test files. M11-53 remains the next feature milestone.
