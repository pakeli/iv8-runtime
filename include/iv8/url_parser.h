#pragma once

#include <string>

namespace iv8 {

// Internal string-only decomposition shared by the Page's URL consumers.
struct UrlParts {
    std::string href;
    std::string origin;
    std::string protocol;
    std::string host;
    std::string hostname;
    std::string pathname;
    std::string search;
    std::string hash;
};

// Preserve the project's minimal URL rules; no WHATWG normalization is applied.
UrlParts decompose_url(const std::string& url);
// On failure, the output argument is left untouched.
bool resolve_url(const std::string& input, bool has_base, const std::string& base,
                 std::string& resolved);

}  // namespace iv8
