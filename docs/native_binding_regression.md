# Window open exposure regression repair

User-reported and release-verified on 2026-11-07.

## Proven regression and boundary correction

Matched fresh input comparisons show source 15edd82 returning HTTP 200, f42acae
returning 400, f42acae with only global open deleted returning 200, and restoration
of the identical open function returning 400 again. Both surrounding old-version
controls returned 200. The new version's pre-lifecycle Cookie returned 200. Its
post-lifecycle Cookie returned 400 and only the target Cookie changed.

The default open stub is withdrawn from the offline Page profile. Existing
callers again observe an absent global, as before the Web IDL repair. The
remaining Web IDL method, constructor, inheritance and descriptor corrections
retain their verified behavior. Fetch rejection and XHR/Worker explicit failure
follow their approved contracts. Window creation requires a later approved
browsing-context implementation.

The immutable Chrome fixture retains its function expectation for global.open.
That comparison is recorded as an expected profile gap, with a separate strict
absence test. The remaining 74 supplied comparisons must match Chrome.

## Evidence limits

The downstream detection field responsible for the rejected post-lifecycle
Cookie is still unresolved. Diagnostic probes observed open length, source and
prototype reads and zero calls. They also confirmed Crypto method metadata and
Event constructor arity differences. Correcting those separately and together
still returned 400. Cookie bootstrap placement and Cookie visibility controls
also returned 400. Those diagnostic candidates are excluded from this repair.

Fresh f42acae Customs output returned 400 with tracing enabled or disabled.
A Cookie-expiry correction removed its temporary Cookie and still returned 400.
The open-withdrawal candidate returned 200 with tracing enabled or disabled,
matching both live and recomputed browser controls. The accepted browser Cookie
also returned 200 after the native comparisons. A repeated same-input comparison
returned 400 for the unmodified f42acae wheel and 200 for the candidate. Live,
recomputed browser and restored-browser controls all returned 200. The precise rejected
recomputation field remains unresolved and stable encrypted-output equivalence
has not been claimed.

## Acceptance

Focused offline tests precede the complete suite. All three release matrices
require Python 3.11–3.14. The candidate must retain the 74 applicable Chrome
comparisons, pass the original 27 browser compatibility expressions, return
NMPA document HTTP 200 and obtain business code 200 from the data interface.
Record source/tree, actual test counts, runs, artifacts and deployment here.

## Local verification checkpoint

The normal static-linked macOS arm64/Python 3.11 candidate passed the complete
suite with **2050 passed, 2 skipped, 1 xfailed**. The expected failure is the
immutable Chrome fixture's global.open function expectation. A separate test
requires the unsupported API to be absent across reloads. The two skipped tests
are V8-free-only guards. The 27 earlier browser compatibility expressions passed.

The candidate passed the original NMPA caller with tracing enabled. Document
HTTP status was 200, the expected title was present, the data interface business
code was 200, and the first page contained 10 rows for the existing query.
Diagnostic artifacts and the temporary user-environment rollback receipt are
under ignored `data/nmpa-regression-2026-11-07/` in the primary checkout.

## Causal and recovery checkpoints

| Checkpoint | Observed result |
| --- | --- |
| NMPA old / new / old, matched fresh input | 200 / 400 / 200 |
| New with only open deleted | 200, valid document title |
| Same native open restored | 400 |
| New pre-lifecycle Cookie | 200 |
| New post-lifecycle Cookie | 400, only the target Cookie changed |
| Candidate original NMPA document/data caller | document 200, business code 200, 10 rows |
| Customs matched browser / f42acae / candidate / browser | 200 / 400 / 200 / 200 |
| Candidate Customs tracing enabled and disabled | both 200 |

The diagnostic recovery stayed at level A. The generated execution wrapper and
interpreter were _$gT / _$jN. Property-read opcode 30 uses receiver _$nF and key
_$mb, then pushes the result onto _$fO through stack pointer _$_k. Bytecode is
_$ko with program counter _$jd. The wrapper carries return exit/value through
_$_v[4] and _$_v[5]. Open reads appeared at code lengths 109, 147 and 641.
Conditional breakpoint observations retained the original generated source.
The 109-byte check returned false and the other two returned undefined.
Those observations identify the inspected API boundary and do not decode the
rejected recomputation's internal field. The uninstrumented HTTP controls above
provide the acceptance evidence for the API exposure correction.

## Verified source and release artifacts

Source `f88234402bc5ca3ac84d14dd952557a6a4a6deed`, tree
`c4ba8bee511316e69d1f8f58e865c036a6cec56c`, passed all three release
matrices, Python 3.11–3.14. Every lane reported **2050 passed, 2 skipped,
1 xfailed** (2,053 collected). The expected failure is the unavailable-open
Chrome comparison. macOS focused runs each reported **172 passed, 1 xfailed**.
PR V8 CI `42350703567` passed.

| Platform | Run | Artifacts |
| --- | --- | --- |
| Linux x64 | `42350637889` | `13565632568`, cp311–cp314 |
| Windows x64 | `42350641803` | cp311 `13565462312`, cp312 `13565517282`, cp313 `13565203744`, cp314 `13565316806` |
| macOS arm64, macos-15 | `42350648456` | cp311 `13565846482`, cp312 `13565611883`, cp313 `13565841531`, cp314 `13565901352` |

The cp311 macOS artifact matched GitHub's archive digest
`5b9d39f27ce60df8a96386e33b13debc55ff1d22ca69075ee63a7b7930944f11`.
The wheel SHA-256 is
`ba67ffcb8d8fcdc80cd3e8dcb9a2e52772f21df9c458a09db4218dd7d09e5eef`.
It was installed into `iv8-test` and its native bytes matched the verified wheel.
The existing NMPA caller then returned document HTTP 200, a valid title,
business code 200 and 10 data rows with tracing enabled.

Customs acceptance is bounded to the matched browser consumption controls above.
The original caller also produced 400 in direct checks. A two-header change
once returned 200, but repeated same-Cookie/header controls were inconsistent.
That header cause was retracted and the caller was restored to its original
SHA-256 `7ffc85ff4857f88f47567daa21eea0d483ee9243b2d342edc7847fa3ee948d81`.
Full Customs caller consistency remains under investigation.

macOS driver `234880907a3a0d44ebfa15357ba8be84521c7c03` pinned build-v8 and all wheel checkouts to the
verified source. Restore commit `605da854b17b82d103c01c1dcbcaa4420f197e91` returned the helper workflow to
original blob `eb730d3e77a17d4d527e67646475385cebb2428c` with a SHA guard.
Master's strict required Linux/Python 3.13 V8 gate and administrator enforcement
are retained. PR #10 carries this repair.

## Final caller acceptance on 2026-11-07

A further controlled request-profile comparison isolated the missing second-hop
Referer in the Customs caller. With fresh sessions, both surrounding original
controls returned 400. Adding only `Referer` to the second document request
returned 200 with the business title in three consecutive independent sessions.
The prior platform/Sec-Fetch-Site claim was retracted after inconsistent replay
results. The final caller correction adds only the second-hop Referer.

The original Customs main.py was backed up, then its second GET was changed to
pass `headers={"Referer": index_url}`. Its SHA-256 changed from
`7ffc85ff4857f88f47567daa21eea0d483ee9243b2d342edc7847fa3ee948d81` to
`e3c4f96cdec2f85adf433533214647e0e9f759c4d480a450fae906be9f7bec93`.
Using the installed official cp311 wheel, that entry returned 200 in three
independent runs. A fourth observed run recorded document 412, script 200,
then document 200 with the expected business title. The caller edit is outside
the runtime repository.

NMPA installed-wheel acceptance returned document 200, the expected title,
HTTP 200 and business code 200 for the existing data query, with 10 rows.
The two resolved operational conditions are default open exposure and the
Customs caller's missing second-hop Referer. The deeper internal reason that
the open-enabled lifecycle recomputation produces a rejected Cookie remains
unresolved. No stable encrypted-output equivalence is claimed.

PR #10 merged as `9bc6f2ee34112565d02c4f9f515d6427c9fcb980`. Its final
handoff head was `1ea46cd7e655c0aeb9100e1cdb639465813a6cc5`, with V8 gates
`42351794476` and `42351799839`. Main V8 CI `42352227223` passed. Runtime and
test sources in the merged tree match the release-verified source f882344.

## Current master rebuild on 2026-11-07

At the user's request, source `9a0d20786bf289adce494fdb6e75242fb55c9195`,
tree `904b2cfa2477a91803ceebb5602611d79dfc18bd`, was rebuilt as V8-linked
wheels for Python 3.11–3.14. The pinned V8 15.0.245.19 monolith was restored
from its platform caches. All 12 wheel lanes reported **2050 passed, 2 skipped,
1 xfailed**. macOS focused runs each reported **172 passed, 1 xfailed**.
The expected profile gap is the unavailable global.open entry.

| Platform | Run | Artifacts |
| --- | --- | --- |
| Linux x64 | `42353755315` | `13567246115`, cp311–cp314 |
| Windows x64 | `42353765215` | cp311 `13566867411`, cp312 `13566219965`, cp313 `13566008836`, cp314 `13566697573` |
| macOS arm64 | `42353779252` | cp311 `13566253261`, cp312 `13566193499`, cp313 `13567021892`, cp314 `13566677629` |

macOS driver `9cfa8d566384d004e88c8dd882f0ed3efa5d6dfa` pinned all five checkouts to the rebuilt source.
Restore commit `6f32bc15b9d770bab16a47635248bcd917e050bb` returned the helper workflow to original blob
`eb730d3e77a17d4d527e67646475385cebb2428c` using the current-content SHA guard.
Main V8 CI `42352867708` passed on the same source.

The cp311 macOS archive matched GitHub's SHA-256
`c9073847eb81eea5f3c2f1333605baffdf8d976e7b96245153373ed9d7a17ed4`.
Its extracted wheel SHA-256 is `907dba73cf0d2f29849f09275691787de650d33f6153fcca7b3a2cd1c07cc411`.
The verified wheel and build receipt are retained in ignored
`data/rebuild-2026-11-07/`. The rebuilt runtime/test sources are identical to
the prior f882344 release. The installed iv8-test runtime retains the verified
NMPA and Customs caller acceptance recorded above.

## Native method binding attribution

Recorded `2026-11-07` at the user's request as evidence for browser-consistent
development and review. The user clarified that each API's browser behavior
defines the required shape and implementation kind. The controls separate the
original Customs failure from the later open exposure regression and the
caller's missing second-hop Referer.

The current Customs and NMPA callers supply identical navigator, screen and
window environment dictionaries. The same captured Customs HTML, matching JS,
first-hop Cookie O, URL and accepted browser request headers, including Referer,
were used across native versions. Canvas getContext returned null in the browser
reference. Each native case generated and consumed three P values.

Four binding dimensions were varied across all 16 combinations on `f882344`.
They were naming/native source, arity, constructability and receiver validation.
The combination **anonymous native + constructable + own `prototype`** returned
400 in all three comparisons, including combinations that also restored old
arity or receiver behavior. The other combinations returned 200 in all three
comparisons. The same-batch old `15edd82` control returned 400. Live browser,
offline browser recomputation and restored-browser controls returned 200.

An independent reverse control applied minimal binding corrections to `15edd82`.
The old prototype chains, descriptors, length=0 and receiver handling were
retained in these diagnostic candidates.

| Native version or diagnostic candidate | Results from three same-input comparisons |
| --- | --- |
| Old `15edd82` | 400 / 400 / 400 |
| Old source with only declared names and named native source | 200 / 200 / 200 |
| Old source with only construction rejected and own prototype absent | 200 / 200 / 200 |
| Old source with both corrections | 200 / 200 / 200 |
| Current source with anonymous and constructable methods restored | 400 / 400 / 400 |
| Old source repeated at the end | 400 / 400 / 400 |
| Current `f882344` repeated at the end | 200 / 200 / 200 |

Live browser, offline recomputation and restored-browser controls again returned
200. Every successful response contained the expected business title.

The changes applied to groups of native methods. The exact individual API and
detection-value-to-Cookie mapping remain unresolved. Time and randomness were
unfrozen. These controls establish observed request acceptance and the binding
regression pattern, with encrypted-output equivalence still unresolved. The
open-triggered lifecycle recomputation remains a separate unresolved internal
field, with API-exposure causality recorded above.

### Development and review requirements

1. Capture each API's presence, implementation kind, shape and behavior from the
   target real browser, including browser version and execution conditions.
   Within each approved slice, implement browser-native methods through native
   bindings and track missing APIs and profile differences explicitly.
2. Match per-API names, arities, `Function.prototype.toString` output, own
   `prototype`, constructability, inheritance, descriptors, receivers, identity,
   return values and exceptions to that reference. Browser-supported constructors
   retain their reference construction behavior and prototype ownership.
3. Choose the factory and V8 constructor behavior that produce the observed
   browser semantics. The current named, non-constructable native operations use
   `make_web_method`. For these operations, the browser-derived expectations
   include `kThrow`, absence of an own `prototype`, and `TypeError` on construction.
4. Review every affected installation path, including HostObject methods, Window
   globals, Storage callbacks and separate native utility callbacks.
5. Add or extend offline browser fixtures and focused tests for each new or
   changed binding. The existing `test_native_method_metadata` checks name, arity,
   exact native source, own properties and construction together for its approved
   operation set. Derive expectations for additional function kinds from their
   browser captures. Receiver and identity cases cover the related call behavior.

The Customs combination above remains a concrete regression case. Per-API browser
evidence supplies the compatibility requirements for future work.

`AGENTS.md` carries these requirements for future development. `CLAUDE.md` and
decision 57 in `docs/decision_log.md` preserve them across handoffs.

The documentation follow-up on `2026-11-07` ran the existing metadata and brand
tests against the verified `f882344` macOS arm64/Python 3.11 runtime. The focused
result was **17 passed, 103 deselected**.

```sh
python -m pytest test/test_webidl_environment_semantics.py \
  -k 'native_method_metadata or native_method_brand' -q
```

### Local diagnostic evidence

The isolated diagnostic builds and captured inputs are under ignored
`data/open-cookie-deep-2026-11-07/`. The portable results above summarize these
local artifacts. Raw Cookies remain in private local inputs.

| Artifact within that directory | SHA-256 |
| --- | --- |
| `timeline-cross/boundary-results.json` | `b5bd09c5cfe53ee3b5dfe01dbbf480f569b0f2794f8cece0cb483f5d9bbcf8f2` |
| `binding-combinations/boundary-results.json` | `cc1131e7e4e7561ed784d39f1bf6b3dfdbbf05f308724d801fa1d0b04a8836bd` |
| `old-rescue-cross/boundary-results.json` | `b4fbda3ad11f4158a334b4a08d1d3f3065c1eda1a5167bae535221f76a1bfe19` |

The local index is `attribution-summary.json`. Native diagnostic candidates were
built and run in isolated targets. The detailed caller record is
`/Users/lil/Desktop/iv8-test/海关/reverse-records/请求链路.md`.
