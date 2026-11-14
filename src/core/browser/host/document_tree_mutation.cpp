#include "document_host.h"

#include <algorithm>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "iv8/dom_url.h"
#include "../dom/dom_query.h"
#include "../dom/form_controls.h"
#include "../dom/html_serializer.h"
#include "../dom/selector_match.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {

std::vector<DomNode*> DocumentHost::parse_html_fragment(const std::string& html) {
    std::vector<std::unique_ptr<DomNode>> pool;
    std::vector<DomNode*> roots;
    std::vector<ScriptRecord> scripts;  // inserted scripts stay inert
    parse_html(html, pool, roots, scripts);
    initialize_parsed_nodes(pool);
    for (std::unique_ptr<DomNode>& node : pool) {
        detached_pool_.push_back(std::move(node));
    }
    return roots;
}

DomNode* DocumentHost::make_detached_fragment() {
    auto fragment = std::make_unique<DomNode>();
    fragment->kind = NodeKind::DocumentFragment;
    DomNode* raw = fragment.get();
    detached_pool_.push_back(std::move(fragment));
    return raw;
}

bool DocumentHost::insert_dom_node(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   DomNode* parent, DomNode* child,
                                   DomNode* ref, std::string* error) {
    if (parent == nullptr || child == nullptr) {
        *error = "invalid node";
        return false;
    }
    if (child->kind == NodeKind::Document) {
        *error = "Cannot insert a Document";
        return false;
    }
    if (const char* invalid = document_insertion_error(parent, {child})) {
        *error = invalid;
        return false;
    }
    if (ref != nullptr) {
        if (ref->parent != parent) {
            *error = "reference is not a child of this node";
            return false;
        }
        if (child == ref) {
            return true;
        }
    }
    if (is_self_or_ancestor(child, parent)) {
        *error = "cannot insert a node into its own subtree";
        return false;
    }
    if (child->kind == NodeKind::DocumentFragment) {
        std::vector<DomNode*> moved = child->children;
        if (moved.empty()) {
            return true;
        }
        std::size_t insert_at = parent->children.size();
        if (ref != nullptr) {
            const auto pos =
                std::find(parent->children.begin(), parent->children.end(), ref);
            insert_at = static_cast<std::size_t>(
                std::distance(parent->children.begin(), pos));
        }
        DomNode* previous =
            insert_at > 0 ? parent->children[insert_at - 1] : nullptr;
        DomNode* next = ref;
        for (DomNode* moved_child : moved) {
            moved_child->parent = nullptr;
        }
        child->children.clear();
        std::vector<DomNode*> none;
        queue_child_list(isolate, context, child, none, moved, nullptr,
                         nullptr);
        for (std::size_t k = 0; k < moved.size(); ++k) {
            moved[k]->parent = parent;
            parent->children.insert(
                parent->children.begin() +
                    static_cast<std::ptrdiff_t>(insert_at + k),
                moved[k]);
        }
        queue_child_list(isolate, context, parent, moved, none, previous, next);
        return true;
    }
    DomNode* old_parent = child->parent;
    DomNode* old_previous = nullptr;
    DomNode* old_next = nullptr;
    if (old_parent != nullptr) {
        siblings_of(child, &old_previous, &old_next);
    }
    detach_from_parent(child);
    child->parent = parent;
    if (ref == nullptr) {
        parent->children.push_back(child);
    } else {
        const auto pos =
            std::find(parent->children.begin(), parent->children.end(), ref);
        parent->children.insert(pos, child);
    }
    DomNode* new_previous = nullptr;
    DomNode* new_next = nullptr;
    siblings_of(child, &new_previous, &new_next);
    if (old_parent != nullptr) {
        std::vector<DomNode*> added;
        std::vector<DomNode*> removed = {child};
        queue_child_list(isolate, context, old_parent, added, removed,
                         old_previous, old_next);
    }
    std::vector<DomNode*> added = {child};
    std::vector<DomNode*> removed;
    queue_child_list(isolate, context, parent, added, removed, new_previous,
                     new_next);
    return true;
}

bool DocumentHost::remove_dom_node(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   DomNode* parent, DomNode* child,
                                   std::string* error) {
    if (parent == nullptr || child == nullptr || child->parent != parent) {
        *error = "node is not a child of this node";
        return false;
    }
    DomNode* previous = nullptr;
    DomNode* next = nullptr;
    siblings_of(child, &previous, &next);
    detach_from_parent(child);
    std::vector<DomNode*> added;
    std::vector<DomNode*> removed = {child};
    queue_child_list(isolate, context, parent, added, removed, previous, next);
    return true;
}

bool DocumentHost::replace_dom_node(v8::Isolate* isolate,
                                    v8::Local<v8::Context> context,
                                    DomNode* parent, DomNode* new_child,
                                    DomNode* old_child, std::string* error) {
    if (parent == nullptr || new_child == nullptr || old_child == nullptr ||
        old_child->parent != parent) {
        *error = "oldChild is not a child of this node";
        return false;
    }
    if (const char* invalid = document_insertion_error(parent, {new_child}, old_child)) {
        *error = invalid;
        return false;
    }
    if (new_child == old_child) {
        return true;
    }
    if (is_self_or_ancestor(new_child, parent)) {
        *error = "cannot insert a node into its own subtree";
        return false;
    }
    if (new_child->kind == NodeKind::DocumentFragment) {
        std::vector<DomNode*> moved = new_child->children;
        std::size_t index = 0;
        const auto old_it =
            std::find(parent->children.begin(), parent->children.end(),
                      old_child);
        if (old_it != parent->children.end()) {
            index = static_cast<std::size_t>(
                std::distance(parent->children.begin(), old_it));
        }
        DomNode* old_previous = nullptr;
        DomNode* old_next = nullptr;
        siblings_of(old_child, &old_previous, &old_next);
        if (!moved.empty()) {
            for (DomNode* moved_child : moved) {
                moved_child->parent = nullptr;
            }
            new_child->children.clear();
            std::vector<DomNode*> none;
            queue_child_list(isolate, context, new_child, none, moved, nullptr,
                             nullptr);
        }
        detach_from_parent(old_child);
        std::vector<DomNode*> none;
        std::vector<DomNode*> old_removed = {old_child};
        queue_child_list(isolate, context, parent, none, old_removed,
                         old_previous, old_next);
        if (!moved.empty()) {
            std::size_t insert_at =
                index > parent->children.size() ? parent->children.size()
                                                : index;
            DomNode* previous =
                insert_at > 0 ? parent->children[insert_at - 1] : nullptr;
            DomNode* next = insert_at < parent->children.size()
                                ? parent->children[insert_at]
                                : nullptr;
            for (std::size_t k = 0; k < moved.size(); ++k) {
                moved[k]->parent = parent;
                parent->children.insert(
                    parent->children.begin() +
                        static_cast<std::ptrdiff_t>(insert_at + k),
                    moved[k]);
            }
            queue_child_list(isolate, context, parent, moved, none, previous,
                             next);
        }
        return true;
    }
    if (new_child->parent != nullptr) {
        DomNode* old_parent = new_child->parent;
        DomNode* previous = nullptr;
        DomNode* next = nullptr;
        siblings_of(new_child, &previous, &next);
        detach_from_parent(new_child);
        std::vector<DomNode*> added;
        std::vector<DomNode*> removed = {new_child};
        queue_child_list(isolate, context, old_parent, added, removed,
                         previous, next);
    }
    std::size_t index = 0;
    const auto old_it =
        std::find(parent->children.begin(), parent->children.end(), old_child);
    if (old_it != parent->children.end()) {
        index = static_cast<std::size_t>(
            std::distance(parent->children.begin(), old_it));
    }
    DomNode* old_previous = nullptr;
    DomNode* old_next = nullptr;
    siblings_of(old_child, &old_previous, &old_next);
    detach_from_parent(old_child);
    std::vector<DomNode*> added;
    std::vector<DomNode*> removed = {old_child};
    queue_child_list(isolate, context, parent, added, removed, old_previous,
                     old_next);
    if (index > parent->children.size()) {
        index = parent->children.size();
    }
    new_child->parent = parent;
    parent->children.insert(
        parent->children.begin() + static_cast<std::ptrdiff_t>(index),
        new_child);
    DomNode* new_previous = nullptr;
    DomNode* new_next = nullptr;
    siblings_of(new_child, &new_previous, &new_next);
    added = {new_child};
    removed.clear();
    queue_child_list(isolate, context, parent, added, removed, new_previous,
                     new_next);
    return true;
}

void DocumentHost::replace_all_children(
    v8::Isolate* isolate, v8::Local<v8::Context> context, DomNode* parent,
    const std::vector<DomNode*>& children) {
    if (parent == nullptr) {
        return;
    }
    std::vector<DomNode*> removed = parent->children;
    for (DomNode* child : removed) {
        child->parent = nullptr;
    }
    parent->children.clear();
    for (DomNode* child : children) {
        child->parent = parent;
        parent->children.push_back(child);
    }
    if (!removed.empty() || !children.empty()) {
        queue_child_list(isolate, context, parent, children, removed, nullptr,
                         nullptr);
    }
}

bool DocumentHost::build_argument_fragment(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const v8::FunctionCallbackInfo<v8::Value>& args, DomNode** fragment,
    DomNode* insertion_parent, DomNode* replaced) {
    std::vector<DomNode*> nodes;
    for (int index = 0; index < args.Length(); ++index) {
        auto* host = host_object_backing(args[index]);
        DomNode* node = nullptr;
        if (host != nullptr && host->is_node()) {
            auto* node_host = static_cast<NodeHost*>(host);
            if (node_host->document() != this) {
                isolate->ThrowException(dom_exception(isolate, "WrongDocumentError", "Node belongs to another document"));
                return false;
            }
            node = node_host->node();
            if (node->kind == NodeKind::Document || is_self_or_ancestor(node, insertion_parent)) {
                isolate->ThrowException(dom_exception(isolate, "HierarchyRequestError", "Invalid insertion hierarchy"));
                return false;
            }
        } else {
            v8::Local<v8::String> text;
            if (!args[index]->ToString(context).ToLocal(&text)) return false;
            v8::String::Utf8Value utf8(isolate, text);
            auto created = std::make_unique<DomNode>();
            created->kind = NodeKind::Text;
            if (*utf8 != nullptr) created->data.assign(*utf8, static_cast<std::size_t>(utf8.length()));
            node = created.get();
            detached_pool_.push_back(std::move(created));
        }
        nodes.push_back(node);
    }
    if (const char* invalid = document_insertion_error(insertion_parent, nodes, replaced)) {
        isolate->ThrowException(dom_exception(isolate, "HierarchyRequestError", invalid));
        return false;
    }
    auto* result = make_detached_fragment();
    for (auto* node : nodes) {
        std::string error;
        if (!insert_dom_node(isolate, context, result, node, nullptr, &error)) {
            isolate->ThrowException(v8::Exception::TypeError(v8_string(isolate, error).As<v8::String>()));
            return false;
        }
    }
    *fragment = result;
    return true;
}

v8::Local<v8::Value> DocumentHost::call_mutation_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, DomNode* target,
    const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& args) {
    const auto throw_type_error = [&](const std::string& message) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8_string(isolate, message).As<v8::String>()));
        return v8::Undefined(isolate);
    };
    if (name == "before" || name == "after" || name == "replaceWith") {
        if (target->parent == nullptr) {
            return v8::Undefined(isolate);  // ChildNode methods: no parent, no-op
        }
    }
    const bool child_operation = name == "before" || name == "after" || name == "replaceWith";
    DomNode* insertion_parent = child_operation ? target->parent : target;
    DomNode* reference = nullptr;
    if (child_operation) {
        std::unordered_set<DomNode*> moving;
        for (int index = 0; index < args.Length(); ++index) {
            auto* node = NodeHost::node_of_arg(args[index]);
            if (node == nullptr) continue;
            if (node->kind == NodeKind::DocumentFragment) moving.insert(node->children.begin(), node->children.end());
            else moving.insert(node);
        }
        reference = name == "before" ? target : sibling_node(target, 1);
        while (reference != nullptr && moving.contains(reference)) reference = sibling_node(reference, 1);
    }
    DomNode* fragment = nullptr;
    if (!build_argument_fragment(isolate, context, args, &fragment, insertion_parent,
            name == "replaceWith" ? target : nullptr)) {
        return v8::Undefined(isolate);
    }
    if (name == "append" || name == "prepend") {
        DomNode* ref = nullptr;
        if (name == "prepend" && !target->children.empty()) {
            ref = target->children.front();
        }
        std::string error;
        if (!insert_dom_node(isolate, context, target, fragment, ref, &error)) {
            return throw_type_error(error);
        }
        return v8::Undefined(isolate);
    }
    if (name == "replaceChildren") {
        std::vector<DomNode*> moved = fragment->children;
        fragment->children.clear();
        for (DomNode* child : moved) {
            child->parent = nullptr;
        }
        replace_all_children(isolate, context, target, moved);
        return v8::Undefined(isolate);
    }
    std::string error;
    const bool replacing = name == "replaceWith" && target->parent == insertion_parent;
    const bool changed = replacing
        ? replace_dom_node(isolate, context, insertion_parent, fragment, target, &error)
        : insert_dom_node(isolate, context, insertion_parent, fragment, reference, &error);
    if (!changed) return throw_type_error(error);

    return v8::Undefined(isolate);
}

v8::Local<v8::Value> DocumentHost::call_insert_adjacent(
    v8::Isolate* isolate, v8::Local<v8::Context> context, DomNode* target,
    const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& args) {
    const auto syntax_error = [&](const char* message) {
        isolate->ThrowException(v8::Exception::SyntaxError(
            v8_string(isolate, message).As<v8::String>()));
        return v8::Undefined(isolate);
    };
    const auto type_error = [&](const std::string& message) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8_string(isolate, message).As<v8::String>()));
        return v8::Undefined(isolate);
    };
    std::string position;
    if (args.Length() > 0) {
        v8::String::Utf8Value utf8(isolate, args[0]);
        if (*utf8 != nullptr) {
            position.assign(*utf8, static_cast<std::size_t>(utf8.length()));
        }
    }
    if (position != "beforebegin" && position != "afterbegin" &&
        position != "beforeend" && position != "afterend") {
        return syntax_error("insertAdjacent: invalid position");
    }
    const bool needs_parent =
        position == "beforebegin" || position == "afterend";
    DomNode* parent = needs_parent ? target->parent : target;
    if (parent == nullptr) {
        return type_error("insertAdjacent: element has no parent");
    }
    if (parent->kind == NodeKind::Document && name == "insertAdjacentHTML") {
        isolate->ThrowException(dom_exception(isolate, "NoModificationAllowedError", "Cannot parse HTML beside the document element"));
        return v8::Undefined(isolate);
    }
    if (name == "insertAdjacentElement" && args.Length() > 1) {
        auto* host = host_object_backing(args[1]);
        if (host != nullptr && host->is_node() && static_cast<NodeHost*>(host)->document() != this) {
            isolate->ThrowException(dom_exception(isolate, "WrongDocumentError", "Node belongs to another document"));
            return v8::Undefined(isolate);
        }
    }
    DomNode* node = nullptr;
    if (name == "insertAdjacentHTML") {
        std::string html;
        if (args.Length() > 1) {
            v8::String::Utf8Value utf8(isolate, args[1]);
            if (*utf8 != nullptr) {
                html.assign(*utf8, static_cast<std::size_t>(utf8.length()));
            }
        }
        std::vector<DomNode*> roots = parse_html_fragment(html);
        DomNode* fragment = make_detached_fragment();
        for (DomNode* root : roots) {
            root->parent = fragment;
            fragment->children.push_back(root);
        }
        node = fragment;
    } else if (name == "insertAdjacentElement") {
        HostObject* host = args.Length() > 1 ? host_object_backing(args[1])
                                             : nullptr;
        if (host == nullptr || !host->is_node() ||
            host->node_kind() != NodeKind::Element) {
            return type_error("insertAdjacentElement: argument is not an element");
        }
        node = static_cast<NodeHost*>(host)->node();
    } else {  // insertAdjacentText
        std::string text;
        if (args.Length() > 1) {
            v8::Local<v8::String> str;
            if (!args[1]->ToString(context).ToLocal(&str)) {
                return v8::Undefined(isolate);
            }
            v8::String::Utf8Value utf8(isolate, str);
            if (*utf8 != nullptr) {
                text.assign(*utf8, static_cast<std::size_t>(utf8.length()));
            }
        }
        auto text_node = std::make_unique<DomNode>();
        text_node->kind = NodeKind::Text;
        text_node->data = text;
        node = text_node.get();
        detached_pool_.push_back(std::move(text_node));
    }
    DomNode* ref = nullptr;
    if (position == "beforebegin") {
        ref = target;
    } else if (position == "afterbegin") {
        ref = target->children.empty() ? nullptr : target->children.front();
    } else if (position == "beforeend") {
        ref = nullptr;
    } else {  // afterend
        ref = sibling_node(target, 1);
    }
    std::string error;
    if (!insert_dom_node(isolate, context, parent, node, ref, &error)) {
        if (parent->kind == NodeKind::Document) {
            isolate->ThrowException(dom_exception(isolate, "HierarchyRequestError", error));
            return v8::Undefined(isolate);
        }
        return type_error(error);
    }
    if (name == "insertAdjacentElement") {
        return args[1];
    }
    return v8::Undefined(isolate);
}

bool DocumentHost::call_node_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, DomNode* target,
    const std::string& name, const v8::FunctionCallbackInfo<v8::Value>& args,
    v8::Local<v8::Value>& out) {
    const auto type_error = [&](const char* message) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8_string(isolate, message).As<v8::String>()));
        out = v8::Undefined(isolate);
        return true;
    };
    const auto arg = [&](int index) {
        return args.Length() > index ? args[index] : v8::Local<v8::Value>();
    };
    if (name == "hasChildNodes") {
        out = v8::Boolean::New(isolate, !target->children.empty());
        return true;
    }
    if (name == "cloneNode") {
        const bool deep = args.Length() > 0 && args[0]->BooleanValue(isolate);
        out = wrap_node(isolate, context, clone_detached(target, deep));
        return true;
    }
    if (name == "isSameNode" || name == "isEqualNode") {
        DomNode* other = NodeHost::node_of_arg(arg(0));
        if (other == nullptr) {
            out = v8::False(isolate);
            return true;
        }
        out = v8::Boolean::New(
            isolate, name == "isSameNode" ? other == target
                                          : nodes_equal(target, other));
        return true;
    }
    if (name == "contains") {
        DomNode* other = NodeHost::node_of_arg(arg(0));
        bool result = false;
        for (DomNode* n = other; n != nullptr; n = n->parent) {
            if (n == target) {
                result = true;
                break;
            }
        }
        out = v8::Boolean::New(isolate, result);
        return true;
    }
    if (name == "remove") {
        DomNode* parent = target->parent;
        if (parent != nullptr) {
            DomNode* previous = nullptr;
            DomNode* next = nullptr;
            siblings_of(target, &previous, &next);
            detach_from_parent(target);
            std::vector<DomNode*> added;
            std::vector<DomNode*> removed = {target};
            queue_child_list(isolate, context, parent, added, removed, previous,
                             next);
        }
        out = v8::Undefined(isolate);
        return true;
    }
    if (name != "appendChild" && name != "removeChild" &&
        name != "insertBefore" && name != "replaceChild") {
        return false;
    }

    const auto dom_error = [&](const char* error_name, const char* message) {
        auto error = v8::Exception::Error(v8_string(isolate, message).As<v8::String>());
        (void)error.As<v8::Object>()->Set(context, v8_string(isolate, "name"), v8_string(isolate, error_name));
        isolate->ThrowException(error);
        out = v8::Undefined(isolate);
        return true;
    };
    if (target->kind == NodeKind::Text || target->kind == NodeKind::Comment) {
        return dom_error(name == "removeChild" || name == "replaceChild" ? "NotFoundError" : "HierarchyRequestError",
            "CharacterData nodes cannot have children");
    }
    if (auto* child_host = host_object_backing(arg(0)); child_host != nullptr && child_host->is_node() &&
        static_cast<NodeHost*>(child_host)->document() != this) {
        return dom_error("WrongDocumentError", "Node belongs to another document");
    }
    DomNode* child = NodeHost::node_of_arg(arg(0));
    if (child != nullptr && child->kind == NodeKind::Document)
        return dom_error("HierarchyRequestError", "Cannot insert a Document");
    if (name == "replaceChild") {
        DomNode* old_child = NodeHost::node_of_arg(arg(1));
        if (child == nullptr || old_child == nullptr) {
            return type_error("replaceChild: arguments must be nodes");
        }
        if (old_child->parent != target) {
            return type_error(
                "replaceChild: oldChild is not a child of this node");
        }
        if (child == old_child) {
            out = args[1];
            return true;
        }
        if (is_self_or_ancestor(child, target)) {
            return type_error(
                "replaceChild: cannot insert a node into its own subtree");
        }
        if (child->kind == NodeKind::DocumentFragment) {
            const auto old_it =
                std::find(target->children.begin(), target->children.end(),
                          old_child);
            const std::size_t index = static_cast<std::size_t>(
                std::distance(target->children.begin(), old_it));
            std::vector<DomNode*> moved = child->children;
            DomNode* old_previous = nullptr;
            DomNode* old_next = nullptr;
            siblings_of(old_child, &old_previous, &old_next);
            if (!moved.empty()) {
                for (DomNode* moved_child : moved) {
                    moved_child->parent = nullptr;
                }
                child->children.clear();
                std::vector<DomNode*> none;
                queue_child_list(isolate, context, child, none, moved, nullptr,
                                 nullptr);
            }
            detach_from_parent(old_child);
            std::vector<DomNode*> none;
            std::vector<DomNode*> old_removed = {old_child};
            queue_child_list(isolate, context, target, none, old_removed,
                             old_previous, old_next);
            if (!moved.empty()) {
                std::size_t insert_at =
                    index > target->children.size() ? target->children.size()
                                                    : index;
                DomNode* previous =
                    insert_at > 0 ? target->children[insert_at - 1] : nullptr;
                DomNode* next = insert_at < target->children.size()
                                    ? target->children[insert_at]
                                    : nullptr;
                for (std::size_t k = 0; k < moved.size(); ++k) {
                    moved[k]->parent = target;
                    target->children.insert(
                        target->children.begin() +
                            static_cast<std::ptrdiff_t>(insert_at + k),
                        moved[k]);
                }
                queue_child_list(isolate, context, target, moved, none, previous,
                                 next);
            }
            out = args[1];
            return true;
        }
        if (child->parent != nullptr) {
            DomNode* old_parent = child->parent;
            DomNode* previous = nullptr;
            DomNode* next = nullptr;
            siblings_of(child, &previous, &next);
            detach_from_parent(child);
            std::vector<DomNode*> added;
            std::vector<DomNode*> removed = {child};
            queue_child_list(isolate, context, old_parent, added, removed,
                             previous, next);
        }
        DomNode* old_previous = nullptr;
        DomNode* old_next = nullptr;
        siblings_of(old_child, &old_previous, &old_next);
        std::size_t index = 0;
        const auto old_it =
            std::find(target->children.begin(), target->children.end(),
                      old_child);
        if (old_it != target->children.end()) {
            index = static_cast<std::size_t>(
                std::distance(target->children.begin(), old_it));
        }
        detach_from_parent(old_child);
        std::vector<DomNode*> added;
        std::vector<DomNode*> removed = {old_child};
        queue_child_list(isolate, context, target, added, removed, old_previous,
                         old_next);
        if (index > target->children.size()) {
            index = target->children.size();
        }
        child->parent = target;
        target->children.insert(
            target->children.begin() + static_cast<std::ptrdiff_t>(index),
            child);
        DomNode* new_previous = nullptr;
        DomNode* new_next = nullptr;
        siblings_of(child, &new_previous, &new_next);
        added = {child};
        removed.clear();
        queue_child_list(isolate, context, target, added, removed, new_previous,
                         new_next);
        out = args[1];
        return true;
    }

    if (child == nullptr) {
        return type_error("argument is not a node");
    }
    if (name == "removeChild") {
        if (child->parent != target) {
            return type_error("removeChild: not a child of this node");
        }
        DomNode* previous = nullptr;
        DomNode* next = nullptr;
        siblings_of(child, &previous, &next);
        detach_from_parent(child);
        std::vector<DomNode*> added;
        std::vector<DomNode*> removed = {child};
        queue_child_list(isolate, context, target, added, removed, previous,
                         next);
        out = args[0];
        return true;
    }
    // appendChild / insertBefore: resolve ref (insertBefore only; null = end).
    DomNode* ref = nullptr;
    if (name == "insertBefore") {
        const bool ref_is_null =
            args.Length() > 1 && (args[1]->IsNull() || args[1]->IsUndefined());
        if (!ref_is_null) {
            ref = NodeHost::node_of_arg(arg(1));
            if (ref == nullptr) {
                return type_error("insertBefore: ref is not a node");
            }
            if (ref->parent != target) {
                return type_error(
                    "insertBefore: ref is not a child of this node");
            }
            if (child == ref) {
                out = args[0];  // stable no-op
                return true;
            }
        }
    }
    if (is_self_or_ancestor(child, target)) {
        return type_error("cannot insert a node into its own subtree");
    }
    if (child->kind == NodeKind::DocumentFragment) {
        std::vector<DomNode*> moved = child->children;
        std::size_t insert_at = target->children.size();
        if (ref != nullptr) {
            const auto pos =
                std::find(target->children.begin(), target->children.end(),
                          ref);
            insert_at = static_cast<std::size_t>(
                std::distance(target->children.begin(), pos));
        }
        if (moved.empty()) {
            out = args[0];
            return true;
        }
        DomNode* previous =
            insert_at > 0 ? target->children[insert_at - 1] : nullptr;
        DomNode* next = ref;
        for (DomNode* moved_child : moved) {
            moved_child->parent = nullptr;
        }
        child->children.clear();
        std::vector<DomNode*> none;
        queue_child_list(isolate, context, child, none, moved, nullptr,
                         nullptr);
        for (std::size_t k = 0; k < moved.size(); ++k) {
            moved[k]->parent = target;
            target->children.insert(
                target->children.begin() +
                    static_cast<std::ptrdiff_t>(insert_at + k),
                moved[k]);
        }
        queue_child_list(isolate, context, target, moved, none, previous, next);
        out = args[0];
        return true;
    }
    DomNode* old_parent = child->parent;
    DomNode* old_previous = nullptr;
    DomNode* old_next = nullptr;
    if (old_parent != nullptr) {
        siblings_of(child, &old_previous, &old_next);
    }
    detach_from_parent(child);
    child->parent = target;
    if (ref == nullptr) {
        target->children.push_back(child);  // append
    } else {
        const auto pos =
            std::find(target->children.begin(), target->children.end(), ref);
        target->children.insert(pos, child);
    }
    DomNode* new_previous = nullptr;
    DomNode* new_next = nullptr;
    siblings_of(child, &new_previous, &new_next);
    if (old_parent != nullptr) {
        std::vector<DomNode*> added;
        std::vector<DomNode*> removed = {child};
        queue_child_list(isolate, context, old_parent, added, removed,
                         old_previous, old_next);
    }
    std::vector<DomNode*> added = {child};
    std::vector<DomNode*> removed;
    queue_child_list(isolate, context, target, added, removed, new_previous,
                     new_next);
    out = args[0];
    return true;
}

}  // namespace iv8::browser
