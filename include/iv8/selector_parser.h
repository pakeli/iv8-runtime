#pragma once

#include <string>
#include <vector>

namespace iv8 {

// Internal selector syntax shared by the parser and Page's DOM matcher.
// No V8 handles or DOM pointers are retained here.
struct SelectorAttributeTest {
    enum class Op {
        Exists,
        Equals,
        Includes,
        DashMatch,
        Prefix,
        Suffix,
        Substring,
    };
    std::string name;
    Op op = Op::Exists;
    std::string value;
};

struct SelectorCompound {
    std::string tag;  // empty = universal / no type selector
    std::vector<std::string> ids;
    std::vector<std::string> classes;
    std::vector<SelectorAttributeTest> attributes;
};

struct SelectorComplex {
    std::vector<SelectorCompound> compounds;
    // combinators[i] joins compounds[i] and compounds[i + 1]:
    // ' ' descendant, '>' child, '+' adjacent sibling, '~' general sibling.
    std::vector<char> combinators;
};

struct SelectorList {
    std::vector<SelectorComplex> selectors;
};

// Parse into a fresh list. On failure the caller must discard the partial list.
// Unsupported/malformed syntax returns false; it does not throw a JS exception.
bool parse_selector_list(const std::string& text, SelectorList* out);

}  // namespace iv8
