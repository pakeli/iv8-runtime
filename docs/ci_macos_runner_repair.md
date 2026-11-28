# Ordinary CI macOS runner maintenance

Date: `2026-11-28`

Status: **Verified: all 16 ordinary CI jobs and fixed-source V8 CI pass.** This changes the
V8-free CI workflow, not the already verified BOM-09/BOM-10 release wheels.

## Problem and selected correction

The fixed-source ordinary CI run `46037969494` never executed its macOS jobs:
macOS 14 failed runner acquisition with
`The configuration 'macos-14-arm64-us-default' is not supported`; macOS 13 stayed
queued without a runner or test steps. These states were recorded separately
from the successful 12-lane release matrix in `bom_10_release_report.md`.

GitHub's current
[supported runner list](https://docs.github.com/en/actions/reference/runners/github-hosted-runners)
identifies `macos-15` as arm64 and `macos-15-intel` as Intel. Its
[macOS 13 retirement notice](https://github.blog/changelog/2025-07-11-upcoming-changes-to-macos-hosted-runners-macos-latest-migration-and-xcode-support-policy-updates/)
documents that the older image has retired. Use those explicit replacement
labels instead of relying on a moving latest alias.

The matrix retains Python 3.11–3.14 on Linux x64, Windows x64 and required macOS
arm64, with four additional non-blocking Intel macOS jobs. The Intel-only
continue-on-error expression follows the new label; arm64 remains required.
`setup-python` selects the interpreter architecture explicitly and a separate
step checks/reports actual architecture. Windows still uses clang-cl, and
fail-fast stays disabled.

Builds now explicitly set `IV8_LINK_V8=OFF`. Installed-wheel checks still assert
V8-free behavior, package/V8 version separation and imports outside the checkout
before running pytest. Native V8-linked release workflows and their source are
unchanged.

## Protection and evidence

Before the change, a read-only check of master protection confirmed the sole
required status context is `Linux x64 / Python 3.13 / V8` (app 15368), with strict
checks and administrator enforcement. No rulesets were returned. That required
context name and branch protection are not modified by this task. Ordinary Mac
job display names change to their actual new runner labels.

Local checks pass for YAML/matrix structure, Bash syntax and the architecture
probe. Remote results below verify actual runner assignment, architecture, wheel
build/import and pytest execution; no local substitute is counted as hosted CI.


## Fixed source and actual verification

- Initial migration: `93727f1c7e1bf3ff04e53122ceb2fe04a1a01cb4`.
- Final validated source: `c143deef1c151660c274a10243bd4f87fe95126c`.
- Final source tree: `016b92d34b1c1119da0853331dc42f431d57672a`.
- [Ordinary CI 46104162652](https://github.com/pakeli/iv8-runtime/actions/runs/46104162652): **16/16 jobs success**, including all 12 required platform-family jobs and all four supplementary Intel jobs.

| Runner family | Python versions | Actual interpreter architecture | Tests per job |
|---|---|---|---|
| ubuntu-latest | 3.11, 3.12, 3.13, 3.14 | x86_64 | 554 passed, 3052 skipped |
| windows-latest | 3.11, 3.12, 3.13, 3.14 | x86_64 | 552 passed, 3054 skipped |
| macos-15 | 3.11, 3.12, 3.13, 3.14 | arm64 | 554 passed, 3052 skipped |
| macos-15-intel | 3.11, 3.12, 3.13, 3.14 | x86_64 | 554 passed, 3052 skipped |

All 16 raw logs contain the exact checkout SHA, successful architecture probe,
runner architecture, installed-package imports and tests outside the checkout,
`_v8_linked=False`, version separation and actual pytest summary. The large skip count is the
expected V8-free profile; native behavior is verified by the separate V8 lane.

[V8 CI 46104162581](https://github.com/pakeli/iv8-runtime/actions/runs/46104162581)
passes on the same final source/tree: **83 audit**, **1375 gate**, and full
**3604 passed, 2 skipped**. The branch-protection required context remains
`Linux x64 / Python 3.13 / V8`; before/after protection and ruleset JSON are equal.

## First-attempt failure and evidence

[Initial CI 46103521969](https://github.com/pakeli/iv8-runtime/actions/runs/46103521969)
verified the new Mac runners successfully, but all four Windows jobs stopped
before CMake with `Arguments to -C must be of the form KEY=VAL`. The attached,
unquoted `-Ccmake.define.IV8_LINK_V8=OFF` form was not preserved by PowerShell.
The final source uses `--config-settings "cmake.define.IV8_LINK_V8=OFF"` and
reruns the entire 16-job matrix; old successes are not combined with new jobs.

Raw metadata, checkouts, architecture facts, logs and SHA-256 evidence:

- `data/ci-macos-runner-2026-11-28/ci/summary.json`: initial failed attempt.
- `data/ci-macos-runner-2026-11-28/ci/attempt-c143dee/summary.json`: final 16 jobs.
- `data/ci-macos-runner-2026-11-28/v8-ci/summary.json`: final native lane.
- `data/ci-macos-runner-2026-11-28/{protection,rulesets}-{before,after}.json`.

## Effect on release evidence

Runtime, tests, native build settings and release workflows remain byte-identical
to the release-verified `d475309` candidate. Only ordinary CI and documentation
changed. The BOM-10 12-wheel release evidence in `bom_10_release_report.md`
remains valid at its original fixed source; historical unsupported/queued runner
states stay recorded rather than being rewritten as successful old runs.
No branch protection, required context, release tag, PR merge or shared SDK was
changed by this task. Current branch changes still require the normal merge flow.
