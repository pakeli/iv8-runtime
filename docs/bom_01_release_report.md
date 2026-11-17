# BOM-01 V8-linked release build

Date: `2026-11-17`. **PR submission and release validation in progress.**

The user authorizes submitting BOM-01 to the existing
[PR #32](https://github.com/pakeli/iv8-runtime/pull/32) and building the
V8-linked wheels. Local acceptance is recorded in
`docs/bom_01_capabilities_report.md`: 94 new tests, 917 combined focused passes,
and 3143 passed / 2 skipped on a fresh installed macOS arm64/Python 3.11 wheel.

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

## Evidence and completion

Source identities, workflow/run/job/artifact metadata, logs, cache outcomes,
downloaded wheels and SHA-256/CRC/tag checks are retained under ignored
`data/bom-01-release-2026-11-17/`. Release completion requires successful
verification of every lane and matching artifacts. At this report's initial
submission, the release matrix has not completed and the latest fully hosted
release-verified source remains aa72e50.

No merge, version tag or shared SDK installation is included. Shared deployment
remains BIND-01; remaining BOM and monitoring gaps retain their separate queues.
