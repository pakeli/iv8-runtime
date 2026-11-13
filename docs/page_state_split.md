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
| 0.3 `tools/audit_move.py` and regression fixes | pakeli/iv8-runtime#15 | verification pending | required V8 CI includes audit tests | 13,052 | focused 56 passed locally |

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

## Local audit regression follow-up

The working-tree follow-up after `9095c2b` fixes variable-definition and directive
literal false negatives. Brace/direct initializers retain their storage
specifiers and values. Directive literals retain their exact contents, including
whitespace, escaped quotes and raw strings. Ambiguous parenthesized declarations
are conservatively gated as definitions.

The new `test/test_audit_move_regressions.py` contains 40 cases, including CLI
failure checks for changed definitions and a successful move across files. It is
collected by the normal `pytest test` invocation. Together with the original
tool self-tests, the focused result is **56 passed**. Local macOS arm64/Python
3.11 full-suite verification against the V8-linked `9095c2b` runtime and current
tests reported **2090 passed, 2 skipped, 1 xfailed**. The three browser source
files self-matched with **431 identical definitions, 0 differences**.

The required Linux/Python 3.13 V8 job now runs all 56 audit tests before its V8
build and runtime suite. These results cover local verification of this
follow-up. Release and CI records for the follow-up remain pending.
