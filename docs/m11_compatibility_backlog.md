# M11 Compatibility Backlog

Last reviewed: `2026-10-28`

## 1. Purpose

This is the Priority 4 deliverable from `docs/next_tasks.md`: replace per-slice
intuition with a ranked, source-backed compatibility backlog against the community
edition of `HanZzzzz000/iv8`, then choose `M11-5` from it.

The ranking is planning guidance, not a promise of full parity.

## 2. Sources and method

- **Target:** `HanZzzzz000/iv8` community edition, default branch `main` (last
  public push `2026-10-13`). The repository distributes precompiled binaries, so
  this survey is based on `README_EN.md` / `README.md`, `demos/`, and `examples/`
  — not on source.
- **Current surface:** `docs/runtime_surface.md`, the milestone boundary docs, and
  the test suite.
- **Scoring:** compatibility value (1–5, how often target scripts rely on it),
  architecture fit (1–5), implementation cost/risk (1–5, higher = cheaper/safer).
  The scores compare candidates with each other; they are not absolute.

## 3. Ranked candidates

| Rank | Candidate | Target evidence | Current gap | Value | Fit/cost | Verdict |
|---|---|---|---|---|---|---|
| 1 | **`MutationObserver`** | Listed in the DOM/HTML compatibility table and in the micro-task examples (`README_EN.md`); used by framework and anti-bot code | Our DOM has mutation paths (`appendChild` / `removeChild` / `insertBefore` / `setAttribute` / `removeAttribute` / `textContent`) and M11-4 microtasks, but no observation model | 5 | 5 / 3 | **M11-5 delivered & verified (`2026-10-28`)** |
| 2 | **Event subclass cluster** (`CustomEvent`, `MouseEvent`, `KeyboardEvent`, `FocusEvent`) | Target claims 80+ event types; demos use clicks and lifecycle events | We install only a minimal `Event` constructor; no subclasses or their read-only fields | 4 | 5 / 5 | **M11-6 delivered & verified (`2026-10-28`)** |
| 3 | **Web Storage** (`localStorage`, `sessionStorage`) | Listed in the Storage compatibility category | No storage surface at all | 4 | 4 / 4 | **M11-7 delivered & verified (`2026-10-28`)** |
| 4 | **`Blob` / `File` / `FileReader`** | Listed under Encoding & File | We have `TextEncoder` / `TextDecoder`, `URL`, `URLSearchParams`, `atob` / `btoa`; no binary/file objects | 3 | 4 / 3 | **M11-8 delivered & verified (`2026-10-28`)** |
| 5 | **Nested environment + `get_defaults()`** | `JSContext(environment={...})` with nested navigator/screen/location/webgl paths; `get_defaults()` enumerates them | M11-1 has a flat 11-key `Page(environment=...)` baseline and `get_default_environment()` | 3 | 4 / 4 | **M11-17 delivered & verified (`2026-10-28`)** |
| 6 | **Event-loop facade** (`advance` / `sleep` / `tick` / `drain`) | Central to the target's `window.__iv8__.eventLoop` model | We deliberately expose four independent manual pumps and no scheduler | 5 | 2 / 2 | **M11-29 delivered timers + microtasks; M11-30 delivered rAF/idle integration; real clock/sleep deferred** |
| 7 | **Offline resources + runtime XHR/fetch** | Community edition matches XHR/fetch/external resources against an injected offline bundle (`add_resource`, `page.load.resources`) | `Page.load(resources)` only pre-resolves `<script src>`; no runtime XHR/fetch | 5 | 2 / 2 | High-risk; standing networking exclusion needs approval |
| 8 | **JS→Python `expose()`** | Target exposes Python objects under `__iv8__.data` | Explicitly never implemented; architecture boundary | 5 | 1 / 1 | Requires a separate architecture decision |
| 9 | **Canvas / WebGL / CSSOM / Workers / full Storage stack** | Large target categories | Entirely absent | 5 | 1 / 1 | Not a near-term slice |

## 4. Decision

**M11-5: `MutationObserver`** — selected by the owner on `2026-10-28`; boundary
document: `docs/m11_5_mutation_observer.md`. Delivered and verified on real V8
(Linux + Windows) on `2026-10-28`; see `docs/m11_5_report.md`.

Why this slice:

- It is a real target capability, not an invented extension.
- It builds directly on two things the repository already has: the DOM mutation
  paths and the M11-4 microtask checkpoint (`Page.run_jobs()`).
- It needs no fifth pump, no clock, no network, and no JS→Python callback.
- It is bounded enough to freeze and test: observer registration/options, record
  construction, microtask delivery, `takeRecords()`, `disconnect()`, and
  `load()`/`dispose()` lifecycle.

Minimal scope to freeze in `docs/m11_5_mutation_observer.md`:

- `new MutationObserver(callback)` with `observe(target, options)`,
  `disconnect()`, `takeRecords()`;
- `childList`, `attributes`, `subtree`, `attributeOldValue`, `attributeFilter`;
- records are queued in mutation order and delivered by the existing
  `Page.run_jobs()` microtask checkpoint, once per observer;
- mutations covered: child insertion/removal and attribute writes/removals via
  `setAttribute` / `removeAttribute`; `.id` / `.className` are getter-only in this
  runtime, so property writes remain no-ops and produce no records;
- `characterData` / `characterDataOldValue` are out of scope because this DOM is
  element-only (no text/comment nodes);
- no new Python API, no scheduler, no real-time observation, no cross-generation
  observers.

Tests: `test/test_mutation_observer.py`.

## 5. Why not the others now

- **Event subclasses** are the smallest follow-up and a good M11-6 candidate, but
  they close less of the target's DOM/event surface than observation does.
- **Web Storage** is independent and attractive, but its origin/persistence/event
  semantics need their own boundary decisions; it is better as a separate slice.
- **Nested environment / `get_defaults()`** is an API-shape convergence question
  (`docs/decision_log.md` §B) rather than a pure runtime feature; it should not be
  smuggled in under M11-5.
- **Event-loop facade** now has a deterministic timer/microtask/rAF/idle slice
  (M11-29 + M11-30); real clock/sleep, **networking**, and **`expose()`** remain
  larger architecture decisions and are still deliberately deferred.

## 6. Next step

M11-5 is delivered and verified (`2026-10-28`).

`M11-6` — **Event subclass cluster** — was selected on `2026-10-28` (rank 2:
`CustomEvent` / `MouseEvent` / `KeyboardEvent` / `FocusEvent`). Its boundary
document is `docs/m11_6_event_subclasses.md`; implementation and real-V8
verification are in progress on the `m11-6-event-subclasses` branch.

`M11-7` (**Web Storage**) and `M11-8` (**`Blob` / `File` / `FileReader`**) were
selected on `2026-10-28` as one batch on the same verification branch, to
amortize the cold V8 build. Boundaries:
`docs/m11_7_web_storage.md`, `docs/m11_8_blob_file_reader.md`.

The batch was extended to five slices on `2026-10-28`: `M11-9` **Event
cancellation** and `M11-10` **classList / toggleAttribute** come from the gap
inventory (`docs/dom_bom_gaps.md`) rather than the ranked shortlist, and were
verified together with M11-6 through M11-8. Boundaries:
`docs/m11_9_event_cancellation.md`, `docs/m11_10_class_list.md`.

All five slices are delivered and verified. Final batch runs: Linux
`40661574145`, Windows `40661580891`, commit `35c85fe`, each wheel lane
`1352 passed, 2 skipped`. Reports: `docs/m11_6_report.md` …
`docs/m11_10_report.md`.

`M11-11` — **Event capture phase + listener options** — was subsequently
selected on `2026-10-28` from the gap inventory and is now delivered and
verified (Linux `40683001239`, Windows `40683010032`, commit `a269aa9`, each
`1370 passed, 2 skipped`); boundary document
`docs/m11_11_event_capture.md`, report `docs/m11_11_report.md`.

`M11-12` through `M11-17` are a six-slice DOM/web completeness batch selected
on `2026-10-28` (attribute API, tree editing, cloneNode/equality,
URL.searchParams snapshot, select extension, nested environment); boundary
document `docs/m11_12_17_dom_web_batch.md`. Delivered and verified on real V8:
Linux `40695004440`, Windows `40695029530`, commit `8d606e9`, each wheel lane
`1378 passed, 2 skipped`; report `docs/m11_12_17_report.md`.

`M11-18` — **form-control attribute reflection** — was selected on
`2026-10-28` from `docs/dom_bom_gaps.md` rather than the ranked list, because
the remaining ranked candidates need separate architecture decisions
(event-loop facade, networking, `expose()`). Boundary:
`docs/m11_18_form_control_reflection.md`. Delivered and verified on real V8:
Linux `40698705892`, Windows `40698728631`, commit `08350ce`, each wheel lane
`1386 passed, 2 skipped`; report `docs/m11_18_report.md`.

`M11-19` through `M11-23` — **DOM/form completeness batch** — were selected
`2026-10-28` from the gap inventory: multi-token class queries and element-level
`getElementsByClassName`, `document.getElementsByName`, extended form
reflection, select `length` / `item` / `namedItem`, and deterministic document
metadata. Boundary: `docs/m11_19_23_dom_form_batch.md`. Delivered and verified
on real V8: Linux `40704330688`, Windows `40704362549`, commit `999c851`, each
wheel lane `1412 passed, 2 skipped`; report `docs/m11_19_23_report.md`.

`M11-24` through `M11-28` — **host environment / console batch** — were selected
`2026-10-28` from the gap inventory: navigator metadata/identity, screen
color/pixel depth, outer window geometry, and console method extensions.
Boundary: `docs/m11_24_28_host_environment_batch.md`. Delivered and verified on
real V8: Linux `40711526339`, Windows `40711548915`, commit `a00e140`, each wheel
lane `1474 passed, 2 skipped`; report `docs/m11_24_28_report.md`.

`M11-29` — **deterministic event-loop facade** — was selected/frozen on
`2026-10-29` as the first composed driver over timers + microtasks. Boundary:
`docs/m11_29_event_loop_facade.md`. Delivered and verified on real V8: Linux
`40820152813`, Windows `40820163797`, commit `b211481`, each wheel lane
`1495 passed, 2 skipped`; report `docs/m11_29_report.md`.

`M11-30` — **rAF/idle integration into the event-loop facade** — was
selected/frozen on `2026-10-29`: fixed 16ms virtual frames, idle rounds when no
timer/frame is due, and microtask checkpoints after every task. Boundary:
`docs/m11_30_frame_idle_facade.md`. Delivered and verified on real V8: Linux
`40826630371`, Windows `40826634489`, commit `7dde955`, each wheel lane
`1509 passed, 2 skipped`; report `docs/m11_30_report.md`. The next slice
(`M11-31`) should again be chosen from this backlog or the gap inventory.

Browser-surface gaps that are not (yet) ranked here are inventoried in
`docs/dom_bom_gaps.md`.
