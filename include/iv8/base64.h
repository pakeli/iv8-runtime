#pragma once

#include <string>

namespace iv8 {

// Internal byte codec; the host owns JS string conversion and exceptions.
std::string base64_encode(const std::string& bytes);
// Appends decoded bytes, retaining the original partial-output behavior on error.
bool base64_decode(const std::string& encoded, std::string& decoded);

}  // namespace iv8
