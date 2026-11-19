# BOM-02 build validation

Date: `2026-11-19`. **Mac first: all four local lanes accepted;
Linux/Windows validation in progress.**

The approved BOM-02 plan builds Mac wheels before Linux/Windows. All local
CPython 3.11.18, 3.12.4, 3.13.5 and 3.14.8 arm64 wheels are newly built and
independently installed. Each passes the **36-module/1107-test gate** and
**3333 passed, 2 skipped** full suite. The 190 new tests and browser evidence
are recorded in `docs/bom_02_navigator_report.md`.

All 431 code/build/test/workflow inputs match frozen candidate
`worktree-sha256:97a27d601fc480f90ec40d97819b484959a9975181b25038c5ce01a65a59e21e`
before and after the local runs. Baseline is merged PR #32 source
`090c604e855af0e6938c1680a2bde2234e78f11f`; the candidate is committed after
Mac validation and used as the fixed Linux/Windows build source. Exact commit,
tree, run/job/artifact IDs and native/wheel checksums are recorded in the
release receipt under ignored `data/bom-02-2026-11-19/`.

V8 remains pinned to **15.0.245.19**, commit
`209c9cea0db17d8caf23e9d2c7de08c351609744`. Matching engines are reused; the
extensions/wheels are rebuilt. Local Mac ABI tags, CRC, source provenance and
installed imports are verified. Local Mac lanes do not have hosted artifact IDs;
the unreliable hosted Mac runner is not required to produce these artifacts.

Linux x64 and Windows x64 release workflows target Python 3.11–3.14 and use the
same gate. Record actual full counts (Windows has two extra Unix-only skips).
Do not mark a pending lane successful. After completion, collect all artifacts
and verify source, archive digest, CRC, ABI/platform tags and native hashes.

No shared SDK replacement or version-tag publication is part of this batch.
Remaining BOM, iterator monitoring and diagnostic-profile limits stay explicit
in the boundary and acceptance report.
