#include "named_properties.h"

#include <algorithm>
#include <string>
#include <vector>

#include "../dom/dom_query.h"
#include "../dom/form_controls.h"
#include "document_host.h"

namespace iv8::browser {

DomNode* named_control_form(const DomNode* node) {
    if (node == nullptr) return nullptr;
    DomNode* owner = node->parent;
    while (owner != nullptr && owner->tag != "form") {
        owner = owner->parent;
    }
    return owner;
}

bool window_named_property_matches(const DomNode* node,
                                   const std::string& name) {
    if (node->kind != NodeKind::Element || name.empty()) return false;
    if (node->id == name) return true;
    const bool supports_name = node->tag == "form" || node->tag == "img" ||
        node->tag == "embed" || node->tag == "object";
    const auto attribute = node->attributes.find("name");
    return supports_name && attribute != node->attributes.end() &&
        attribute->second == name;
}

bool form_named_property_matches(const DomNode* node, const DomNode* form,
                                 const std::string& name) {
    if (node->kind != NodeKind::Element || !is_form_control(node) || name.empty() ||
        named_control_form(node) != form) {
        return false;
    }
    const auto attribute = node->attributes.find("name");
    return node->id == name ||
        (attribute != node->attributes.end() && attribute->second == name);
}

std::vector<DomNode*> ElementHost::named_controls() const {
    std::vector<DomNode*> controls;
    if (!has_named_properties()) return controls;
    collect_matching(node_, [this](const DomNode* node) {
        return node->kind == NodeKind::Element && is_form_control(node) &&
            named_control_form(node) == node_;
    }, controls);
    return controls;
}

std::vector<std::string> ElementHost::named_property_names() const {
    if (!has_named_properties()) return {};
    std::vector<std::string> names;
    const auto append = [&names](const std::string& name) {
        if (!name.empty() &&
            std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
    };
    for (DomNode* control : named_controls()) {
        append(control->id);
        const auto attribute = control->attributes.find("name");
        if (attribute != control->attributes.end()) append(attribute->second);
    }
    for (const auto& [name, control] : past_named_controls_) {
        if (named_control_form(control) == node_) append(name);
    }
    return names;
}

v8::MaybeLocal<v8::Value> ElementHost::named_property(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const std::string& name) {
    if (!has_named_properties() || name.empty()) return {};
    std::vector<DomNode*> nodes;
    for (DomNode* control : named_controls()) {
        if (form_named_property_matches(control, node_, name)) {
            nodes.push_back(control);
        }
    }
    const auto found = std::find_if(
        past_named_controls_.begin(), past_named_controls_.end(),
        [&name](const auto& entry) { return entry.first == name; });
    if (nodes.empty()) {
        if (found == past_named_controls_.end()) return {};
        if (named_control_form(found->second) != node_) {
            past_named_controls_.erase(found);
            return {};
        }
        return document_->wrap_node(isolate, context, found->second);
    }
    if (nodes.size() == 1) {
        if (found == past_named_controls_.end()) {
            past_named_controls_.emplace_back(name, nodes.front());
        } else {
            found->second = nodes.front();
        }
        return document_->wrap_node(isolate, context, nodes.front());
    }
    return document_->live_collection(isolate, context, node_, "form-name", name);
}

void ElementHost::prune_past_named_controls() {
    past_named_controls_.erase(
        std::remove_if(past_named_controls_.begin(), past_named_controls_.end(),
                       [this](const auto& entry) {
                           return named_control_form(entry.second) != node_;
                       }),
        past_named_controls_.end());
}

v8::MaybeLocal<v8::Value> DocumentHost::window_named_property(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const std::string& name) {
    if (!html_document_ || name.empty()) return {};
    const auto nodes = collect([&name](const DomNode* node) {
        return window_named_property_matches(node, name);
    });
    if (nodes.empty()) return {};
    if (nodes.size() == 1) return wrap_node(isolate, context, nodes.front());
    return live_collection(isolate, context, nullptr, "window-name", name);
}

void DocumentHost::prune_past_named_controls() {
    for (const auto& [node, host] : wrappers_) {
        if (node->kind == NodeKind::Element && node->tag == "form") {
            static_cast<ElementHost*>(host)->prune_past_named_controls();
        }
    }
}

std::vector<DomNode*> DocumentHost::named_collection_nodes(
    const LiveCollectionBinding& binding) const {
    if (!html_document_ || binding.query.empty()) return {};
    if (binding.kind == "window-name") {
        return collect([&binding](const DomNode* node) {
            return window_named_property_matches(node, binding.query);
        });
    }
    if (binding.kind != "form-name" || binding.root == nullptr) return {};
    std::vector<DomNode*> nodes;
    for (DomNode* child : binding.root->children) {
        collect_matching(child, [&binding](const DomNode* node) {
            return form_named_property_matches(node, binding.root, binding.query);
        }, nodes);
    }
    return nodes;
}

}  // namespace iv8::browser
