#include "form_controls.h"

#include <cctype>
#include <cstdint>
#include "../internal/text.h"
#include "dom_query.h"

namespace iv8::browser {

// M5-5: an <option>'s value — its `value` attribute if present (read live, so
// setAttribute('value', ...) is reflected), else its text content (empty -> "").
std::string option_value_of(const DomNode* option) {
    const auto it = option->attributes.find("value");
    if (it != option->attributes.end()) {
        return it->second;
    }
    return text_content_of(option);
}

// M7-3: minimal <form>.method normalization. ASCII-lowercase the raw string; if it
// is exactly "get" or "post", keep it; any other value (incl. "" and "dialog") maps
// to "get". This is the whole normalization contract — no attribute reflection.
std::string normalize_form_method(const std::string& raw) {
    const std::string lower = ascii_lower(raw);
    if (lower == "get" || lower == "post") {
        return lower;
    }
    return "get";
}

// M7-5: minimal <form>.enctype normalization. ASCII-lowercase the raw string; if it
// is one of the three HTML enctypes ("application/x-www-form-urlencoded",
// "multipart/form-data", "text/plain"), keep it; any other value maps to the default
// "application/x-www-form-urlencoded". No body-encoding behaviour — metadata only.
std::string normalize_form_enctype(const std::string& raw) {
    const std::string lower = ascii_lower(raw);
    if (lower == "multipart/form-data" || lower == "text/plain") {
        return lower;
    }
    return "application/x-www-form-urlencoded";
}

// M11-18: input.type / button.type reflection. The getter lower-cases the raw
// attribute and returns it only when it is a known type; an absent, empty, or
// unknown value maps to the documented default. The setter (see ElementHost)
// writes the raw string back to the attribute.
std::string normalize_input_type(const std::string& raw) {
    static const char* const known[] = {
        "hidden", "text",     "search", "tel",      "url",
        "email",  "password", "date",   "month",    "week",
        "time",   "datetime-local", "number", "range", "color",
        "checkbox", "radio",  "file",   "submit",   "image",
        "reset",  "button"};
    const std::string lower = ascii_lower(raw);
    for (const char* candidate : known) {
        if (lower == candidate) {
            return lower;
        }
    }
    return "text";
}

std::string normalize_button_type(const std::string& raw) {
    const std::string lower = ascii_lower(raw);
    if (lower == "reset" || lower == "button") {
        return lower;
    }
    return "submit";
}

// M5-5: the <option> descendants of `root` in document order (used by
// select.value's read/write and the initial-selection normalization).
std::vector<DomNode*> collect_options(DomNode* root) {
    std::vector<DomNode*> out;
    collect_matching(root, [](const DomNode* n) { return n->tag == "option"; },
                     out);
    return out;
}

// M11-49: shared post-parse initialization for a document load and for HTML
// fragments inserted by innerHTML/insertAdjacentHTML. parse_html has already
// seeded input/option fields and textarea values; this performs select
// normalization, form metadata seeding, and the form.reset() baseline snapshot.
void initialize_parsed_nodes(
    const std::vector<std::unique_ptr<DomNode>>& pool) {
    for (const std::unique_ptr<DomNode>& node : pool) {
        if (node->tag != "select") {
            continue;
        }
        bool seen = false;
        for (DomNode* option : collect_options(node.get())) {
            if (!option->selected) {
                continue;
            }
            if (seen) {
                option->selected = false;
            } else {
                seen = true;
            }
        }
    }
    for (const std::unique_ptr<DomNode>& node : pool) {
        if (node->tag != "form") {
            continue;
        }
        const auto mit = node->attributes.find("method");
        if (mit != node->attributes.end()) {
            node->method = normalize_form_method(mit->second);
        }
        const auto ait = node->attributes.find("action");
        if (ait != node->attributes.end()) {
            node->action = ait->second;
        }
        const auto eit = node->attributes.find("enctype");
        if (eit != node->attributes.end()) {
            node->enctype = normalize_form_enctype(eit->second);
        }
        const auto tit = node->attributes.find("target");
        if (tit != node->attributes.end()) {
            node->target = tit->second;
        }
        node->no_validate = node->attributes.count("novalidate") != 0;
    }
    for (const std::unique_ptr<DomNode>& node : pool) {
        node->initial_value = node->value;
        node->initial_selected = node->selected;
        node->initial_checked = node->checked;
    }
}

// --- M11-32 minimal constraint validation ------------------------------------
//
// Supported control set: input / button / select / textarea (the same four
// form.elements returns). Supported constraints: required, input type=email /
// url typeMismatch, input/textarea minlength/maxlength, and customError from
// setCustomValidity(). The remaining ValidityState flags are exposed but always
// false (pattern/min/max/step/date/number semantics are deferred).
bool is_form_control(const DomNode* node) {
    return node->tag == "input" || node->tag == "button" ||
           node->tag == "select" || node->tag == "textarea";
}

bool has_attribute(const DomNode* node, const char* name) {
    return node->attributes.find(name) != node->attributes.end();
}

std::string attribute_value_or_empty(const DomNode* node, const char* name) {
    const auto it = node->attributes.find(name);
    return it != node->attributes.end() ? it->second : std::string();
}

std::string control_type_of(const DomNode* node) {
    if (node->tag == "input") {
        return normalize_input_type(attribute_value_or_empty(node, "type"));
    }
    if (node->tag == "button") {
        return normalize_button_type(attribute_value_or_empty(node, "type"));
    }
    return std::string();
}

bool is_text_like_input_type(const std::string& type) {
    return type == "text" || type == "search" || type == "tel" ||
           type == "url" || type == "email" || type == "password";
}

bool control_will_validate(const DomNode* node) {
    if (!is_form_control(node) || has_attribute(node, "disabled")) {
        return false;
    }
    if (node->tag == "input") {
        const std::string type = control_type_of(node);
        if (type == "hidden" || type == "reset" || type == "button" ||
            type == "image") {
            return false;
        }
        if (has_attribute(node, "readonly")) {
            return false;
        }
    } else if (node->tag == "textarea") {
        if (has_attribute(node, "readonly")) {
            return false;
        }
    }
    return true;
}

std::string control_value_of(DomNode* node) {
    if (node->tag == "select") {
        for (DomNode* option : collect_options(node)) {
            if (option->selected) {
                return option_value_of(option);
            }
        }
        return std::string();
    }
    return node->value;
}

// Parse a non-negative integer attribute; absent or malformed -> -1 (ignored).
int parse_non_negative_attribute(const DomNode* node, const char* name) {
    const auto it = node->attributes.find(name);
    if (it == node->attributes.end() || it->second.empty()) {
        return -1;
    }
    long long value = 0;
    for (char c : it->second) {
        if (c < '0' || c > '9') {
            return -1;
        }
        value = value * 10 + (c - '0');
        if (value > 2147483647LL) {
            return 2147483647;
        }
    }
    return static_cast<int>(value);
}

// Value length in UTF-16 code units (the unit browsers use for minlength /
// maxlength). Invalid UTF-8 bytes count as one unit each.
std::size_t utf16_length(const std::string& text) {
    std::size_t units = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::uint32_t code_point = 0;
        std::size_t length = 1;
        if (c < 0x80) {
            code_point = c;
        } else if ((c & 0xE0) == 0xC0) {
            code_point = c & 0x1F;
            length = 2;
        } else if ((c & 0xF0) == 0xE0) {
            code_point = c & 0x0F;
            length = 3;
        } else if ((c & 0xF8) == 0xF0) {
            code_point = c & 0x07;
            length = 4;
        } else {
            units += 1;
            i += 1;
            continue;
        }
        if (i + length > text.size()) {
            units += 1;
            break;
        }
        bool valid = true;
        for (std::size_t k = 1; k < length; k++) {
            const unsigned char continuation =
                static_cast<unsigned char>(text[i + k]);
            if ((continuation & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            code_point = (code_point << 6) | (continuation & 0x3F);
        }
        if (!valid) {
            units += 1;
            i += 1;
            continue;
        }
        units += (code_point > 0xFFFF) ? 2 : 1;
        i += length;
    }
    return units;
}

bool looks_like_email(const std::string& text) {
    std::size_t at = std::string::npos;
    for (std::size_t i = 0; i < text.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '@') {
            if (at != std::string::npos) {
                return false;  // more than one @
            }
            at = i;
        } else if (std::isspace(c) || c < 0x20) {
            return false;
        }
    }
    return at != std::string::npos && at > 0 && at + 1 < text.size();
}

bool looks_like_absolute_url(const std::string& text) {
    if (text.empty() || !std::isalpha(static_cast<unsigned char>(text[0]))) {
        return false;
    }
    std::size_t i = 1;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (!std::isalnum(c) && c != '+' && c != '-' && c != '.') {
            break;
        }
        i++;
    }
    if (i >= text.size() || text[i] != ':') {
        return false;
    }
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isspace(u) || u < 0x20) {
            return false;
        }
    }
    const std::string scheme = ascii_lower(text.substr(0, i));
    if (scheme == "http" || scheme == "https") {
        const std::size_t host_start = i + 3;  // past "://"
        if (text.compare(i + 1, 2, "//") != 0 ||
            host_start >= text.size() || text[host_start] == '/' ||
            text[host_start] == '?' || text[host_start] == '#') {
            return false;
        }
    }
    return true;
}

ValidityInfo compute_validity(DomNode* node) {
    ValidityInfo info;
    if (!control_will_validate(node)) {
        return info;  // barred controls are always valid
    }
    const std::string value = control_value_of(node);
    const std::string type = control_type_of(node);
    if (has_attribute(node, "required") && node->tag != "button") {
        const bool checked_control = type == "checkbox" || type == "radio";
        if (checked_control ? !node->checked : value.empty()) {
            info.value_missing = true;
        }
    }
    if (node->tag == "input" && !value.empty()) {
        if (type == "email" && !looks_like_email(value)) {
            info.type_mismatch = true;
        } else if (type == "url" && !looks_like_absolute_url(value)) {
            info.type_mismatch = true;
        }
    }
    const bool length_checked =
        node->tag == "textarea" ||
        (node->tag == "input" && is_text_like_input_type(type));
    if (length_checked) {
        const std::size_t length = utf16_length(value);
        info.min_length = parse_non_negative_attribute(node, "minlength");
        info.max_length = parse_non_negative_attribute(node, "maxlength");
        if (info.min_length >= 0 && !value.empty() &&
            length < static_cast<std::size_t>(info.min_length)) {
            info.too_short = true;
        }
        if (info.max_length >= 0 &&
            length > static_cast<std::size_t>(info.max_length)) {
            info.too_long = true;
        }
    }
    info.custom_error = !node->custom_validity.empty();
    return info;
}

std::string validation_message_of(const DomNode* node,
                                  const ValidityInfo& info) {
    if (info.custom_error) {
        return node->custom_validity;
    }
    if (info.value_missing) {
        return "Please fill out this field.";
    }
    if (info.type_mismatch) {
        return control_type_of(node) == "email"
                   ? "Please enter a valid email address."
                   : "Please enter a URL.";
    }
    if (info.too_short) {
        return "Please lengthen this text to " +
               std::to_string(info.min_length) +
               " characters or more.";
    }
    if (info.too_long) {
        return "Please shorten this text to " +
               std::to_string(info.max_length) +
               " characters or fewer.";
    }
    return std::string();
}

}  // namespace iv8::browser
