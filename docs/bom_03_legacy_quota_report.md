# BOM-03 legacy quota acceptance

Local acceptance: `2026-11-20`; release validation: `2026-11-21`.
**Implemented; all 12 selected lanes and all 12 wheels validated.**
Boundary: `docs/bom_03_legacy_quota.md`.

## Delivered behavior

Navigator.webkitTemporaryStorage is added and returns the same stable native
DeprecatedStorageQuota as webkitPersistentStorage. Native accessors belong to
Navigator.prototype; quota methods belong to a shared private interface
prototype with no global constructor. The tag retains its browser data shape.
This replaces the old persistent-quota JS shim and its flat own-property shape.

queryUsageAndQuota validates its required callback and optional error callback,
then reports usage zero and the existing 10 GiB offline quota. requestQuota
performs unsigned-long-long conversion before callback validation and reports
the minimum of the converted request and available quota. It does not allocate
storage or consult user replacements of StorageManager.estimate. The runtime
and captured fresh Chrome empty-profile values coincide; the constant remains
an explicit runtime profile, not a measurement of the user's disk.

Nonopaque callbacks run as private Page tasks after microtasks. In opaque
contexts, query failure is a deferred NotSupportedError; request failure invokes
the AbortError callback synchronously. Both provide native DOMError values with
browser-derived name/message fields. DOMError's supported native constructor
preserves DOMString UTF-16 units and user conversion errors; Reflect.construct
reads NewTarget.prototype only once. Callback errors use opt-in storage_quota
diagnostics and do not stop subsequent delivery. Global ErrorEvent reporting is
not expanded beyond the existing Page diagnostic model.

Original DeprecatedStorageQuota.method watch selectors remain valid, including
rejected receiver attempts. The two Navigator alias selectors additionally
match the same native calls. Structured tracing emits one canonical CALL;
conversion and callback API reentry stays visible, and internal setup stays
quiet. Existing Symbol.toStringTag observation covers the new defining
prototypes without extending Symbol.iterator monitoring.

## Evidence and validation

`test/fixtures/bom_03/` records Chrome **157.0.8103.80**, macOS arm64, fresh
isolated profiles, exact probe hashes, request lists and CDP execution flags.
Secure HTTP loopback, mapped insecure HTTP and opaque about:blank profiles are
captured. Callbacks, number conversion, errors, aliases and native shape are
checked directly against the fixture; no business site is contacted.

Mac CPython **3.11.18**, pinned **V8 15.0.245.19**:

- **118 new tests passed**.
- **37-module / 1225-test regression gate passed**.
- Full suite: **3451 passed, 2 skipped**.
- The primitive conversion extraction also passes the existing vibration cases;
  the revised quota completion/tag tests retain assertions for the approved
  browser-derived behavior changes.
- The first combined gate exposed missing legacy quota watch hits. The binding
  restores those paths; old-script negative receiver behavior was verified on
  the previous installed wheel before adding the native regression.

Code/build/test inputs are frozen under
`data/bom-03-2026-11-20/macos/candidate-manifest.json`, candidate
`worktree-sha256:04a1b66edcd899249e6136ca4ab7469366ccb1c978a9ead5a566c333e9f920e9`.
Per-lane build/install/test logs and wheel/native/source hashes are in
`data/bom-03-2026-11-20/macos/`. Existing build environments and the shared SDK
are not upgraded. All local Mac lanes are complete:

| Python | Gate | Full suite |
|---|---:|---|
| 3.11.18 | 1225 passed | 3451 passed, 2 skipped |
| 3.12.4 | 1225 passed | 3451 passed, 2 skipped |
| 3.13.5 | 1225 passed | 3451 passed, 2 skipped |
| 3.14.8 | 1225 passed | 3451 passed, 2 skipped |

All four wheels are newly built and independently installed. CRC, ABI tags,
arm64/minimum OS 11.0, installed native/Python package provenance and source
hashes pass. All 442 code/build/test/checkout-policy inputs remain frozen.
`data/bom-03-2026-11-20/wheel-manifest.json` and `SHA256SUMS` identify the
collected artifacts under `wheels/macos/`. These are local validations with no
invented hosted run or artifact IDs.

Five pinned offline scenarios retain their prior execution outcomes: History
and EPUB pass; two negative controls match; Jetstar executes without a first
error while its old expected NotSupportedError remains stale. Original input
hashes match the prior baseline and no business request is sent. The runner's
exit code 1 is preserved for that stale expectation; server status is not_tested.
See `data/bom-03-2026-11-20/offline-receipt.json`.

## Presence and limits

Against the fixed secure Chrome157 inventory, Navigator increases **34/70 to
35/70**. The added name is webkitTemporaryStorage; persistent storage's name was
already present. DOMError is the single added global interface, required by the
quota error callbacks. DeprecatedStorageQuota has no global interface object.
Exact profile membership is in `docs/bom_03_api_delta.json`.

No disk writes, persistent quota allocation, new origin inheritance, iframe or
worker model, networking or JS-to-Python callbacks are implemented. Standalone
run_jobs and run_timers keep their separate-queue semantics. The existing
generated-error diagnostic object-label limitations under modified metadata
remain in the shared primitive conversion helper; user-thrown values are not
rewritten. Symbol.iterator monitoring remains deferred.

BOM-02 PR #33 is ready for review and remains unmerged. BOM-03 PR #34 is
also ready, stacked above it. Full Linux/Windows validation on source 4cbab17
subsequently completes: every gate passes 1225 tests; Linux full is 3451/2,
Windows 3449/4. The four prior Mac wheels are retained and all 12 artifacts are
verified. Source trees, CI/run/artifact IDs and integrity checks are recorded in
`docs/bom_03_release_report.md`. Shared SDK replacement and version publication
are not performed.
