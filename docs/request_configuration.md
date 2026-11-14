# Request / Page configuration alignment

Authorized 2026-11-14: audit the caller's HTTP settings against the configuration
passed to iv8, and make their shared identity settings consistent. This is caller
configuration tooling, not a native runtime or browser-API milestone.

## Boundary

- Compare actual configured HTTP identity headers with the same environment used
  to construct Page: UA, language ordering, recognizable UA-CH platform/mobile
  and Chrome brand versions. Preserve unknown/unsupported evidence explicitly.
- Use one configuration to supply both environment and identity headers. Reject
  contradictory per-request overrides. Preserve non-identity application headers.
- For curl_cffi, resolve and verify an explicit supported profile in the actual
  interpreter before creating a session. Disable hidden default browser headers
  when using the explicit shared header configuration. A no-URL local Curl
  impersonate setup can test profile availability; it performs no HTTP/TLS
  handshake and cannot prove network fingerprint equivalence.
- Keep existing browser captures and historical manifests immutable. Preserve
  existing custom dimensions/CPU values. A new configured Chrome150 profile is
  distinct from the historical Chrome156 browser reference.
- No new live requests, Cookie synchronization, transport implementation,
  dependency installation, native bindings or default Page changes are included.
  Cookies/proxy/IP/timezone and unimplemented JS UAData remain separate evidence
  gaps, not guaranteed matches.
- User caller edits are staged, checked offline, applied with original-file hash
  guards and backed up under ignored data/. No user-owned file is committed.

## Deliverables and acceptance

Portable standard-library request configuration/audit helpers, local installed
curl profile inspection, focused offline tests and actual caller audit receipts.
Existing project Page callers are checked by reading their configuration AST,
not by importing scripts that may perform requests at module scope. Matching
identity values are also read back from a real Page locally. A manifest/report
must distinguish configured equality from unmeasured network/browser behavior.

## Shared configuration

`tools/iv8_diagnostics/request_config.py` is a portable standard-library helper.
It is copied to `iv8-test/browser_request_config.py` for the three existing Page
callers; the deployment receipt verifies identical hashes. It adds no package
exports or dependency requirements.

```python
configuration = build_request_config(environment, application_headers)
page = iv8.Page(environment=configuration["environment"])
session.headers.update(configuration["headers"])
headers = merge_request_headers(configuration, per_request_headers)
```

The environment is the authority for UA, primary language/language order,
recognizable UA-CH platform and mobile declarations. Builder output is a deep
copy. Application headers remain intact, including the `None` deletion semantics
used by requests. These four derived identity headers are removed from caller
literals. Existing captured brand lists remain explicit and are checked against
the UA major; no new GREASE list or high-entropy values are invented.

`audit_request_config(environment, headers, transport=None)` returns only check
counts, fixed finding codes/field names and unchecked categories. It never
returns UA/Cookie/Authorization values. Exact language ordering is this caller
configuration policy, not an assertion that every browser exposes identical
HTTP/JS language lists. An absent transport profile keeps the overall audit
`unknown` even when every supplied HTTP/JS identity check passes.

`merge_request_headers` rejects changing, deleting or adding a protected identity
header at request time. Referer, Accept, Content-Type and other application
headers can still vary by request. Changing an identity requires rebuilding the
shared configuration and the corresponding Page, not overriding a request.

## Installed curl profile

`inspect_curl_profile(curl_cffi, target)` reads installed enum/native registry
metadata and optionally calls `Curl.impersonate(target, default_headers=False)`
on a fresh handle without a URL or `perform()`. A moving alias is invalid.
Unsupported profiles remain unsupported; unknown OS metadata remains unknown.
The utility never loads remote/custom fingerprint data or creates a Session.

For curl callers, pass only `profile`, `browser_major`, `platform` and
`client_version` from a supported inspection result as `transport` to the
builder, along with the same explicit `impersonate` target. Construct Session
with the builder's headers, explicit profile and `default_headers=False`.
Disabling implicit browser headers prevents a library update from silently
adding a conflicting identity declaration. This option's behavior is documented
in the upstream [curl_cffi quick start](https://curl-cffi.readthedocs.io/en/stable/quick_start.html).

## Applied caller scope

- Customs, NMPA and Epub retain Chrome156 and their existing environment values;
  identity headers now derive from those values. Every request in these callers
  passes merged headers rather than relying on duplicated literals.
- The private project Jetstar driver uses a separate configured Chrome150 profile
  with explicit `chrome150`; both installed clients support its macOS profile.
  The unchanged worker receives that same environment. Historical Chrome156
  browser captures, old comparison identities and original collectors remain
  unchanged. Existing live permission gating remains; exhausted POST budget now
  also stops before Session creation, and old receipts cannot be overwritten.
- The community iv8 0.1.4 runner is separate historical code and is not migrated
  into this project's Page contract. Its current UA150 is not confused with the
  separate Chrome156 offline probe.

Implementation and local acceptance are complete. Receipts, actual counts and
remaining evidence limits are in `docs/request_configuration_report.md`.
