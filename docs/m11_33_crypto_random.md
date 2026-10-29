# M11-33 — OS-backed `crypto.getRandomValues` / `crypto.randomUUID`

Status: **VERIFIED — real V8 green on Linux + Windows (`2026-10-29`)**

M11-33 adds the smallest high-value Crypto surface: a `crypto` global with
`getRandomValues()` and `randomUUID()`. It deliberately does **not** add
`crypto.subtle`; that remains a separate, larger milestone.

## Public API

```js
crypto.getRandomValues(typedArray)  // fills and returns the same TypedArray
crypto.randomUUID()                 // lower-case UUID v4 string
```

The `crypto` global and both methods are read-only. `crypto` is installed on
every generation (`window.crypto === crypto`). There is no Python-side Crypto
API.

### `getRandomValues`

- Accepts integer TypedArrays only: `Int8Array` / `Uint8Array` /
  `Uint8ClampedArray` / `Int16Array` / `Uint16Array` / `Int32Array` /
  `Uint32Array` / `BigInt64Array` / `BigUint64Array`.
- Fills the view's byte range (`ByteOffset` … `ByteOffset + ByteLength`), not
  the whole backing buffer; bytes outside a subarray view are untouched.
- Returns the **same** TypedArray object.
- A zero-length view is returned unchanged.
- The per-call quota is **65536 bytes**. Larger requests throw a JS
  `RangeError` (the project has no `DOMException`, so this is the deterministic
  substitute for `QuotaExceededError`).
- `ArrayBuffer`, `DataView`, float TypedArrays, missing arguments, and detached
  buffers throw a JS `TypeError`.

### `randomUUID`

- Returns a lower-case RFC 4122 version-4 UUID
  (`xxxxxxxx-xxxx-4xxx-[89ab]xxx-xxxxxxxxxxxx`).
- Version/variant bits are set explicitly; all other bits come from the same OS
  entropy source.

## Randomness semantics

This is the runtime's **one deliberate non-deterministic surface**.

- Windows builds use advapi32 `SystemFunction036` (RtlGenRandom) from the OS.
- Linux builds use `getrandom(2)` with a `/dev/urandom` fallback.
- Other platforms fall back to `std::random_device` (not a supported release
  target).
- There is no seed, no deterministic mode, and no replay guarantee. Tests
  validate types, lengths, UUID format/version/variant, and non-zero output, not
  exact bytes.
- The clock remains virtual (M11-31); Crypto randomness is independent of it.

## Non-goals

- `crypto.subtle` / `SubtleCrypto`, `CryptoKey`, AES-GCM/AES-CBC, RSA-OAEP /
  RSA-PSS, ECDH/ECDSA, HMAC, HKDF, PBKDF2, digest algorithms;
- `crypto.getRandomValues` on an `ArrayBuffer` or `DataView`;
- deterministic seeding / reproducible random sequences;
- secure-context / permissions gating, `Crypto.randomUUID` polyfills, or
  `msCrypto`;
- a Python Crypto API or JS→Python callback bridge.

The target community edition claims `SubtleCrypto`, but documents that some of
its interfaces may be incomplete stubs. This repository intentionally ships only
real, tested Crypto behavior and defers SubtleCrypto until an algorithm/library
decision is made.

## Tests

`test/test_m11_33_crypto_random.py` covers the surface and read-only guards,
same-view return values, all accepted integer TypedArray kinds, subarray byte
ranges, zero-length views, the 65536-byte quota, rejected argument types,
non-identical fills, UUID v4 format/uniqueness, `watch_apis` recording, and
presence after `load()`.

## Verification

Verified on real V8 at commit `c9b2fb5`:

- Linux `release-linux` run `40834145493` — cp311–cp314, each
  `1553 passed, 2 skipped`;
- Windows `release-windows` run `40834153439` — Python 3.11–3.14, each
  `1553 passed, 2 skipped`.

`test/test_m11_33_crypto_random.py` ran in every lane (13 items); the full suite
collects 1555 items. See `docs/m11_33_report.md`.
