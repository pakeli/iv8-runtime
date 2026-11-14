# Window/form named access restoration

Authorized 2026-11-15: port the existing Epub candidate's named-access repair
into the current split source tree, include its regressions in release
acceptance, and build wheels retaining both History and named access.

## Boundary

The prior candidate is the working tree based on `6191958` under
`iv8-test/epub/runtime-src`; it was not merged into this repository. Installation
of source `0f0ab4b` on 2026-11-14 replaced that candidate with a History-capable
wheel missing the private named-access implementation. This work ports those
semantics without reverting History or the Page source split.

- WindowProperties resolves active-document element IDs and existing supported
  form/img/embed/object name attributes. Existing globals and inherited browser
  members preserve priority; duplicate names use the current live collection.
- HTML form named access covers the current input/button/select/textarea
  descendant model. Control names precede inherited IDL properties, with the
  original browser-derived query/descriptor/write/definition behavior. Past
  names retain access order and expire when ownership changes.
- Resolve against generation-owned DOM/wrapper state, preserve wrapper identity,
  trace named reads through existing capture, and reset on load/dispose.
- Preserve the candidate's immutable Chrome156 reference and regression suite,
  with additional History/coexistence/lifecycle regressions. RadioNodeList,
  cross-tree form ownership, iframe/Worker, navigation execution, networking and
  candidate-only unrelated callback metadata changes remain separate gaps.
- Release pipelines must explicitly run History and named-access focused tests
  against installed wheels before their full suite. A package version alone
  cannot identify feature coverage; record source and binary/wheel hashes.

## Acceptance

First demonstrate the old/current negative control, then run browser-derived
named regressions, History and monitoring/lifecycle checks on a real-V8 build.
Build CPython 3.11–3.14 wheels and use each installed wheel for acceptance.
Keep local acceptance separate from the 12-platform-lane release verdict.
No live business request or shared-SDK replacement is needed for this port's
offline execution acceptance. User-owned candidate sources remain unchanged.

Implementation and verification are complete: all 12 platform/Python release
lanes pass on source `374c81b`, retaining both History and named access. Exact
counts, source/tree identity and artifact receipts are recorded in
`docs/named_access_restore_report.md`.
