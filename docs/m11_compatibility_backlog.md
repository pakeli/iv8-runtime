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
| 1 | **`MutationObserver`** | Listed in the DOM/HTML compatibility table and in the micro-task examples (`README_EN.md`); used by framework and anti-bot code | Our DOM has mutation paths (`appendChild` / `removeChild` / `insertBefore` / `setAttribute` / `removeAttribute` / `textContent`) and M11-4 microtasks, but no observation model | 5 | 5 / 3 | **M11-5 selected (`2026-10-28`)** |
| 2 | **Event subclass cluster** (`CustomEvent`, `MouseEvent`, `KeyboardEvent`, `FocusEvent`) | Target claims 80+ event types; demos use clicks and lifecycle events | We install only a minimal `Event` constructor; no subclasses or their read-only fields | 4 | 5 / 5 | Low-risk fallback |
| 3 | **Web Storage** (`localStorage`, `sessionStorage`) | Listed in the Storage compatibility category | No storage surface at all | 4 | 4 / 4 | Independent, medium slice |
| 4 | **`Blob` / `File` / `FileReader`** | Listed under Encoding & File | We have `TextEncoder` / `TextDecoder`, `URL`, `URLSearchParams`, `atob` / `btoa`; no binary/file objects | 3 | 4 / 3 | Medium slice; async event delivery needed |
| 5 | **Nested environment + `get_defaults()`** | `JSContext(environment={...})` with nested navigator/screen/location/webgl paths; `get_defaults()` enumerates them | M11-1 has a flat 11-key `Page(environment=...)` baseline and `get_default_environment()` | 3 | 4 / 4 | API-shape decision; subset only |
| 6 | **Event-loop facade** (`advance` / `sleep` / `tick` / `drain`) | Central to the target's `window.__iv8__.eventLoop` model | We deliberately expose four independent manual pumps and no scheduler | 5 | 2 / 2 | Deferred design decision |
| 7 | **Offline resources + runtime XHR/fetch** | Community edition matches XHR/fetch/external resources against an injected offline bundle (`add_resource`, `page.load.resources`) | `Page.load(resources)` only pre-resolves `<script src>`; no runtime XHR/fetch | 5 | 2 / 2 | High-risk; standing networking exclusion needs approval |
| 8 | **JS→Python `expose()`** | Target exposes Python objects under `__iv8__.data` | Explicitly never implemented; architecture boundary | 5 | 1 / 1 | Requires a separate architecture decision |
| 9 | **Canvas / WebGL / CSSOM / Workers / full Storage stack** | Large target categories | Entirely absent | 5 | 1 / 1 | Not a near-term slice |

## 4. Decision

**M11-5: `MutationObserver`** — selected by the owner on `2026-10-28`; boundary
document: `docs/m11_5_mutation_observer.md`.

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
- **Event-loop facade**, **networking**, and **`expose()`** all represent larger
  architecture decisions and remain deliberately deferred.

## 6. Next step

M11-5 is confirmed; the boundary document is `docs/m11_5_mutation_observer.md`.
Implement only the frozen scope above, add focused tests, and update
`docs/current_status.md`, `docs/next_tasks.md`, and `docs/decision_log.md` when
the slice lands.
