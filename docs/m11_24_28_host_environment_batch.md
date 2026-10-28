# M11-24..M11-28 — Host Environment / Fingerprint Batch

Five bounded slices developed on one branch and verified together against the
warmed master V8 cache. M11-24..M11-27 extend the M11-1/M11-17 environment
baseline; M11-28 extends the JS-only console. No new architecture, networking,
clock, or automatic event loop.

## M11-24 — Navigator metadata

New read-only `navigator` values and environment keys:

| Property | Flat key | Default | Type |
|---|---|---|---|
| `navigator.languages` | `languages` | `["en-US", "en"]` | non-empty `list[str]` |
| `navigator.onLine` | `onLine` | `True` | `bool` |
| `navigator.cookieEnabled` | `cookieEnabled` | `True` | `bool` |
| `navigator.hardwareConcurrency` | `hardwareConcurrency` | `8` | `int` |

`navigator.languages` returns a fresh JS array per access; mutating it does not
change the configured value. The original `language` / `webdriver` /
`userAgent` / `platform` values are unchanged.

## M11-25 — Screen metadata

| Property | Flat key | Default | Type |
|---|---|---|---|
| `screen.colorDepth` | `screenColorDepth` | `24` | `int` |
| `screen.pixelDepth` | `screenPixelDepth` | `24` | `int` |

Both are independent configured values, not derived from width/height or a
display.

## M11-26 — Window metadata

| Property | Flat key | Default | Type |
|---|---|---|---|
| `outerWidth` | `outerWidth` | `1280` | `int` |
| `outerHeight` | `outerHeight` | `720` | `int` |
| `screenX` | `screenX` | `0` | `int` |
| `screenY` | `screenY` | `0` | `int` |

These are read-only properties on the intrinsic global (`window`). No
`window.name` in this batch: it is writable browser state rather than a fixed
environment baseline and needs a separate lifecycle decision.

## M11-27 — Navigator identity fields

New read-only `navigator` strings, all configurable through the same flat or
nested environment:

| Property | Default |
|---|---|
| `appName` | `"Netscape"` |
| `appCodeName` | `"Mozilla"` |
| `product` | `"Gecko"` |
| `vendor` | `""` |
| `vendorSub` | `""` |
| `productSub` | `"20100101"` |

No host OS / locale derivation and no UA-CH / `userAgentData` surface.

## M11-28 — Console methods

Adds deterministic, JS-only console methods:

- `debug` → DEBUG;
- `trace` → DEBUG (the joined arguments; no stack capture);
- `table` / `dir` → INFO (plain joined arguments; no table/dir formatting);
- `group` → INFO with the joined label; `groupEnd` → no output (no nesting);
- `time` / `timeEnd` → INFO with the label; there is no clock, so no elapsed
  time is reported;
- `assert(condition, ...)` → ERROR with `Assertion failed[: ...]` only when the
  condition is falsey;
- `count(label?)` → INFO with a per-generation counter; counts reset on
  `Page.load()`.

Existing `log` / `info` / `warn` / `error` behavior is unchanged.

## Environment shape

The flat environment grows from 11 to 27 keys. The nested form gains the same
keys under `navigator` / `screen` / `window`; `Page.get_default_environment()`
and `Page.get_defaults()` stay the flat and nested templates respectively.
Existing keys and defaults are unchanged. Validation stays strict and
Python-side: new strings are `str`, new booleans are `bool`, new dimensions are
`int` (not `bool`), and `languages` is a non-empty list of non-empty strings.

## Non-goals

- No `window.name`, `screen.orientation`, `outerWidth`/`outerHeight` layout
  computation, `matchMedia`, or viewport resize behavior.
- No `navigator.userAgentData`, `connection`, `maxTouchPoints`, permissions,
  clipboard, geolocation, or other device APIs.
- No console formatting library, stack traces, timers, or nesting state.
- No Python setter/reconfigure API and no host-OS/display/locale derivation.

## Tests

New focused files per slice; the existing
`test/test_host_environment.py` and M11-17 nested-environment assertions are
extended for the new key set.

## Verification

Verified on real V8 at commit `a00e140`:

- Linux `release-linux` run `40711526339` — cp311–cp314, each
  `1474 passed, 2 skipped`;
- Windows `release-windows` run `40711548915` — Python 3.11–3.14, each
  `1474 passed, 2 skipped`.

All five new test files ran in every lane (22 items); the full suite collects
1476 items. The first attempt (`f857d03`) failed one fresh-array test, fixed in
`a00e140`. See `docs/m11_24_28_report.md`.
