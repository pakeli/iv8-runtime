# BOM-02 Navigator acceptance

Date: `2026-11-19`. **Implemented; all four local Mac Python lanes and eight hosted
Linux/Windows lanes passed. All 12 wheel artifacts are verified in the release report.**
Boundary: `docs/bom_02_navigator.md`.

## Baseline and browser evidence

Five pinned offline scenarios ran with the accepted BOM-01 macOS CPython 3.11
wheel. History and EPUB execute and pass their checks; two negative controls
match the expected failures. Jetstar completes load and all pump steps without
an execution error, while its old expected NotSupportedError is absent. The
original runner exit code 1 is retained as a stale regression expectation.
There is no new execution blocker or live server verdict. Original manifests
and asset hashes are unchanged. Evidence:
`data/bom-02-2026-11-19/baseline-offline/receipt.json`.

The user-approved baseline integration merged PR #32 at exact head 520fb4d after
the required V8 checks passed. Merge commit:
`090c604e855af0e6938c1680a2bde2234e78f11f`. BOM-02 uses the separate branch
`codex/bom-02-navigator`.

Fresh **Chrome 157.0.8103.80**, macOS arm64, secure and insecure loopback-backed
documents provide the committed `test/fixtures/bom_02/` evidence. Each capture
records origin, flags, wall time, requests and probe SHA-256. Native asynchronous
browser callbacks are awaited through a bounded local DevTools pipe; no user
profile or business site is accessed.

The vibration reference includes 69 successful/error cases with actual side
effects. Real arrays use indexed conversion and their live length; non-array
iterables may read their iterator twice. Result value precedes done in this
Chrome implementation, and conversion failures do not invoke iterator.return.
Related-app cases cover secure-only exposure, fresh Promise/array identity,
ignored extra arguments, rejected receiver failures and result lookup after
the current microtask checkpoint.

## Delivery and verification

Both native bindings, security exposure and task delivery are implemented.
Two new offline modules pass **190 tests** on a freshly built/installed macOS
arm64 CPython 3.11.18 wheel linked to unchanged pinned **V8 15.0.245.19**.
The **36-module combined gate passes 1107 tests**; full suite is
**3333 passed, 2 skipped**. Timer/event-loop/origin regression checks also pass.
Installed import origin, ABI tag, wheel CRC and wheel/native hashes are verified.

Secure Navigator membership increases **32/70 → 34/70**, leaving 36 reference
names absent. In an insecure HTTP Page, the same fixed secure-reference names
have 32 present: the existing getBattery and new getInstalledRelatedApps are
both absent. No new interface global is introduced. The exact delta and context
qualification are recorded in `docs/bom_02_api_delta.json`.

Candidate code/test/build/workflow bytes are frozen in
`data/bom-02-2026-11-19/macos/candidate-manifest.json` (the matrix runner's manifest)
before each local lane. CP311 build/install/focused/gate/full logs and artifact
identities live under `macos/cp311/`; other lane receipts are
`macos/cp312/verification.json`, `macos/cp313/verification.json` and
`macos/cp314/verification.json`. All four new built/installed wheels pass
**1107 gate / 3333 passed, 2 skipped full**, including CPython 3.11.18, 3.12.4,
3.13.5 and 3.14.8. No old build environment or shared SDK is modified.
Linux/Windows dispatch follows local Mac acceptance. Release evidence is recorded
in `docs/bom_02_release_report.md`.

## Remaining boundaries

The Page stays a single top-level context with no installed related apps,
manifest/OS lookup, trusted activation or vibration device. Secure-only exposure
uses the pre-existing Page URL classification; its other profile differences
are not silently changed. Related-app delivery uses the existing explicit task
queue, while standalone run_jobs/run_timers retain independent pump semantics.
Other BOM families and Symbol.iterator monitoring remain in their own queues.

An independent diagnostic review of 50 non-callable Symbol.toPrimitive hook and
prototype-metadata variants found zero extra getter/Proxy-trap invocations with
tracing on or off. In 34 variants, Chrome safely renders `[object Object]` where
the runtime's ordinary-object diagnostic normalization renders `#<Object>`.
This is an explicit generated-error text/profile difference, not a change to
user-thrown error identity or conversion effects. The captured ordinary cases
still compare complete messages. Arbitrary original receiver-class diagnostic
labels are not fully modeled. Evidence:
`data/bom-02-2026-11-19/vibration-diagnostic-review/comparison.json`.

BOM-02 is committed as 1dff793; d97412d changes only Windows probe checkout
line endings. The full validated source/run/artifact map is in
`docs/bom_02_release_report.md`; all 12 wheels are verified. PR #33 remains a
draft and shared SDK deployment remains unchanged.
