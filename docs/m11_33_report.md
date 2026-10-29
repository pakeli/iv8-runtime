# M11-33 Delivery Report — OS-backed Crypto random APIs

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-29`)**

Branch `m11-33-crypto-random`, verified commit `c9b2fb5`.

## Evidence

- Linux `release-linux` run `40834145493` — cp311 / cp312 / cp313 / cp314,
  each `1553 passed, 2 skipped`.
- Windows `release-windows` run `40834153439` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1553 passed, 2 skipped`.
- `test/test_m11_33_crypto_random.py` ran in every lane (13 items). Full suite
  collects 1555 items; 1553 pass and 2 are skipped.
- Both runs were green on the first attempt; no guard refresh was needed.

## Scope landed

- Read-only `crypto` global with `getRandomValues()` and `randomUUID()`.
- `getRandomValues` accepts all integer TypedArrays, fills the view's byte range
  (respecting subarray offsets), returns the same view, accepts zero-length
  views, enforces the 65536-byte quota with a JS `RangeError`, and rejects
  ArrayBuffer/DataView/float/missing/detached inputs with `TypeError`.
- `randomUUID` returns a lower-case RFC 4122 v4 UUID with version/variant bits.
- `watch_apis` records `crypto.getRandomValues` and `crypto.randomUUID` calls.
- Randomness comes from the platform CSPRNG (advapi32 `SystemFunction036` on
  Windows; `getrandom` with `/dev/urandom` fallback on Linux). This is the
  runtime's single deliberate non-deterministic surface; there is no seed.
- `crypto.subtle`, `CryptoKey`, AES/RSA/ECDH/ECDSA/HMAC/KDF/digest algorithms,
  deterministic seeding, and a Python Crypto API remain out of scope.

See `docs/m11_33_crypto_random.md`.

## Verification notes

- Local `zig c++ -fsyntax-only` passed for `page_state.cpp`; the new test file
  passed `py_compile`; 1555 tests collect locally.
- The V8 monolith cache was reused; both workflows completed from the warmed
  master cache.
- Windows links advapi32 through the existing V8 link configuration; Linux uses
  glibc `getrandom` with the urandom fallback.

## Outcome

M11-33 is verified and merged to fork `master`. `crypto.getRandomValues` /
`randomUUID` are now available; `SubtleCrypto` remains a separate milestone
requiring an algorithm/library decision.
