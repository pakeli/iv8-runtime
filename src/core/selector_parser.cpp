#include "iv8/selector_parser.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <utility>

namespace iv8 {

namespace {

std::string ascii_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

bool selector_name_char(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '-' || c == '_';
}

bool selector_attribute_name_char(char c) {
    return selector_name_char(c) || c == ':';
}

class SelectorParser {
public:
    explicit SelectorParser(const std::string& text) : text_(text) {}

    bool parse(SelectorList* out) {
        skip_ws();
        if (at_end()) {
            return false;
        }
        while (true) {
            SelectorComplex complex;
            if (!parse_complex(&complex)) {
                return false;
            }
            out->selectors.push_back(std::move(complex));
            skip_ws();
            if (at_end()) {
                return true;
            }
            if (peek() != ',') {
                return false;
            }
            ++pos_;
            skip_ws();
            if (at_end()) {
                return false;  // trailing comma
            }
        }
    }

private:
    bool at_end() const { return pos_ >= text_.size(); }

    char peek() const {
        return at_end() ? '\0' : text_[pos_];
    }

    void skip_ws() {
        while (!at_end() &&
               std::isspace(static_cast<unsigned char>(peek()))) {
            ++pos_;
        }
    }

    std::string read_name(bool attribute_name) {
        const std::size_t start = pos_;
        while (!at_end()) {
            const char c = peek();
            const bool ok = attribute_name ? selector_attribute_name_char(c)
                                           : selector_name_char(c);
            if (!ok) {
                break;
            }
            ++pos_;
        }
        return text_.substr(start, pos_ - start);
    }

    bool parse_complex(SelectorComplex* out) {
        SelectorCompound first;
        if (!parse_compound(&first)) {
            return false;
        }
        out->compounds.push_back(std::move(first));
        while (true) {
            const std::size_t before_ws = pos_;
            skip_ws();
            const bool had_ws = pos_ != before_ws;
            if (at_end() || peek() == ',') {
                return true;
            }
            char combinator = '\0';
            const char c = peek();
            if (c == '>' || c == '+' || c == '~') {
                combinator = c;
                ++pos_;
                skip_ws();
                if (at_end()) {
                    return false;
                }
            } else if (had_ws) {
                combinator = ' ';
            } else {
                return false;
            }
            SelectorCompound next;
            if (!parse_compound(&next)) {
                return false;
            }
            out->combinators.push_back(combinator);
            out->compounds.push_back(std::move(next));
        }
    }

    bool parse_compound(SelectorCompound* out) {
        bool any = false;
        if (peek() == '*') {
            ++pos_;
            any = true;
        } else if (!at_end() && selector_name_char(peek())) {
            const std::string name = read_name(/*attribute_name=*/false);
            if (name.empty()) {
                return false;
            }
            out->tag = ascii_lower(name);
            any = true;
        }
        while (!at_end()) {
            const char c = peek();
            if (c == '#') {
                ++pos_;
                const std::string id = read_name(/*attribute_name=*/false);
                if (id.empty()) {
                    return false;
                }
                out->ids.push_back(id);
                any = true;
            } else if (c == '.') {
                ++pos_;
                const std::string cls = read_name(/*attribute_name=*/false);
                if (cls.empty()) {
                    return false;
                }
                out->classes.push_back(cls);
                any = true;
            } else if (c == '[') {
                SelectorAttributeTest attribute;
                if (!parse_attribute(&attribute)) {
                    return false;
                }
                out->attributes.push_back(std::move(attribute));
                any = true;
            } else if (c == ':') {
                // Pseudo-classes and pseudo-elements remain unsupported.
                return false;
            } else {
                break;
            }
        }
        return any;
    }

    bool parse_attribute(SelectorAttributeTest* out) {
        if (peek() != '[') {
            return false;
        }
        ++pos_;
        skip_ws();
        const std::string name = read_name(/*attribute_name=*/true);
        if (name.empty()) {
            return false;
        }
        out->name = ascii_lower(name);
        skip_ws();
        if (at_end()) {
            return false;
        }
        if (peek() == ']') {
            ++pos_;
            out->op = SelectorAttributeTest::Op::Exists;
            return true;
        }
        const char first = peek();
        if (first == '=') {
            out->op = SelectorAttributeTest::Op::Equals;
            ++pos_;
        } else if (first == '~' || first == '|' || first == '^' ||
                   first == '$' || first == '*') {
            if (pos_ + 1 >= text_.size() || text_[pos_ + 1] != '=') {
                return false;
            }
            switch (first) {
                case '~':
                    out->op = SelectorAttributeTest::Op::Includes;
                    break;
                case '|':
                    out->op = SelectorAttributeTest::Op::DashMatch;
                    break;
                case '^':
                    out->op = SelectorAttributeTest::Op::Prefix;
                    break;
                case '$':
                    out->op = SelectorAttributeTest::Op::Suffix;
                    break;
                default:
                    out->op = SelectorAttributeTest::Op::Substring;
                    break;
            }
            pos_ += 2;
        } else {
            return false;
        }
        skip_ws();
        if (!read_attribute_value(&out->value)) {
            return false;
        }
        skip_ws();
        if (at_end() || peek() != ']') {
            // The optional case-sensitivity flag (i/s) is not supported.
            return false;
        }
        ++pos_;
        return true;
    }

    bool read_attribute_value(std::string* out) {
        if (at_end()) {
            return false;
        }
        const char quote = peek();
        if (quote == '"' || quote == '\'') {
            ++pos_;
            while (!at_end()) {
                const char c = peek();
                if (c == quote) {
                    ++pos_;
                    return true;
                }
                if (c == '\\') {
                    return false;  // no CSS escapes
                }
                out->push_back(c);
                ++pos_;
            }
            return false;  // unterminated quote
        }
        const std::size_t start = pos_;
        while (!at_end()) {
            const char c = peek();
            if (std::isspace(static_cast<unsigned char>(c)) || c == ']') {
                break;
            }
            if (c == '\\') {
                return false;  // no CSS escapes
            }
            ++pos_;
        }
        if (pos_ == start) {
            return false;
        }
        *out = text_.substr(start, pos_ - start);
        return true;
    }

    const std::string& text_;
    std::size_t pos_ = 0;
};

}  // namespace

bool parse_selector_list(const std::string& text, SelectorList* out) {
    SelectorParser parser(text);
    return parser.parse(out);
}

}  // namespace iv8
