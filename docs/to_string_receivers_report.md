# toString receiver logging repair

Date: `2026-11-17`. **Implemented and locally accepted; full release verification pending.**
Boundary: [to_string_monitoring.md](to_string_monitoring.md), receiver update.

## Problem and repaired view

The intrinsic observer previously printed only its defining entrypoint, for
example Function.prototype.toString() returning createElement source, or
Object.prototype.toString() returning HTMLDocument. The actual V8 results were
correct, but omitting the receiver made these look like direct prototype calls.

Retained intrinsic events now add `receiver_summary`, using the existing full
primitive/type-only encoding, and optional `receiver_label` for a direct native
HostObject. Capture precedes internal monitor-Proxy mapping. Labels come from
native trace_label only, not JS name/constructor/toStringTag or returned text.
User Proxies are not unwrapped; Window/plain objects/managers retain type-only
summaries when they lack native HostObject backing. No new receiver-ID mechanism
or retained JS value is added.

Text normalizes `.call(this, ...extraArgs)` while raw argument_count and
argument_summaries continue to describe only actual extra arguments. Native
labels are identifiers or JSON-quoted/escaped strings. Events/reports remain
immutable during formatting. Older reports without receiver fields keep their
old form, without inventing this. Actual return values, exceptions, getter counts,
origin filtering, lifecycle and the existing interposer boundary stay unchanged.

Examples from the new installed package:

```text
CALL Function.prototype.toString.call(<Function>) -> "function createElement() { [native code] }"
CALL Object.prototype.toString.call(<HostObject: document>) -> "[object HTMLDocument]"
CALL Object.prototype.toString.call(null) -> "[object Null]"
CALL Object.prototype.toString.call(undefined) -> "[object Undefined]"
```

These are canonical invocation displays, not a claim to reconstruct the exact
source syntax used by direct, bound, call/apply or Reflect.apply calls. Unknown
functions stay `<Function>` rather than inspecting a possibly user-defined name.

## Verification and release status

- Two changed modules: **133 passed**; three new native cases and 38 new pure
  formatter cases (**41 new cases**) supplement the strengthened prior coverage.
- Covers direct prototypes/native DOM methods/Document/monitor functions,
  primitive/null/undefined, user and revoked Proxies, labels without getter
  reads, actual argument count, long strings/legacy encoding, error paths,
  safe labels, saved reports and unmodified snapshots.
- Independent review confirms direct-host metadata does not execute JS or unwrap
  Proxies. Formatter unsafe-label/snapshot checks pass on the new CP311 package.
- Fresh Python 3.11.18 / macOS arm64 installation verifies linked V8 15.0.245.19,
  package origin, wheel tag/CRC and actual debug output.

Build, install, focused/gate/full logs and source/native/wheel hashes live under
ignored `data/to-string-receivers-2026-11-17/`; see `source-receipt.json`. Final
installed gate: **31 modules / 789 passed**; full suite: **3015 passed, 2 skipped**.
Required Linux V8 CI source/run evidence is recorded after push in the source
receipt and PR metadata. Existing monolith/pin are reused and the extension/wheel rebuilt.

This updates PR #32. Full 12-lane release verification remains pending; earlier
fixed-source release records remain separate. Inspector-visible Proxy internals
and pristine cross-Realm transparency retain the documented TOSTRING-MON-01
limits. No shared SDK installation, PR merge, version tag, network or new M1
capability is included.

Local wheel SHA-256: `d00ce5c1bf94b9e6789d7b6627e12d3662af0a7a57b687c789cd85582e479b71`.
