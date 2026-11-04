#pragma once

#include <string>
#include <utility>
#include <vector>

namespace iv8 {

// Internal deterministic document.cookie store. Owned by PageState across load()
// generations; contains no V8 handles, document pointers, or wall-clock state.
class CookieJar {
public:
    std::string cookie_string() const;
    void set_cookie(const std::string& value);

private:
    // Updates retain their position; deleting and re-adding appends a new entry.
    std::vector<std::pair<std::string, std::string>> entries_;
};

}  // namespace iv8
