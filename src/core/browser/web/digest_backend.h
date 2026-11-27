#pragma once

#include <cstddef>
#include <cstdint>
#include <string_view>
#include <vector>

namespace iv8::browser {

// Hashes an already-snapshotted input with a browser-normalized SHA name.
// No Python/V8 access, global initialization, retained buffers, or key state.
// Throws std::runtime_error on an unsupported algorithm or backend failure.
std::vector<std::uint8_t> digest_bytes(std::string_view canonical_algorithm,
    const std::uint8_t* data, std::size_t length);

}  // namespace iv8::browser
