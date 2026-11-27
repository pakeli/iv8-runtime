#include "digest_backend.h"

#include <stdexcept>

#include "mbedtls/private/sha1.h"
#include "mbedtls/private/sha256.h"
#include "mbedtls/private/sha512.h"

namespace iv8::browser {

std::vector<std::uint8_t> digest_bytes(std::string_view canonical_algorithm,
    const std::uint8_t* data, std::size_t length) {
    // The public JS caller validates/snapshots BufferSource before entering this
    // adapter. Keep a valid empty-input address for upstream C implementations.
    const std::uint8_t empty = 0;
    if (length > 0 && data == nullptr) {
        throw std::runtime_error("digest backend received a null input buffer");
    }
    const auto* input = length == 0 ? &empty : data;
    std::vector<std::uint8_t> output;
    int status = 0;
    if (canonical_algorithm == "SHA-1") {
        output.resize(20);
        status = mbedtls_sha1(input, length, output.data());
    } else if (canonical_algorithm == "SHA-256") {
        output.resize(32);
        status = mbedtls_sha256(input, length, output.data(), 0);
    } else if (canonical_algorithm == "SHA-384") {
        output.resize(48);
        status = mbedtls_sha512(input, length, output.data(), 1);
    } else if (canonical_algorithm == "SHA-512") {
        output.resize(64);
        status = mbedtls_sha512(input, length, output.data(), 0);
    } else {
        throw std::runtime_error("digest backend received an unsupported algorithm");
    }
    if (status != 0) {
        throw std::runtime_error("native SHA digest computation failed");
    }
    return output;
}

}  // namespace iv8::browser
