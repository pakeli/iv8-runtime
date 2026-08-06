# Next Tasks

Last reviewed: `2026-08-06`

This file is intentionally short and should be kept current. It is the preferred
task queue for Claude after chat-history loss.

## Priority 1: Reconcile contracts with implementation reality — DONE (`2026-08-06`)

### Goal

Reduce confusion between the original M1 contract and the real runtime surface now
present in `HEAD`.

### What was delivered

- **`docs/runtime_surface.md`** — a new overview index: the three-layer model, the
  complete public Python surface (`JSContext` + every `Page` member with its
  milestone), the JS-side surface, the standing exclusions, a milestone map linking
  every summary, and the build/platform reality.
- **Scope notes** on the four M1-era documents — `api_contract.md` (M1 `JSContext`
  only), `architecture.md` (ownership/lifecycle still authoritative, §2 Non-Goals
  historical), `implementation_plan.md` (completed historical plan), `test_plan.md`
  (M1 suite; later milestones add collar suites).
- **`AGENTS.md`** — the Project Scope rules no longer read as a live ban on
  DOM/timers/DevTools; they are marked as the M1 kernel boundary, with a pointer to
  the milestone-scoped working rule and to the index.
- **`CLAUDE.md`** — reading order now includes the index; the Reality Check names it.
- **`docs/current_status.md`** — §5 rewritten from "mismatch to remember" to
  "layering reconciled"; open questions now cite their `decision_log.md` entries;
  a new §8 records the stale local venv.

### Acceptance — met

- a new contributor can identify the current milestone and surface from
  `docs/runtime_surface.md` without reading commit history;
- no document now implies the repository exposes only M1 behavior.

### Deliberately not done

`runtime_surface.md` is an **index, not a contract** — it does not freeze `Page`
behavior. Whether `Page` should gain a frozen contract document remains Priority 4 /
`decision_log.md` §B.

## Priority 2: Decide the next implementation branch after M10-2

### Goal

Pick the next milestone direction instead of continuing ad hoc.

### Candidate directions

- continue monitoring breadth in the M10 family;
- formalize a new M11 plan;
- invest in contract/document cleanup before more runtime growth;
- strengthen release/CI/platform consistency.

### Acceptance

- one approved direction is written down in `docs/decision_log.md`;
- `docs/current_status.md` and this file reflect the choice.

## Priority 3: Refresh stale build/CI comments

### Goal

Update comments or docs that still describe a much earlier repository state.

### Why this matters

- some comments still mention “placeholder extension”, “no eval yet”, or
  M1-era constraints that no longer match `HEAD`;
- this slows future handoffs and code review.

### Suggested targets

- `src/module.cpp`
- `CMakeLists.txt`
- `.github/workflows/ci.yml`
- other docstrings/comments discovered during edits

### Acceptance

- comments describe the real current surface or clearly note that they are
  historical.

## Priority 4: Decide whether to publish a `Page`-level contract

### Goal

Choose whether the `Page` API should remain milestone-doc driven or gain a single
public contract document similar to M1.

### Why this matters

- `Page` is now central to the real product surface;
- behavior is currently spread across many milestone files and tests.

### Acceptance

- either a new contract doc is created, or a recorded decision explains why the
  milestone-doc approach remains preferred.

## Priority 5: Keep context files updated after every meaningful milestone

### Goal

Prevent another loss of working context after machine migration.

### Required discipline

After any milestone or multi-file feature lands, update:

- `docs/current_status.md`
- `docs/next_tasks.md`
- `docs/decision_log.md` when a real direction choice was made

### Acceptance

- future contributors can resume work from the repo alone.
