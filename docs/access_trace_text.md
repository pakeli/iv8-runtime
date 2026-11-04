# Readable host-access trace

Scope approved by the log-readability request: `2026-11-04`.
Status: **VERIFIED on real V8 — Linux x64, Windows x64, macOS arm64
(`2026-11-04`)**. Baseline: `0cff79a` (PR #6).

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

Source commit: `1599fd5f1afca3a3acbd9981870a0a68b9d10cfb`, Git tree
`9320586725f0034b1a33695771d04578f5c7ad91`. All release runs succeeded;
every interpreter reported **1878 passed, 2 skipped** (1,880 collected),
including all five new native integrations, and imported V8 `15.0.245.19`.

| Lane | Run | Python versions | Result |
|---|---|---|---|
| V8 CI | [41848183724](https://github.com/pakeli/iv8-runtime/actions/runs/41848183724) | 3.13 | 1878 passed, 2 skipped |
| release-linux | [41848358166](https://github.com/pakeli/iv8-runtime/actions/runs/41848358166) | 3.11–3.14 | 1878 passed, 2 skipped |
| release-windows | [41848373389](https://github.com/pakeli/iv8-runtime/actions/runs/41848373389) | 3.11–3.14 | 1878 passed, 2 skipped |
| macOS arm64 fallback | [41850244647](https://github.com/pakeli/iv8-runtime/actions/runs/41850244647) | 3.11–3.14 | focused 79 passed; full 1878 passed, 2 skipped |

Linux artifact: `13334085925` (all four wheels). Windows artifacts:
`13331378432` (cp311), `13331224422` (cp312), `13334155193` (cp313),
`13331309638` (cp314).

macOS arm64 artifacts: `13335870151` (cp311), `13334217003` (cp312),
`13335425379` (cp313), `13335120402` (cp314), tagged `macosx_11_0_arm64`.

Initial macos-15 driver commit `185d6703be041b07adad28797f18cd9d9f5aa6a4`
started run `41848718450`, which was cancelled while queued without a runner.
Fallback commit `b0d249400f7a9a6c935d46100b157fadf7ccd2f7` uses macos-14 arm64
with the same cache, full source checkout pinned to `1599fd5`, and focused
new/API/report/value tests before the full suite (79 passed per interpreter).
All five checkouts were confirmed in the log. Restore commit
`1a71b7b2af8f63bb54bf8778f7b16e3f2934e52a` restored original workflow blob
`eb730d3e77a17d4d527e67646475385cebb2428c` without rewriting history. These
temporary drivers are not part of the source branch.

The existing real-V8 merge gate remains mandatory with up-to-date branches and
administrator enforcement. Subsequent handoff-only documentation changes
preserve the tested runtime/build/test files. This is a Python monitoring
presentation addition; M11-53 stays the next browser feature milestone.
