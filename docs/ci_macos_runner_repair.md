# Ordinary CI macOS runner maintenance

Date: `2026-11-28`

Status: **Configuration updated; remote verification pending.** This changes the
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

Local checks cover YAML/matrix structure and workflow-script syntax. Remote
acceptance requires runner assignment, successful architecture checks, wheel
build/import and actual pytest results; queue placement or a local substitute
does not establish hosted CI success. Run IDs, source/tree and actual counts
will be added after execution.
