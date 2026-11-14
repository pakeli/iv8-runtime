#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include "dom_node.h"

namespace iv8::browser {

// M3-5 script descriptor extracted from HTML in document order. An inline script
// has has_src == false and its JS source in `code`; a `<script src>` has
// has_src == true, its raw src attribute in `src`, and an empty `code` (its
// source is resolved by the host via Page.load(resources=...)).
struct ScriptRecord {
    bool has_src = false;
    std::string src;
    std::string code;
    DomNode* node = nullptr;  // M3-7 backing <script> node (for document.currentScript)
};

extern bool is_void_element(const std::string& tag);

extern std::vector<std::string> split_class_tokens(const std::string& value);

extern void parse_attributes(const std::string& attrs, DomNode& node);

extern std::size_t find_raw_text_close(const std::string& html, std::size_t from, const std::string& tag);

extern DomNode* attach_parsed_node(std::vector<std::unique_ptr<DomNode>>& pool, std::vector<DomNode*>& roots, std::vector<DomNode*>& stack, std::unique_ptr<DomNode> node);

extern void append_parsed_text(std::vector<std::unique_ptr<DomNode>>& pool, std::vector<DomNode*>& roots, std::vector<DomNode*>& stack, const std::string& raw_text);

extern void append_parsed_comment(std::vector<std::unique_ptr<DomNode>>& pool, std::vector<DomNode*>& roots, std::vector<DomNode*>& stack, const std::string& text);

extern void parse_html(const std::string& html, std::vector<std::unique_ptr<DomNode>>& pool, std::vector<DomNode*>& roots, std::vector<ScriptRecord>& scripts);

extern bool is_executable_classic_script(const DomNode* node);

}  // namespace iv8::browser
