# M11-31 / M11-32 Delivery Report — Performance Clock + Form Validation

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-29`)**

Branch `m11-31-32-perf-validation-batch`, final verified commit `a88b69d`
(feature commit `faf5e28`).

## Evidence

- Linux `release-linux` run `40831677507` — cp311 / cp312 / cp313 / cp314,
  each `1540 passed, 2 skipped`.
- Windows `release-windows` run `40831682182` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1540 passed, 2 skipped`.
- `test/test_m11_31_virtual_performance_clock.py` (14 items) and
  `test/test_m11_32_form_validation.py` (17 items) ran in every lane. The full
  suite collects 1542 items; 1540 pass and 2 are skipped.
- The first attempt (`faf5e28`) failed the new readOnly validation test with
  `ReferenceError: r is not defined`: the test helper exposed only some element
  ids as JS globals and omitted `r`. `a88b69d` adds it; this was a test-side
  omission, not a runtime defect. The reruns above are green.

## M11-31 scope landed

- `performance.now()` reports the M11-29/M11-30 facade's virtual clock;
  `performance.timeOrigin` is the virtual epoch `0`.
- `requestAnimationFrame` callbacks receive the current virtual timestamp as
  their only argument; all callbacks in a frame share it. Legacy pumps pass the
  unchanged clock.
- `watch_apis(["performance.now"])` records calls. `load()` resets the clock.
- No real clock, marks/measures/timelines, or Python `performance` surface.

## M11-32 scope landed

- Control surface on input/button/select/textarea: `willValidate`, `validity`
  (all standard flags, fresh read-only object), `validationMessage`,
  `checkValidity()`, `reportValidity()`, `setCustomValidity()`.
- Form surface: `checkValidity()` / `reportValidity()` aggregate
  `form.elements`; the form itself has no `validity` / `willValidate`.
- Constraints: `required` (checkbox/radio via `checked`), `minlength` /
  `maxlength` (UTF-16 units), email/URL `typeMismatch`, and `customError`.
  Unsupported flags stay false.
- Barred controls: disabled, read-only input/textarea, and input types
  `hidden` / `reset` / `button` / `image`.
- Invalid controls receive a target-only, non-bubbling minimal `invalid` event;
  `invalid` never bubbles to the form. `form.noValidate` does not skip explicit
  checks.
- No `pattern` / `min` / `max` / `step`, radio-group semantics, `FormData`,
  submission, native UI, or `ValidityState` global.

See `docs/m11_31_32_perf_validation_batch.md`.

## Verification notes

- Local `zig c++ -fsyntax-only` passed for `page_state.cpp` and
  `context_host.cpp`; the new tests and Python package passed `py_compile`;
  1542 tests collect locally.
- The V8 monolith cache was reused; both workflows completed from the warmed
  master cache.
- Historical guards for a missing clock/validation surface were refreshed in
  the same change (M11-2/M11-3 docs and the form/M6/M7 contract tests).

## Outcome

M11-31/M11-32 are verified and merged to fork `master`. The facade now exposes
its virtual time to JS, and the DOM has a bounded constraint-validation surface.
Real time, native validation UI, and the deferred constraints remain out of
scope.
