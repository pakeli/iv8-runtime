# Generic binding monitoring local acceptance

Date: `2026-11-16`. **Implemented and locally accepted; full release verification pending.**

Boundary: [generic_binding_monitoring.md](generic_binding_monitoring.md).

## Delivered scope

Lazy generic interface registration installs shape-preserving native data hooks
on defining prototypes. Direct/inherited prototype method references emit one
completed GET on the defining prototype. Canonical instance reads return the
same cached Function from the existing interceptor, retaining instance
get_method/call folding without a duplicate prototype GET. Override/accessor,
assignment, deletion and descriptor semantics remain unchanged.

Method and IDL getter/setter brand rejections establish CALL/GET/SET completion
before the original Illegal invocation TypeError, without coercing arguments or
reading receiver properties. Shared binding failures use their defining
prototype's generation-owned identity. Own Location/Document and legacy methods
use their original native owner; this owner is diagnostic only. Own accessors
hold a generation-owned native pointer rather than capturing their JS receiver.
Console's existing receiver normalization is unchanged. Private binding arrays
use CreateDataProperty so Array.prototype setters cannot intercept their metadata.

Only src/core/host_object.cpp changes native implementation. It reuses the
existing native data/completion/origin helpers; no browser API, native function
wrapper, V8 engine pin, M1 API, Python mode or event schema is added.

## Actual acceptance

- **36 new tests**: prototype references, cached failures, getter/setter/method
  and wrong native/primitive/Proxy receivers, no coercion or diagnostic getters,
  preserved native shape/data descriptors, borrowed/base calls, override/delete,
  filter/capacity/stop/load/Page isolation and quiet debug output. Own Location
  bindings preserve identity without adding Location.prototype methods. Lazy
  registration does not execute an Array.prototype setter.
- Old-build negative checks: the original 33 cases have 28 failures / 5 passing
  behavior controls; the added wrong-native-brand case also fails on missing
  entries. The two additional root controls pass the fixed candidate.
- Related focused modules: **233 passed**.
- Updated identical Linux/Windows/macOS release gate: **25 modules, 558 passed**
  against the extracted real-V8 local wheel.
- Full macOS arm64/Python 3.13 suite: **2848 passed, 2 skipped**.
- Independent review finds no blocker in owner lifetimes, origin/capacity gating,
  receiver validation, native shapes or preserved instance folding.

Local wheel SHA-256:
`b9afa74b6c1866546c37af6d2506a0dcde04d1f1e07ed109e3b0f1664d8ac37a`.
Source/native/log hashes and isolated wheel are under ignored
`data/generic-binding-monitoring-2026-11-16/`; see `source-receipt.json`.

## Limits and release status

The two generic binding gaps are closed within this shared path; independent
callback families are not silently claimed covered. Symbol/structural queries,
engine-rejected operations and runtime-owned returned structures remain open.
The user-origin noise fix still applies before sequence/identity/capacity use.
Watch channels keep their existing independent dispatch contract.

This patch updates existing PR #32 after local acceptance. Full release lanes
are not rerun by this local implementation report; keep the prior fixed-source
release/local-fallback records separate. No shared SDK installation, version tag
or merge is performed. The engine remains pinned to V8 15.0.245.19.
