# Page refactor — first extraction

Scope agreed: `2026-11-03`. Status: **implemented; local checks passed;
V8-linked/release verification pending**. Source baseline: `3e76871`.

This is an internal maintenance slice after M11-52, not M11-53. It changes
neither the Python/JavaScript surface nor the frozen selector behavior in
`docs/m11_34_selector_engine.md`. M11-53 remains the next feature milestone.

## Boundary

- Move selector syntax structures and parsing out of `page_state.cpp` into
  `include/iv8/selector_parser.h` and `src/core/selector_parser.cpp`.
- Keep the parser independent of V8, DOM nodes, and Page ownership. Its output
  is the same selector list consumed by the existing DOM matching functions.
- Leave matching, tree traversal, host-object ownership, and generation teardown
  in place. Unsupported or malformed selectors still match nothing.
- Add an automatic Linux x64 / CPython 3.13 V8-linked PR/push check, using the
  pinned manylinux image and existing monolith cache/build path. The check must
  build with V8 explicitly enabled and test an installed wheel outside the
  checkout. Release workflows keep their full interpreter/platform matrices.

## Validation

Compile the extracted parser independently and compare its results with the
pre-refactor parser over valid, invalid, and generated selectors. Run the
existing focused selector tests and full pytest suite on a newly built wheel;
distinguish skeleton results from actual V8 runtime results. Check shell and
workflow syntax locally. Record results and outstanding release verification
below without claiming that previous release runs tested this change.

Configuring the new check as required in GitHub branch protection is a separate
repository setting; a workflow file alone does not enable that policy.

## Implementation and local results — `2026-11-03`

- The parser's syntax structures and implementation moved to the two files
  above. The sole Page call site now uses `parse_selector_list`; its matching
  and invalid-input behavior are unchanged. `page_state.cpp` went from 13,190
  to 12,900 lines. The new translation unit is registered in CMake.
- `.github/workflows/v8-ci.yml` runs on PRs, pushes, and manual dispatch. It
  shares the release monolith key and builds the pinned V8 on a cache miss.
  `tools/build_linux_wheels.sh` accepts a single `IV8_PYTHON_TAG`, explicitly
  enables V8, rejects missing interpreters/wheels, and runs installed-wheel
  tests from `/tmp`. The default remains cp311–cp314.
- Compiled both the pre-refactor and extracted parser with Apple Clang,
  C++20, `-Wall -Wextra -Werror`, and UBSan. Compared success/failure and the
  complete parsed structures for **20,119** inputs: existing acceptance-test
  strings, explicit edge cases, 10,000 generated valid expressions, and 10,000
  generated malformed/mixed expressions. Outputs were identical with no UBSan
  diagnostics. The corpus seed was `110334`.
- A newly built, installed **V8-free** CPython 3.13 arm64 wheel passed focused
  pytest (selector/node-foundation/import): **4 passed, 25 skipped**. The full
  suite collected 1,789 items: **225 passed, 1,564 skipped**. These skips do
  **not** validate runtime behavior; `_v8_linked` was explicitly checked false.
- `bash -n`, actionlint `1.7.7` on the new workflow, and interpreter-selection
  checks (single tag, default matrix, invalid tag, missing interpreter) passed.
- ASan could not initialize on this host: `sanitizer_malloc_mac.inc:189`
  aborted before parser execution. It is not reported as a passed ASan run.
- Scratch builds, comparison executables/corpus, and pytest logs are under
  ignored `data/page-refactor/`; no environment or wheel is part of the change.

No V8 monolith is installed locally. No V8-linked wheel was built or tested for
this maintenance slice. At the local-verification snapshot, no new CI
run/artifact evidence was available. The latest
verified feature milestone remains M11-52; its historical three-platform
results do not verify this refactor. Run the new V8 CI check and all three
release lanes before marking this maintenance slice release-verified.

Publishing the workflow starts its first CI verification. This slice does not
configure a required branch-protection check. After a successful run, select its
`Linux x64 / Python 3.13 / V8` check in the repository's merge policy.
