# M11-24..M11-28 Delivery Report — Host Environment / Console Batch

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-28`)**

Branch `m11-24-28-host-environment-batch`, final verified commit `a00e140`.

## Evidence

- Linux `release-linux` run `40711526339` — cp311 / cp312 / cp313 / cp314,
  each `1474 passed, 2 skipped`.
- Windows `release-windows` run `40711548915` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1474 passed, 2 skipped`.
- All five new test files ran in every lane (22 items). Full suite collects
  1476 items; 1474 pass and 2 are skipped.
- The first attempt (`f857d03`, Linux `40710708930` / Windows `40710735445`)
  failed one test that read `navigator.languages` twice without holding the
  fresh array; `a00e140` fixed the test. Both reruns above are green.

## Scope landed

- **M11-24** — navigator `languages` (fresh array), `onLine`, `cookieEnabled`,
  `hardwareConcurrency`, all environment-configurable.
- **M11-25** — screen `colorDepth` / `pixelDepth`.
- **M11-26** — window `outerWidth` / `outerHeight` / `screenX` / `screenY`
  (read-only globals).
- **M11-27** — navigator identity: `appName` / `appCodeName` / `product` /
  `vendor` / `vendorSub` / `productSub`.
- **M11-28** — console `debug` / `trace` / `table` / `dir` / `group` /
  `groupEnd` / `time` / `timeEnd` / `assert` / `count`, deterministically
  mapped to Python logging (no clock, stack, formatting, or nesting).

The flat environment grows from 11 to 27 keys; the M11-17 nested form and
`get_defaults()` carry the same keys. Existing keys/defaults are unchanged.
No new Python method or setter was added. See
`docs/m11_24_28_host_environment_batch.md` for the boundary.

## Verification notes

- A local `zig c++ -fsyntax-only` pass against the real V8 headers was clean
  before pushing; the modified Python files and tests passed `py_compile`.
- The V8 monolith cache was reused; both workflows completed from the warmed
  master cache.
- The only failure in the first run was the fresh-array test, not runtime
  behavior.

## Outcome

M11-24 through M11-28 are verified and merged to fork `master`. The next
compatibility slice (`M11-29`) should be chosen from
`docs/m11_compatibility_backlog.md` or another gap-inventory slice with an
explicit boundary.
