# BOM-10 digest implementation and local verification report

Date: `2026-11-27`

This is the historical digest-only candidate report. The subsequent directory
implementation is documented in `bom_10_storage_report.md`.

Status at this stage: **Digest slice implemented and locally verified. BOM-10 as a whole and
the cross-platform release matrix remain incomplete.** Branch:
`codex/bom-10-crypto-storage`; base commit
`a476387cc83e802dfa2d2e90a6872ba4775ce7d2`. The verified working-tree runtime,
test, CMake, tool and workflow hashes are recorded in
`data/bom-10-digest-2026-11-27/verification-manifest.json`. No commit/tree ID is
invented for these uncommitted changes. BOM-08 remains latest cross-platform
release-verified; shared SDK remains BIND-01.

## Delivered behavior

- Native Crypto, secure-only SubtleCrypto and CryptoKey interface shape,
  browser-derived names/arities/prototypes/descriptors/native source and
  constructor/receiver behavior. Global crypto is a configurable readonly
  native accessor; methods reside on writable/configurable prototypes.
- Real digest SHA-1/256/384/512 using TF-PSA-Crypto **1.1.2**, source/hash pinned
  by `cmake/crypto_pin.cmake`. Only four unchanged upstream portable SHA C source
  files are statically compiled. No system libcrypto, Python callback, custom
  SHA primitive, TLS stack or key store. See `bom_10_digest_backend.md`.
- Browser-derived argument conversion, BufferSource view offset, copy timing,
  detached-buffer empty input, resizable/shared rejection, Promise failures,
  preserved user error identity and explicit Page microtask pumping.
- All 17 SubtleCrypto native method shapes. **Only digest executes crypto**;
  the other 16 validate receiver/minimum arity and reject a clear digest-profile
  NotSupportedError. Their complete parameter conversion and key operations
  are not implemented. CryptoKey metadata exposure does not provide key objects.
- Native static `SubtleCrypto.supports`: reports the four implemented digest
  algorithms and returns false for unsupported operation families. Argument
  coercion, dictionary failure and invalid-operation read ordering are covered.
- Existing random APIs keep OS entropy; secure gating, invalid receiver,
  TypeMismatchError, QuotaExceededError and detached-view behavior now follow
  the added Chrome references. Crypto access-trace identity, watch events,
  prototype references and instance GET/CALL folding have replacement tests.

The secure `crypto` inventory has **3/3 names**. This is not full WebCrypto
execution coverage. `navigator.storage.getDirectory` was absent in this candidate; the
memory filesystem and writable streams have not been implemented.

## Reference and regression evidence

The original six Chrome receipts remain unchanged. Three additional
`digest_edges` receipts (Google Chrome **158.0.8127.88**) cover random errors,
static supports, recognized non-digest algorithms and conversion order. Fresh
profile repeat results match for all three additional receipts. Receipt hashes
bind each JSON to its exact JS source. Normal pytest uses only local artifacts.

Fresh V8-linked wheel on macOS arm64 / Python 3.13:

- Focused crypto, reference, legacy shape and trace suite: **144 passed**.
- Full `test/` suite: **3577 passed, 2 skipped**, no exclusions.
- **48** digest cases compare block-boundary and 1MiB inputs against hashlib;
  additional reference, mutation, scope, exception, monitoring and lifecycle
  cases cover the browser API boundary.
- Native backend independent checks: **12** known vectors, **2** negative
  archive-configuration checks (missing offline archive and wrong hash).
- V8-free smoke wheel builds and imports with downloads disabled and a missing
  archive; `_v8_linked` is false, no SHA target/dependency/license is included,
  and JSContext retains its unavailable-runtime guard. Logs:
  `data/bom10-backend-check/skeleton-{build,install}.log` and
  `skeleton-verification.json`.

V8-linked wheel:
`data/bom-10-digest-2026-11-27/dist/iv8-0.0.1-cp313-cp313-macosx_11_0_arm64.whl`.
SHA-256: `5e2ef9904c87d8330c8c5e1ac621b5ba79c5605bdf37614b69ea5b8ec434a330`.
Native extension SHA-256:
`884120eeb089a62c92254041209895f00845e9ebfeebb4d27b5a8ee580228a91`.
The archive pin and upstream LICENSE are present in the wheel. `otool -L`
shows system Foundation/CoreFoundation/libc++/libSystem only, with no dynamic
crypto library. Build, focused and full test logs are alongside the manifest.

The macOS/Windows release workflows and Linux wheel script include both new
BOM-10 test modules in their **45-module** focused gate; the three module sets
are identical. This wiring is not evidence that
those remote release lanes have executed.

## Limits and next slice

- Digest computation is synchronous native work with the Python GIL released;
  Promise reactions are delivered by the existing manual Page job pump. Chrome
  background-worker scheduling and pending-operation timing are not claimed.
- The pre-existing V8 SharedArrayBuffer global remains available even in a Page
  profile where non-isolated Chrome hides it. Tests check shared-input rejection
  separately; global SharedArrayBuffer exposure was not changed in this slice.
- Raw AES/HMAC import success cases in the discovery probe are explicitly
  excluded from digest conformance; unsupported key execution is separately
  tested to reject rather than return fake success.
- The upstream built-in SHA API is private and tied to the exact source pin;
  upgrades require adapter recompilation, vectors and release-matrix validation.
- Linux/Windows execution and remaining local Python versions are not verified
  in this report. Run the complete selected 12-lane matrix before publishing.
- The next slice selected at this stage was native StorageManager/getDirectory and the bounded
  origin-partitioned memory filesystem described in `bom_10_crypto_storage.md`;
  key algorithms and writable streams retain their separate scope.
