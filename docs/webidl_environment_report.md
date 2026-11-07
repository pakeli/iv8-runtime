# Web IDL environment repair report

Implemented and release-verified on **2026-11-07**.
The approved boundary is `docs/webidl_environment_semantics.md`.

## Completed sequence

1. Native method creation now carries names and required argument counts,
   exposes native source, rejects construction and validates receivers. Storage
   borrowed calls use the actual receiver's backing store.
2. A context-private interface registry owns constructors, inheritance,
   Symbol.toStringTag and shared function identities. It includes the Window
   WindowProperties link, HTMLDocument/Document/Node/EventTarget, Screen's
   EventTarget base and HTML tag interface selection.
3. Attributes and methods use defining prototypes. Document.location and Location
   members have their instance descriptors. The supplied Chrome probe matches
   all **75 of 75** results. Related tests cover constructors, forged receivers,
   subclassing, shared methods, prototype mutation, isolation, navigation intents,
   explicit capability failures, root tree constraints and lifecycle.

The 75-expression probe and saved Chrome results are repository-local fixtures
under `test/fixtures/webidl_environment`. Tests use synthetic offline documents.
A fresh local Chrome check also confirmed constructor failure modes, Window
method identity, Node constants and additional HTML tag interface identities.

## Local verification

The first binding stage passed **1949 passed, 2 skipped** locally. The integrated
static-linked macOS arm64/Python 3.13 wheel passed **189 focused tests**, followed
by **2050 passed, 2 skipped** (2,052 collected). This includes the final lifecycle
assertion, nullable Node setter regression and document-root observer coverage.
The two skips are the V8-free-only lifecycle/framework guard tests.

The initial development loop resolved V8 symbols from the prior pinned wheel.
The integrated checkpoint above used the normal CMake static-monolith build.
The SDK came from restored pinned macOS cache run `42324406100`, artifact
`13554711733`, archive SHA-256
`54e2dd06c3e4796212acd5980d11ade3fd0b1abc9038d10481937617cba152a8`.
Its BUILD_INFO records V8 `15.0.245.19`, commit
`209c9cea0db17d8caf23e9d2c7de08c351609744`, macOS arm64 and deployment target
11.0. The archive digest matched GitHub before extraction.

The SDK export first encountered an unavailable `macos-14-arm64-us-default`
runner configuration. macos-15 completed the export. The temporary macOS helper
workflow was restored with a content-SHA guard after export.

## Release verification

Verified source `f42acae6757cae42e9f80e98624ef44b2de81ad2`, tree
`3d2d9351d45815e85f53f0f91fa4ad805fff49f8`. The implementation consists of
`8ec7e36` followed by `f42acae`. All release jobs below tested Python 3.11–3.14
on that source and each reported **2050 passed, 2 skipped** (2,052 collected).

| Platform | Run | Artifacts |
| --- | --- | --- |
| Linux x64 manylinux2_28 | `42326713519` | `13555309844`, containing cp311–cp314 |
| Windows x64 | `42326717311` | cp311 `13555811666`, cp312 `13556016439`, cp313 `13556156162`, cp314 `13555229492` |
| macOS arm64, macos-15 | `42326726715` | cp311 `13555487067`, cp312 `13554674682`, cp313 `13555606911`, cp314 `13555098734` |

The macOS jobs first passed **189 focused tests** in every interpreter using the
Web IDL, HTML string API and MutationObserver files. All four import checks
reported linked V8 `15.0.245.19`. Push V8 CI `42326682563` and PR V8 CI
`42326787518` also passed. The required Linux/Python 3.13 V8 job reported
`2050 passed, 2 skipped`.

macOS driver `939bf387bbf25064ef1ee2b04530aa68f8925633` pinned the checkout in
build-v8 and all four wheel jobs to the verified source. The five checkout logs
confirm that source. Restore commit
`fbc0377b70a9109fcdc52ee43d84a87f2b9d829d` returned the helper workflow to
original blob `eb730d3e77a17d4d527e67646475385cebb2428c` with a content-SHA
guard. History was preserved.

General CI's optional macos-14 jobs encountered the unavailable runner
configuration and its macos-13 jobs queued. Its Ubuntu/Windows jobs passed.
The supported macos-15 native release matrix above completed all four lanes.
Master retains the strict required `Linux x64 / Python 3.13 / V8` check from
app `15368`, with administrator enforcement enabled. PR #9 uses that gate.

## Installed-wheel acceptance

The cp311 macOS artifact archive matched GitHub's SHA-256
`6219f48a7f0a718e586e64bfd16cd031a1f0c26fc665e237e0c55baabf8e971c`.
The extracted `iv8-0.0.1-cp311-cp311-macosx_11_0_arm64.whl` has SHA-256
`6acc8f138dad3acf63a126a4bcda4c78c2bf5a00f30b8c113eb830e731e2172b`.

The wheel passed the user's original inert-capture `repro_environment.py` in an
isolated package directory with **75 total, 0 different**, then produced the same
result after installation into the existing `iv8-test` Python 3.11 environment.
The prior 27-expression browser compatibility report also passed **27/27**.
The installed runtime reports linked V8 `15.0.245.19`. The existing installation
was copied with a file-hash manifest before replacement. The backup, verified
wheel and deployment receipt are retained under ignored
`data/webidl-semantics/`. A fresh routed local Chrome session independently
matched all 75 saved reference results.
