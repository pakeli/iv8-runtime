# BOM-10 digest implementation slice

Date: `2026-11-27`

Status: **Implemented and locally verified on macOS arm64 / Python 3.13;
included in the 12-lane release-verified d475309 candidate.** The user requested the
next execution step after reference preparation: select a native backend and
implement digest. Storage/getDirectory and key/stream operations remain separate.

## Contract selected before implementation

- Install native Crypto, secure-only SubtleCrypto and CryptoKey interfaces with
  Chrome 158 shapes, constructor rejection, descriptors and receiver rules.
  CryptoKey exposes its interface metadata but no key instances are produced yet.
- Secure `crypto.subtle` is stable per generation; only `getRandomValues` remains
  on Crypto in insecure/opaque contexts. `randomUUID` is secure-only. Global and
  prototype descriptors follow the three captured reference contexts.
- Implement digest SHA-1, SHA-256, SHA-384 and SHA-512 through a maintained,
  pinned native C backend: TF-PSA-Crypto 1.1.2, four unchanged portable SHA C
  sources statically linked. Source hash, license and offline build instructions
  are in `docs/bom_10_digest_backend.md`. No custom cryptographic primitives or
  Python callback.
- Copy exactly the supplied BufferSource view at the browser-observed point in
  argument conversion. Match detached-buffer, case-insensitive algorithm,
  Promise rejection and user exception identity behavior from the reference.
  Promise reactions continue to require the existing explicit Page job pump.
- Hash calculation is synchronous inside the native call with the Python GIL
  released; the returned Promise settles before a reaction is run by the Page
  pump. This bounded offline execution model does not claim Chrome's worker
  scheduling, pending-operation timing, or parallel background crypto work.
- Install the native static `SubtleCrypto.supports(operation, algorithm)` with
  Chrome's shape and conversion/error rules, reporting true only for the four
  implemented digest algorithms. Unsupported key operations report false even
  when the reference browser supports them; this is deliberate capability
  reporting for the digest-only profile.
- Provide all 17 reference SubtleCrypto native methods and minimum arities.
  Non-digest methods validate receiver and missing argument count, then reject
  NotSupportedError identifying this digest-only profile. Their full WebIDL
  conversion and cryptographic execution remain explicitly unsupported. Never
  resolve fake key material, signatures or ciphertext. The corresponding 16
  methods are tracked as shape present / execution missing, not complete APIs.
- Migrate the old flat Crypto object contract to browser-native prototypes.
  `getRandomValues` and `randomUUID` retain OS entropy; browser receiver errors,
  invalid-view exceptions and quota DOMException replace historical substitutes
  where the new `digest_edges` fixtures supply direct evidence.
- Preserve crypto tracing classification, stable identity, watch paths and
  completed call/error summaries. Prototype reads and instance method reads use
  the established native HostObject monitoring convention; migrate affected
  flat-object tests with replacement assertions.

## Acceptance

Use the existing crypto receipts for interfaces, constructors, receivers,
missing arguments and digest results; raw key import success cases remain
reference-only and must be explicitly excluded from digest conformance claims.
Additional `digest_edges` receipts cover random errors and conversion ordering.
Verify fixed and large byte vectors independently with hashlib, subviews, copy
timing, mutation, Promise timing, retained functions, reload/disposal, separate
Pages, tracing toggles and unsupported-operation rejection. Run focused tests,
then the full suite on a freshly built wheel. Keep platform/source/build evidence
and cross-platform release-pending status explicit in the handoff set.

Local verification: **144 focused passed**; full **3577 passed, 2 skipped**.
Evidence and implementation limits: `docs/bom_10_digest_report.md`.

Combined release verification completed on `2026-11-28`: see
`docs/bom_10_release_report.md` for fixed source/tree, 12-lane results, artifacts
and the separate ordinary hosted macOS CI limitation.
