# Page refactor — bounded HTML character references and escaping

Scope agreed: `2026-11-04`. Status: **implemented; local checks passed;
real-V8/release verification pending**.
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

Pending. Record this slice's source tree, CI/release runs, artifacts, focused
results, and actual full-suite counts. Prior extraction runs do not verify it.
