#include "iv8/query_parameters.h"

#include <cstddef>

namespace iv8 {

namespace {

int hex_value(char character) {
    if (character >= '0' && character <= '9') return character - '0';
    if (character >= 'a' && character <= 'f') return character - 'a' + 10;
    if (character >= 'A' && character <= 'F') return character - 'A' + 10;
    return -1;
}

std::string decode_component(const std::string& text) {
    std::string decoded;
    for (std::size_t index = 0; index < text.size(); index++) {
        const char character = text[index];
        if (character == '+') {
            decoded += ' ';
        } else if (character == '%' && index + 2 < text.size()) {
            const int high = hex_value(text[index + 1]);
            const int low = hex_value(text[index + 2]);
            if (high >= 0 && low >= 0) {
                decoded += static_cast<char>((high << 4) | low);
                index += 2;
            } else {
                decoded += character;
            }
        } else {
            decoded += character;
        }
    }
    return decoded;
}

std::string encode_component(const std::string& text) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    for (unsigned char character : text) {
        // Explicit ASCII rules preserve UTF-8 bytes across host locales.
        const bool unreserved =
            (character >= '0' && character <= '9') ||
            (character >= 'A' && character <= 'Z') ||
            (character >= 'a' && character <= 'z') || character == '*' ||
            character == '-' || character == '.' || character == '_';
        if (unreserved) {
            encoded += static_cast<char>(character);
        } else if (character == ' ') {
            encoded += '+';
        } else {
            encoded += '%';
            encoded += kHex[character >> 4];
            encoded += kHex[character & 0x0F];
        }
    }
    return encoded;
}

}  // namespace

QueryParameterList parse_query_parameters(const std::string& query) {
    QueryParameterList pairs;
    std::size_t start = 0;
    while (true) {
        const std::size_t ampersand = query.find('&', start);
        const std::string token =
            (ampersand == std::string::npos)
                ? query.substr(start)
                : query.substr(start, ampersand - start);
        if (!token.empty()) {
            const auto equals = token.find('=');
            if (equals == std::string::npos) {
                pairs.emplace_back(decode_component(token), std::string());
            } else {
                pairs.emplace_back(decode_component(token.substr(0, equals)),
                                   decode_component(token.substr(equals + 1)));
            }
        }
        if (ampersand == std::string::npos) break;
        start = ampersand + 1;
    }
    return pairs;
}

std::string serialize_query_parameters(const QueryParameterList& pairs) {
    std::string query;
    for (std::size_t index = 0; index < pairs.size(); index++) {
        if (index > 0) query += '&';
        query += encode_component(pairs[index].first);
        query += '=';
        query += encode_component(pairs[index].second);
    }
    return query;
}

}  // namespace iv8
