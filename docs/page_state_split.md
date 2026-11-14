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
| 1.1 crypto, Base64 globals, encoding and shared helpers | pakeli/iv8-runtime#16 | `a3dcb20` | V8 CI `43477576437`, `43477909432` passed | 12,637 | 431 identical definitions, 0 differences |
| 1.2–1.8 remaining leaf modules | pakeli/iv8-runtime#17 | `f5dbea5` | 12 hosted release lanes passed | 10,065 | 403 identical definitions, 0 differences |
| 2.1–2.4 pure C++ DOM data layer | pakeli/iv8-runtime#18 | `be5bdb4` | 12 hosted release lanes passed | 8,774 | 317 identical definitions, 0 differences |

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
administrator enforcement. PR #15 merged as `fcefbf1`; its final V8 checks were
`43473957475` and `43473968706`. Main V8 CI `43475218937` passed.

## Stage 1.1 verification (`2026-11-14`)

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

Verified source `a3dcb200ebea4a899a56526271968ffe5af586e9`, tree
`ca1c0c7d0618a8c88b9191b67159de18add43206`. Required V8 CI `43477576437` and
`43477909432` passed. The latter recorded 56 audit tests and the complete
`2090 passed, 2 skipped, 1 xfailed` runtime suite.

Standalone checks compiled all five browser translation units without PCH under
`-Wall -Wextra -Werror`. Exceptions cover unused V8 header parameters and the
existing `btoa` signed/unsigned comparison and `WatchHit` partial aggregate
initializers. Those existing warnings are recorded for later cleanup in the
plan's issue list. The source move preserves their original definitions.

The remaining stage 1 slices were grouped in PR #17 under the user-confirmed
per-stage policy on `2026-11-14`.

## Complete stage 1 verification (`2026-11-14`)

The remaining URL/structured clone, performance, console/probe/storage,
navigator/media query/BOM, event/capability and bootstrap modules are extracted.
Small host classes retain their inline definitions in private headers. Shared
non-DOM event helpers live in `internal/event_support`. Performance and media
query stores retain their `iv8` types and Page ownership. Bootstrap arrays have
explicit declarations preserving the installer's existing sizeof expressions.

`page_state.cpp` is 10,065 lines. Local syntax/link checks passed; focused tests
reported 136 passed and 1 xfailed, the complete suite reported 2090 passed,
2 skipped and 1 xfailed, and all 56 audit tests passed. The audit matched
403 definitions with zero differences. Reflection matched 17 root objects and
their prototype-chain descriptors.


Verified source `f5dbea5082c49b49419d6ef290f0a4ac665b1b50`, tree
`f2cf965a2cc6037fb0933aeba03afe634f11e478`. PR #17 includes all slices 1.2–1.8.
The initial Windows build exposed a missing `<cmath>` include in the event
constructor module; the verified source includes that explicit dependency.

| Platform (Python 3.11–3.14) | Run | Artifact IDs | Per-lane result |
|---|---|---|---|
| linux | `43551493341` | `linux-manylinux2_28-wheels`: `14096643267` | 2090 passed, 2 skipped, 1 xfailed |
| windows | `43551496515` | `windows-wheel-cp3.11`: `14096945839`, `windows-wheel-cp3.12`: `14095804822`, `windows-wheel-cp3.13`: `14096856659`, `windows-wheel-cp3.14`: `14096163119` | 2090 passed, 2 skipped, 1 xfailed |
| macos | `43551709339` | `macos-arm64-wheel-cp3.11`: `14096367947`, `macos-arm64-wheel-cp3.12`: `14096946075`, `macos-arm64-wheel-cp3.13`: `14096441720`, `macos-arm64-wheel-cp3.14`: `14096612623` | 2090 passed, 2 skipped, 1 xfailed |

All 12 lanes passed in hosted CI against pinned V8 15.0.245.19. The macOS
helper pinned both source checkouts to the verified source; restore commit
`c508577074b56a150bd0b9f1516a0c79d704e10a` restored workflow blob
`eb730d3e77a17d4d527e67646475385cebb2428c`. Required V8 checks
`43551491659` and `43551496150` passed. The complete receipt and local logs
are under ignored `data/page-split-stage1/`. Stage 2 follows this acceptance.

## Stage 2 verification (`2026-11-14`)

The pure C++ data layer lives in `browser/dom`: nodes/detach/siblings, HTML
parsing and serialization, queries and selector matching, form normalization,
values and validity. Shared case conversion lives in `internal/text`. `NodeKind`
retains its `iv8` type and numeric values in a V8-free public header included by
the existing `host_object.h`; Page still owns node pools and host wrappers.
CMake excludes all seven pure translation units from the V8/Python PCH.

Pre-PR syntax/link checks passed. Each pure translation unit compiled separately
with `-Wall -Wextra -Werror` and without V8/Python include paths; dependency scans
confirmed no V8/Python headers. `collect_options` has an out-of-line definition
so cross-file callers link correctly. `document_insertion_error` keeps its
unchanged default argument and body inline in the node header. These placement
changes preserve the public runtime behavior.

The move audit matched **317 definitions, 0 differences**. All **56 audit tests**
passed. Focused DOM/Web IDL tests reported **153 passed, 1 xfailed**; the complete
real-V8 suite reported **2090 passed, 2 skipped, 1 xfailed**. Reflection matched
all **17 root object graphs**, including prototype-chain descriptors.

A standalone C++20 differential executable linked only the new pure modules and
existing string-only selector/entity utilities. The baseline copied the old
stage 1 definitions into a separate namespace. **20,000 deterministic cases**
compared parsed trees, parent/child/sibling identities, script records,
serialization, selector/query results, form values and validation messages,
document insertion validation and detaching. UBSan ran with
`-fno-sanitize-recover=undefined`; all cases matched. Commands, baseline,
executable and logs are under ignored `data/page-split-stage2/`.

`page_state.cpp` decreases from 10,065 to **8,774 lines**. PR #18 completes all
four stage 2 slices. Stop after this stage for user confirmation.

Verified source `be5bdb4cc6c41d8bacc45f57b711a9da1aa5dcc4`, tree
`cda6b32a807e3243a4ecb2b578e97d81eda92ded`.

| Platform (Python 3.11–3.14) | Run | Artifact IDs | Per-lane result |
|---|---|---|---|
| linux | `43553093264` | `linux-manylinux2_28-wheels`: `14096893644` | 2090 passed, 2 skipped, 1 xfailed |
| windows | `43553096281` | `windows-wheel-cp3.11`: `14097500473`, `windows-wheel-cp3.12`: `14096004454`, `windows-wheel-cp3.13`: `14097112010`, `windows-wheel-cp3.14`: `14097037831` | 2090 passed, 2 skipped, 1 xfailed |
| macos | `43553098353` | `macos-arm64-wheel-cp3.11`: `14097241005`, `macos-arm64-wheel-cp3.12`: `14097216265`, `macos-arm64-wheel-cp3.13`: `14096928056`, `macos-arm64-wheel-cp3.14`: `14097375664` | 2090 passed, 2 skipped, 1 xfailed |

All 12 hosted lanes passed using pinned V8 15.0.245.19. The macOS helper
pinned both source checkouts to the verified source. Restore commit
`509441d6a9b4ed73f0c94db6f98fe8967b99b32c` restored workflow blob
`eb730d3e77a17d4d527e67646475385cebb2428c`. Required V8 checks
`43553061427` and `43553068202` passed. The complete native release
receipt is `data/page-split-stage2/build-verification.json`; local commands,
result counts and hashes are in `local-verification.json` in that directory.
These records are ignored local build evidence. Later handoff commits change
documentation only and preserve the verified native/build/test sources.

Stage 1 PR #17 merged as `747667b` after final required checks
`43552733883` and `43552739184`. The optional skeleton CI still targets
retired macOS runner labels; its obsolete queued jobs were cancelled only
after all eight Linux/Windows jobs passed. Native macOS acceptance uses the
successful four-version hosted release matrix above.

**Stages 1 and 2 are complete. The user authorized stage 3 on `2026-11-14`.**

## Stage 3.1 — event targets (`2026-11-14`)

The two unchanged event-target class definitions live in private
`host/event_target.h`. Their inline member bodies, listener handles, dispatch
and teardown remain intact. Non-DOM event support retains its stage 1 owner
`internal/event_support` because web leaf modules also use it. No empty source
file or wrapper layer is added. `page_state.cpp` is **8,436 lines**.

The move audit matched **237 definitions, 0 differences**. The native syntax/link
build passed. Reflection matched **27 root object graphs**, extending the stage 2
snapshot to element/Text/Comment/Fragment/collections/classList/MutationObserver/
FileReader/EventTarget. Focused events/Web IDL tests passed **170 tests, 1 xfailed**; the complete
real-V8 suite passed **2090 tests, 2 skipped, 1 xfailed**. All **56 audit
tests** passed. Independent header compilation without PCH passed under
`-Wall -Wextra -Werror`, with the existing V8 unused-parameter and WatchHit
aggregate warnings handled as recorded in the plan.
The required V8 CI gate follows PR submission; the complete platform release
matrix follows all seven stage 3 slices.

## Stage 3.2 — node host declarations (`2026-11-14`)

PR #19 merged as `ada54c3` after V8 checks `43558729900` and `43558735606` passed.
Step 3.2 moves the four unchanged node-host class declarations (including existing
inline members) into `host/node_hosts.h`. Out-of-line implementations remain in
Page until step 3.6; the document back-pointer is forward-declared.
`page_state.cpp` is **8,048 lines**. The move audit matched **235 definitions,
0 differences**. Native syntax/link and standalone header compilation without
PCH passed. Focused tests: **235 passed, 1 xfailed**; full real-V8 tests:
**2090 passed, 2 skipped, 1 xfailed**. All **56 audit tests** passed, and **27
reflected root object graphs** matched. The required V8 check gates this slice.

## Stage 3.3 — DocumentHost members (`2026-11-14`)

PR #20 merged as `1defe22` after V8 checks `43559124965` and `43559130046`.
The **45** previously inline DocumentHost methods now have out-of-line bodies in
`host/document_host.cpp`; `document_host.h` retains the class declaration,
defaults, access levels and member layout. The unchanged state/binding types live
in their host headers. `host/file_reader_state.h` holds the generation-owned
FileReader state so DocumentHost need not depend on the later web callback module.
Callback implementations remain in Page for steps 3.4 and 3.7.

The move audit's opt-in `--class-members DocumentHost` decomposes only the named
class into its complete declaration/data contract and qualified method bodies.
Constructors/destructors, default arguments, access, qualifiers, field and
constructor initializers are gated. Unsupported member declarators fail closed;
other classes keep their existing whole-definition gate. **27 new regressions**
cover accepted moves, changed bodies/contracts, missing/duplicate implementations,
unsupported declarators, token source spans and CLI exit status, for **83 passed**
audit checks including the original 56.

The native syntax/link build passed. The member-aware move audit reports
**276 identical definitions, 0 differences**. Focused tests passed **235 tests,
1 xfailed**, full real-V8 tests passed **2090 tests, 2 skipped, 1 xfailed**, and
**27 reflected root object graphs** matched. Independent host headers and the
new source compile without PCH under the established strict-warning exceptions.
`page_state.cpp` is **6,957 lines**. Required V8 CI follows this PR.

## Stage 3.4 — collections and observers (`2026-11-14`)

PR #21 passed V8 checks `43559837029` and `43559841756` and merged before this
slice. The live-collection, classList and MutationObserver definitions move into
three host implementation modules. Page calls the same callbacks during install;
the observer tag retains one definition with an extern declaration for the
installer. The move gate's member diagnostics now retain the owning namespace,
with an existing regression strengthened to check that attribution.

The audit matched **227 definitions, 0 differences**. Native syntax/link,
standalone host headers/sources and **83 audit tests** passed. Focused tests:
**201 passed, 1 xfailed**; full real-V8 tests: **2090 passed, 2 skipped,
1 xfailed**. All **27 root object graphs** matched. `page_state.cpp` is **6,058
lines**. Required V8 CI gates this slice; stage release acceptance remains pending.

## Stage 3.5 — document tree mutation (`2026-11-14`)

Ten unchanged DocumentHost tree/fragment/variadic-node implementations move into
`host/document_tree_mutation.cpp`. They continue to use the same node pools,
validation, detach/insert ordering and observer notification methods.
The move audit matched **182 definitions, 0 differences**. Native syntax/link,
standalone host headers/sources and **83 audit tests** passed. Focused tree/HTML/
observer tests passed **137 tests**; the full real-V8 suite passed **2090 tests,
2 skipped, 1 xfailed**. All **27 root object graphs** matched.
`page_state.cpp` is **5,299 lines**. Required V8 CI gates this slice.
