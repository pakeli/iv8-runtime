# User-origin tracing acceptance report

Date: `2026-11-16`. **Implemented; Linux/Windows hosted and macOS local fallback validation passed. Hosted macOS did not execute.**

Boundary: [access_trace_origins.md](access_trace_origins.md). The latest fully hosted
release-verified source remains `aa72e50` in [PR #32](https://github.com/pakeli/iv8-runtime/pull/32).
This repair is source `4c777f78e535d54a003a297c029d9fc87ce20950`, tree
`6184c309a03437de7bb13774a0bdf8008ba198cf`, validated as detailed below. Shared deployment remains BIND-01.

## Implemented behavior

An empty debug load is silent. `page.eval("window.location.href")` prints only
Window.window, Window.location and Location.href gets. Explicit user Event
construction is recorded, while synthetic construction and event field maintenance
are excluded before event sequence/identity/capacity accounting. Manual/raw traces
use the same native gate. Python mode parameters and event schemas are unchanged.

Page-generation origin scopes distinguish native implementation work from user
callbacks and input operations. User getters, setters, conversions, iterators,
listeners/handlers, timers, Promise/microtask/frame/idle callbacks and native
observer/FileReader/message deliveries preserve user API accesses. Runtime
bootstrap scripts use engine script IDs; their begin/end tokens restore origin
also when a call is filtered, capacity is full, or an exception is thrown.
Stack inspection is active only while tracing and occurs before the trace lock.

A pinned-V8 standalone experiment established that bound native callbacks have
no extra JS frame, so explicit user boundaries supplement stack-based reentry.
CSS Proxy/native-data forwarding retains visible method references, and descriptor
classification preserves custom native-bound accessors/method replacements without
running user Proxy traps for diagnostics. Normal CSS backend calls stay internal.
Independent watch channels retain their original contract.

## Actual local acceptance

- **40 new origin tests** plus the revised empty-load mode assertion. The first
  baseline run (18 origin cases plus the empty-load assertion) failed **10** and
  passed **9**, demonstrating the old internal-noise/capacity behavior.
- Final **24-module / 522-test installed-wheel release gate passes**.
- Full real-V8 macOS arm64/Python 3.13: **2812 passed, 2 skipped**.
- Existing descriptor/forwarding tests remove only internal duplicate event
  expectations; direct reads/writes, query events, original errors, native shapes
  and callback side effects retain positive assertions.
- The full suite also covers native binding metadata, History/named access,
  conversion/disposal/isolation/threading and monitoring-on/off behavior.

The tests run against the extracted final wheel outside the source root. Local
wheel SHA-256: `e80bab48427f143bc84abbaa92377d0143f8327498b78146ff51bcf15323b547`.
Build, gate, full logs and per-source/native hashes are under ignored
`data/access-trace-origins-2026-11-16/`; see `source-receipt.json`.

## Release and limits

The fix is included in existing PR #32. V8 remains pinned
to 15.0.245.19; no kernel/browser surface expansion, SDK install or version tag
is part of the repair. The separate prototype/invalid-receiver/symbol/query/
returned-object coverage gaps remain open: user-only logs do not imply complete
browser API coverage.


## Cross-platform build validation

Fixed source `4c777f78e535d54a003a297c029d9fc87ce20950` /
tree `6184c309a03437de7bb13774a0bdf8008ba198cf` is used for all rows below.
The V8 pin/build scripts and cache keys are unchanged; existing matching engine
monoliths are reused and the host extension/wheels rebuilt. No merge or tag is
published. Windows has two additional Unix-only skips.

| Platform | Python | Execution | Focused | Full suite | Run / artifact or local receipt |
|---|---|---|---:|---|---|
| linux x64 | 3.11 | GitHub Actions | 522 passed | 2812 passed, 2 skipped | 43862858961 / 14224368133 |
| linux x64 | 3.12 | GitHub Actions | 522 passed | 2812 passed, 2 skipped | 43862858961 / 14224368133 |
| linux x64 | 3.13 | GitHub Actions | 522 passed | 2812 passed, 2 skipped | 43862858961 / 14224368133 |
| linux x64 | 3.14 | GitHub Actions | 522 passed | 2812 passed, 2 skipped | 43862858961 / 14224368133 |
| windows x64 | 3.11 | GitHub Actions | 522 passed | 2810 passed, 4 skipped | 43862861011 / 14223713152 |
| windows x64 | 3.12 | GitHub Actions | 522 passed | 2810 passed, 4 skipped | 43862861011 / 14224097713 |
| windows x64 | 3.13 | GitHub Actions | 522 passed | 2810 passed, 4 skipped | 43862861011 / 14224686975 |
| windows x64 | 3.14 | GitHub Actions | 522 passed | 2810 passed, 4 skipped | 43862861011 / 14224172724 |
| macOS arm64 | 3.11 | local fallback | 522 passed | 2812 passed, 2 skipped | `macos/local/cp311/verification.json` |
| macOS arm64 | 3.12 | local fallback | 522 passed | 2812 passed, 2 skipped | `macos/local/cp312/verification.json` |
| macOS arm64 | 3.13 | local fallback | 522 passed | 2812 passed, 2 skipped | `macos/local/cp313/verification.json` |
| macOS arm64 | 3.14 | local fallback | 522 passed | 2812 passed, 2 skipped | `macos/local/cp314/verification.json` |

Hosted runs: [Linux 43862858961](https://github.com/pakeli/iv8-runtime/actions/runs/43862858961),
[Windows 43862861011](https://github.com/pakeli/iv8-runtime/actions/runs/43862861011).
Both succeeded on the first attempt; all five remote ZIP digests, wheel CRC/tags
and native hashes are verified. Required source V8 checks
[43862742750](https://github.com/pakeli/iv8-runtime/actions/runs/43862742750) and
[43862731339](https://github.com/pakeli/iv8-runtime/actions/runs/43862731339)
also pass. Branch protection is unchanged.

### macOS runner failure and local fallback

[Hosted macOS 43862864018](https://github.com/pakeli/iv8-runtime/actions/runs/43862864018)
failed before acquiring a runner. GitHub reports:
"The job was not acquired by Runner of type hosted even after multiple attempts".
The build job was cancelled without a checkout, cache restore, compilation or
test execution; wheel/publish jobs were skipped and no hosted artifacts exist.
This is recorded as an infrastructure failure, not a passing hosted lane.

The same source, fixed V8 15.0.245.19 / commit
`209c9cea0db17d8caf23e9d2c7de08c351609744`, macOS arm64 deployment target 11.0,
24-module gate and full suite were then built/run with the existing isolated
local Python 3.11.18, 3.12.4, 3.13.5 and 3.14.8 environments. No dependency or
shared SDK install/upgrade was performed. Per-lane receipts include actual
commands, compiler/source/log/native/wheel hashes and isolated imports. cp311's
initial sandbox-only localhost bind failures are retained separately; its
identical localhost-enabled full rerun passes 2812/2. cp313 reuses the identical
source-hash-verified local acceptance build above. All four local wheels pass
CRC, cp311–cp314/macosx_11_0_arm64 tags and Mach-O arm64 verification.

The temporary helper driver `880861a3511fcf5a3f7c575790401bc468a51d74` pinned
both checkout sites to the source. After hosted capacity failure, blob-guarded
restore `0edc2009fa8e301deee74b18dbb4a7376201997e` restores original workflow blob
`eb730d3e77a17d4d527e67646475385cebb2428c`; a fresh connector read verifies original
bytes. SHA-256: `c8f8a0a3dce3c34f7c887724fe17ab70ec7f387abe4d10314db80332be6c9a05`.
No helper change is merged into the PR.

### Artifacts

All 12 verified wheels are retained under
`data/access-trace-origins-2026-11-16/release/wheels/{linux,windows,macos}/`.
The combined `wheel-manifest.json`, `SHA256SUMS`, `release-receipt.json`, raw CI
logs/artifact metadata and local/failure/restore receipts are in the parent
release directory. macOS local wheels have no invented hosted run/artifact IDs.
Shared SDK deployment remains BIND-01; no wheel is installed into that SDK.
