# M11-31 / M11-32 batch — Virtual performance clock + form validation

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-29`)**

This batch adds two independent compatibility slices on top of M11-30:

- **M11-31** — a virtual `performance` clock tied to the event-loop facade, plus
  the virtual timestamp argument for animation-frame callbacks.
- **M11-32** — a minimal, deterministic form constraint-validation API.

Both are JS-side only. Neither adds a Python method, a real clock, a
background thread, an automatic loop, or any networking.

## M11-31 — Virtual performance clock

### Public API

```js
performance.now()       // current virtual milliseconds (Number, starts at 0)
performance.timeOrigin  // 0 (the virtual epoch), read-only
```

- `now()` reports `Page.event_loop_time_ms` (the M11-29/M11-30 facade clock),
  not wall-clock time. It advances only when Python calls `tick()`, `advance()`,
  or `run_until_idle()`; timer/frame jumps included.
- `load()` installs a new generation and resets the clock (and therefore
  `now()`) to `0`; `timeOrigin` stays `0`.
- The legacy pumps (`run_timers` / `run_jobs` / `run_animation_frame` /
  `run_idle_callbacks`) do not advance this clock.
- `Date.now()` remains V8's real clock and is not tied to this value.

### Animation-frame timestamp

Every `requestAnimationFrame` callback receives one argument: the current
virtual timestamp, the same value `performance.now()` reports while the
callback runs. All callbacks in one frame receive the same timestamp. A legacy
`run_animation_frame()` call passes the unchanged current virtual time (it does
not advance the clock).

### Non-goals

- No real clock, `performance.mark` / `measure` / `getEntries`, `PerformanceTiming`,
  navigation timing, resource timing, or `PerformanceObserver`.
- No vsync, paint, layout, or rendering.
- No Python-side `Page.performance` API; `Page.event_loop_time_ms` remains the
  Python view.

### Tests

`test/test_m11_31_virtual_performance_clock.py` covers the surface, read-only
members, clock progression, timer/microtask/rAF timestamps, legacy-pump
isolation, `load()` reset, `watch_apis(["performance.now"])`, and non-goals.

`test/test_animation_frames.py` and `test/test_idle_callbacks.py` had historical
frozen-surface guards for a missing clock; they are refreshed in this batch.

## M11-32 — Minimal form validation API

### Public API

On `input` / `button` / `select` / `textarea`:

```js
element.willValidate       // boolean, read-only
element.validity           // fresh ValidityState-like object, read-only flags
element.validationMessage  // deterministic string, read-only
element.checkValidity()    // boolean; fires `invalid` on this control when false
element.reportValidity()   // same as checkValidity; no UI in this runtime
element.setCustomValidity(message)  // String coercion; "" clears
```

On `form`:

```js
form.checkValidity()    // validates every control in form.elements, in order
form.reportValidity()   // same; no UI
```

The form itself does not gain `willValidate` / `validity` /
`validationMessage` / `setCustomValidity`.

### Supported constraints

- `required` on input / textarea / select (checkbox and radio use `checked`;
  radio-group semantics are not implemented — each radio is evaluated alone);
- `minlength` / `maxlength` on textarea and text-like inputs, counted in UTF-16
  code units (empty values are not "too short");
- `typeMismatch` for `input[type=email]` (one `@`, non-empty sides, no
  whitespace) and `input[type=url]` (absolute URL with a scheme; http/https need
  a host);
- `customError` from `setCustomValidity()`.

`validity` always exposes the standard flags
(`valueMissing` / `typeMismatch` / `patternMismatch` / `tooLong` / `tooShort` /
`rangeUnderflow` / `rangeOverflow` / `stepMismatch` / `badInput` / `customError`
/ `valid`). Unsupported constraints stay `false`.

`willValidate` is false for disabled controls, read-only input/textarea, and
input types `hidden` / `reset` / `button` / `image`. Barred controls are always
valid and are skipped by form-level checks.

### Messages

Deterministic strings:

- custom: the string passed to `setCustomValidity`;
- `valueMissing`: `Please fill out this field.`
- email `typeMismatch`: `Please enter a valid email address.`
- URL `typeMismatch`: `Please enter a URL.`
- `tooShort` / `tooLong`: `Please lengthen this text to N characters or more.`
  / `Please shorten this text to N characters or fewer.`

### `invalid` events

`checkValidity()` and `reportValidity()` fire a target-only, non-bubbling
`invalid` event at every invalid control they visit. The event is a minimal
plain object (`type` / `bubbles` / `cancelable` / `defaultPrevented` /
`eventPhase` / `target` / `currentTarget`) like the M3-4 lifecycle events; it
does **not** install `preventDefault` / `stopPropagation`. Form-level checks
fire one event per invalid control, in `form.elements` order. `invalid` never
bubbles to the form.

`form.noValidate` remains a submission-skip switch; explicit
`checkValidity()` / `reportValidity()` still validate when it is true.

### Non-goals

- No `pattern`, `min` / `max` / `step`, date/number/file constraint semantics,
  radio-group validation, `FormData`, submission, or native validation UI.
- No `ValidityState` global/constructor; `validity` is a fresh plain object per
  access, not a live object.
- No new Python API; `setCustomValidity` is a runtime slot and does not write an
  attribute. `form.reset()` does not clear it.

### Tests

`test/test_m11_32_form_validation.py` covers surface shape, required
(input / checkbox / select), barred controls, custom errors, length and
email/URL constraints, unsupported flags, read-only/fresh `validity`,
`invalid` events, form aggregation, `noValidate`, detached controls, and
`load()` reset.

Historical frozen-surface guards in `test_form_novalidate.py`,
`test_form_request_submit.py`, `test_m6_contract.py`, and `test_m7_contract.py`
are refreshed in this batch.

## Verification

Verified on real V8 at commit `a88b69d` (feature commit `faf5e28`):

- Linux `release-linux` run `40831677507` — cp311–cp314, each
  `1540 passed, 2 skipped`;
- Windows `release-windows` run `40831682182` — Python 3.11–3.14, each
  `1540 passed, 2 skipped`.

Both new test files ran in every lane (14 + 17 items); the full suite collects
1542 items. The first attempt (`faf5e28`) failed the readOnly test because the
test helper omitted the `r` id (not a runtime defect); `a88b69d` fixed it. See
`docs/m11_31_32_report.md`.
