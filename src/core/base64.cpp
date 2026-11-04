#include "iv8/base64.h"

#include <cstddef>
#include <cstdint>

namespace iv8 {

namespace {

int base64_value(char character) {
    if (character >= 'A' && character <= 'Z') return character - 'A';
    if (character >= 'a' && character <= 'z') return character - 'a' + 26;
    if (character >= '0' && character <= '9') return character - '0' + 52;
    if (character == '+') return 62;
    if (character == '/') return 63;
    return -1;
}

}  // namespace

std::string base64_encode(const std::string& bytes) {
    static const char kAlphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string encoded;
    std::size_t offset = 0;
    const std::size_t length = bytes.size();
    while (offset + 3 <= length) {
        const std::uint32_t chunk =
            (static_cast<std::uint8_t>(bytes[offset]) << 16) |
            (static_cast<std::uint8_t>(bytes[offset + 1]) << 8) |
            static_cast<std::uint8_t>(bytes[offset + 2]);
        encoded += kAlphabet[(chunk >> 18) & 63];
        encoded += kAlphabet[(chunk >> 12) & 63];
        encoded += kAlphabet[(chunk >> 6) & 63];
        encoded += kAlphabet[chunk & 63];
        offset += 3;
    }
    const std::size_t remaining = length - offset;
    if (remaining == 1) {
        const std::uint32_t chunk =
            static_cast<std::uint8_t>(bytes[offset]) << 16;
        encoded += kAlphabet[(chunk >> 18) & 63];
        encoded += kAlphabet[(chunk >> 12) & 63];
        encoded += "==";
    } else if (remaining == 2) {
        const std::uint32_t chunk =
            (static_cast<std::uint8_t>(bytes[offset]) << 16) |
            (static_cast<std::uint8_t>(bytes[offset + 1]) << 8);
        encoded += kAlphabet[(chunk >> 18) & 63];
        encoded += kAlphabet[(chunk >> 12) & 63];
        encoded += kAlphabet[(chunk >> 6) & 63];
        encoded += "=";
    }
    return encoded;
}

// Preserve the existing forgiving decoder: strip its six whitespace bytes,
// remove up to two trailing padding bytes only at lengths divisible by four,
// reject a remainder of one or any remaining non-alphabet byte, discard bits.
bool base64_decode(const std::string& encoded, std::string& decoded) {
    std::string normalized;
    for (char character : encoded) {
        if (character == ' ' || character == '\t' || character == '\n' ||
            character == '\r' || character == '\f' || character == '\v') {
            continue;
        }
        normalized += character;
    }
    if (normalized.size() % 4 == 0) {
        std::size_t trimmed_length = normalized.size();
        int padding = 0;
        while (trimmed_length > 0 && normalized[trimmed_length - 1] == '=' &&
               padding < 2) {
            padding++;
            trimmed_length--;
        }
        normalized.resize(trimmed_length);
    }
    if (normalized.size() % 4 == 1) {
        return false;
    }
    std::uint32_t buffer = 0;
    int bits = 0;
    for (char character : normalized) {
        const int value = base64_value(character);
        if (value < 0) {
            return false;
        }
        buffer = (buffer << 6) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            decoded += static_cast<char>((buffer >> bits) & 0xFF);
        }
        buffer &= (bits > 0) ? ((1u << bits) - 1) : 0u;
    }
    return true;
}

}  // namespace iv8
