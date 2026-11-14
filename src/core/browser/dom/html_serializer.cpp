#include "html_serializer.h"

#include <algorithm>
#include "iv8/html_entities.h"
#include "html_parser.h"

namespace iv8::browser {

std::vector<std::pair<std::string, std::string>>
serialization_attributes(const DomNode* node) {
    std::vector<std::pair<std::string, std::string>> attributes;
    if (node->has_id) {
        attributes.emplace_back("id", node->id);
    }
    if (node->has_class) {
        attributes.emplace_back("class", node->class_name);
    }
    std::vector<std::pair<std::string, std::string>> raw;
    for (const auto& entry : node->attributes) {
        if (entry.first == "id" || entry.first == "class") {
            continue;
        }
        raw.push_back(entry);
    }
    std::sort(raw.begin(), raw.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    attributes.insert(attributes.end(), raw.begin(), raw.end());
    return attributes;
}

std::string serialize_dom_node(const DomNode* node) {
    if (node == nullptr) {
        return std::string();
    }
    if (node->kind == NodeKind::Text) {
        std::string out;
        append_escaped_html_text(out, node->data);
        return out;
    }
    if (node->kind == NodeKind::Comment) {
        return "<!--" + node->data + "-->";
    }
    if (node->kind == NodeKind::DocumentFragment) {
        return serialize_dom_children(node);
    }
    std::string out = "<" + node->tag;
    for (const auto& attribute : serialization_attributes(node)) {
        out += ' ';
        out += attribute.first;
        out += "=\"";
        append_escaped_html_attribute(out, attribute.second);
        out += '"';
    }
    out += '>';
    if (is_void_element(node->tag)) {
        return out;
    }
    out += serialize_dom_children(node);
    out += "</" + node->tag + ">";
    return out;
}

std::string serialize_dom_children(const DomNode* node) {
    std::string out;
    if (node == nullptr) {
        return out;
    }
    const bool raw_text =
        node->kind == NodeKind::Element &&
        (node->tag == "script" || node->tag == "style");
    for (const DomNode* child : node->children) {
        if (raw_text && child->kind == NodeKind::Text) {
            out += child->data;
        } else {
            out += serialize_dom_node(child);
        }
    }
    return out;
}

}  // namespace iv8::browser
