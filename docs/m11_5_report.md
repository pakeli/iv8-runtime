# M11-5 Delivery Report — MutationObserver

Status: **VERIFIED — real-V8 green on Linux + Windows (`2026-10-28`)**

Both acceptance lanes are green. The run records and pytest summaries below
are the evidence.

## Branch and commits

- Branch: `m11-5-mutation-observer`
- Remote: `fork` → `pakeli/iv8-runtime`
- Feature commit: `41d2ad7949457c9427c2cbd4f5e1bfe35e7a00a9` (`41d2ad7`) —
  MutationObserver implementation
- Fix commit 1: `ab1d507` — pass `ExternalPointerTypeTag` on
  `External::New` / `External::Value` (compile fix; see below)
- Fix commit 2 (verified commit): `4fa6469` — set ids via `setAttribute` in
  the tests (see below)
- Previous commit: `8ad5a4e` — M11-4 verification docs + M11-5 selection
- M11-4 verified baseline: `0305b49`, `1213 passed, 2 skipped`
  (Linux run `40471874328`, Windows run `40483069548`)

## Run 1 (`41d2ad7`) — Linux compile failure

Linux run `40622582430` failed in `Build + repair + test wheels (cp311-cp314)`,
before pytest. V8 15.0 requires an `ExternalPointerTypeTag` argument on
`External::New` / `External::Value` — the same requirement previously fixed for
M3-4 in `52ded6e`. Six M11-5 call sites omitted the tag, producing
`too few arguments` errors in `src/core/page_state.cpp`.

Fix commit `ab1d507` adds a `kMutationObserverTag` constant (default tag) and
uses it consistently on both the New and Value sides.

The Windows run `40622586307` on `41d2ad7` was still building V8 when the fix
was pushed; it is left to finish so its V8 monolith cache save completes, and
its wheel jobs are expected to fail with the same compile error. That failure
is superseded by the rerun.

## Run 2 (`ab1d507`) — wheels build, 8 test failures (test bug)

The `ExternalPointerTypeTag` fix compiled on both platforms. The test stage
then reported, identically on Linux run `40630757184` and Windows run
`40630760166`:

```text
8 failed, 1257 passed, 2 skipped
```

All 52 new MutationObserver items executed (1257 + 8 = 1265). The 8 failures
were a test-authoring bug: the tests set ids with `c.id = 'c1'`, but this
runtime has no `.id` property setter (documented in
`docs/m11_5_mutation_observer.md`); the sloppy-mode assignment is a no-op, so
the record-summary helper fell back to `tagName` (`'B'`, `'DIV'`).

Fix commit `4fa6469` switches those assignments to
`setAttribute('id', ...)`, except the one test that deliberately asserts
property writes are ignored.

## Run 3 (`4fa6469`) — accepted

| Platform | Workflow | Run | Wheel lanes | Result |
|---|---|---|---|---|
| Linux | `release-linux` | `40632597636` | cp311, cp312, cp313, cp314 | each `1265 passed, 2 skipped` |
| Windows | `release-windows` | `40632601387` | Python 3.11, 3.12, 3.13, 3.14 | each `1265 passed, 2 skipped` |

- https://github.com/pakeli/iv8-runtime/actions/runs/40632597636
- https://github.com/pakeli/iv8-runtime/actions/runs/40632601387

`test/test_mutation_observer.py` ran in every lane (it has its own progress
line in the logs), so all 52 new items are included. 1265 is the 1213-item
M11-4 baseline plus those 52 items. No `FAILED` / `ERROR` lines in either run.

Both reruns hit the V8 monolith cache saved by the earlier runs and went
straight to wheel build + pytest.

Push-triggered skeleton `CI` runs (e.g. `40630753711`, `40632593954`) are
shape-guard lanes, not acceptance lanes. macOS is outside the M11-5 acceptance
scope. Linux and Windows are the only acceptance lanes.

## Reproduce / inspect the runs

`gh` is installed at `C:\Program Files\GitHub CLI\gh.exe` and authenticated as
`pakeli`.

```bash
gh run view 40632597636 --repo pakeli/iv8-runtime
gh run view 40632601387 --repo pakeli/iv8-runtime
```

The pytest summaries above were extracted from these job logs;
`test/test_mutation_observer.py` has its own progress line, so a missing file
would have lowered the count.

## Local static verification already done

- `python -m py_compile` on the new test module — passed
- `pytest --collect-only` — 52 items collected
- skeleton shape guard `test_no_new_python_surface` — passed on the M11-4 wheel
- 59 JS snippets in the new test file parsed by Node — no syntax errors
- `git diff --check` — clean
- no local native compile: this machine has no C++ toolchain / Windows V8
  monolith, so real-V8 CI is the first native execution (it caught the missing
  `ExternalPointerTypeTag` in run `40622582430`; fixed in `ab1d507`)

## Outcome

1. Recorded both run IDs, conclusions, and exact pytest summaries here.
2. Confirmed the new file ran and there are no `FAILED` / errors.
3. Marked M11-5 verified in `docs/m11_5_mutation_observer.md`,
   `docs/current_status.md`, `docs/next_tasks.md`, `docs/decision_log.md`,
   `docs/runtime_surface.md`, `CLAUDE.md`, and `docs/dev_setup.md`.
4. Committed the report plus status updates, then fast-forwarded `master` on
   the fork to `6f4f6dc` (upstream sync out of scope).
