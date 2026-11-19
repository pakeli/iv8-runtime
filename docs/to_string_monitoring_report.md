# TOSTRING-MON-01 acceptance report

Date: `2026-11-17`. **Implemented and locally accepted; full release verification pending.**
Boundary: [to_string_monitoring.md](to_string_monitoring.md).
Subsequent receiver-logging repair and its separate results:
[to_string_receivers_report.md](to_string_receivers_report.md).

## Delivered behavior

The two Page intrinsic entries now record host_object CALL events with fixed
Function.prototype/Object.prototype labels, real extra argument summaries and
full return strings or existing synchronous exception completion. Hooks are
installed before user scripts, so aliases and bound calls cached before start
remain observable. Direct, detached, call/apply/bind and Reflect.apply forms
preserve raw receiver behavior, including null/undefined/primitives. Two native
owner tokens are generation-owned; stop/filters/capacity and reload/disposal
retain existing semantics. No new Python watch API is introduced.

Native V8 Proxy apply interposers delegate the original builtins. Function
stringification maps only the two private monitor entries to their originals
before V8 generates source; no user Proxy is unwrapped, no JS wrapper or hardcoded
source is used. Each operation retains original tag getter/trap counts, return
and thrown identities. Safe metadata does not inspect function name or execute
extra conversion getters. Internal runtime calls stay silent; user tag accessors
keep provenance even for bound native callbacks and filtered/unretained calls.

The inherited generic-binding shape check now asserts its six actual
Function.prototype.toString CALLs from String(method), alongside the original
six defining-prototype GETs. Treating those real user stringifications as noise
would hide the behavior this task repairs.

## Reference and verification

Fresh Chrome **157.0.8103.51**, captured at UTC `2026-11-16T16:00:11` (Shanghai
`2026-11-17`), supplies both intrinsic metadata, 46 receiver factories, 72 call-form
comparisons and side-effect observations. Fixtures, probe hash and loopback-only
execution conditions are in `test/fixtures/to_string_monitoring/`. Tests remain
offline and use that committed reference. Pinned V8's revoked-Proxy wording was
also checked against the reference.

New monitoring tests: **35 passed**; new plus generic-binding regressions:
**71 passed**. Early related shape/origin/mode/M9 checks: **226 passed**. All three
release workflow module lists match. Final installed-wheel gate: **31 modules / 748 passed**; full suite:
**2974 passed, 2 skipped**. Required Linux V8 CI source/run evidence is recorded
after push in the source receipt and PR metadata.

The local Python **3.11.18 / macOS arm64** wheel is rebuilt against the unchanged
pinned **V8 15.0.245.19** monolith, then installed into a fresh isolated environment.
Import origin, linked engine/version, wheel ABI tag/CRC and actual debug output
are checked. Artifacts, logs and source/native hashes live under ignored
`data/to-string-monitoring-2026-11-17/`; see `source-receipt.json`.

## Explicit limits

This is a monitoring interposer, not an unchanged builtin object: Inspector can
observe Proxy Target/Handler/subtype, and a future pristine cross-Realm
stringifier could distinguish it. Page currently has no cross-Realm bridge.
Full internal-kind transparency would require a separate engine hook/build.
The original builtin algorithms produce outputs/errors; original error objects
and stacks are propagated without rewriting. Engine constructor rejection
before apply entry records no fabricated invocation. User replacements retain
normal semantics and remain outside this intrinsic observer; cached observer
aliases continue to work. Symbol property reads, array/index paths and other
independent callback gaps remain open.

This batch updates PR #32. Full 12-lane release verification is pending; older
fixed-source release/fallback evidence remains separate. No merge, version tag,
shared SDK replacement, network or M1-kernel capability is added.

Local wheel SHA-256: `2347bd108e324a52429e47afc7b1936b5b0cf6268eb05ffdb1460f3283b23204`.
