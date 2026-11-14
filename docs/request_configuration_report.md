# Request / Page configuration — audit and alignment report

Date: 2026-11-14. This records the caller configuration slice's local acceptance.
Its tests are subsequently included in the 2026-11-15 NAMED-01 release
(`named_access_restore_report.md`). Boundary: `request_configuration.md`.

## Findings and changes

| Caller | Before | Applied configuration |
|---|---|---|
| Customs Page | macOS UA/MacIntel, HTTP UA-CH platform Windows; HTTP languages omit configured English | Platform and full ordered languages derive from the existing Page environment |
| NMPA Page | HTTP/JS identity fields agree, UA duplicated between function/local headers | Environment moved to one top-level source; shared headers and per-request guards |
| Epub Page | HTTP languages omit configured English; duplicated identity headers | Headers derive from existing Page environment; fetch merges protected headers |
| Private project Jetstar driver | Cached Chrome156 Page profile, floating curl alias and library defaults | Separate configured Chrome150/macOS profile, explicit chrome150, defaults disabled, same environment sent to worker |

The three requests-based Page callers keep Chrome156 and custom dimensions/CPU
settings. Only configuration ownership/header consistency changes; application
URLs, request bodies, signing, Cookie handling and business logic remain intact.
Non-identity per-request `None` headers still remove Session defaults.

Portable helper source is `tools/iv8_diagnostics/request_config.py`. Its external
copy `iv8-test/browser_request_config.py` is byte-identical. Shared identity is
checked again after request-header merging, blocking case-insensitive override,
removal or addition of protected declarations. Browser-native method behavior,
Page defaults and the installed SDK are unchanged.

## Actual local client inspection

| Interpreter | curl_cffi | chrome alias | Explicit chrome150 |
|---|---|---|---|
| miniconda base, Python 3.12 | 0.16.3 | chrome150 | supported; native registry macOS/Tahoe; setup 0 |
| iv8-test, Python 3.11 | 0.17.0 | chrome152 | supported; native registry macOS/Tahoe; setup 0 |

The newer alias's profile is Windows, so the alias cannot be treated as a stable
macOS configuration. `chrome156` is not an installed native profile in either
inspected version. Checks use installed declarations and no-URL local handles;
no Session/perform or TLS handshake is used to inspect support. See the installed
`curl_cffi/requests/impersonate.py` and `curl_cffi/fingerprints.py` in each
interpreter. Canonical profile setup is independently fake-tested.

The community `akamai/main.py` currently declares UA150 consistently with its
HTTP header; it is not the historical project Page156 driver. Its separate
Chrome156/zh-CN offline probe has no HTTP client. These sources remain unchanged.

## Offline acceptance

- Focused new tests: **47 request configuration + 19 curl profile = 66 passed**.
- Complete local real-V8 suite: **2314 passed, 2 skipped, 1 xfailed** (2,317
  collected), CPython 3.13/macOS arm64. The real-V8 wheel is the unchanged native
  build from the preceding offline scenario task.
- All three deployed caller configurations were extracted from a whitelist of
  AST assignments; the scripts themselves were not imported or executed.
- Actual Page readback of UA/platform/language/languages has no configured
  conflicts. requests' real `prepare_request` path was exercised without send;
  merged headers have no conflicts and unrelated `None` deletion still works.
  Synthetic UA override attempts are rejected.
- The single new Jetstar configuration passes audit under both installed curl
  versions; project Page readback also passes under iv8-test. No community
  runtime result is substituted for project SDK acceptance.
- The unchanged cached original collector still loads and fails with the known
  asynchronous `NotSupportedError` at line 1/column 51597 during the bounded
  advance phase. Its new environment report passes; regression passes because
  that failure is explicitly expected, execution remains failed and server is
  `not_tested`. Correct configuration alone does not establish server success.

## Deployment and receipts

User-owned caller files were staged and offline-checked first, then applied with
original SHA-256 guards, backups and deployed-hash verification. Backups and
receipts remain under ignored `data/request-configuration-2026-11-14/`:

- `deployment-plan.json`, `deployment-receipt.json`, `caller-configuration.patch`
- `staged-audit.json`, `curl-0.16.3-audit.json`, `curl-0.17.0-audit.json`
- `jetstar-driver-receipt.json`, `offline/summary.json`
- `budget-guards.json`, `full-pytest.log`, `verification.json`

No user-owned files or local environments are added to Git. The private driver
retains its explicit live gate and cumulative budget, refuses existing output
directories/receipt overwrite, requires live-history output names, and stops
before creating a Session when the existing two-POST budget is exhausted.

## Evidence limits

This task sent **zero new live requests** and did not install or upgrade either
client or SDK. Audits verify configured identity, native profile availability
and Page readback, not on-wire TLS equivalence. Ordinary requests clients have
no browser transport profile to verify, so their overall status stays `unknown`
with all supplied identity checks passing. Missing Page UAData/high-entropy
fields, cookies, proxy/IP, time and cross-context behavior remain unchecked.
No server-pass claim or attribution of prior `_abck=-1` to this configuration
is made. Cross-platform coverage of these tooling tests is now recorded by the
subsequent NAMED-01 release report; the counts above remain this slice's original
local checkpoint.
