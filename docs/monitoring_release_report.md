# Environment monitoring and Page modes release build

Date: `2026-11-16`. **PR/build preparation; release verification pending.**

The user authorizes a PR for the current candidate and V8-linked builds. This
combines the History parent-entry fix, ENV-MON-01 native observation expansion
and Python `Page.load(..., mode="debug" / "prod")` convenience layer.

## Build contract

- Keep V8 pinned to **15.0.245.19** /
  `209c9cea0db17d8caf23e9d2c7de08c351609744`.
- Build Linux x64, Windows x64 and macOS arm64 wheels for Python 3.11–3.14.
  Reuse matching engine monolith caches where available; rebuild the host
  extension/wheels and distinguish cache restoration from engine compilation.
- Every installed wheel runs the same **23-module / 482-test focused gate**
  before the complete suite, outside the source tree. Local acceptance is
  **482 focused passed**, full macOS arm64/Python 3.13 **2772 passed, 2 skipped**.
- Record exact source/tree, run/job/artifact IDs and actual lane counts after
  completion. Until all lanes are green, the verified release remains the
  combined `40d1c1f` build recorded in `docs/bom_release_report.md`.
- Shared SDK replacement and version-tag publication are not performed.

## Coverage boundary

The mode parameter automates trace activation and output; it does not establish
complete API observation. Reproduced prototype/invalid-receiver/symbol/query/
returned-object gaps remain open in `docs/environment_monitoring_report.md`.
The PR/build must retain this distinction rather than claiming all-use coverage.
