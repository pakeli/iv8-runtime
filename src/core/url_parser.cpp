#include "iv8/url_parser.h"

#include <cctype>
#include <cstddef>

namespace iv8 {

namespace {

// A valid scheme followed by :// is the project's absolute-URL boundary.
bool url_is_absolute(const std::string& text) {
    const auto scheme_end = text.find("://");
    if (scheme_end == std::string::npos || scheme_end == 0) {
        return false;
    }
    if (!std::isalpha(static_cast<unsigned char>(text[0]))) {
        return false;
    }
    for (std::size_t i = 0; i < scheme_end; i++) {
        const char character = text[i];
        if (!(std::isalnum(static_cast<unsigned char>(character)) ||
              character == '+' || character == '-' || character == '.')) {
            return false;
        }
    }
    return true;
}

}  // namespace

// Minimal, deterministic decomposition of an absolute `scheme://host[:port]/
// path?query#hash` URL. NOT a full WHATWG URL parser (no userinfo, default-port
// elision, or percent-decoding) — just enough for the fixed page base URL.
UrlParts decompose_url(const std::string& url) {
    UrlParts parts;
    parts.href = url;

    std::string rest = url;
    const auto scheme_end = rest.find(':');
    if (scheme_end != std::string::npos) {
        parts.protocol = rest.substr(0, scheme_end + 1);  // includes ':'
        rest = rest.substr(scheme_end + 1);
    }
    if (rest.rfind("//", 0) == 0) {  // has an authority
        rest = rest.substr(2);
        const auto auth_end = rest.find_first_of("/?#");
        const std::string authority =
            (auth_end == std::string::npos) ? rest : rest.substr(0, auth_end);
        rest = (auth_end == std::string::npos) ? "" : rest.substr(auth_end);
        parts.host = authority;  // host + optional port
        const auto port = authority.find(':');
        parts.hostname =
            (port == std::string::npos) ? authority : authority.substr(0, port);
        parts.origin = parts.protocol + "//" + parts.host;
    }
    const auto hash_pos = rest.find('#');
    if (hash_pos != std::string::npos) {
        parts.hash = rest.substr(hash_pos);  // includes '#'
        rest = rest.substr(0, hash_pos);
    }
    const auto query_pos = rest.find('?');
    if (query_pos != std::string::npos) {
        parts.search = rest.substr(query_pos);  // includes '?'
        rest = rest.substr(0, query_pos);
    }
    parts.pathname = rest;
    return parts;
}

// Resolve `input` against optional `base` under the project's minimal URL rules:
// an absolute input is taken as-is (base ignored); otherwise a base that is itself
// absolute-with-authority is required, and the reference is merged (protocol-relative
// `//`, absolute-path `/`, query `?`, fragment `#`, or a directory-relative path — no
// dot-segment collapsing). Returns false (=> constructor throws TypeError) when the
// input is relative and no usable base is available.
bool resolve_url(const std::string& input, bool has_base, const std::string& base,
                 std::string& resolved) {
    if (url_is_absolute(input)) {
        resolved = input;
        return true;
    }
    if (!has_base || !url_is_absolute(base)) {
        return false;
    }
    const UrlParts base_parts = decompose_url(base);
    if (base_parts.origin.empty()) {
        return false;  // base had no authority to resolve against
    }
    if (input.empty()) {
        // An empty reference drops the base fragment.
        resolved = base_parts.protocol + "//" + base_parts.host +
                   base_parts.pathname + base_parts.search;
        return true;
    }
    const char first_character = input[0];
    if (first_character == '#') {
        resolved = base_parts.protocol + "//" + base_parts.host +
                   base_parts.pathname + base_parts.search + input;
        return true;
    }
    if (first_character == '?') {
        resolved = base_parts.protocol + "//" + base_parts.host +
                   base_parts.pathname + input;
        return true;
    }
    if (first_character == '/') {
        resolved = (input.rfind("//", 0) == 0)
                       ? (base_parts.protocol + input)  // protocol-relative
                       : (base_parts.origin + input);   // absolute path
        return true;
    }
    std::string directory = base_parts.pathname;
    const auto slash = directory.rfind('/');
    directory = (slash == std::string::npos) ? std::string("/")
                                           : directory.substr(0, slash + 1);
    if (directory.empty()) {
        directory = "/";
    }
    resolved = base_parts.origin + directory + input;  // directory-relative merge
    return true;
}

}  // namespace iv8
