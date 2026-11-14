#pragma once

#include <string>
#include <utility>
#include <vector>

#include "dom_node.h"

namespace iv8::browser {

extern std::vector<std::pair<std::string, std::string>> serialization_attributes(const DomNode* node);

extern std::string serialize_dom_node(const DomNode* node);

extern std::string serialize_dom_children(const DomNode* node);

}  // namespace iv8::browser
