#pragma once

#include <cstdint>

namespace iv8 {

// M11-48: DOM node kinds. Numeric values match the DOM Node.nodeType values the
// runtime exposes; None marks a host object that is not a DOM node.
enum class NodeKind : std::uint8_t {
    None = 0,
    Element = 1,
    Text = 3,
    Comment = 8,
    Document = 9,
    DocumentFragment = 11,
};

}  // namespace iv8
