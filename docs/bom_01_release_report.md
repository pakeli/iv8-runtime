# BOM-01 V8-linked release build

Date: `2026-11-17`. **PR updated; Linux/Windows hosted and macOS local
fallback validation passed. Hosted macOS did not execute.**

The user authorizes submitting BOM-01 to the existing
[PR #32](https://github.com/pakeli/iv8-runtime/pull/32) and building the
V8-linked wheels. Local acceptance is recorded in
`docs/bom_01_capabilities_report.md`: 94 new tests, 917 combined focused passes,
and 3143 passed / 2 skipped on a fresh installed macOS arm64/Python 3.11 wheel.

## Fixed source and runs

- Runtime source: `6839c9ab4fec026cedf24b4de351c81c6e7d92e6`.
- Runtime tree: `7d5f85f379b0f797b8512f8d318aa104183b4975`.
- PR: [#32](https://github.com/pakeli/iv8-runtime/pull/32), updated with BOM-01.
- [Linux release 44097591287](https://github.com/pakeli/iv8-runtime/actions/runs/44097591287).
- [Windows release 44097588159](https://github.com/pakeli/iv8-runtime/actions/runs/44097588159).
- [macOS release 44097632473](https://github.com/pakeli/iv8-runtime/actions/runs/44097632473).
- macOS workflow driver: `4b2de58b29d1b8ce92eee8c9f6b86506f66f1e15`;
  both checkout sites pin the runtime source above. The driver head is not the
  runtime build source.

## Build scope

Build Linux x64, Windows x64 and macOS arm64 wheels for Python 3.11–3.14.
V8 remains pinned to 15.0.245.19 / 209c9cea0db17d8caf23e9d2c7de08c351609744.
Matching engine caches may be reused; the native extension and wheels are
rebuilt. Each installed wheel must pass the same 34-module/917-test gate and
the full suite outside the source tree. Windows retains its two additional
Unix-only skips.

All builds use one fixed runtime commit/tree. The macOS cache-scoped helper
must pin both checkout sites to that source, use the updated gate, and restore
its original workflow bytes afterward. Record the helper run head separately
from the actual runtime source. A runner-allocation failure is infrastructure
failure, never a passing lane; any local fallback must remain explicitly labeled.

## Completed hosted lanes

Linux release 44097591287 and Windows release 44097588159 succeed on their
first attempts, using the fixed source above and matching V8 caches. Extensions
and wheels are rebuilt; engine build/save steps are skipped. Publish is skipped.

| Platform | Python | Gate | Full suite | Artifact ID |
|---|---|---:|---|---|
| Linux x64 | 3.11 | 917 passed | 3143 passed, 2 skipped | 14335337172 |
| Linux x64 | 3.12 | 917 passed | 3143 passed, 2 skipped | same archive |
| Linux x64 | 3.13 | 917 passed | 3143 passed, 2 skipped | same archive |
| Linux x64 | 3.14 | 917 passed | 3143 passed, 2 skipped | same archive |
| Windows x64 | 3.11 | 917 passed | 3141 passed, 4 skipped | 14333969236 |
| Windows x64 | 3.12 | 917 passed | 3141 passed, 4 skipped | 14334203582 |
| Windows x64 | 3.13 | 917 passed | 3141 passed, 4 skipped | 14335385777 |
| Windows x64 | 3.14 | 917 passed | 3141 passed, 4 skipped | 14334353665 |

The required V8 CI runs also pass: push 44097417010 checks out the fixed source;
PR 44097425525 checks out merge snapshot
`68b37b5e75aba39b505b3365a704c434f030565e`. Both pass 83 audit checks, 917
focused tests and 3143 passed / 2 skipped full. The merge snapshot is not
misidentified as the fixed release source. Branch protection still requires
Linux x64 / Python 3.13 / V8 and is unchanged.

## macOS capacity failure and completed local fallback

[Hosted run 44097632473](https://github.com/pakeli/iv8-runtime/actions/runs/44097632473)
failed during runner acquisition. Job 134088875569 has runner_id 0 and no steps;
GitHub reports that the job was not acquired and annotates macOS arm64 capacity
constraints. No checkout, cache restore, compilation or test ran, and no hosted
Mac artifacts exist. This is not a passing hosted lane.

The same fixed source and pinned V8 were validated in the existing local macOS
arm64 environments. New isolated verification environments actually install the
wheels, assert site-packages import and linked V8, then run gate and full suites
outside the source tree. The 421 code/build/test/workflow source hashes match the
fixed source; mutable handoff documents are recorded separately.

| Python | Execution / build | Gate | Full suite | Receipt |
|---|---|---:|---|---|
| 3.11.18 | local; source-verified acceptance wheel reused and freshly installed | 917 passed | 3143 passed, 2 skipped | macos/local/cp311/verification.json |
| 3.12.4 | local; new build and isolated install | 917 passed | 3143 passed, 2 skipped | macos/local/cp312/verification.json |
| 3.13.5 | local; new build and isolated install | 917 passed | 3143 passed, 2 skipped | macos/local/cp313/verification.json |
| 3.14.8 | local; new build and isolated install | 917 passed | 3143 passed, 2 skipped | macos/local/cp314/verification.json |

All four wheels pass CRC, cp311–cp314/macosx_11_0_arm64 tags, Mach-O arm64 and
minimum OS 11.0 checks, installed native/Python-source identity and SHA-256.
V8 is reused, not rebuilt. The supplemental work started while the hosted job
was queued and became the explicitly labeled fallback after allocation failure;
execution-context.json retains that sequence. No old build environment or
shared SDK is modified. Local lanes have no invented hosted artifact IDs.

After failure, blob-guarded restore commit
`6b2386c62af55714c0fee1c66ec042bc75071767` restores original workflow blob
`eb730d3e77a17d4d527e67646475385cebb2428c`. A fresh connector read verifies exact
original bytes, SHA-256
`c8f8a0a3dce3c34f7c887724fe17ab70ec7f387abe4d10314db80332be6c9a05`.
The helper change is not part of PR #32.

## Artifacts, source identity and completion

All **five remote artifact ZIPs** (one Linux, four Windows) and all **12 wheels**
are collected and verified under `data/bom-01-release-2026-11-17/`.
`wheel-manifest.json` and `SHA256SUMS` cover the combined
`wheels/{linux,windows,macos}/` collection. ZIP API/upload digests, archive/wheel
CRC, platform/ABI tags, native architecture and wheel/native hashes are recorded
per platform. Mac wheels are local outputs with no hosted artifact IDs.

Windows retained partial gh-download timeouts and then completed connector
retrieval; cp313 needed one additional retry. Linux completed a slow 63,391,892-byte
ZIP using an already-downloaded prefix and four verified HTTP Range suffixes.
The recombined ZIP matches the API and upload-log digest exactly:
`303a1b254484d1e2e2f2b1695d45f384c00fb73caf63bb24ed70f38288c413f6`.
Download retries were not workflow/test reruns and their raw receipts remain.

All 12 lanes have passed the gate/full suite, with eight hosted lanes and four
explicit local Mac fallback lanes. The failed hosted Mac attempt is preserved;
this is not a fully hosted green matrix. The latest fully hosted release-verified
source remains aa72e50. Both successful source V8 checks and the PR merge tree
identity are recorded separately. Follow-up commits update handoff documents
only; runtime/test/build/workflow files remain identical to the fixed build
source, as checked before committing.

## Future execution order

The user requires **macOS wheels first** on future builds. Produce and validate
all four Python versions with the existing local arm64 environments before
starting Linux/Windows release builds. Hosted Mac remains supplementary while
capacity is unreliable. Keep matching pinned V8 reuse, isolated installed-wheel
checks, source identities and artifact verification unchanged. This direction
is synchronized in the handoff set and BOM queue.

PR #32 is updated. No merge, version tag or shared SDK installation is included;
shared deployment remains BIND-01. Remaining BOM and monitoring gaps retain their
separate queues.
