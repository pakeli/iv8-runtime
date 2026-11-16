# TOSTRING-MON-01 — intrinsic stringifier call monitoring

Authorized `2026-11-17`: record method/object toString detection through
Function.prototype.toString and Object.prototype.toString in the Page monitoring
layer. No new Python watch API, JS global, network or M1 kernel change. This is a
post-M11 structured trace slice, not an extension of the historical M9 watch APIs.

## Native entry and observable contract

Install once during each generation's bootstrap, before user scripts, independent
of whether tracing is enabled. Direct calls, cached-before-start aliases,
call/apply/bind/Reflect.apply use the same entries. Start/stop never reinstalls
over a user's replacement. Replacing/deleting either prototype member retains
ordinary JS semantics; saved monitoring aliases still work in their generation.

Each retained call uses `host_object`, label `Function.prototype` or
`Object.prototype`, member `toString`, operation `call`, with two stable
generation-owned identities. Capture actual extra arguments (receiver is not an
argument), full safe return strings and existing synchronous exception metadata.
No receiver name/tag/source is inspected to construct a label. Object values
remain type-only. Native browser API stringifiers such as location.toString keep
their existing entries; arbitrary user-defined replacement toString functions
are not rewritten or monitored by this intrinsic slice. Constructor rejection
before the apply boundary produces no fabricated CALL event.

## Receiver metadata update — `2026-11-17`

The user reports that an entry such as Function.prototype.toString() hides the
actual receiver and can imply the wrong result for a direct prototype call.
Each retained intrinsic CALL now includes optional `receiver_summary`, using the
existing complete primitive/type-only encoding. `receiver_label` is present only
for a direct native HostObject with a nonempty native trace_label. Capture uses
the raw apply receiver before mapping either private monitor Proxy to its target.
Never infer labels from function.name, constructor, Symbol.toStringTag, returned
text or JS variable names; do not unwrap user Proxies or inspect their properties.
Unknown objects/functions retain type-only summaries.

Receiver fields are separate from argument_count/argument_summaries, which still
count only actual extra arguments. New text events normalize the invocation as
`CALL <entry>.call(<receiver>[, <extra arguments>])`, regardless of whether the JS
used a direct, bound, call/apply or Reflect.apply form. This is an explicit
receiver view, not reconstruction of the original source syntax. Native host
labels are bare identifiers when possible and JSON-quoted otherwise. Existing
reports without receiver_summary retain their old text, without guessing this.
None/missing receiver metadata is explicitly marked, never invented. Returned
values, exceptions, native function shape and provenance remain unchanged.

Examples:

```text
CALL Function.prototype.toString.call(<Function>) -> "function createElement() { [native code] }"
CALL Object.prototype.toString.call(<HostObject: document>) -> "[object HTMLDocument]"
CALL Object.prototype.toString.call(null) -> "[object Null]"
CALL Object.prototype.toString.call(undefined) -> "[object Undefined]"
```

Current receiver-update results/source/release evidence:
`docs/to_string_receivers_report.md`.

The original V8 builtins still compute results/errors. V8 API Function callbacks
coerce null/undefined receivers, so this monitor uses V8 Proxy apply interposers
with native C++ traps to preserve raw this. No JS wrapper or hardcoded function
source is installed. Function stringification recognizes only the two private
monitor interposers by identity and delegates their underlying originals to V8;
user Proxies remain untouched. Names, lengths, own keys, descriptors,
constructability and ECMAScript results follow the Chrome fixture. The two
stringifiers ignore their extra arguments; those are logged without coercion.

This is an explicit monitoring interposition: the exposed entries internally are
runtime-owned Proxies over the original builtins. Inspector can expose their
Target/Handler/subtype; a future pristine cross-Realm stringifier could expose
that kind too. Do not claim unchanged builtin identity/internal kind or complete
cross-Realm transparency. Page currently provides no cross-Realm bridge. An
engine-level hook preserving these internals would be a separate V8 build change.

## Provenance, lifecycle and acceptance

Internal runtime stringification stays silent and consumes no event capacity.
User tag getters/Proxy traps and native accessors invoked by an actual operation
retain user attribution and original call counts/errors; diagnostics never read
name/toString/toJSON or invoke a tag getter. Existing filters, capacity, stop,
clear, reload, disposal, Page isolation and debug/prod behavior remain intact.

Chrome 157.0.8103.51 fresh-profile loopback comparison fixtures and conditions:
`test/fixtures/to_string_monitoring/`. Compare both intrinsic function metadata,
strict null/undefined/primitives, ordinary/native/bound/user functions, user and
revoked Proxies, custom tags, exceptions and all supported call forms. The old
M9 `watch_function_to_string` API exclusion stays unchanged; this is structured
access tracing. Current results/source/release status:
`docs/to_string_monitoring_report.md`.
