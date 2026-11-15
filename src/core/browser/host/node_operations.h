#pragma once

#include <cstdint>

#include "../dom/dom_node.h"

namespace iv8::browser {

DomNode* node_root(DomNode* node);
std::uint16_t compare_node_position(const DomNode* node, const DomNode* other);

}  // namespace iv8::browser
