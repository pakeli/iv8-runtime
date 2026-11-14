# Offline scenarios and environment diagnostics — implementation report

Date: 2026-11-14. Latest implemented tooling; local acceptance is recorded below.
No new all-platform release verification is claimed. Boundary and usage:
`docs/offline_scenarios.md`.

## Implementation

- `tools/run_offline_scenarios.py` is the common CLI. The runner validates a
  strict v1 manifest, raw-byte SHA-256 hashes and input limits before Page
  execution. It uses the existing offline HTML/resource loader, pumps,
  asynchronous error channel and access-trace aggregator.
- Each scenario uses a fresh worker with a wall deadline. Atomic progress
  reports retain validated identity and completed checkpoints on timeout.
  Pump-budget exhaustion is incomplete. Check timeouts/errors preserve the
  already-frozen collector verdict and trace.
- `environment.py` diagnoses schema/native integer representability, numeric
  ranges, language and dimension relationships, supported UA/platform families,
  explicit partial browser references, and declared JS/HTTP/transport profiles.
  Unknown/missing evidence is explicit; no values are repaired or returned.
- Error/resource/member evidence is hashed/redacted; raw Cookie, trace values,
  check outputs, throw messages and stacks are not serialized. There is no
  HTTP adapter, network library, script rewriting or public runtime API change.

## Local build identity

Baseline HEAD: `0f0ab4b` (JET-01 implementation source `dd67e49` plus subsequent
documentation). Native/package sources are unchanged by this tooling change.
The local wheel was built explicitly with `IV8_LINK_V8=ON` and macOS deployment
target 11.0, against pinned V8 `15.0.245.19`, using CPython 3.13/macOS arm64.
It was installed into ignored `data/offline-scenarios-2026-11-14/site`, separate
from user-owned environments.

Wheel SHA-256:
`f58064f25ec9f6112402f1b3242da175e5ff376c1553f35cafcdcbacf8823280`.

Each scenario records the actual loaded native extension, Python facade,
runner/environment diagnostic source, manifest, assets and configuration hashes.
The private local verification receipt additionally inventories scoped tool,
test, fixture and handoff files by hash. No CI run IDs or release artifacts exist for this uncommitted
tooling change; historical JET-01 release evidence remains in its own report.

## Scenario acceptance

| Scenario | Regression | Execution | Checks | Environment | Server |
|---|---|---|---|---|---|
| Synthetic History/lifecycle/native shape | passed | passed | passed | unknown (default profile) | not_tested |
| Same collector with History removed | passed | failed as expected | not_run | unknown | not_tested |
| Synthetic timer error | passed | failed as expected | not_run | unknown | not_tested |
| Private cached original Jetstar collector | passed | failed as expected | not_run | warning | not_tested |

The Jetstar collector remains unchanged, SHA-256
`1d9baa8754ebad40dc7d52940bef7ac6ec17710a26231968fc86c99808f13565`.
Load completes; the `advance(1000, max_steps=100)` phase observes asynchronous
`NotSupportedError` at line 1, column 51597, matching the previous offline
checkpoint. Declared transport Chrome150 versus HTTP/JS UA Chrome156 produces
two major-version warnings. This is a configuration diagnosis, not a new TLS
measurement or proof of the prior Cookie result's cause.

The original cached source and browser environment were read locally. Only a
private ignored manifest/minimal HTML and redacted reports were created. No
third-party source or session data was added to tracked fixtures. No new live
request was made; this work does not extend the prior POST budget.

## Tests and evidence

Focused environment tests: **84 passed**. Focused scenario tests: **44 passed**.
The complete local real-V8 suite reports **2248 passed, 2 skipped, 1 xfailed**
(2,251 collected). The 128 new checks cover diagnostics, schema/hash validation,
execution/check verdict isolation, synchronous/asynchronous error evidence,
expected errors, task/wall budgets, cross-process state isolation, CLI behavior
and redaction. A separate V8-free CLI check returned `execution: unavailable`
and exit 1, preventing an empty successful run.

Artifacts:

- `data/offline-scenarios-2026-11-14/final-pytest.log`
- `data/offline-scenarios-2026-11-14/final-scenarios/summary.json` and four reports
- `data/offline-scenarios-2026-11-14/verification.json`

The first sandbox full run had 21 pre-existing localhost-bind permission
failures. Re-running with local binding permitted passed the full suite; this
does not introduce external HTTP in any test or scenario.

All-platform release verification is pending. The latest release-verified native
runtime remains JET-01 (`2120 passed, 2 skipped, 1 xfailed` per lane); do not
substitute local tooling counts for its release receipt.
