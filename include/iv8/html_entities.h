#pragma once

#include <string>

namespace iv8 {

// Internal M11-49 bounded string codec; DOM contexts/ownership stay with Page.
std::string decode_html_entities(const std::string& text);
void append_escaped_html_text(std::string& output, const std::string& text);
void append_escaped_html_attribute(std::string& output, const std::string& text);

}  // namespace iv8
