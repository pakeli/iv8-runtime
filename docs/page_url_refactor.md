# Page refactor — minimal URL parsing

Scope agreed: `2026-11-04`. Status: **VERIFIED on real V8 — Linux x64,
Windows x64, macOS arm64 (`2026-11-04`)**.
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

Source commit: `2e30d48e7abb3bb79ee6c0cd8b20dd02423d3738`, Git tree
`a95990e92f9e4dbc3d2caaeb23de6be5692ea2e0`. All three release runs succeeded;
the 1,801-item full suite reported **1799 passed, 2 skipped** in every lane.
Every release interpreter imported a V8-linked build reporting `15.0.245.19`.

| Lane | Run | Python versions | Result |
|---|---|---|---|
| V8 CI | [41783767829](https://github.com/pakeli/iv8-runtime/actions/runs/41783767829) | 3.13 | 1799 passed, 2 skipped |
| release-linux | [41783823684](https://github.com/pakeli/iv8-runtime/actions/runs/41783823684) | 3.11–3.14 | 1799 passed, 2 skipped |
| release-windows | [41783830832](https://github.com/pakeli/iv8-runtime/actions/runs/41783830832) | 3.11–3.14 | 1799 passed, 2 skipped |
| release-macos | [41783905526](https://github.com/pakeli/iv8-runtime/actions/runs/41783905526) | 3.11–3.14 | focused 76 passed; full 1799 passed, 2 skipped |

Linux artifact: `13299574871` (`iv8-linux-manylinux2_28-wheels`, all four
interpreters).

Windows artifacts: `13300311829` (cp311), `13300097265` (cp312),
`13299499809` (cp313), `13301095769` (cp314).

macOS arm64 artifacts: `13300332469` (cp311), `13299739746` (cp312),
`13300412455` (cp313), `13300681645` (cp314).

macOS uses the existing release-branch V8 cache with temporary workflow commit
`8bccd50814ee719b19a2c25b497c9cd2e47623f6`; both checkout steps pin the full
`2e30d48` source SHA. The 76-item URL/consumer set ran before the full suite in
each interpreter. Restore commit `ca1f862408a9a9ac92063f4f2a9bf1f45a9160df`
restored original workflow blob `eb730d3e77a17d4d527e67646475385cebb2428c`
without rewriting history. The driver is not part of the source branch.

The existing real-V8 merge check remains mandatory on master, with up-to-date
branches and administrator enforcement. Subsequent handoff-only documentation
edits preserve the release-tested runtime/build/test files. M11-53 remains the
next feature milestone; this maintenance adds no browser surface.
