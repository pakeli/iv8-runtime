#pragma once

#include <string>

namespace iv8 {

// Bounded browser-facing URL reflection, separate from the historical minimal
// standalone URL constructor. Owns no V8, network, or document state.
struct DomUrl {
    bool valid = false;
    bool opaque = false;
    std::string href;
    std::string protocol;
    std::string credentials;
    std::string host;
    std::string hostname;
    std::string port;
    std::string pathname;
    std::string search;
    std::string hash;
    std::string origin;
};

DomUrl resolve_dom_url(std::string input, const std::string& base);
DomUrl set_dom_url_component(const DomUrl& current, const std::string& member,
                             const std::string& value);

}  // namespace iv8
