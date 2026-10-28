# Development Setup

Last reviewed: `2026-10-27`

This document is a practical quick-start for resuming development on a new machine.

## 1. Repository Basics

- Python package source lives under `python/iv8/`.
- native headers live under `include/iv8/`.
- native implementation lives under `src/`.
- tests live under `test/`.
- build scripts live under `tools/`.

## 2. Python / Build Baseline

From `pyproject.toml`:

- Python: `>=3.11,<3.15`
- build backend: `scikit-build-core`
- bindings: `pybind11`
- tests: `pytest`

## 3. Useful Local Commands

### Inspect recent status

```powershell
git log --oneline --decorate -n 20
git status --short
```

### Build a wheel locally

```powershell
python -m pip wheel . --no-deps -w dist
```

### Install test dependencies

```powershell
python -m pip install -e .[test]
```

### Run the full test suite

```powershell
python -m pytest test -v
```

### Run a focused test file

```powershell
python -m pytest test/test_watch_object_keys.py -v
```

## 4. Platform Notes

### Linux

- Linux is the main release/distribution target according to
  `docs/phase9_release_strategy.md`.
- wheel and monolith build helpers exist in:
  - `tools/build_v8_linux.sh`
  - `tools/build_v8_manylinux.sh`
  - `tools/build_linux_wheels.sh`

### Windows

- Windows build helpers exist:
  - `tools/fetch_v8_windows.bat`
  - `tools/build_v8_windows.bat`
- `pyproject.toml` config expects `clang-cl` on Windows.
- Treat Windows as a development/build target, not the primary release baseline,
  unless repository docs are updated.

### macOS

- CI mentions macOS lanes, but current repo context does not indicate it is the
  primary release target.

## 5. CI / Release Entry Points

- general CI: `.github/workflows/ci.yml`
- Linux release wheels: `.github/workflows/release-linux.yml`
- Windows release lane exists: `.github/workflows/release-windows.yml`

## 6. How To Resume Safely After Context Loss

Read, in this order:

1. `AGENTS.md`
2. `CLAUDE.md`
3. `docs/current_status.md`
4. `docs/next_tasks.md`
5. `docs/decision_log.md`
6. `docs/api_contract.md`
7. `docs/architecture.md`

Then scan recent commits and only after that start code changes.

## 7. Recommended Editing Discipline

- For new runtime behavior, update tests first or alongside code.
- For milestone completion, update the milestone note or summary doc.
- For project-direction changes, update `docs/decision_log.md`.
- For resume-context changes, update `docs/current_status.md` and
  `docs/next_tasks.md`.

## 8. Current Resume Fact

If you only remember one thing:

> `HEAD` is not an M1-only repository; implementation is through M11-28
> (M11-24..M11-28 host environment / console batch), verified on real V8
> (Linux + Windows) on `2026-10-28` and merged to `master` on the fork
> (`pakeli/iv8-runtime`); upstream sync is out of scope. See
> `docs/m11_24_28_report.md`.
