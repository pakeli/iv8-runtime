# Environment monitoring and Page modes release build

Date: `2026-11-16`. **All 12 V8-linked release lanes verified.**

[PR #32](https://github.com/pakeli/iv8-runtime/pull/32) combines the History
parent-entry fix, ENV-MON-01 native observation expansion and
`Page.load(..., mode="debug" / "prod")` convenience layer. The PR remains open;
no merge, version-tag publication or shared SDK replacement is performed.

## Source and build scope

- Fixed runtime/build source: `aa72e50f29bd2d1f472bad87a4038eb09086854b`.
- Build tree: `3f07dc72a09b78f97f74924ecd3f15b2cb0624cd`.
- V8 remains pinned to **15.0.245.19** /
  `209c9cea0db17d8caf23e9d2c7de08c351609744`.
- Linux x64, Windows x64 and macOS arm64 build Python 3.11–3.14 wheels.
  All restored the existing matching engine monolith; engine-build/save steps
  were skipped. The host extension and wheels were rebuilt for this source.
- Each installed wheel runs the same **23-module / 482-test focused gate**
  before the complete suite, outside the source tree. The gate preserves the
  History/named/native/presence regressions and adds History root reads, nine
  ENV-MON modules and the 57 Page-mode cases.

## Completed release lanes

| Platform | Python | Focused | Full suite | Run / artifact ID |
|---|---|---:|---|---|
| Linux x64, manylinux | 3.11 | 482 passed | 2772 passed, 2 skipped | 43822383750 / 14204703978 |
| Linux x64, manylinux | 3.12 | 482 passed | 2772 passed, 2 skipped | same archive, four wheels |
| Linux x64, manylinux | 3.13 | 482 passed | 2772 passed, 2 skipped | same archive |
| Linux x64, manylinux | 3.14 | 482 passed | 2772 passed, 2 skipped | same archive |
| Windows x64 | 3.11 | 482 passed | 2770 passed, 4 skipped | 43822388334 / 14205386484 |
| Windows x64 | 3.12 | 482 passed | 2770 passed, 4 skipped | 43822388334 / 14205082201 |
| Windows x64 | 3.13 | 482 passed | 2770 passed, 4 skipped | 43822388334 / 14205172502 |
| Windows x64 | 3.14 | 482 passed | 2770 passed, 4 skipped | 43822388334 / 14203999894 |
| macOS arm64, target 11.0 | 3.11 | 482 passed | 2772 passed, 2 skipped | 43822440892 / 14205675350 |
| macOS arm64, target 11.0 | 3.12 | 482 passed | 2772 passed, 2 skipped | 43822440892 / 14205630192 |
| macOS arm64, target 11.0 | 3.13 | 482 passed | 2772 passed, 2 skipped | 43822440892 / 14204549879 |
| macOS arm64, target 11.0 | 3.14 | 482 passed | 2772 passed, 2 skipped | 43822440892 / 14205277445 |

All three platform runs succeeded on their first attempts. Windows has two
additional Unix-only skips. Publish steps were skipped on these non-tag builds.
Run links: [Linux](https://github.com/pakeli/iv8-runtime/actions/runs/43822383750),
[Windows](https://github.com/pakeli/iv8-runtime/actions/runs/43822388334),
[macOS](https://github.com/pakeli/iv8-runtime/actions/runs/43822440892).

The required Linux/Python 3.13 V8 source checks also passed:
[PR run 43822315757](https://github.com/pakeli/iv8-runtime/actions/runs/43822315757)
and [push run 43822266086](https://github.com/pakeli/iv8-runtime/actions/runs/43822266086).
Each has 83 audit checks, 482 focused passes and full 2772 passed / 2 skipped.
Branch protection still requires `Linux x64 / Python 3.13 / V8`; it is unchanged.
Separate legacy V8-free CI retains its macos-14/13 cancelled/queued jobs; those
are not the successful macOS arm64 V8-linked release matrix above.

## macOS cache driver and restoration

The matching macOS engine cache is scoped to `macos-release`. Temporary helper
commit `22bb539d9dea98067ec6203df3e1de95e5846315` uses this source's updated gate
and pins both checkout sites to the exact runtime source. Logs verify all five
actual checkouts (cache job and four wheel jobs) as
`aa72e50f29bd2d1f472bad87a4038eb09086854b`, with five cache hits. The run head is
a workflow-driver identity, not a different runtime source.

After success, blob-guarded restore
`5baaf744ff9fb925e9905cfade4a6c2f402015aa` restores the original workflow blob
`eb730d3e77a17d4d527e67646475385cebb2428c`. A fresh connector read verifies the
original bytes. Original workflow SHA-256:
`c8f8a0a3dce3c34f7c887724fe17ab70ec7f387abe4d10314db80332be6c9a05`.
No helper-driver change is merged into this PR.

## Artifacts and evidence

Raw jobs/logs, API artifact metadata, source/restore records and verification
manifests are retained under ignored `data/monitoring-release-2026-11-16/`.
The three platform evidence directories are `linux/`, `windows/`, `macos/`;
local wheels are collected under `wheels/{linux,windows,macos}/`.
All **nine artifact ZIPs** match their published API SHA-256 digests, and all
**12 extracted wheels** pass CRC and WHEEL-tag verification. Downloads succeeded
on their first attempts; the wheels remain uninstalled.
API ZIP digests are kept distinct from individual wheel and native-module
SHA-256 values. Whole-archive digest checks, ZIP/wheel CRC checks and WHEEL-tag
verification are recorded per platform in `wheel-manifest.json`; the combined
manifest and `SHA256SUMS` cover the final collection. No wheel is installed into
the shared SDK.

## Coverage and deployment boundaries

The strict requirement that every browser API use is observable is still
**incomplete**. Generic prototype method reads, invalid receiver entries, some
symbol/structural queries and API-returned structures retain reproduced gaps.
The debug/prod parameter automates activation/output and avoids the manual
10,000-event ceiling in its automatic path; it does not close those gaps.
See [environment_monitoring_report.md](environment_monitoring_report.md) and
[page_monitoring_modes.md](page_monitoring_modes.md).

The shared SDK deployment remains BIND-01, native SHA-256
`d29024112409b373a9a714d9dcd9bb532e22716b2164d5d9b4f773c9b94eef89`.
No package/dependency upgrade, tag publication, live business request or
server-acceptance claim is made by this PR/build task.
