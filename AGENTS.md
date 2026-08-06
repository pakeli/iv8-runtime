# AGENTS.md

This file defines project-specific guidance for the `iv8-runtime` repository.

## Project Scope

- Build a Python native extension that embeds Google V8.
- Use C++20, pybind11, CMake, scikit-build-core, and pytest.
- M1 covers only V8 initialization, `JSContext`, JavaScript evaluation, value conversion, exception translation, disposal, and isolated multi-context execution. **M1 is complete** — the two rules below are its historical boundary, kept because they still describe what belongs *outside* the kernel.
- Do not implement browser APIs, DOM, networking, timers, DevTools, or Python callbacks *in the M1 kernel*. The hosted `Page` runtime (M2–M8) and the DevTools/monitoring layer (M9–M10) own those concerns instead, each within its own milestone boundary; see `docs/runtime_surface.md`.
- Work at `HEAD` is milestone-scoped, not M1-scoped: implement what the current milestone approves and nothing beyond it. Networking, navigation, and JS→Python callbacks remain unimplemented across **every** milestone so far.

## Architecture Rules

- Pin an explicit V8 release; never build against V8 `master` by default.
- Use one `v8::Isolate` and one ArrayBuffer allocator per `JSContext`.
- Keep process-wide V8 platform initialization separate from context-owned resources.
- Keep pybind11 registration thin. Put V8 execution, conversion, and exception logic in dedicated C++ modules.
- Release the Python GIL while compiling or executing JavaScript.
- Acquire the Python GIL before creating Python values or raising Python exceptions.
- Do not permit concurrent execution of the same `JSContext`; reject it or serialize it explicitly.
- Destruction and `dispose()` must be idempotent and must not throw from C++ destructors.
- Avoid unchecked `ToLocalChecked()` calls on fallible V8 operations.
- Preserve JavaScript exception names, messages, source locations, and stack traces.

## Coding Rules

- Place imports and includes at the top of each file.
- Use descriptive lowercase Python names with underscores.
- Use descriptive C++ type and method names consistent with the architecture documents.
- Avoid one-letter variable names except for conventional short loop indices.
- Keep changes small and aligned with the current milestone.
- Do not add abstractions for unapproved future features.
- Do not introduce Boost.Python, Node.js, QuickJS, or another JavaScript engine.

## Repository Layout

- Store architecture and planning documents under `docs/`.
- Store Python package files under `python/iv8/` when implementation begins.
- Store public C++ headers under `include/iv8/`.
- Store native implementation under `src/`.
- Store tests under `test/`.
- Store generated caches, downloaded archives, V8 source trees, and build outputs under `data/` or ignored build directories.

## Testing Rules

- Use pytest for Python-facing behavior.
- Add tests under `test/`.
- Start with focused tests for the changed behavior before running the full suite.
- Every lifecycle, conversion, exception, isolation, and threading requirement in `docs/test_plan.md` must have coverage before M1 is considered complete.
- Tests must not depend on network access.

## Change Control

- Treat `docs/api_contract.md` as the public M1 `JSContext` API contract — **not** as
  the whole current runtime contract. The later `Page` / DOM / monitoring surface is
  frozen by the per-milestone summaries; `docs/runtime_surface.md` indexes all of it.
- Treat `docs/architecture.md` as the source of truth for ownership and lifecycle.
- Treat `docs/stpyv8_reference.md` as design evidence, not as a compatibility requirement or source-copying instruction.
- If implementation requires changing either contract, update the document first and explain the reason.
- Do not expand M1 scope without explicit approval.
