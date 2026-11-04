# Host Access Trace Usage Workflow

Status: **VERIFIED — aggregated `Page.access_trace_report()` workflow green on
real V8 Linux + Windows release lanes and a macOS arm64 release build on the
macos-14 fallback runner (`2026-11-02`)**

This document freezes step 7 of `docs/host_access_tracing_plan.md`: turning the
raw host-access event log into an actionable dynamic-run report. It builds on
`docs/host_access_trace.md` and adds no native code, event type, JS-visible
global, or top-level Python export.

## 1. Purpose

For a short per-event log, use the optional readable view:

```python
print(page.read_access_trace(clear=False, format="text"))
# GET document.cookie -> ""
# CALL hostProbe.add(2, 3) -> 5
```

`string:0:` in the raw schema means an empty string. Text mode displays it as
`""`, uses captured values/arguments, and explicitly marks truncated or absent
metadata. The default `format="raw"` preserves the dictionary below; aggregation
is unchanged. This presentation addition has its own contract/verification in
`docs/access_trace_text.md`.

Verified `2026-11-04` on source `1599fd5`: Linux `41848358166`, Windows
`41848373389`, macOS arm64 fallback `41850244647`, Python 3.11–3.14 each
`1878 passed, 2 skipped`; focused macOS checks each `79 passed`. The historical
step-7 verification below remains its original snapshot.

A run report answers three questions:

1. **What was accessed?** — grouped access counts with the first event's
   sequence and source.
2. **What looked missing?** — grouped `get_probe`/`resolution: "missing"`
   candidates.
3. **What are the boundaries of this run?** — coverage, capacity/filter/stopped
   warnings, caller-supplied event-loop progress, and the explicit non-coverage
   list.

The report is a view over the events executed in this trace session. It is not a
static dependency list and does not prove that unrecorded code would not access
anything.

## 2. Public API

```python
report = page.access_trace_report(clear=False, progress=None)
```

- `clear` (default `False`) is a strict `bool`. The report is built from a
  `read_access_trace(clear=False)` snapshot by default, so the raw log remains
  available. `clear=True` snapshots and then clears events/dropped counts while
  preserving sequence, configuration, generation, and object-ID mappings.
- `progress` is `None` or a `dict`. It is copied into the report and never
  interpreted by the runtime. Recommended fields are caller-defined; for
  example `{"event_loop": "run_until_idle", "complete": True, "max_steps": 200}`.
- `TypeError` is raised for a non-`bool` `clear` or a non-`dict`/`None`
  `progress`. `JSContextDisposedError` is raised after `dispose()`.
- The method is pure Python and does not execute page code. Raw events remain
  available through `read_access_trace`.

## 3. Report shape

```python
{
    "trace": {
        "enabled": bool,
        "generation": int,
        "max_events": int,
        "dropped_events": int,
        "filter_object_types": list[str] | None,
    },
    "coverage": {...},  # verbatim read_access_trace() coverage
    "events": {
        "count": int,
        "by_operation": {operation: count},
        "by_object_type": {object_type: count},
    },
    "accesses": [{
        "object_type": str,
        "label": str,
        "object_id": int,
        "member": str,
        "operation": str,
        "count": int,
        "first_seq": int,
        "last_seq": int,
        "first_resource_name": str,
        "first_source_kind": str,
        "resolution": str,   # only when the source events carry one
        "outcomes": {outcome: count},
    }, ...],
    "missing_candidates": [{
        "object_type": str,
        "label": str,
        "object_id": int,
        "member": str,
        "count": int,
        "first_seq": int,
        "last_seq": int,
        "first_resource_name": str,
        "first_source_kind": str,
        "resolution": "missing",
    }, ...],
    "warnings": [{"code": str, "message": str}, ...],
    "progress": dict | None,
}
```

### 3.1 Aggregation

- `accesses` groups by `(object_type, label, object_id, member, operation)`.
  The target's stable `object_id` keeps two same-type/label instances separate.
- The first event in each group supplies `first_seq`, `first_resource_name`, and
  `first_source_kind`; `last_seq` and `count` cover the rest of the group.
- `outcomes` counts optional `returned`/`threw` outcomes; it is `{}` when the
  events have no outcome completion.
- `missing_candidates` groups only `get_probe` events whose `resolution` is
  `"missing"` by `(object_type, label, object_id, member)`.
- Lists are ordered by `first_seq` for stable output.

### 3.2 Warnings

| Code | Meaning |
|---|---|
| `capacity_exceeded` | `dropped_events > 0`; the earliest-N capacity dropped later events. |
| `object_type_filter_active` | An object-type filter was configured; unlisted events were not recorded. |
| `trace_not_recording` | The trace is stopped or was never started; any retained events are historical. |
| `no_events` | This snapshot contains no events. |

Warnings are not mutually exclusive. `trace` and `coverage` remain available
even when `warnings` is non-empty.

## 4. Recommended workflow

```python
import iv8

with iv8.Page() as page:
    # Enable before load() when the target scripts run during load.
    page.start_access_trace()
    error = None
    progress = {"event_loop": "run_until_idle", "max_steps": 200}
    try:
        page.load(
            html="<html><body><div id='app'></div></body></html>",
            base_url="https://example.test/",
            scripts=[{
                "name": "probe.js",
                "code": "document.querySelector('#app'); navigator.notThere;",
            }],
        )
        progress["complete"] = page.run_until_idle(max_steps=200)
    except iv8.JSError as exc:
        error = exc
    report = page.access_trace_report(progress=progress)
    # Read the report even when error is not None, before dispose.
```

Guidance:

1. Start tracing before `load()` if scripts inside the document matter.
2. Load or evaluate named scripts; catch `JSError` if the scenario may fail.
3. Drive only the event-loop turns the scenario needs, using bounded
   `advance`/`run_until_idle` calls. Record whether the budget was exhausted in
   the caller-supplied `progress` dictionary.
4. Read the report before `dispose()` even when the target script threw.
5. Use `accesses`/`missing_candidates` for the next investigation; use
   `read_access_trace(clear=False)` when raw events, summaries, or exception
   fields are needed.

## 5. Interpreting boundaries

- An empty `events` list can mean no access, an unexecuted branch, an
  `object_types` filter, a stopped/never-started trace, or cleared events. The
  warnings and `trace` fields distinguish several of these; none proves absence
  of a dependency statically.
- `missing_candidates` are dynamic missing-property probes. A property may be
  present in a different branch, added later, or hidden by an unexercised
  feature.
- `coverage["not_covered"]` lists the trace's explicit blind spots, including
  the `in` operator, non-HostObject query targets, pre-trace cached query
  wrappers, asynchronous exception metadata, and general call-site
  locations/timing.
- `method`/member and target identity are grouped using the event's native
  identity; no JS getter or user `toString` is executed to build the report.
- The report does not include raw events. If `clear=True` is used, raw evidence
  is discarded after the snapshot, so use the default `clear=False` when it must
  be retained.

## 6. Verification

Verified on real V8 on `2026-11-02` against the tree of code commit `c678ede`
(the release runs used the identical tree `62af2e6`, cleanly
re-committed as `c678ede`):

- Linux `release-linux` run `41426804187` — cp311 / cp312 / cp313 / cp314,
  each `1720 passed, 2 skipped`;
- Windows `release-windows` run `41426817304` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1720 passed, 2 skipped`;
- macOS arm64 fallback run `41466841016` (`macos-14`) — built cp311–cp314
  `macosx_11_0_arm64` wheels and ran the full suite: each lane `1720 passed,
  2 skipped`. The V8 monolith was restored from the existing macos-arm64 cache;
  the official macos-15 runner was unavailable (`release-macos` run
  `41426828872` was cancelled while queued), so the same release steps ran on
  macos-14 with deployment target 11.0. Artifacts:
  cp3.11 `13153386446`, cp3.12 `13153186853`, cp3.13 `13153201862`, cp3.14
  `13153935120`.

`test/test_access_trace_report.py` (6 items) ran in every lane. The full suite
collects 1722 items; 1720 pass and 2 are skipped. The first attempt (`3fcc156`)
failed one stopped-report warning expectation because capacity warnings persist
with retained events; `c678ede` corrected that expectation (identical code tree
to the tested `62af2e6`).
