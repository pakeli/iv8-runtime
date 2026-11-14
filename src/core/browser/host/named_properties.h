#pragma once

#include <string>

#include "../dom/dom_node.h"

namespace iv8::browser {

// The bounded four-control form slice uses the nearest ancestor form; explicit
// form-attribute associations and additional listed elements remain separate.
DomNode* named_control_form(const DomNode* node);

bool window_named_property_matches(const DomNode* node,
                                   const std::string& name);
bool form_named_property_matches(const DomNode* node, const DomNode* form,
                                 const std::string& name);

}  // namespace iv8::browser
