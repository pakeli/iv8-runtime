# MutationRecord field monitoring local acceptance

Date: `2026-11-16`. **Implemented and locally accepted; full release verification pending.**

Boundary: [mutation_record_monitoring.md](mutation_record_monitoring.md).

## Delivered scope

The nine existing record data fields use the established native-data callbacks,
with host_object / MutationRecord attribution and one generation-owned token per
record. Reads and supported writes carry safe completion. Hooks install even
when tracing is off, so cached-before-start and async-delivered records work.
Materialization stays runtime-origin and does not consume event capacity;
observer/user initialization callbacks retain their actual API entries.

No MutationRecord global/prototype/backend is introduced. Records retain their
plain Object.prototype, extensibility, original data flags, values, node/array
identities and ordering. The old Set executes once before diagnostic own-data
inspection; inherited setters/readonly fields and actual stored values/flags are
preserved. Accessor/nonconfig/consumed fields are not forcibly hooked. Diagnostics
never read the field or an accessor and do not capture original setter errors.

## Actual acceptance

- **23 new tests pass**; the old candidate gives **20 failures / 3 passing
  behavior controls** against the same tests.
- Related MutationObserver/origin/generic/node-lifecycle modules: **153 passed**.
- Same Linux/Windows/macOS release gate: **26 modules / 581 passed** on the
  isolated real-V8 macOS arm64/Python 3.13 candidate.
- Full local suite: **2871 passed, 2 skipped**.
- Covers nine GET/SET paths, childList/attributes/characterData/namespace/null/
  oldValue, cached and async records, per-record/node identity, flags/JSON,
  assignment/delete/redefinition/freeze, safe summaries, filters/capacity/stop/
  load/Page isolation and user prototype initialization hooks.
- Independent implementation review found no blocker in Set/error ordering,
  diagnostic reads, object identities, provenance or cached-record behavior.

Local wheel SHA-256:
`d294c9e6ae005aa6495ba96d5fc73830666b3b4d8844cef387f06d36b7ac781e`.
Build/focused/gate/full logs and source/native hashes are under ignored
`data/mutation-record-monitoring-2026-11-16/`; see `source-receipt.json`.

## Preserved limits

The builder already ignores individual Set Maybe results. In the tested inherited
throwing-setter baseline, tracing off suppresses that exception after later field
work, while tracing on preserves the original sentinel through its existing outer
native-call TryCatch. This monitoring slice preserves both actual outcomes and
later setter call counts; it does not claim to repair that pre-existing error
handling asymmetry.

Arrays remain ordinary arrays: independent records.length/index and
addedNodes/removedNodes length/index monitoring is still open. User-replaced
accessors/nonconfig paths retain ordinary semantics rather than a fabricated
native field. Structural queries/native-data descriptor materialization follow
existing helper semantics. Symbol/query/other returned-object gaps remain open.

The current patch updates PR #32 after local acceptance. Full release lanes have
not been rerun for this slice; older release/fallback source records remain
separate. No SDK installation, dependency upgrade, merge or version tag is
performed. V8 stays pinned to 15.0.245.19 and the M1 API is unchanged.
