# Web IDL environment repair report

Implemented on **2026-11-07**. Release verification is pending.
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
static-linked macOS arm64/Python 3.13 wheel passed **135 focused tests**, followed
by **2048 passed, 2 skipped** (2,050 collected). This includes the final lifecycle
assertion. Full release lane verification is pending.

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

The latest verified source remains `15edd82`, tree
`905d1f671bc31fd25b2aee78c13258a6318ec36a`. Record the integrated source SHA,
tree, V8 gate, all platform/Python release runs, artifact IDs and actual test
counts here before marking this batch verified.
