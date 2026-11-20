# BOM-02 handoff

Saved on `2026-11-19` after the user's explicit request to pause and save
progress. The user resumed work on `2026-11-20`; the pause no longer applies.

## Resume check (`2026-11-20`)

PR #33 still has the validated implementation and documentation head 13292b4.
Its required PR V8 CI run 44545908287 completed successfully. There are no
submitted reviews or inline review threads. The PR is now ready for review;
it has not been merged. The following sections retain the saved pause snapshot.
BOM-02 runtime, tests and build inputs are unchanged, so its existing binaries
are retained. The resumed BOM queue now continues as BOM-03 on
`codex/bom-03-quota`; see `docs/bom_03_legacy_quota_report.md` for its separate
local acceptance and remaining release status.

## Completed state

- Branch: `codex/bom-02-navigator`.
- Latest pushed implementation/evidence head:
  `13292b4305928174ee912b36d60e90d7a3416109`.
- [PR #33](https://github.com/pakeli/iv8-runtime/pull/33) contains BOM-02 and the
  final validation description. It was a draft at pause and is ready for review
  after the resume check above. It is open and has not been merged.
- Baseline PR #32 was merged as
  `090c604e855af0e6938c1680a2bde2234e78f11f`.
- Native getInstalledRelatedApps and vibrate are implemented. Secure-reference
  Navigator membership is 34/70. There are 190 new tests.
- Mac Python 3.11–3.14 was built and verified first. All 12 selected lanes pass
  the 1107-test gate. Mac/Linux full suites: 3333 passed, 2 skipped each;
  Windows: 3331 passed, 4 skipped each.
- Five remote ZIPs and all 12 wheels are downloaded and hash/CRC/ABI verified.
  The final wheel hashes were checked again after the documentation push.

## Sources and receipts

Runtime/Linux source is `1dff793c5349206976dcb575d761c8331732da6e`.
Windows checkout is `d97412dcd97df4ababba40fa55b1c7c92888b3b4`: it adds only
scoped .gitattributes to preserve browser-probe LF bytes. All 431 canonical
runtime/test/build/workflow inputs match the validated Mac candidate. The
documentation head above changes no runtime, test, build, workflow or checkout
policy bytes. Source-build CI passed on both implementation/check-out commits;
automatic CI for later documentation commits is not a new release validation
and is not being actively monitored during this pause.

- Full report: `docs/bom_02_release_report.md`.
- Scope/profile limits: `docs/bom_02_navigator.md` and
  `docs/bom_02_navigator_report.md`.
- Combined artifact/source receipts:
  `data/bom-02-2026-11-19/release/wheel-manifest.json`, `SHA256SUMS` and
  `source-receipt.json`.
- Latest Mac CP311 wheel:
  `data/bom-02-2026-11-19/release/wheels/macos/iv8-0.0.1-cp311-cp311-macosx_11_0_arm64.whl`.
- Linux run: 44541914392. Corrected Windows run: 44543050218.
  Initial Windows run 44541927380 failed fixture hashes due to CRLF checkout;
  its complete evidence is retained, and the corrected run passed.

## Resume boundary

Implementation and the planned build verification are complete. On resumption,
inspect PR #33 and the current branch before choosing further work; do not
rebuild already validated binaries merely because this chat was paused.
PR #33 merge, shared SDK replacement and version publication have not been
performed. Symbol.iterator monitoring remains deferred. BOM work retains
priority, and future builds validate local Mac wheels before Linux/Windows.
Generated diagnostic object-label differences under modified prototype metadata
remain explicitly documented. User-owned `Claude outputs/` remains untracked
and untouched. No new BOM batch has been selected or started.
