#include "document_host.h"

#include <algorithm>
#include <unordered_set>

namespace iv8::browser {

void DocumentHost::ensure_html_structure() {
    const auto original_roots = roots_;
    DomNode* html = nullptr;
    for (auto* root : original_roots) {
        if (root->kind == NodeKind::Element && root->tag == "html") {
            html = root;
            break;
        }
    }
    const auto make_element = [&](const std::string& tag) {
        auto node = std::make_unique<DomNode>();
        node->tag = tag;
        auto* result = node.get();
        pool_.push_back(std::move(node));
        return result;
    };
    if (html == nullptr) html = make_element("html");
    const auto original_children = html->children;
    DomNode* head = nullptr;
    DomNode* body = nullptr;
    const auto find_containers = [&](const std::vector<DomNode*>& nodes) {
        for (auto* node : nodes) {
            if (node->kind != NodeKind::Element) continue;
            if (node->tag == "head" && head == nullptr) head = node;
            if (node->tag == "body" && body == nullptr) body = node;
        }
    };
    find_containers(original_children);
    find_containers(original_roots);
    if (head == nullptr) head = make_element("head");
    if (body == nullptr) body = make_element("body");
    html->parent = &document_node_;
    html->children = {head, body};
    head->parent = html;
    body->parent = html;
    bool body_started = !body->children.empty();
    const std::unordered_set<std::string> head_tags = {
        "base", "basefont", "bgsound", "link", "meta", "title", "noscript", "noframes", "style", "script", "template"};
    const auto distribute = [&](DomNode* node) {
        if (node == html || node == head || node == body) return;
        if (node->kind == NodeKind::Text && !body_started &&
            node->data.find_first_not_of(" \t\r\n\f") == std::string::npos) {
            node->parent = nullptr;
            return;
        }
        auto* parent = !body_started && node->kind == NodeKind::Element &&
            head_tags.contains(node->tag) ? head : body;
        if (parent == body) body_started = true;
        node->parent = parent;
        parent->children.push_back(node);
    };
    for (auto* node : original_children) distribute(node);
    roots_.clear();
    bool inserted_html = false;
    for (auto* node : original_roots) {
        if (node->kind == NodeKind::Comment) {
            node->parent = &document_node_;
            roots_.push_back(node);
            continue;
        }
        if (!inserted_html) {
            roots_.push_back(html);
            inserted_html = true;
        }
        distribute(node);
    }
    if (!inserted_html) roots_.push_back(html);
    title_node_ = find_tag("title");
    title_ = title_node_ == nullptr ? std::string() : text_content_of(title_node_);
}

}  // namespace iv8::browser
