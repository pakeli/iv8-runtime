# Readable host-access trace

Scope approved by the log-readability request: `2026-11-04`.
Status: **implemented; local Python checks passed; real-V8/release verification
pending**. Baseline: `0cff79a` (PR #6).

## Public contract

```python
page.read_access_trace(clear=True, *, format="raw")  # existing dict
page.read_access_trace(clear=True, *, format="text") # one event per line, str
```

`clear` remains a strict bool. `format` is a strict string, either `raw` or
`text`; unsupported values raise ValueError. Validate both arguments before
reading/clearing native events. The default raw dictionary, event schema, and
`access_trace_report()` aggregation remain unchanged.

Examples:

```text
GET document.cookie -> ""
SET document.cookie = "session=abc"
GET document.cookie -> "session=abc"
CALL document.querySelector("#app") -> <HostObject>
```

Text shows paths, attempted setter inputs, call argument summaries, captured
values/results, missing properties, and synchronous throws. Internal IDs,
sequence/generation/source fields, and full stacks remain available in raw mode.
Lines retain native entry order, not completion timing. Non-identifier
property names use quoted bracket notation. Control characters
are escaped so each event occupies one line. Rendered labels are native labels,
not reconstructed JS variable names or complete object paths.

Do not invent values: empty string is `""`, null is `null`, undefined is
`undefined`, type-only summaries are `<Object>`/`<Array>`/etc., and absent
metadata is explicitly `not recorded`. Strings over the existing native
64-byte preview bound are marked truncated, with their total UTF-8 byte length;
an omitted preview is not displayed as an empty string. This view cannot recover
full values or object snapshots discarded by native capture. Argument summaries
retain the eight-argument bound and mark omitted arguments. Dropped-event
capacity warnings remain visible. Setter summaries represent assigned inputs,
not necessarily the post-coercion stored values.

Formatting is pure Python over one native snapshot: no eval, getter/property
inspection, JS/Python callback bridge, or extra JS execution. It changes only
the Page-level monitoring presentation, not M1 or the JS runtime surface.

## Verification plan

Test summary escaping/decoding, type placeholders, truncation, operations,
exceptions, missing metadata, capacity, and read/clear validation independently
of V8. Add real-V8 Cookie/call/exception/string-boundary/lifecycle tests and
confirm existing raw/aggregate workflows still work. Run focused tests, V8 CI,
and all three release matrices before marking verified; record actual trees,
runs, artifacts, and counts here.

## Local verification — `2026-11-04`

- New formatter tests: **51 passed, 5 skipped** on the V8-free wheel; the five
  native integrations still require real V8. Focused new/API/report/value tests:
  **53 passed, 26 skipped**. Full suite: **276 passed, 1604 skipped**,
  **1,880 collected**. Runtime skips are not native verification.
- Tests use the exact empty-Cookie event from the request and cover primitive/
  type summaries, native escapes, Unicode line separators, real versus appended
  ellipses at byte boundaries, omitted previews/arguments, setter attempts,
  exceptions, missing probes/metadata, other operations, capacity warnings,
  raw/aggregate compatibility, and argument validation before destructive reads.
- A fresh wheel was built and installed in ignored `data/page-refactor/venv`;
  source/import/native-link state was checked. Logs and artifacts are under
  ignored `data/readable-trace/`, not committed.

## Real-V8 verification

Pending. Record source commit/tree, CI/release run IDs, artifacts, and actual
focused/full counts after every release lane passes.
