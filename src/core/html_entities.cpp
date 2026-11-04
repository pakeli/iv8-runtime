#include "iv8/html_entities.h"

#include <cstddef>
#include <cstdint>

namespace iv8 {

namespace {

// M11-49: bounded HTML character-reference decoding. Supports the common named
// references plus decimal/hex numeric references. Unknown names and unterminated
// references stay literal; invalid numeric references become U+FFFD.
void append_utf8_code_point(std::string& output, std::uint32_t code_point) {
    if (code_point <= 0x7F) {
        output.push_back(static_cast<char>(code_point));
    } else if (code_point <= 0x7FF) {
        output.push_back(static_cast<char>(0xC0 | (code_point >> 6)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else if (code_point <= 0xFFFF) {
        output.push_back(static_cast<char>(0xE0 | (code_point >> 12)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    } else {
        output.push_back(static_cast<char>(0xF0 | (code_point >> 18)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 12) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | ((code_point >> 6) & 0x3F)));
        output.push_back(static_cast<char>(0x80 | (code_point & 0x3F)));
    }
}

bool named_entity_code_point(const std::string& name, std::uint32_t* output) {
    if (name == "amp") {
        *output = 0x26;
    } else if (name == "lt") {
        *output = 0x3C;
    } else if (name == "gt") {
        *output = 0x3E;
    } else if (name == "quot") {
        *output = 0x22;
    } else if (name == "apos") {
        *output = 0x27;
    } else if (name == "nbsp") {
        *output = 0xA0;
    } else {
        return false;
    }
    return true;
}

}  // namespace

std::string decode_html_entities(const std::string& text) {
    std::string output;
    output.reserve(text.size());
    std::size_t i = 0;
    while (i < text.size()) {
        if (text[i] != '&') {
            output.push_back(text[i++]);
            continue;
        }
        const std::size_t semi = text.find(';', i + 1);
        if (semi == std::string::npos || semi - i > 32) {
            output.push_back('&');
            ++i;
            continue;
        }
        const std::string body = text.substr(i + 1, semi - i - 1);
        std::uint32_t code_point = 0;
        bool decoded = false;
        if (!body.empty() && body[0] == '#') {
            const bool hex = body.size() > 1 &&
                             (body[1] == 'x' || body[1] == 'X');
            const std::size_t start = hex ? 2 : 1;
            if (start < body.size()) {
                bool valid = true;
                bool overflow = false;
                std::size_t digits = 0;
                std::uint64_t value = 0;
                for (std::size_t k = start; k < body.size(); ++k) {
                    const unsigned char character = static_cast<unsigned char>(body[k]);
                    int digit = -1;
                    if (character >= '0' && character <= '9') {
                        digit = character - '0';
                    } else if (hex && character >= 'a' && character <= 'f') {
                        digit = character - 'a' + 10;
                    } else if (hex && character >= 'A' && character <= 'F') {
                        digit = character - 'A' + 10;
                    }
                    if (digit < 0) {
                        valid = false;
                        break;
                    }
                    value = value * (hex ? 16 : 10) +
                            static_cast<std::uint64_t>(digit);
                    ++digits;
                    if (value > 0x10FFFF) {
                        overflow = true;
                        break;
                    }
                }
                if (valid && digits > 0 && !overflow) {
                    code_point = static_cast<std::uint32_t>(value);
                    decoded = true;
                } else if (overflow || (valid && digits > 0 && value == 0)) {
                    code_point = 0xFFFD;
                    decoded = true;
                }
            }
        } else if (named_entity_code_point(body, &code_point)) {
            decoded = true;
        }
        if (!decoded) {
            output.push_back('&');
            ++i;
            continue;
        }
        if (code_point == 0 || code_point > 0x10FFFF ||
            (code_point >= 0xD800 && code_point <= 0xDFFF)) {
            code_point = 0xFFFD;
        }
        append_utf8_code_point(output, code_point);
        i = semi + 1;
    }
    return output;
}

// M11-49: HTML fragment serialization. Text escapes &, <, > and U+00A0;
// attribute values escape &, " and U+00A0; script/style text is emitted raw.
void append_escaped_html_text(std::string& output, const std::string& text) {
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char character = static_cast<unsigned char>(text[i]);
        if (character == '&') {
            output += "&amp;";
            ++i;
        } else if (character == '<') {
            output += "&lt;";
            ++i;
        } else if (character == '>') {
            output += "&gt;";
            ++i;
        } else if (character == 0xC2 && i + 1 < text.size() &&
                   static_cast<unsigned char>(text[i + 1]) == 0xA0) {
            output += "&nbsp;";
            i += 2;
        } else {
            output.push_back(text[i++]);
        }
    }
}

void append_escaped_html_attribute(std::string& output,
                                   const std::string& text) {
    for (std::size_t i = 0; i < text.size();) {
        const unsigned char character = static_cast<unsigned char>(text[i]);
        if (character == '&') {
            output += "&amp;";
            ++i;
        } else if (character == '"') {
            output += "&quot;";
            ++i;
        } else if (character == 0xC2 && i + 1 < text.size() &&
                   static_cast<unsigned char>(text[i + 1]) == 0xA0) {
            output += "&nbsp;";
            i += 2;
        } else {
            output.push_back(text[i++]);
        }
    }
}

}  // namespace iv8
