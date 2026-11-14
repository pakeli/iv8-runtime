#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "dom_node.h"

namespace iv8::browser {

struct ValidityInfo {
    bool value_missing = false;
    bool type_mismatch = false;
    bool pattern_mismatch = false;
    bool too_long = false;
    bool too_short = false;
    bool range_underflow = false;
    bool range_overflow = false;
    bool step_mismatch = false;
    bool bad_input = false;
    bool custom_error = false;
    int min_length = -1;
    int max_length = -1;

    bool valid() const {
        return !(value_missing || type_mismatch || pattern_mismatch ||
                 too_long || too_short || range_underflow || range_overflow ||
                 step_mismatch || bad_input || custom_error);
    }
};

extern std::string option_value_of(const DomNode* option);

extern std::string normalize_form_method(const std::string& raw);

extern std::string normalize_form_enctype(const std::string& raw);

extern std::string normalize_input_type(const std::string& raw);

extern std::string normalize_button_type(const std::string& raw);

extern std::vector<DomNode*> collect_options(DomNode* root);

extern void initialize_parsed_nodes(const std::vector<std::unique_ptr<DomNode>>& pool);

extern bool is_form_control(const DomNode* node);

extern bool has_attribute(const DomNode* node, const char* name);

extern std::string attribute_value_or_empty(const DomNode* node, const char* name);

extern std::string control_type_of(const DomNode* node);

extern bool is_text_like_input_type(const std::string& type);

extern bool control_will_validate(const DomNode* node);

extern std::string control_value_of(DomNode* node);

extern int parse_non_negative_attribute(const DomNode* node, const char* name);

extern std::size_t utf16_length(const std::string& text);

extern bool looks_like_email(const std::string& text);

extern bool looks_like_absolute_url(const std::string& text);

extern ValidityInfo compute_validity(DomNode* node);

extern std::string validation_message_of(const DomNode* node, const ValidityInfo& info);

}  // namespace iv8::browser
