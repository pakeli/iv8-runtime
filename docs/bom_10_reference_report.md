# BOM-10 reference preparation report

Date: `2026-11-27`

Status of this historical preparation stage: **Boundary draft and browser
baseline complete; no runtime implementation in that stage.** Subsequent digest
implementation and the additional edge receipts are recorded separately in
`docs/bom_10_digest_report.md`.
Base: `a476387cc83e802dfa2d2e90a6872ba4775ce7d2` on
`codex/bom-10-crypto-storage`. BOM-09 remains latest implemented; BOM-08 remains
latest cross-platform verified. The runtime inventory still has the same two
missing entry points.

## Artifacts

- Boundary/implementation sequence: `docs/bom_10_crypto_storage.md`.
- Exact scripts: `test/fixtures/bom_10/crypto_probe.js`, `storage_probe.js`.
- Six browser receipts: `{crypto,storage}-{secure,insecure,opaque}.json` in that
  directory, with version, timestamp, source hash and execution conditions.
- Reusable manual collector: `tools/capture_bom_10_reference.py` (POSIX Python,
  private Chrome DevTools pipe, temporary profiles, local HTTP server only).
- Offline checks: `test/test_bom_10_reference.py`.
- Ignored diagnostic logs and repeated receipts:
  `data/bom-10-reference-2026-11-27/` (not release artifacts).

## Findings that determine the boundary

Google Chrome `158.0.8127.88` exposes 17 SubtleCrypto operations, including five
beyond the earlier twelve-method surface. All four sampled SHA digest families
match independently computed `abc` and empty-input vectors; buffer offsets,
input-copy timing and Promise error behavior are captured. Fixed AES-GCM/HMAC
raw-key import results expose CryptoKey shape without generating random keys.

StorageManager is absent in the sampled insecure/opaque contexts. A secure OPFS
root resolves without a gesture or extra grant. Repeated retrieval returns new
wrappers pointing at the same entry. The probe exercises an isolated temporary
directory, empty File metadata, async entries, resolution, kind/name errors,
write/close/read and recursive cleanup. WritableStream inheritance confirms
that full writes require more than the getDirectory entry point.

The source audit found no existing crypto backend, native FileSystem handles or
Streams implementation. The next execution slice should therefore select the
crypto backend and implement digest alongside the native parent interfaces;
directory/handle behavior and stream-backed writes have separate acceptance
criteria. Full algorithm/storage support is not inferred from name coverage.

## Verification

- First capture: all **6** receipts completed without probe failure or timeout.
- Independent repeat with fresh profiles: **6/6 result payloads identical**.
  Capture timestamps/profile identifiers are metadata, not compared results.
- `python -m pytest test/test_bom_10_reference.py -q`: **19 passed**.
  Eight cases independently verify Chrome SHA output against Python hashlib.
- Ordinary tests consume JSON only; browser collection is a separate manual
  command. No production C++/Python runtime or wheel is changed in this stage.

At the end of this preparation stage, runtime comparisons, backend builds and
the 12-lane release matrix were pending. The subsequent digest report records
the newly implemented subset and fresh local results; cross-platform release
verification remains pending. This historical reference-only count must not
be substituted for a runtime verification result.
