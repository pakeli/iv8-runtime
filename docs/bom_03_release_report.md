# BOM-03 release validation

Date: `2026-11-21`. **All 12 selected lanes and all 12 wheels verified.**
Four local Mac lanes passed first on `2026-11-20`; the resumed hosted Linux and
Windows matrices pass on the same runtime source. Existing Mac binaries are
retained, with their hashes and CRC rechecked instead of rebuilding on resume.

## Fixed source

- Runtime/build source: `4cbab176ec8105d5d0545f61335c6600d9db8830`.
- Runtime tree: `c76e926f74a7bcc1995ed697d0841a3950195a65`.
- All 442 frozen code/build/test/checkout-policy inputs match this source.
  Mac wheels were built from the byte-equivalent uncommitted candidate before
  source submission; their original provenance is retained.
- V8 remains **15.0.245.19** /
  `209c9cea0db17d8caf23e9d2c7de08c351609744`. Matching monolith caches are
  reused. Native extensions/wheels are rebuilt; V8 itself is not rebuilt.
- [PR #34](https://github.com/pakeli/iv8-runtime/pull/34) is ready for review and
  stacked on unmerged [PR #33](https://github.com/pakeli/iv8-runtime/pull/33).

## Verified lanes

Every installed wheel passes the same **37-module / 1225-test gate**, followed
by the full suite outside the source tree. Windows has the existing two extra
Unix-only skips. Mac receipt paths below are relative to
`data/bom-03-2026-11-20/macos/`.

| Platform / execution | Python | Gate | Full suite | Run / artifact or local receipt |
|---|---|---:|---|---|
| macOS arm64 / local first | 3.11 | 1225 passed | 3451 passed, 2 skipped | cp311/artifact-identity.json |
| macOS arm64 / local first | 3.12 | 1225 passed | 3451 passed, 2 skipped | cp312/verification.json |
| macOS arm64 / local first | 3.13 | 1225 passed | 3451 passed, 2 skipped | cp313/verification.json |
| macOS arm64 / local first | 3.14 | 1225 passed | 3451 passed, 2 skipped | cp314/verification.json |
| Linux x64 / hosted | 3.11 | 1225 passed | 3451 passed, 2 skipped | 44769697781 / 14647678386 |
| Linux x64 / hosted | 3.12 | 1225 passed | 3451 passed, 2 skipped | same four-wheel archive |
| Linux x64 / hosted | 3.13 | 1225 passed | 3451 passed, 2 skipped | same four-wheel archive |
| Linux x64 / hosted | 3.14 | 1225 passed | 3451 passed, 2 skipped | same four-wheel archive |
| Windows x64 / hosted | 3.11 | 1225 passed | 3449 passed, 4 skipped | 44769706158 / 14645608752 |
| Windows x64 / hosted | 3.12 | 1225 passed | 3449 passed, 4 skipped | 44769706158 / 14647965688 |
| Windows x64 / hosted | 3.13 | 1225 passed | 3449 passed, 4 skipped | 44769706158 / 14646208894 |
| Windows x64 / hosted | 3.14 | 1225 passed | 3449 passed, 4 skipped | 44769706158 / 14648000460 |

Hosted runs: [Linux](https://github.com/pakeli/iv8-runtime/actions/runs/44769697781),
[Windows](https://github.com/pakeli/iv8-runtime/actions/runs/44769706158).
Both pass on their first attempts. Linux's checkout and all five Windows
checkouts match the fixed source; the matching cache keys are verified in logs.
Engine-build/cache-save and tag-only publish steps are skipped. No hosted Mac
runner is requested for this resumed validation; Mac's actual local execution
and absence of hosted artifact IDs remain explicit.

## Source CI

The source [push V8 CI 44722569894](https://github.com/pakeli/iv8-runtime/actions/runs/44722569894)
and [PR V8 CI 44722637832](https://github.com/pakeli/iv8-runtime/actions/runs/44722637832)
both pass **83 audit checks**, **1225 gate tests**, and full **3451 passed,
2 skipped**. The push directly checks out 4cbab17; the PR checks out merge
snapshot `fb508e9a869cf35b2928fb8f73489be1188cf47f`, whose tree is exactly the
runtime tree above. These checkout identities are recorded separately in
`data/bom-03-release-2026-11-21/ci/verification.json`.

## Artifacts and integrity

All **five hosted ZIP archives** and **12 wheels** are collected under
`data/bom-03-release-2026-11-21/wheels/{macos,linux,windows}/`.
The combined `wheel-manifest.json` and `SHA256SUMS` identify source, Python/ABI,
execution mode, artifact IDs, wheel hashes and native hashes. Per-platform
receipts preserve full run/jobs/logs, actual test counts and cache outcomes.

Remote downloads use bounded parallel byte ranges. Only complete archives that
match API size and SHA-256 are accepted: Windows uses 44 ranges and Linux 61,
with zero range retries. ZIP digests also match the upload logs. All ZIP/wheel
CRC, WHEEL tags, RECORD hashes and native architecture checks pass. Mac's prior
arm64/minimum OS 11.0 checks and installed imports remain recorded.

Linux's 63,418,791-byte artifact SHA-256 is
`76a0f10e9c6e288ce716198a0fc930b2b09cdfe132d715117692a7086d940b75`.
Windows' four archive/wheel hashes are in its verification manifest. Signed URLs
and authentication are not persisted in these receipts. Follow-up documentation
commits do not change runtime, test, build, workflow or checkout-policy bytes.

## Handoff

BOM-03 implements native legacy quota aliases, callback behavior and the
required DOMError dependency; its 118 new tests, browser comparisons and profile
limits remain in `docs/bom_03_legacy_quota_report.md`. The fixed-reference
Navigator presence count remains **35/70**. No new API is added during this
release-validation continuation.

PR #34 is ready for review, with PR #33 as its base. Review/merge #33 first and
then retarget #34 to master. Neither PR is merged by this continuation. No
shared SDK replacement, version tag or published GitHub Release is performed.
Future development retains BOM priority, local Mac-first validation and the
explicit deferral of further Symbol.iterator monitoring.
