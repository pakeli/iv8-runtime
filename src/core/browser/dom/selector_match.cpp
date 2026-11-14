#include "selector_match.h"

#include <algorithm>
#include <cctype>
#include <vector>
#include "../internal/text.h"
#include "html_parser.h"

namespace iv8::browser {

// --- M11-34 selector engine --------------------------------------------------
//
// A small CSS Selectors subset shared by querySelector(All), matches(), and
// closest(): selector lists, compound selectors, descendant / child / adjacent-
// sibling / general-sibling combinators, attribute selectors, and `*`. Pseudo-
// classes/pseudo-elements, CSS escapes, namespaces, and case-insensitivity
// flags are intentionally unsupported; an unparseable selector matches nothing
// (stable empty / null / false, matching the historical behavior).
bool selector_attribute_value(const DomNode* node, const std::string& name,
                              std::string* out) {
    if (name == "id") {
        if (!node->has_id) {
            return false;
        }
        *out = node->id;
        return true;
    }
    if (name == "class") {
        if (!node->has_class) {
            return false;
        }
        *out = node->class_name;
        return true;
    }
    const auto it = node->attributes.find(name);
    if (it == node->attributes.end()) {
        return false;
    }
    *out = it->second;
    return true;
}

bool selector_match_attribute(const DomNode* node,
                              const SelectorAttributeTest& test) {
    std::string value;
    if (!selector_attribute_value(node, test.name, &value)) {
        return false;
    }
    switch (test.op) {
        case SelectorAttributeTest::Op::Exists:
            return true;
        case SelectorAttributeTest::Op::Equals:
            return value == test.value;
        case SelectorAttributeTest::Op::Includes: {
            if (test.value.empty()) {
                return false;
            }
            for (char c : test.value) {
                if (std::isspace(static_cast<unsigned char>(c))) {
                    return false;
                }
            }
            for (const std::string& token : split_class_tokens(value)) {
                if (token == test.value) {
                    return true;
                }
            }
            return false;
        }
        case SelectorAttributeTest::Op::DashMatch:
            return value == test.value ||
                   (value.size() > test.value.size() &&
                    value.compare(0, test.value.size(), test.value) == 0 &&
                    value[test.value.size()] == '-');
        case SelectorAttributeTest::Op::Prefix:
            return !test.value.empty() &&
                   value.size() >= test.value.size() &&
                   value.compare(0, test.value.size(), test.value) == 0;
        case SelectorAttributeTest::Op::Suffix:
            return !test.value.empty() &&
                   value.size() >= test.value.size() &&
                   value.compare(value.size() - test.value.size(),
                                 test.value.size(), test.value) == 0;
        case SelectorAttributeTest::Op::Substring:
            return !test.value.empty() &&
                   value.find(test.value) != std::string::npos;
    }
    return false;
}

bool selector_match_compound(const DomNode* node,
                             const SelectorCompound& selector) {
    if (!is_element_node(node)) {  // M11-48: selectors match elements only
        return false;
    }
    if (!selector.tag.empty() && node->tag != selector.tag) {
        return false;
    }
    for (const std::string& id : selector.ids) {
        if (!node->has_id || node->id != id) {
            return false;
        }
    }
    for (const std::string& cls : selector.classes) {
        if (std::find(node->classes.begin(), node->classes.end(), cls) ==
            node->classes.end()) {
            return false;
        }
    }
    for (const SelectorAttributeTest& attribute : selector.attributes) {
        if (!selector_match_attribute(node, attribute)) {
            return false;
        }
    }
    return true;
}

const DomNode* selector_previous_sibling(const DomNode* node) {
    if (node->parent == nullptr) {
        return nullptr;
    }
    const std::vector<DomNode*>& siblings = node->parent->children;
    for (std::size_t i = 1; i < siblings.size(); i++) {
        if (siblings[i] == node) {
            // M11-48: sibling combinators skip Text/Comment nodes.
            for (std::size_t j = i; j > 0; --j) {
                if (is_element_node(siblings[j - 1])) {
                    return siblings[j - 1];
                }
            }
            return nullptr;
        }
    }
    return nullptr;
}

bool selector_match_complex_at(const DomNode* node,
                               const SelectorComplex& selector,
                               std::size_t index) {
    if (index >= selector.compounds.size() ||
        !selector_match_compound(node, selector.compounds[index])) {
        return false;
    }
    if (index == 0) {
        return true;
    }
    const char combinator = selector.combinators[index - 1];
    switch (combinator) {
        case '>':
            return node->parent != nullptr &&
                   selector_match_complex_at(node->parent, selector, index - 1);
        case ' ':
            for (const DomNode* parent = node->parent; parent != nullptr;
                 parent = parent->parent) {
                if (selector_match_complex_at(parent, selector, index - 1)) {
                    return true;
                }
            }
            return false;
        case '+': {
            const DomNode* previous = selector_previous_sibling(node);
            return previous != nullptr &&
                   selector_match_complex_at(previous, selector, index - 1);
        }
        case '~':
            for (const DomNode* previous = selector_previous_sibling(node);
                 previous != nullptr;
                 previous = selector_previous_sibling(previous)) {
                if (selector_match_complex_at(previous, selector, index - 1)) {
                    return true;
                }
            }
            return false;
        default:
            return false;
    }
}

bool selector_match_list(const DomNode* node, const SelectorList& list) {
    for (const SelectorComplex& selector : list.selectors) {
        if (!selector.compounds.empty() &&
            selector_match_complex_at(node, selector,
                                      selector.compounds.size() - 1)) {
            return true;
        }
    }
    return false;
}

// Selector -> node predicate, shared by document- and element-level
// querySelector / querySelectorAll and by matches() / closest(). The whole
// expression is parsed once; an unsupported/malformed selector matches nothing.
std::function<bool(const DomNode*)> selector_predicate(const std::string& raw) {
    SelectorList list;
    if (!parse_selector_list(raw, &list) || list.selectors.empty()) {
        return [](const DomNode*) { return false; };
    }
    return [list](const DomNode* node) {
        return selector_match_list(node, list);
    };
}

// getElementsByTagName predicate: ASCII case-insensitive tag; "*" = every element.
std::function<bool(const DomNode*)> tag_predicate(const std::string& raw) {
    if (raw == "*") {
        // M11-48: tag queries and the universal selector only match elements.
        return [](const DomNode* n) { return is_element_node(n); };
    }
    const std::string tag = ascii_lower(raw);
    return [tag](const DomNode* n) {
        return n->kind == NodeKind::Element && n->tag == tag;
    };
}

}  // namespace iv8::browser
