# page_state.cpp split — progress

Plan (confirmed `2026-11-08`): `docs/page_state_split_plan.md`. Structure only:
every step is buildable and behavior-preserving. Each PR requires V8 CI (Linux
x64 / Python 3.13) and a local real-V8 full pytest. The three-platform release
matrix (Python 3.11–3.14) runs once at the end of each stage. Pure-move PRs paste
`tools/audit_move.py` output showing **0 definition differences**.

Baseline before the split: `master` `6191958`, `page_state.cpp` 13,058 lines,
every lane `2050 passed, 2 skipped, 1 xfailed`.

## Progress

| Step | PR | Source commit | CI | `page_state.cpp` lines | Audit |
|---|---|---|---|---|---|
| 0.1 local real-V8 loop | (this PR) | (this PR) | — | — | — |
| 0.2 `iv8::browser`, `internal/common.h`, inline tokens, PCH | pakeli/iv8-runtime#14 | `ae4163b` (merge `c1d4e34`) | V8 CI `42466767787`, `42467387443`; release-windows `42466768875` | 13,052 | n/a (intentional changes; tool added in 0.3) |
| 0.3 `tools/audit_move.py` | (this PR) | (this PR) | pending | 13,052 | self-tests 16 passed |

Stage 0 three-platform matrix: pending.

## Notes

- 0.1 changed course from the handoff: the macOS arm64 monolith already in
  `data/v8/` gives a local real-V8 loop, so no CI-cache export workflow or
  `tools/fetch_v8_cache.sh` was added. Commands: `docs/dev_setup.md` "Local
  real-V8 loop".
- 0.2 left `kJavaEnabledTraceToken` and `kStorageTraceToken` defined but
  unreferenced, as they were before. This is a candidate for plan section 7.
- 0.3: `tools/audit_move.py` gates top-level definitions. It does not yet
  decompose class members moved out of a class body. Extend it before stage 3.3.
