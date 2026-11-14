#pragma once

#include <cstddef>
#include <functional>
#include <string>

#include "dom_node.h"
#include "iv8/selector_parser.h"

namespace iv8::browser {

extern bool selector_attribute_value(const DomNode* node, const std::string& name, std::string* out);

extern bool selector_match_attribute(const DomNode* node, const SelectorAttributeTest& test);

extern bool selector_match_compound(const DomNode* node, const SelectorCompound& selector);

extern const DomNode* selector_previous_sibling(const DomNode* node);

extern bool selector_match_complex_at(const DomNode* node, const SelectorComplex& selector, std::size_t index);

extern bool selector_match_list(const DomNode* node, const SelectorList& list);

extern std::function<bool(const DomNode*)> selector_predicate(const std::string& raw);

extern std::function<bool(const DomNode*)> tag_predicate(const std::string& raw);

}  // namespace iv8::browser
