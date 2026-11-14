# Unified offline scenarios and environment diagnostics

Authorized 2026-11-14. This is repository diagnostic tooling, separate from the
public `iv8` and `Page` contracts. It reuses cached resources, explicit Page
pumps, asynchronous error capture and access-trace aggregation. No runtime
binding, clock, network transport or callback behavior changes.

## Boundary

- A versioned JSON manifest identifies local UTF-8 HTML/resources by SHA-256,
  the base URL, environment, optional browser/transport reference metadata,
  bounded pump steps and explicit JSON-valued checks. This schema is version 1.
- Each scenario executes in a fresh subprocess with a wall timeout. Resource
  URLs are keys in Page's offline resource mapping; the tool never fetches them.
- Synchronous and asynchronous first errors retain phase and JS source location.
  Execution, explicit checks, environment consistency and server verdicts remain
  separate. An expected failure can pass a regression without implying execution
  completed. Server verdict is always `not_tested`.
- Environment diagnostics reuse Page's input validation, report only supported
  relationship checks and explicit reference comparisons, and identify missing
  evidence. They do not alter Page's configuration or validation contract. The
  scenario tool refuses diagnostic `fail` inputs (invalid schema, native integer
  overflow, non-finite/nonpositive DPR, negative dimensions/capacity). This is a
  stricter tooling boundary; plausible relationship differences only warn.
- Reports contain asset/runtime/configuration hashes, bounded error evidence and
  aggregate counts. They omit raw trace values, Cookie contents, check return
  values, URLs, and error message/stack contents. Resources use manifest-order
  labels and source-name hashes; messages/stacks/custom error names and dynamic
  missing member names use SHA-256 plus lengths where applicable. Scenario/check
  identifiers are public manifest metadata: never put secrets in identifiers.
  Private manifests and reports belong in ignored `data/`. Nothing is uploaded.
- Bundled synthetic scenarios run without third-party scripts or network.
  Private cached collectors are exercised only through local manifests and are
  not copied into tracked fixtures. Time/randomness stay as implemented; this
  tool does not promise byte-identical output or server acceptance.

## Acceptance

Focused tests cover malformed manifests and hash mismatch before execution,
successful execution, explicit check failures, synchronous and async first
failures, missing resources, independent runs and hard wall timeout. Environment
tests cover partial evidence, known mismatches, invalid types/numbers and
non-disclosure of original values. Then run the complete local real-V8 suite.

## Run

Use a Python 3.11–3.14 interpreter with an installed V8-linked `iv8` wheel. The
tools are repository utilities, not new exports in the distributed package.

```bash
python tools/run_offline_scenarios.py \
  test/fixtures/offline_scenarios/history.json \
  test/fixtures/offline_scenarios/history_missing.json \
  test/fixtures/offline_scenarios/async_error.json \
  --output data/offline-report --timeout 10
```

This writes one numbered report per scenario and `summary.json`. Exit 0 means all
scenario regression expectations matched. Exit 1 means at least one failed,
timed out, was invalid, or used an unavailable SDK. CLI syntax errors exit 2.
Environment warnings/unknowns remain separate and do not make the regression
exit code fail. Inspect each report's `environment.findings` and `unchecked`.

## Manifest contract

Start from the three bundled examples in `test/fixtures/offline_scenarios/`.
Required fields: `schema_version: 1`, a simple `id`, an absolute HTTP(S)
`base_url` without credentials, and `html: {path, sha256}`. Paths resolve relative
to the manifest; absolute paths are supported for private cached collectors.
Optional `resources` is a list of `{url, path, sha256}`. Hashes cover raw bytes,
before UTF-8 decoding. All inputs must be regular files: manifest at most 1 MiB,
each asset 16 MiB, total assets 64 MiB. Duplicate JSON keys, duplicate resource
URLs/check IDs, unknown fields and non-finite JSON numbers are rejected.

Optional `environment` uses Page's existing flat or nested schema. Optional
`reference` accepts `browser_version`, `conditions` (an object) and a partial
`environment`; only explicitly supplied reference fields are compared. Reference
metadata records provenance, not proof of a live browser comparison. Optional
`transport` accepts `browser_major` (positive integer) and `http_user_agent`.
These are caller-declared metadata: no TLS measurement or HTTP library runs.
The parsed browser version metadata is provenance only; it does not replace
comparing the reference's explicit environment fields.

Optional `steps` has at most 100 entries, executed after load, in order:

| action | arguments | meaning |
|---|---|---|
| `jobs`, `timers`, `animation_frame`, `idle` | none | existing one-queue Page pump |
| `tick` | none | one facade turn; false is an idle turn, not failure |
| `advance` | `ms`, `max_steps` | process a virtual-time window |
| `run_until_idle` | `max_steps` | process facade tasks until idle or bounded |

`max_steps` is 1–10,000; `ms` is 0–86,400,000. An exhausted facade budget yields
`execution.status: incomplete`, not success. A process wall timeout also bounds
load, JS loops and unbounded microtask work. The last atomic checkpoint retains
identity and completed phases when available. A check timeout affects `checks`,
preserving the already-frozen collector execution verdict.

Optional `checks` has at most 100 `{id, expression, expected}` entries. Expected
values are finite JSON; nested booleans differ from numbers and undefined differs
from null. Checks run only after successful execution. Their JS can have side
effects, so execution errors, navigation and trace are frozen first. The tool
does not claim that arbitrary check expressions are read-only.

Optional `expected_error` requires `phase` (`load` or `step:0`, etc.) and `name`;
it may also match `channel` (`synchronous`/`asynchronous`), original
`resource_name`, `line` and `column`. Error name/resource matching uses original
hashes, including URL queries, while output stays redacted. Expected-error
scenarios cannot also require checks. A matched expected error yields
`regression: passed`, `execution: failed`, `checks: not_run`.

`first_error` means first observed failure at a phase boundary. Synchronous
exceptions are observed before reading that phase's async buffer; checkpoints
retain both channels. There is no shared cross-channel chronological ordering.
Later handlers can remove buffered Promise rejections; the tool reports what it
observed at each boundary, not a claim of permanently unhandled rejection.

Trace recording begins before load, retains at most 10,000 events, and reports
drops and coverage limits. Missing candidates are dynamic observations; at most
100 groups are emitted, with explicit total/truncation fields. They are not a
complete static API dependency inventory. No JS XHR shim is installed.

## Environment diagnostic interpretation

The pure Python function `iv8_diagnostics.environment.diagnose_environment` is
available to repository tooling without constructing a Page. `pass` refers only
to performed configuration checks; `unchecked` always includes runtime behavior
and any missing evidence. Unknown UA/platform families yield `unknown`. Known
OS/UA differences, differing JS/HTTP/transport Chrome majors, complete HTTP/JS UA
declaration differences, primary-language differences and suspicious dimensions
yield warnings. Signed window/screen coordinates are valid. iPad desktop-mode
and Linux/Android platform ambiguity are preserved. The function performs no
configuration repair and asserts no universal browser identity rule.

Implementation is complete; verification receipts are recorded in
`docs/offline_scenarios_report.md`. All-platform release verification remains
separate from local diagnostic-tool acceptance.
