# BOM-10 digest backend decision and build contract

Date: `2026-11-27`

## Selected source and interface

Use the unchanged portable SHA implementations from **TF-PSA-Crypto 1.1.2**,
the maintained Mbed TLS project's crypto library. The official release was
published on 2026-11-16. Its 1.1 LTS branch is maintained until March 2029;
the Mbed TLS 3.6 release notes recommend this branch for crypto-only users.
This avoids introducing a new dependency on the 3.6 branch, whose maintenance
ends in March 2027.

Primary sources:

- [Official TF-PSA-Crypto 1.1.2 release](https://github.com/Mbed-TLS/TF-PSA-Crypto/releases/tag/tf-psa-crypto-1.1.2)
- [Upstream maintained-branches policy at the selected tag](https://github.com/Mbed-TLS/TF-PSA-Crypto/blob/tf-psa-crypto-1.1.2/BRANCHES.md)
- [Mbed TLS 3.6.8 migration recommendation](https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.8)

`cmake/crypto_pin.cmake` is the single source of version, URL and archive hash:

```text
archive: tf-psa-crypto-1.1.2.tar.bz2
SHA-256: 799764c15cebd0d8cc161c71f9601009b2992f471828739fb4d72573b87f8298
URL: https://github.com/Mbed-TLS/TF-PSA-Crypto/releases/download/tf-psa-crypto-1.1.2/tf-psa-crypto-1.1.2.tar.bz2
```

The downloaded bytes match the official release asset's SHA-256. Build-time
verification applies even when the archive is already cached; mismatched bytes
fail configuration instead of silently selecting a system library or updating
the pin.

The extension's internal interface is:

```cpp
std::vector<std::uint8_t> iv8::browser::digest_bytes(
    std::string_view canonical_algorithm,
    const std::uint8_t* data,
    std::size_t length);
```

It accepts `SHA-1`, `SHA-256`, `SHA-384`, and `SHA-512`, returning exactly
20/32/48/64 bytes. The caller owns and snapshots the BufferSource before calling;
the adapter retains no pointers, calls neither V8 nor Python, and throws a
native runtime error on backend failure. Calls are independent and use upstream
per-call SHA contexts, with no global key state or initialization.

## Narrow static build

`cmake/crypto_digest.cmake` compiles exactly these unchanged upstream C files:

- `drivers/builtin/src/sha1.c`
- `drivers/builtin/src/sha256.c`
- `drivers/builtin/src/sha512.c`
- `platform/platform_util.c`

`cmake/crypto_digest_config.h` enables only the four SHA variants. There is no
PSA key store, PSA random initialization, TLS/X.509, entropy backend, hardware
dispatch, filesystem support, or dynamically linked libcrypto. The project
does not implement any SHA primitive itself.

The one-shot built-in SHA entry points are **private upstream interfaces** in
TF-PSA-Crypto 1.1.2. The small `digest_backend.cpp` adapter deliberately ties
their use to the exact source pin; it alone enables the upstream declaration
macro and includes private headers. No private structure layout is passed into
V8 or exposed to Python. A source upgrade must recompile this adapter and rerun
the vectors and release matrix; upstream's public-API compatibility promise
must not be assumed for these entry points.

The four C files form a PIC static library with hidden symbol visibility. The
adapter is compiled as part of `_core`, so its C++ standard library and allocator
match the extension. The boundary to upstream is C ABI, including on Windows,
where `_core` uses V8's libc++ and the C sources use the configured C compiler.
The adapter opts out of the Python/V8 precompiled header.

## Cache, offline builds and licenses

The default archive cache is
`data/crypto/tf-psa-crypto-1.1.2.tar.bz2`. A missing archive can be downloaded
over verified TLS during a V8-linked configure; extracted sources remain under
the ignored build directory's `_deps/` subtree. V8-free configuration never
includes this module and neither downloads nor links this dependency.

For a completely offline build, pre-provision the archive and use:

```sh
python -m pip wheel . --no-build-isolation --no-deps \
  -Ccmake.define.IV8_LINK_V8=ON \
  -Ccmake.define.IV8_CRYPTO_ALLOW_DOWNLOAD=OFF \
  -Ccmake.define.IV8_CRYPTO_ARCHIVE=/absolute/path/tf-psa-crypto-1.1.2.tar.bz2
```

All other build dependencies, including the pinned V8 SDK and Python build
requirements, must already be available for that offline command. The archive
path is configurable, but the expected version/hash are not inferred from it.
Ordinary pytest behavior does not download or access the network.

Upstream provides Apache-2.0 OR GPL-2.0-or-later; iv8 takes the code under
Apache-2.0. Every V8-linked wheel installs the complete upstream `LICENSE` and
the exact `crypto_pin.cmake` under `iv8/licenses/tf-psa-crypto-1.1.2/`.
No upstream sources or downloaded archives are committed to this repository.

## Validation evidence and limits

Local macOS arm64 / AppleClang 16, SDK 15.2, deployment target 11.0:

- Standalone C library plus C++ adapter built successfully with downloads disabled.
- **12** independently checked vectors pass: each SHA variant for empty input,
  `abc`, and one million ASCII `a` bytes, compared with Python hashlib.
- Missing offline archive and corrupted archive both fail CMake configuration
  with the intended diagnostic (**2 negative build checks**).
- Build sources, vector results and configure diagnostics are under the ignored
  `data/bom10-backend-check/` directory.

These checks verify the backend selection and local build, not WebIDL conversion
or Promise/monitoring behavior. The runtime layer has its own browser-based
tests. Linux/Windows execution and the Python 3.11–3.14 release matrix remain
pending until their actual build/test evidence is recorded.
