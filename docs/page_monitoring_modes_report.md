# Page monitoring modes local acceptance

Date: `2026-11-16`. **Implemented and locally accepted; release verification pending.**

Boundary: [page_monitoring_modes.md](page_monitoring_modes.md).

This Python-only Page change adds keyword-only `load(mode="debug" / "prod")`.
Debug starts tracing after native setup and before scripts, and automatically
prints/consumes readable events after load, eval and all event-loop pumps. Prod
stops and clears access tracing. Omission preserves legacy manual tracing and
an existing debug mode. A per-Page execution guard includes output and mode
transitions to protect completed event metadata across threads.

## Acceptance

- **57 mode tests pass**, covering automatic output, all seven event pumps,
  HTML/resource/host scripts and lifecycle callbacks, exception preservation,
  ordinary output failures, argument validation, reload/prod transitions,
  legacy manual tracing, Page isolation, 10,025 native getter events without
  drops, competing execution/trace controls/disposal and Python interrupts.
- **328 focused tests pass**, including existing load, access-trace formatting
  and reports, scripts/lifecycle, timer/frame/idle/event-loop and threading tests.
- Full real-V8 macOS arm64/Python 3.13 suite: **2772 passed, 2 skipped**.
- A deterministic pre-fix review reproduced one thread draining another
  thread's unfinished call; the final per-Page guard and seven competing-action
  cases protect the native-return-to-output window.
- Native engine/binding code is unchanged by this usability slice. The wheel
  reuses the local ENV-MON-01 native module and packages the updated Python code;
  V8 remains pinned to 15.0.245.19.

## Source and artifacts

Base HEAD: `b37595a` plus the pre-existing uncommitted ENV-MON-01 working tree
and this Python/test/documentation delta. No commit/tree identity is invented
for this dirty working tree. Per-file hashes and log/native identities are in
`data/page-monitoring-modes-2026-11-16/source-receipt.json`.

Wheel: `iv8-0.0.1-cp313-cp313-macosx_11_0_arm64.whl`, SHA-256
`6311bc656e21d39a84855144445155e3cb3e75a3051753669d23d66490fd2d1a`.
The build, focused and full logs are `build.log`, `focused.log` and `full.log`
under that same ignored data directory. Tests use the extracted final wheel,
with the full suite run outside the repository root. No dependency or shared
environment installation is performed. Hosted run/artifact IDs do not exist for
this local slice.

## Remaining boundaries

The implementation relies on the existing local ENV-MON-01 native candidate;
it does not repair the separate API coverage audit gaps or replace the shared
SDK. No cross-platform release matrix or version-tag publication is part of
this local usability change. The latest release-verified build remains the
combined PRES/WIN-MON-01 source `40d1c1f`; shared deployment remains BIND-01.
