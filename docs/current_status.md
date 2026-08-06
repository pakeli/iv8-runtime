# Current Status

Last reviewed: `2026-08-06`

## 1. Summary

`iv8-runtime` started as an M1-scoped Python/V8 embedding project, but the actual
implementation in `master` has progressed well beyond the original M1 boundary.

As of the current repository state, the latest landed milestone is **M10-2**:

- `M10-1`: `Page.watch_reflect_ownkeys()` / `read_reflect_ownkeys_hits()`
- `M10-2`: `Page.watch_object_keys()` / `read_object_keys_hits()`

This means the project is no longer only a `JSContext` kernel. It now includes a
hosted `Page` runtime, a minimal DOM/form/web-platform layer, and a second-layer
DevTools/monitoring surface.

## 2. What Is Actually Implemented

### M1 core

Implemented and present at `HEAD`:

- process-wide V8 initialization;
- one isolate per `JSContext`;
- `JSContext` lifecycle and idempotent disposal;
- `eval()` with resource names;
- structured JavaScript exceptions;
- value conversion and `JSValue` fallback;
- context busy/disposed guards;
- focused threading behavior.

### M2–M8 first-layer runtime

Implemented and present at `HEAD`:

- `Page` host-object container;
- `window` / `globalThis` / `self` / `console`;
- `navigator` / `location`;
- manual timers and microtask pumping;
- `Page.load()` static load model;
- minimal `document` / `element` tree;
- targeted DOM mutation and query helpers;
- external scripts, inline scripts, lifecycle, ready-state, current-script;
- events and event bubbling subset;
- collections, structure navigation, matching/query APIs;
- form ownership and form control value/default/reset behavior;
- form submission metadata methods/properties;
- `TextEncoder` / `TextDecoder`;
- `URL` / `URLSearchParams`;
- `atob` / `btoa`.

### M9–M10 second-layer monitoring

Implemented and present at `HEAD`:

- `Page.devtools_url()` Inspector attach base;
- `Page.watch_apis()` / `read_watch_api_hits()`;
- `Page.watch_reflect_get()` / `read_reflect_get_hits()`;
- `Page.watch_reflect_has()` / `read_reflect_has_hits()`;
- `Page.watch_reflect_ownkeys()` / `read_reflect_ownkeys_hits()`;
- `Page.watch_object_keys()` / `read_object_keys_hits()`;
- optional `break_on_hit=True` pause behavior when a DevTools client is attached.

## 3. Latest Known Milestone Sequence

Recent implementation sequence from Git history:

- `M7-1` through `M7-7`: form submission metadata and no-op submission methods
- `M8-1` through `M8-4`: text encoding, URL, URLSearchParams, Base64 globals
- `M9-1` through `M9-6`: Inspector attach base and watch API family
- `M10-1` and `M10-2`: reflection/key enumeration watch extensions

## 4. Release / Distribution Status

Current known status from repository docs:

- Linux release strategy was chosen in `docs/phase9_release_strategy.md`.
- The repository records a green Linux wheel workflow result for cp311–cp314.
- Linux is the real release target.
- Windows and macOS exist in CI/build planning, but should still be treated as
  non-primary release targets unless explicitly updated.

## 5. Documentation Layering (reconciled)

The documentation layering mismatch that used to mislead new sessions is now
addressed, not just recorded:

- `docs/runtime_surface.md` is the **overview index** of the surface that actually
  exists at `HEAD`, with a milestone map and links to every frozen boundary;
- the four M1-era documents — `api_contract.md`, `architecture.md`,
  `implementation_plan.md`, `test_plan.md` — each carry a **scope note** at the top
  stating what part of them is still authoritative and what is historical;
- `AGENTS.md` and `CLAUDE.md` point at the index and no longer read as M1-only
  constraints.

`docs/api_contract.md` remains the authoritative contract for `JSContext`. Do not
“downgrade” the repository mentally to M1 because that file is the oldest formal
contract. Cross-check with:

- `docs/runtime_surface.md` (start here)
- `python/iv8/__init__.py` and `python/iv8/page.py`
- the milestone summaries — `docs/m3_summary.md`, `docs/m4_a_summary.md`,
  `docs/m4_b_summary.md`, `docs/m5_summary.md`, `docs/m6_summary.md`,
  `docs/m7_summary.md`, `docs/m8_summary.md`, `docs/m9_summary.md`,
  `docs/m10_1_watch_reflect_ownkeys.md`, `docs/m10_2_watch_object_keys.md`
- recent `git log`

## 6. Current Open Questions

These are the questions a new coding session should resolve before major new work:

- Is the next phase intended to continue M10 monitoring breadth, or begin M11?
  (`docs/decision_log.md` §A — still open.)
- Should `Page` gain a single frozen contract document, or keep the milestone-doc
  approach? (`docs/decision_log.md` §B — still open. `docs/runtime_surface.md` is an
  *index*, not a contract, so it does not settle this.)
- Should Windows release support remain secondary, or be promoted again?
  (`docs/decision_log.md` §C — still open.)
- Should the stale build/CI comments that still mention early/M1-era limitations be
  refreshed? (`docs/next_tasks.md` Priority 3 — not yet done.)

## 7. Practical Status Verdict

If you need a one-line answer:

> The project has completed the original V8 embedding kernel and is currently at
> the post-M9 / M10 monitoring-extension stage, with `M10-2` as the latest landed
> milestone.

For the surface behind that verdict, read `docs/runtime_surface.md`.

## 8. Local Environment Note

`data/review-venv` currently holds a **stale Phase-1 skeleton** install of `iv8` (its
`__init__.py` predates `Page`, and its `_core` extension has no `Page` binding), so
running `pytest test` against it reports widespread `AttributeError` failures rather
than the expected skips. That is a local install artifact, not a repository
regression. Rebuild and reinstall before trusting any local suite result, and check
`iv8._v8_linked` to know whether you are on a real-V8 or skeleton build.
