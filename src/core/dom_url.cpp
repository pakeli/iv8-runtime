#include "iv8/dom_url.h"

#include <algorithm>
#include <cctype>
#include <string>
#include <vector>

#include "iv8/url_parser.h"

namespace iv8 {
namespace {

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(),
        [](unsigned char byte) { return static_cast<char>(std::tolower(byte)); });
    return value;
}

std::size_t scheme_end(const std::string& text) {
    if (text.empty() || !std::isalpha(static_cast<unsigned char>(text[0])))
        return std::string::npos;
    for (std::size_t index = 1; index < text.size(); ++index) {
        const unsigned char byte = text[index];
        if (byte == ':') return index;
        if (!(std::isalnum(byte) || byte == '+' || byte == '-' || byte == '.'))
            break;
    }
    return std::string::npos;
}

bool special(const std::string& protocol) {
    return protocol == "http:" || protocol == "https:" || protocol == "ftp:" ||
           protocol == "ws:" || protocol == "wss:" || protocol == "file:";
}

std::string encoded(std::string value) {
    std::string result;
    constexpr char digits[] = "0123456789ABCDEF";
    for (unsigned char byte : value) {
        if (byte <= 0x20 || byte >= 0x7f || byte == '"' || byte == '<' || byte == '>') {
            result += '%';
            result += digits[byte >> 4];
            result += digits[byte & 15];
        } else result += static_cast<char>(byte);
    }
    return result;
}

std::string encode_delimiter(std::string value, char delimiter,
                              const std::string& replacement) {
    std::string result;
    for (char character : value) result += character == delimiter ? replacement : std::string(1, character);
    return result;
}

std::string query_encoded(const std::string& value) {
    return encode_delimiter(encode_delimiter(encoded(value), '#', "%23"), '\'', "%27");
}

std::string path_normalized(const std::string& path) {
    std::vector<std::string> segments;
    std::size_t start = path.starts_with('/') ? 1 : 0;
    while (start <= path.size()) {
        auto end = path.find('/', start);
        if (end == std::string::npos) end = path.size();
        std::string segment = path.substr(start, end - start);
        const auto folded = lower(segment);
        const bool last = end == path.size();
        if (folded == "." || folded == "%2e") {
            if (last) segments.emplace_back();
        } else if (folded == ".." || folded == ".%2e" || folded == "%2e." ||
                   folded == "%2e%2e") {
            if (!segments.empty()) segments.pop_back();
            if (last) segments.emplace_back();
        } else segments.push_back(encode_delimiter(encode_delimiter(encoded(segment), '?', "%3F"), '#', "%23"));
        if (last) break;
        start = end + 1;
    }
    std::string result;
    for (const auto& segment : segments) result += "/" + segment;
    return result.empty() ? "/" : result;
}

bool authority(DomUrl& result, std::string host) {
    result.credentials.clear();
    result.port.clear();
    const auto at = host.rfind('@');
    if (at != std::string::npos) {
        result.credentials = host.substr(0, at + 1);
        host.erase(0, at + 1);
    }
    std::size_t port_start = std::string::npos;
    if (host.starts_with('[')) {
        const auto close = host.find(']');
        if (close == std::string::npos) return false;
        if (close + 1 < host.size()) {
            if (host[close + 1] != ':') return false;
            port_start = close + 1;
        }
    } else port_start = host.find(':');
    result.hostname = lower(host.substr(0, port_start));
    if (result.hostname.empty() && result.protocol != "file:") return false;
    if (result.hostname.find_first_of(" /?#\\\t\r\n") != std::string::npos) return false;
    if (port_start != std::string::npos) {
        auto port = host.substr(port_start + 1);
        unsigned int number = 0;
        for (unsigned char byte : port) {
            if (byte < '0' || byte > '9') return false;
            number = number * 10 + (byte - '0');
            if (number > 65535) return false;
        }
        if (!port.empty()) result.port = std::to_string(number);
    }
    if (((result.protocol == "http:" || result.protocol == "ws:") && result.port == "80") ||
        ((result.protocol == "https:" || result.protocol == "wss:") && result.port == "443") ||
        (result.protocol == "ftp:" && result.port == "21")) result.port.clear();
    result.host = result.hostname + (result.port.empty() ? "" : ":" + result.port);
    return true;
}

void serialize(DomUrl& result) {
    result.href = result.protocol + (result.opaque ? "" : "//" + result.credentials + result.host) +
                  result.pathname + result.search + result.hash;
    result.origin = result.opaque || result.protocol == "file:" ? "null" :
        result.protocol + "//" + result.host;
    result.valid = true;
}

}  // namespace

DomUrl resolve_dom_url(std::string input, const std::string& base) {
    DomUrl result;
    input.erase(std::remove_if(input.begin(), input.end(), [](char byte) {
        return byte == '\t' || byte == '\r' || byte == '\n';
    }), input.end());
    while (!input.empty() && static_cast<unsigned char>(input.front()) <= 0x20) input.erase(0, 1);
    while (!input.empty() && static_cast<unsigned char>(input.back()) <= 0x20) input.pop_back();
    auto colon = scheme_end(input);
    if (colon == std::string::npos) {
        std::string merged;
        if (!resolve_url(input, true, base, merged)) return result;
        input = merged;
        colon = scheme_end(input);
    }
    if (colon == std::string::npos) return result;
    result.protocol = lower(input.substr(0, colon + 1));
    std::string rest = input.substr(colon + 1);
    if (special(result.protocol)) std::replace(rest.begin(), rest.end(), '\\', '/');
    result.opaque = !rest.starts_with("//");
    if (result.opaque && special(result.protocol)) return result;
    if (!result.opaque) {
        rest.erase(0, 2);
        const auto end = rest.find_first_of("/?#");
        if (!authority(result, rest.substr(0, end))) return DomUrl{};
        rest = end == std::string::npos ? "" : rest.substr(end);
    }
    const auto hash = rest.find('#');
    if (hash != std::string::npos) {
        result.hash = "#" + encoded(rest.substr(hash + 1));
        rest.resize(hash);
    }
    const auto query = rest.find('?');
    if (query != std::string::npos) {
        result.search = "?" + query_encoded(rest.substr(query + 1));
        rest.resize(query);
    }
    result.pathname = result.opaque ? rest : path_normalized(rest);
    serialize(result);
    return result;
}

DomUrl set_dom_url_component(const DomUrl& current, const std::string& member,
                             const std::string& value) {
    if (!current.valid) return current;
    DomUrl result = current;
    if (member == "hash") result.hash = value.empty() ? "" :
        "#" + encoded(value.starts_with('#') ? value.substr(1) : value);
    else if (member == "search") result.search = value.empty() ? "" :
        "?" + query_encoded(value.starts_with('?') ? value.substr(1) : value);
    else if (result.opaque) return current;
    else if (member == "pathname") result.pathname = path_normalized(value);
    else if (member == "protocol") {
        auto protocol = lower(value);
        if (!protocol.ends_with(':')) protocol += ':';
        if (scheme_end(protocol) == std::string::npos ||
            special(protocol) != special(result.protocol)) return current;
        result.protocol = protocol;
        if (!authority(result, current.credentials + current.host)) return current;
    } else if (member == "host" || member == "hostname" || member == "port") {
        auto host = value;
        if (member == "hostname") host += current.port.empty() ? "" : ":" + current.port;
        if (member == "port") host = current.hostname + (value.empty() ? "" : ":" + value);
        if (!authority(result, current.credentials + host)) return current;
    } else return current;
    serialize(result);
    return result;
}

}  // namespace iv8
