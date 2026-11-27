#ifndef IV8_CRYPTO_DIGEST_CONFIG_H
#define IV8_CRYPTO_DIGEST_CONFIG_H

// Compile only the upstream portable SHA implementations. No PSA key store,
// entropy, random generation, TLS, hardware acceleration, or filesystem code.
// These built-in configuration symbols are tied to crypto_pin.cmake's source.
#define TF_PSA_CRYPTO_CONFIG_VERSION 0x01010000
#define MBEDTLS_SHA1_C
#define MBEDTLS_SHA256_C
#define MBEDTLS_SHA384_C
#define MBEDTLS_SHA512_C

#endif
