# BIND-01 — native binding metadata restoration report

Date: 2026-11-15. **All 12 release lanes and shared SDK deployment verified.**
Mainline integration is through PR #30.
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
`data/binding-restore-2026-11-15/`. Verified implementation source is
`f015745266f5eb0cc3a3f59b9baada49e77d162b`, tree
`0914106d38d0c2c245b47e70daa7623b4ed04fc5`. Required V8 checks
`43675863843` / `43675916387` passed. All release wheels ran **87 focused tests**
before the full suite; every lane collected 2,374 tests.

| Platform, Python 3.11–3.14 | Run | Per-lane result | Artifacts |
|---|---|---|---|
| Linux x64 | `43675920281` | 2371 passed, 2 skipped, 1 xfailed | `14146119248`, four wheels |
| Windows x64 | `43675928957`, attempt 2 | 2369 passed, 4 skipped, 1 xfailed | cp311 `14146039847`, cp312 `14146377947`, cp313 `14145914048`, cp314 `14146421823` |
| macOS arm64 | `43675968125` | 2371 passed, 2 skipped, 1 xfailed | cp311 `14146078677`, cp312 `14146552427`, cp313 `14146278262`, cp314 `14146298198` |

Windows has two extra Unix-device skips. Its first cp311 run passed all 87
focused cases but one existing offline manifest-schema test exceeded its
2-second worker deadline. Only that failed lane was rerun, without source
changes; it passed. Both attempts and the initial failure remain recorded.

macOS driver `942f466ea87e7754e6fedc629135462f34086ec6` pins both checkout
sites to the exact implementation source; all five actual job checkouts agree.
Restore `aa0bfe3c0d57b59dc18bb15e53064f552406644d` restores original workflow
blob `eb730d3e77a17d4d527e67646475385cebb2428c` with a SHA guard. Branch
protection and required checks are unchanged.

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

## Verified shared SDK deployment

The actual pre-install shared SDK was the NAMED-01 CI package, native SHA
`4b2f871d40a97452b27a461390c7fe21a46ce149fb66a7f574af4253bed774d4`,
wheel SHA `1c0ba23a815ee35efce38f27d8825bbff9efa66f47402c2a1de2e1f923622ed3`.
Do not use the obsolete History-only or Nov8 backup as this task's rollback point.
The actual package (20 files), dist-info (7 files) and a byte-verified rollback
wheel were backed up under ignored
`data/binding-restore-2026-11-15/backup/20261115-121648/`. Semantic-file hashes
were checked again immediately before installation. No dependency was installed
or upgraded.

The CI cp311 archive matches published SHA-256
`37fc3b4f935ccc7e2c706205949ba8e8b5d057752520d218c98dd0da5b7f75b0`.
Installed wheel SHA-256:
`40a57fa8ebdfd822bc1ca595570044c4605a98eab690798b7e6c8c201ef540a0`.
Installed native SHA-256:
`d29024112409b373a9a714d9dcd9bb532e22716b2164d5d9b4f773c9b94eef89`.

The actual `/Users/lil/miniconda3/envs/iv8-test/bin/python`, from `/tmp` with
PYTHONPATH cleared, verified its module/native and distribution receipt in fresh
processes. It passes **87 focused** and **2371 passed, 2 skipped, 1 xfailed** full
suite checks. Pytest dependencies were borrowed only after binding the verified
shared iv8 module; pytest was not added to the user environment. The same pinned
EPUB manifest also passes using that interpreter, with no first error and server
`not_tested`. No live business request or Cookie/output equivalence is claimed.

Backups, rollback identity, per-file hashes, installed direct_url, focused/full
logs and the replay are in `shared-sdk-backup.json`,
`shared-sdk-installation.json`, `shared-sdk-final-identity.json`,
`shared-focused.log`, `shared-full.log` and `epub-offline-installed/` in that
ignored evidence directory. Subsequent handoff commits are documentation-only
and retain the verified implementation/test/build files.
