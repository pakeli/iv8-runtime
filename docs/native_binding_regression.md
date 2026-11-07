# Window open exposure regression repair

User-reported on 2026-11-07. Implementation and release verification are pending.

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
