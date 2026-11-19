# BOM-02 build validation

Date: `2026-11-19`. **All 12 selected lanes and all 12 wheels verified.**
Mac wheels were built and independently installed first; hosted Linux/Windows
release builds started after local Mac acceptance. [Draft PR #33](https://github.com/pakeli/iv8-runtime/pull/33)
contains the Navigator additions. Scope and browser evidence:
`docs/bom_02_navigator.md` / `docs/bom_02_navigator_report.md`.

## Sources and build provenance

- Runtime/Linux source: `1dff793c5349206976dcb575d761c8331732da6e`;
  tree `3f7e18f50a7e17abf9fed27be7e57cc0aec6f01f`.
- Corrected Windows checkout: `d97412dcd97df4ababba40fa55b1c7c92888b3b4`;
  tree `57ad2e7f7a3c059c34cdc7fa8b2e8baa8a267d78`.
- The only difference between those commits is `.gitattributes`, preserving
  browser probe LF bytes on Windows. All **431** canonical runtime, test,
  build and workflow Git blobs match the frozen Mac candidate. Distinct commit
  trees are retained; they are not presented as one identical tree.
- The local Mac candidate was built before commit, then verified byte-for-byte
  against the committed runtime source. Candidate identity:
  `worktree-sha256:97a27d601fc480f90ec40d97819b484959a9975181b25038c5ce01a65a59e21e`.
- V8 stays pinned to **15.0.245.19** /
  `209c9cea0db17d8caf23e9d2c7de08c351609744`. Matching engine monoliths are
  reused; native extensions and wheels are rebuilt. No V8 master build is used.

## Successful lanes

Every lane runs the same **36-module / 1107-test gate** against an installed
wheel outside the source tree before its complete suite. There are 190 new
BOM-02 tests. Windows retains two additional Unix-only skips.

| Platform / execution | Python | Gate | Full suite | Run / artifact |
|---|---|---:|---|---|
| macOS arm64 / local first | 3.11.18 | 1107 passed | 3333 passed, 2 skipped | macos/cp311/artifact-identity.json |
| macOS arm64 / local first | 3.12.4 | 1107 passed | 3333 passed, 2 skipped | macos/cp312/verification.json |
| macOS arm64 / local first | 3.13.5 | 1107 passed | 3333 passed, 2 skipped | macos/cp313/verification.json |
| macOS arm64 / local first | 3.14.8 | 1107 passed | 3333 passed, 2 skipped | macos/cp314/verification.json |
| Linux x64 / hosted | 3.11 | 1107 passed | 3333 passed, 2 skipped | 44541914392 / 14541145347 |
| Linux x64 / hosted | 3.12 | 1107 passed | 3333 passed, 2 skipped | same four-wheel archive |
| Linux x64 / hosted | 3.13 | 1107 passed | 3333 passed, 2 skipped | same four-wheel archive |
| Linux x64 / hosted | 3.14 | 1107 passed | 3333 passed, 2 skipped | same four-wheel archive |
| Windows x64 / hosted | 3.11 | 1107 passed | 3331 passed, 4 skipped | 44543050218 / 14540365334 |
| Windows x64 / hosted | 3.12 | 1107 passed | 3331 passed, 4 skipped | 44543050218 / 14539795779 |
| Windows x64 / hosted | 3.13 | 1107 passed | 3331 passed, 4 skipped | 44543050218 / 14538764474 |
| Windows x64 / hosted | 3.14 | 1107 passed | 3331 passed, 4 skipped | 44543050218 / 14539243414 |

Runs: [Linux](https://github.com/pakeli/iv8-runtime/actions/runs/44541914392),
[Windows](https://github.com/pakeli/iv8-runtime/actions/runs/44543050218).
Local Mac has no hosted run/artifact IDs and no new hosted Mac capacity failure:
this batch follows the user's local-Mac-first policy. Per-lane source/engine
hashes, installed import/native identities, arm64/minimum macOS 11.0 checks,
commands and complete logs are preserved.

## Windows checkout failure and correction

Initial Windows run **44541927380** built and installed four native wheels,
but every focused gate stopped at **1105 passed, 1 failed, 1 error**. Checkout
converted probe.js and related_then_probe.js to CRLF; exact LF→CRLF SHA-256
calculations reproduce both raw-byte fixture failures. No full suite or artifact
upload occurred. Evidence remains in `release/windows/failed-44541927380/`.

The two-line `.gitattributes` fix retains LF for `test/fixtures/bom_02/*.js`.
Corrected run 44543050218 passes all lanes. All five checkouts use d97412d and
all five matching V8 cache restores hit. The test assertions are unchanged.
`release/windows-checkout-fix.json` verifies source equivalence, so successful
Mac/Linux runtime validation is retained without redundant builds.

## Required V8 checks

Original runtime-source push/PR CI **44541815375 / 44541870914** and checkout-fix
push/PR CI **44542938863 / 44542954665** all pass **83 audit checks**, the
**1107-test gate**, and full **3333 passed, 2 skipped**.

The original PR merge snapshot is
`392988ca13a540d92a57ddac23d4ea4b4a1014e5` with runtime tree `3f7e18f...`;
the checkout-fix merge snapshot is
`556b79cad95730afb2b57779329a899fbaccd409` with checkout-fix tree `57ad2e7...`.
Actual checkouts, raw metadata and log hashes are recorded separately in
`release/linux/verification.json`. Handoff-only follow-up commits do not change
runtime, test, build, workflow or checkout-policy bytes.

## Artifact verification and delivery

All **five remote ZIP archives** and **12 wheels** are collected under
`data/bom-02-2026-11-19/release/wheels/{macos,linux,windows}/`.
The combined `wheel-manifest.json` and `SHA256SUMS` record per-platform source,
execution mode, artifact ID, ABI tags and wheel/native hashes. API/upload/local
ZIP digest checks, ZIP/wheel CRC, native architecture and installed provenance
pass. Linux additionally verifies every wheel RECORD hash.

Linux's 63,405,611-byte artifact matches SHA-256
`a5e20348b6387cac53611e218927d4ccafd6b9513585939c842b73d278bf3ae2`.
Range downloads are recombined only after complete digest verification; partial
files are not accepted as artifacts. Local Mac artifacts have no invented hosted
identifiers. Raw evidence and the failed Windows attempt remain available.

Baseline PR #32 was merged before BOM-02. PR #33 remains a reviewable draft.
No shared SDK replacement, version tag, or BOM-02 PR merge is performed. Remaining
BOM/iterator-monitoring gaps and generated diagnostic object-label profile
differences stay explicit in the boundary and acceptance report.
