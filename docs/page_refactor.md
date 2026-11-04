# Page refactor — first extraction

Scope agreed: `2026-11-03`. Status: **VERIFIED on real V8 — Linux x64,
Windows x64, macOS arm64 (`2026-11-04`)**. Source baseline: `3e76871`.
Verified source commit: `917cbfb265949a3bf3e1c60eabd843f2a10c962b`, Git tree
`b47aad7f9d1897d9cc390e6caf47a8edddd6e147`.

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

Merge policy: require `Linux x64 / Python 3.13 / V8` from the GitHub Actions
app (`15368`), with up-to-date branches and administrator enforcement. This
is a repository branch-protection setting, separate from the workflow file.

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

The local results above used no V8 monolith. Runtime verification was performed
separately by the following GitHub Actions runs.

## Real-V8 verification — `2026-11-04`

| Lane | Run | Python versions | Result per interpreter |
|---|---|---|---|
| PR/push V8 CI | [41674273069](https://github.com/pakeli/iv8-runtime/actions/runs/41674273069) | 3.13 | 1787 passed, 2 skipped |
| release-linux | [41759793220](https://github.com/pakeli/iv8-runtime/actions/runs/41759793220) | 3.11–3.14 | 1787 passed, 2 skipped |
| release-windows | [41759812090](https://github.com/pakeli/iv8-runtime/actions/runs/41759812090) | 3.11–3.14 | 1787 passed, 2 skipped |
| release-macos | [41760140836](https://github.com/pakeli/iv8-runtime/actions/runs/41760140836) | 3.11–3.14 | 1787 passed, 2 skipped |

All release runs succeeded. The suite collects 1,789 items; every interpreter
imported a V8-linked wheel reporting V8 `15.0.245.19`.

Artifacts:

- Linux: `13289198869` (`iv8-linux-manylinux2_28-wheels`, all four wheels).
- Windows: `13289663700` (cp311), `13289643369` (cp312), `13290410661`
  (cp313), `13289158545` (cp314).
- macOS arm64: `13289283806` (cp311), `13290311758` (cp312), `13290481101`
  (cp313), `13289154141` (cp314), tagged `macosx_11_0_arm64`.

Linux and Windows ran directly on `917cbfb`. macOS used the official
`release-macos` workflow on the **macos-15** runner, now available again, with
the existing monolith cache scoped to `macos-release`. Temporary workflow
commit `d847f6ae07cbac3112765604379bbe3564216d24` pinned both checkout steps to
the full `917cbfb` source SHA; all five job checkouts are confirmed in the log.
The initial cache-miss macOS run `41759820659` was cancelled in favor of this
cached run. Restore commit `fba3fed994bec31da41b75e8dc484aa7eccda868` restored
the original workflow blob `eb730d3e77a17d4d527e67646475385cebb2428c` without
rewriting branch history.

The ordinary skeleton CI run `41674273281` completed its 12 Linux/Windows/
macOS arm64 jobs successfully; its four optional macos-13 jobs remained queued
at this verification snapshot. It is separate from the real-V8 merge gate.

This internal maintenance slice is release-verified. The latest feature
milestone remains M11-52, and M11-53 remains the next Window feature. Subsequent
handoff-only documentation edits preserve the tested runtime/build/test files.
