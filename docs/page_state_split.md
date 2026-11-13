# page_state.cpp split — progress

Plan (confirmed `2026-11-08`): `docs/page_state_split_plan.md`. Structure only:
every step is buildable and behavior-preserving. Each PR requires V8 CI (Linux
x64 / Python 3.13) and a local real-V8 full pytest. The three-platform release
matrix (Python 3.11–3.14) runs once at the end of each stage. Pure-move PRs paste
`tools/audit_move.py` output showing **0 definition differences**.

Baseline before the split: `master` `6191958`, `page_state.cpp` 13,058 lines,
every lane `2050 passed, 2 skipped, 1 xfailed`.

## Progress

| Step | PR | Source commit | CI | `page_state.cpp` lines | Audit |
|---|---|---|---|---|---|
| 0.1 local real-V8 loop | pakeli/iv8-runtime#15 | `973ac99` | local macOS matrix passed | 13,052 | — |
| 0.2 `iv8::browser`, `internal/common.h`, inline tokens, PCH | pakeli/iv8-runtime#14 | `ae4163b` (merge `c1d4e34`) | V8 CI `42466767787`, `42467387443`; release-windows `42466768875` | 13,052 | n/a (intentional changes; tool added in 0.3) |
| 0.3 `tools/audit_move.py` and regression fixes | pakeli/iv8-runtime#15 | `973ac99` | V8 CI `43467102840`, `43467112845` passed | 13,052 | 56 audit cases passed in required CI |
| 1.1 crypto, Base64 globals, encoding and shared helpers | follow-up PR | implementation awaiting required CI | local real-V8 tests passed | 12,637 | 431 identical definitions, 0 differences |

Stage 0 native matrix passed all 12 lanes. Linux/Windows used hosted CI and macOS arm64 used the local release matrix described below.

## Notes

- 0.1 changed course from the handoff: the macOS arm64 monolith already in
  `data/v8/` gives a local real-V8 loop, so no CI-cache export workflow or
  `tools/fetch_v8_cache.sh` was added. Commands: `docs/dev_setup.md` "Local
  real-V8 loop".
- 0.2 left `kJavaEnabledTraceToken` and `kStorageTraceToken` defined but
  unreferenced, as they were before. This is a candidate for plan section 7.
- 0.3: `tools/audit_move.py` gates top-level definitions. It does not yet
  decompose class members moved out of a class body. Extend it before stage 3.3.

## Stage 0 verification (`2026-11-14`)

Verified source `973ac99d98855a0cf98015429905e6c3e79698fc`, tree
`6751ff9c9f419d9b67054677744eef8a3d4f4646`. The tool regressions preserve brace/
direct initializer values and linkage, and preserve directive literal contents.
Ambiguous parenthesized declarations are conservatively gated as definitions.
The normal runtime suite collects the 40 new cases under `test/`; required V8 CI
also runs the original 16 tool self-tests, for **56 focused cases**. All 56 passed.
The three browser source files self-matched with **431 identical definitions,
0 differences**.

| Platform and verification route | Run / local record | Artifacts | Per-lane result |
|---|---|---|---|
| Linux x64 hosted CI, cp311–cp314 | `43467351418` | `14059742327` | 2090 passed, 2 skipped, 1 xfailed |
| Windows x64 hosted CI, cp311–cp314 | `43467364824` | cp311 `14059147858`, cp312 `14057929154`, cp313 `14058793673`, cp314 `14059148001` | 2090 passed, 2 skipped, 1 xfailed |
| macOS arm64 local release matrix, cp311–cp314 | `data/stage0-2026-11-14/local-macos/receipt.json` | Local wheels and digests below | 2090 passed, 2 skipped, 1 xfailed |

The local macOS matrix repeated the release workflow's pinned V8 build, wheel
build, clean installation and full test commands for all four interpreters.
It ran on macOS **26.5 arm64**, produced `macosx_11_0_arm64` wheels and checked
that each clean interpreter loaded its installed V8-linked wheel. Python versions
were 3.11.18, 3.12.4, 3.13.5 and 3.14.8.

| Local artifact ID | SHA-256 |
|---|---|
| `local-macos/cp311/wheel/iv8-0.0.1-cp311-cp311-macosx_11_0_arm64.whl` | `3f4369a64103132529d6a6ee16573d8ff238a3d37de536c578045f998074ac1c` |
| `local-macos/cp312/wheel/iv8-0.0.1-cp312-cp312-macosx_11_0_arm64.whl` | `78c5fdd9cdae67ad46ff0c643855ce1f8401f311d7c7b97c4f84e777208d6abf` |
| `local-macos/cp313/wheel/iv8-0.0.1-cp313-cp313-macosx_11_0_arm64.whl` | `4bf8636b2734d0817a71b9e025d63805ee050c2eb426690b76428066286e47a0` |
| `local-macos/cp314/wheel/iv8-0.0.1-cp314-cp314-macosx_11_0_arm64.whl` | `aad70288d453ad112513b64c870d214b256195ad6652a7312fce907e113b7a6f` |

All local paths are relative to ignored `data/stage0-2026-11-14/`. The index
`verification.json` records the source/tree, hosted artifacts and local results.

The hosted macOS 15 run `43467373389` ended before acquiring a runner. Its job
annotations reported arm64 capacity constraints. A macOS 26 arm64 fallback run
`43470590030` was dispatched with both source checkouts pinned to `973ac99` and
also waited for hosted capacity. The local matrix supplies macOS native validation
for this stage, with the hosted runner failures recorded separately.

The fallback driver commit was `a79c353b9a0ab7604c18fb98bcbb249b3e2f3b3d`.
Restore commit `d35dd5b637249b38a3226dba69c9554c71708cb4` restored the original
helper workflow blob `eb730d3e77a17d4d527e67646475385cebb2428c` using a SHA guard.
The merge gate is `Linux x64 / Python 3.13 / V8` with strict status checks and
administrator enforcement. Stage 1.1 extracts crypto, Base64 globals and encoding.

## Stage 1.1 local verification

The three leaf modules live under `src/core/browser/web/`. Platform RNG code is
owned by `crypto.cpp`. Shared ASCII normalization lives in `internal/common.cpp`,
and shared watch/access recording lives in `internal/trace_hooks.cpp`. The Page
installer uses the private callback declarations. `page_state.cpp` decreases
from 13,052 to **12,637 lines**.

The move audit matches **431 definitions with 0 differences**. Local macOS
arm64/Python 3.11 focused crypto, Base64, encoding, tracing, Web IDL and watch tests
reported **192 passed, 1 xfailed**. The full real-V8 suite reported
**2090 passed, 2 skipped, 1 xfailed**, and all **56 audit tests** passed.
Reflection snapshots matched all eight affected functions and the order of all
258 global keys. Logs and snapshots are under ignored
`data/stage1-1-2026-11-14/` in the primary checkout.

Standalone warning checks without precompiled headers exposed an existing
`btoa` signed/unsigned length comparison. Its baseline source is retained by the
zero-difference move. The warning is recorded in the plan's issue list, and the
standalone check allows that warning while treating other warnings as errors.
Required CI and the final source/tree will be recorded before merging this PR.
