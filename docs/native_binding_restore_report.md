# BIND-01 — native binding metadata restoration report

Date: 2026-11-15. Implementation and four-version local acceptance complete;
cross-platform release verification and shared SDK deployment pending.
Boundary: `native_binding_restore.md`.

## Audit and repair

Baseline mainline `cd1699d` retains NAMED-01/History but still lacks all remaining
metadata changes in the private Epub candidate based on `6191958`.

| Existing installation path | Confirmed gap | Restored behavior |
|---|---|---|
| Eight Reflect/Object watch/trace wrappers | Anonymous, arity zero, constructable, own prototype | Named non-constructable native operations, reference arity, no own prototype |
| Eight Performance methods | Default FunctionTemplate shape | Existing native method factory with per-method names/arities |
| Two Crypto methods | Default FunctionTemplate shape | Same native factory; getRandomValues arity 1, randomUUID arity 0 |
| Six Event-family constructors | Already named native/constructable/own prototype, length zero | Arity one, retaining FunctionTemplate inheritance and construction |

The shared factory already implements `ConstructorBehavior::kThrow` plus names
and arities. Only the remaining installation paths change; original delegates,
callback code, flags, Data/PerformanceStore and lifetime ownership remain intact.
Added fallible creation/property-definition paths check Maybe results rather
than copying the candidate's unchecked ToLocalChecked calls. No runtime API,
clock, crypto random source or exception body is added by this slice.

## Browser and regression evidence

The candidate's original 20 tests are preserved, including immutable Event
fixture SHA-256 `147f4b6cf692338be1d6959c621eba4ec39fd79bd7affd0dfc303fe7aff593ce`.
That file contains no provenance metadata; its source document records
2026-11-08, Chrome 156.0.8078.80, macOS arm64 and HTTP publication origin.

A separate capture of Chrome **157.0.8103.51** records all 24 changed functions'
name, length, engine-native source, own prototype and constructability. It uses
a fresh temporary headless profile and loopback HTTP secure context; no target
function is called (constructability uses it only as Reflect.construct's
newTarget). Background networking is disabled and non-loopback DNS is blocked.
`chrome_current.json` preserves capture conditions and probe hash, SHA-256
`ccb41c5eccd917873a5d549ebe05b1238d0e29e8db18de0ea13435cb56dd6ab0`.
All captured metadata agrees with the old candidate's expectations.

Eight new behavioral/integration tests cover installer ordering/idempotence,
normal proxy/Symbol/exception delegation, cached disabled wrappers, independent
channels across load, and combined History/named/event use. Three additional
monitoring modes compare all 24 functions against the new capture. Binding has
31 cases; together with History 30 and named access 26, the focused gate is
**87 tests** on every installed release wheel.

The prior NAMED-only wheel fails all original 20 binding cases; the expanded
negative control has **27 failed, 1 passed** before the new browser comparison
cases were added. The new candidate passes the complete focused gate.

## Local wheels

All macOS arm64 CPython 3.11–3.14 wheels target macOS 11.0 and pinned V8
15.0.245.19. Each isolated installed wheel passes **87 focused** and
**2371 passed, 2 skipped, 1 xfailed** full-suite checks (2,374 collected).

| Python | Wheel SHA-256 |
|---|---|
| 3.11 | `e32003345e77203ac321fb8355ff0f8fba0f8f12feb5c83ff1513f5c2964f1f4` |
| 3.12 | `eb0cc147afe35905ea69657f06d61d93ae16dd9e153930477317e69a254af4b6` |
| 3.13 | `fb741971bbe85c639d7d5191223baf566ed9531279c19ae8d64c711067b73405` |
| 3.14 | `7e61794f11e653a59028b3d90e43efb05f87a0f811336f5af2e66c79001e7a41` |

Logs, source/binary receipts and the negative controls are in ignored
`data/binding-restore-2026-11-15/`. Release source/tree/run/artifact identities
will be recorded after all 12 lanes finish; local results do not replace that
release verdict.

## Cached EPUB execution

An unchanged cached HTML (SHA `4776d302e1281846485d1b7f2bc8d245cc766da13e28763c47bc2bff88e79e03`)
and original script (SHA `bab33462b36b783f31464d3710d409892ca7056f73a5fbcdfbb41a053d305bd5`)
are pinned by a private manifest. Environment is extracted from literal caller
configuration AST, without importing its request-performing entry point.
There is no initial Cookie seed or expected-error override.

The new cp313 build completes load, jobs and bounded advance 100/1000 with no
first synchronous/asynchronous error, two checks passing, one navigation intent,
4,582 trace events and no drops. Execution/check/environment verdicts pass;
server remains `not_tested`. This is local execution evidence, not Cookie/output
equivalence or live acceptance.

## Known behavior boundaries retained

This slice restores the candidate's metadata, not complete behavioral
transparency. Three pre-existing limitations are independently identified in
the same callback bodies in both mainline and the old candidate:

- The hasOwnProperty native wrapper normalizes null/undefined `this` before
  delegation, changing that receiver error behavior while/after trace is enabled.
- Reflect.get/has watch may coerce an object key twice, so side-effecting keys can
  change results. This is distinct from the trace-only non-coercing summary path.
- Base Event's callable-without-new path remains different from the browser;
  the candidate only changed arity. Its five subclasses already check new.

Synthetic evidence and callback hashes are in `known-limitations.json`. These
gaps require separate behavior repairs; no all-receiver/all-conversion or full
Event compatibility claim is made here.

## Deployment boundary

The actual pre-install shared SDK is now the NAMED-01 CI package, native SHA
`4b2f871d40a97452b27a461390c7fe21a46ce149fb66a7f574af4253bed774d4`,
wheel SHA `1c0ba23a815ee35efce38f27d8825bbff9efa66f47402c2a1de2e1f923622ed3`.
Do not use the obsolete History-only or Nov8 backup as this task's rollback point.
After release acceptance, back up the actual package and dist-info with file
hashes, install the verified cp311 wheel without dependency changes, and verify
the shared interpreter/module/native identity, combined gates and cached EPUB.
No live business request is part of deployment acceptance.
