# Combined browser presence and monitoring build

Date: `2026-11-15`. **All 12 V8-linked release lanes verified.** This combines
PRES-01, PRES-02, PRES-03 and WIN-MON-01 in [PR #31](https://github.com/pakeli/iv8-runtime/pull/31).
The shared SDK installation remains BIND-01; this task submits code and builds
artifacts without replacing that environment or publishing a version tag.

## Source and build scope

- Verified build source: `40d1c1f5bd9c3a9d40f9ac9ba3e6930437dfddff`.
- Source tree: `b7d518ecc9ab46a30812622041bd48f3c28ab1aa`.
- Runtime implementation head before the release-gate change:
  `eacdc97e1abaf1fcc4ea3a40eaa1de2fe22a323d`.
- V8 stays pinned to **15.0.245.19** /
  `209c9cea0db17d8caf23e9d2c7de08c351609744`. Engine build scripts and the pin
  are unchanged. Linux, Windows and macOS jobs restored the existing matching
  monolith caches; the extension/wheels were rebuilt with the new hosted runtime.
- The installed-wheel focused gate now includes 12 modules / **257 tests**:
  History, named access, binding metadata and all four presence/monitoring batches.
  Every release lane runs that gate before the full suite outside the source tree.

## Completed release lanes

| Platform | Python | Focused | Full suite | Run / artifact |
|---|---|---:|---|---|
| Linux x64, manylinux_2_28 | 3.11 | 257 passed | 2547 passed, 2 skipped | 43704599952 / 14158158036 |
| Linux x64, manylinux_2_28 | 3.12 | 257 passed | 2547 passed, 2 skipped | same archive, four wheels |
| Linux x64, manylinux_2_28 | 3.13 | 257 passed | 2547 passed, 2 skipped | same archive |
| Linux x64, manylinux_2_28 | 3.14 | 257 passed | 2547 passed, 2 skipped | same archive |
| Windows x64 | 3.11 | 257 passed | 2545 passed, 4 skipped | 43704600017 / 14158231758 |
| Windows x64 | 3.12 | 257 passed | 2545 passed, 4 skipped | 43704600017 / 14158265938 |
| Windows x64 | 3.13 | 257 passed | 2545 passed, 4 skipped | 43704600017 / 14158191903 |
| Windows x64 | 3.14 | 257 passed | 2545 passed, 4 skipped | 43704600017 / 14157439958 |
| macOS arm64, deployment target 11.0 | 3.11 | 257 passed | 2547 passed, 2 skipped | 43704687043 / 14157449916 |
| macOS arm64, deployment target 11.0 | 3.12 | 257 passed | 2547 passed, 2 skipped | 43704687043 / 14158635156 |
| macOS arm64, deployment target 11.0 | 3.13 | 257 passed | 2547 passed, 2 skipped | 43704687043 / 14157882583 |
| macOS arm64, deployment target 11.0 | 3.14 | 257 passed | 2547 passed, 2 skipped | 43704687043 / 14157678109 |

All release jobs passed on their first run. Windows has two additional Unix-only
skips. Publish steps were skipped because these are manual branch builds,
not version-tag releases. Run links:
[Linux](https://github.com/pakeli/iv8-runtime/actions/runs/43704599952),
[Windows](https://github.com/pakeli/iv8-runtime/actions/runs/43704600017),
[macOS](https://github.com/pakeli/iv8-runtime/actions/runs/43704687043).

The required Linux/Python 3.13 V8 checks also passed:
[43704477599](https://github.com/pakeli/iv8-runtime/actions/runs/43704477599) and
[43704409402](https://github.com/pakeli/iv8-runtime/actions/runs/43704409402),
each 257 focused and full 2547 passed / 2 skipped. Branch protection and required
checks are unchanged. The separate legacy V8-free CI contains cancelled/queued
macOS-14/13 jobs; those are not the 12 successful V8-linked release lanes above.

## macOS cache driver and restoration

The matching macOS cache lives on the existing macos-release helper branch.
Driver `17521fdc79536b47bdeb59f3a02ee5f1427c4672` uses the updated release gate
and pins both checkout sites to the exact build source. Logs verify all five
actual checkouts (cache job plus four wheel jobs) are
`40d1c1f5bd9c3a9d40f9ac9ba3e6930437dfddff`; all five hit the V8 cache.

After success, SHA-guarded restore
`921eedf9a478d8298440492ba6cde7c951fcb830` restores the original workflow blob
`eb730d3e77a17d4d527e67646475385cebb2428c`. A fresh API read verifies the original
bytes and SHA-256 `c8f8a0a3dce3c34f7c887724fe17ab70ec7f387abe4d10314db80332be6c9a05`.
No helper workflow change is merged into the feature branch.

## Artifacts and evidence

API artifact metadata, published ZIP digests, downloaded archive/wheel receipts,
logs and source records are kept under ignored `data/release-2026-11-15/`.
Platform directories keep API digest verification separate from individual wheel
SHA-256 values; only completed, matching downloads are marked verified. Download
timeouts/resumes do not alter the successful CI run status. Local wheels are
collected under `data/release-2026-11-15/wheels/{linux,windows,macos}/`.

The component reports retain their original local measurements and artifact
identities. This later combined release supersedes their then-pending release
status for the included code, without rewriting the earlier snapshots:
[PRES-01](presence_01_report.md), [PRES-02](presence_02_report.md),
[PRES-03](presence_03_report.md), [WIN-MON-01](window_monitoring_report.md).


All nine downloaded platform artifacts now match their published GitHub SHA-256
ZIP digests. All 12 extracted wheels pass ZIP CRC and WHEEL-tag inspection;
wheel and native-module SHA-256 values are recorded in
`data/release-2026-11-15/wheel-manifest.json` and `SHA256SUMS`. No wheel is installed
into the shared SDK. Windows/macOS partial downloads and the Linux archive were
resumed with checked HTTP 206 ranges before whole-file digest verification.

| Platform | Python | Wheel SHA-256 |
|---|---|---|
| linux | cp311 | `87b6055e2b385a39a4b0ea613f9971b97a55e1af52d8d1bb65cf318a229ccea2` |
| linux | cp312 | `85c44458ebe26b4919a4eb18eb7197b22e5161afc283ff959f12a555434bd832` |
| linux | cp313 | `4b44917976c5160da3ce60ad2fc56d17e2c11ca5f77fcad45414fd44756b89e0` |
| linux | cp314 | `089f4c2faad53c2ce6e3e4816550da9cb469024c424397b7f0f0d7e93bb303e3` |
| windows | cp311 | `5e79f7532a8162ad3a282223bdeb0d67a4fa511e5ba0f57e2415006c867ac563` |
| windows | cp312 | `8b2da4ef31f6cf30722eb153d60d8a3316c8cdf77c65c0991ef257a69155f355` |
| windows | cp313 | `ec6ca99e0b805e0735785af4f5b0fd663cf2aeddb7389a3003c69c18e522435f` |
| windows | cp314 | `93e4bc65d1e279841fbf0a837e2d32129ef1b8669023bf8366e569477e9d22b1` |
| macos | cp311 | `7efd1e3e4abfdf0eb12c3d32d106b3467111e22666c3acf08536e4ca43014e2a` |
| macos | cp312 | `449dbcd2e09f15c4c48a790943aa2a2bd0bf127b5ae38f5008b1e4ed701863c6` |
| macos | cp313 | `9298fc2ac08a151db907eb672308da009d4b4f496da6783f984d3a983b23352a` |
| macos | cp314 | `12bc70a5f98c2336a1aff11d65ca83ce1906b1568a05e1bd80d5a72fb0c449a9` |

## Deployment and behavior boundaries

The actual shared iv8-test native remains BIND-01 SHA-256
`d29024112409b373a9a714d9dcd9bb532e22716b2164d5d9b4f773c9b94eef89`.
No shared package, dependency or user environment is installed/upgraded here.
The 49 ordinary Window entries remain typed presence with declared backend
limits; the separate 821 global interface entries and further DOM work remain
outside the current request. Monitoring retains its native-entry/Promise/data
property boundaries. This build makes no live business/server acceptance claim.
