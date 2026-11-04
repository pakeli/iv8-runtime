# Page refactor — minimal URL parsing

Scope agreed: `2026-11-04`. Status: **implemented; local checks passed;
real-V8/release verification pending**.
Baseline: `a3e31af` (verified Cookie extraction, PR #2).

Continue internal Page modularization by extracting `UrlParts`, `decompose_url`,
and `resolve_url` into `url_parser.h` / `url_parser.cpp`. The absolute-scheme
predicate stays private to that module. The module owns strings only; no V8,
DOM, Page generation, or network resources move.

The existing location, URL constructor, Document identity/domain, Window origin,
and messaging code consume the same decomposition. JS coercion, read-only
properties, URLSearchParams snapshots, and secure-context policy stay in the
host layer. No public Python/JS surface or contract changes.

Preserve the minimal M2-3/M8-2 behavior: absolute strings are kept verbatim;
relative inputs require an absolute-with-authority base; empty/query/fragment/
path/protocol-relative inputs retain the existing merge rules. This is not a
WHATWG upgrade: do not normalize dot segments, ports, authority syntax, case,
or percent encoding. M11-53 remains the next feature milestone.

## Validation

Compare original and extracted decomposition/resolution results over explicit
and generated inputs under UBSan. Extend Python regression tests for existing
reference-merge boundaries, coercion failures, and consistent Page URL views
after reload. Run focused tests before full-suite validation, then real-V8 CI
and Linux/Windows/macOS arm64 release matrices. Record source trees, run IDs,
artifacts, and actual counts; mark verified only after every release lane passes.

## Local results — `2026-11-04`

- Added `include/iv8/url_parser.h` and `src/core/url_parser.cpp`, registered in
  CMake. `page_state.cpp` decreased from 12,725 to 12,606 lines. The host layer
  includes the shared syntax module and retains its constructors/accessors.
- Original and extracted implementations compiled under C++20 with
  `-Wall -Wextra -Werror`, UBSan, and non-recovering diagnostics. **15,224
  URL/base cases** produced identical resolution success/output and all eight
  decomposed fields, with no UBSan diagnostics; seed `110482`. Corpus includes
  explicit merge boundaries, malformed schemes, arbitrary punctuation,
  embedded NUL, and Unicode; failure output preservation is also compared.
- Added six Python regression items. The focused URL/location/query/BOM/
  Document-identity/messaging set collects **76 items**. Local V8-free focused
  pytest: **6 passed, 70 skipped**; full collection: **1,801 items**. Runtime
  skips are not functional verification of the extracted C++ module.
- Scratch corpus, executables, and logs live in ignored `data/url-refactor/`.

## Real-V8 verification

Pending. Record source commit/tree, CI/release runs, artifact IDs, and actual
counts after the current slice's release lanes pass. Cookie/selector extraction
results do not verify this change.
