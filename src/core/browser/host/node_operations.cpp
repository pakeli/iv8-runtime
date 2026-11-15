#include "node_operations.h"

#include <algorithm>
#include <functional>
#include <memory>
#include <string>
#include <unordered_set>
#include <utility>
#include <vector>

#include "attribute_hosts.h"
#include "document_host.h"

namespace iv8::browser {

namespace {

void throw_node_error(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      const char* name, const char* message) {
    v8::Local<v8::String> text;
    v8::Local<v8::String> error_name;
    if (!v8::String::NewFromUtf8(isolate, message).ToLocal(&text) ||
        !v8::String::NewFromUtf8(isolate, name).ToLocal(&error_name)) return;
    if (std::string(name) == "TypeError") {
        isolate->ThrowException(v8::Exception::TypeError(text));
        return;
    }
    auto error = v8::Exception::Error(text);
    if (!error.As<v8::Object>()->CreateDataProperty(context,
            v8::String::NewFromUtf8Literal(isolate, "name"), error_name).FromMaybe(false)) return;
    isolate->ThrowException(error);
}

template <typename Value, typename Predicate>
void move_owned_values(std::vector<std::unique_ptr<Value>>& source,
                       std::vector<std::unique_ptr<Value>>& target,
                       Predicate selected) {
    for (auto value = source.begin(); value != source.end();) {
        if (selected(value->get())) {
            target.push_back(std::move(*value));
            value = source.erase(value);
        } else {
            ++value;
        }
    }
}

}  // namespace

DomNode* node_root(DomNode* node) {
    if (node == nullptr) return nullptr;
    while (node->parent != nullptr) node = node->parent;
    return node;
}

std::uint16_t compare_node_position(const DomNode* node, const DomNode* other) {
    if (node == other) return 0;
    const DomNode* first_root = node;
    const DomNode* second_root = other;
    std::size_t first_depth = 0;
    std::size_t second_depth = 0;
    while (first_root->parent != nullptr) { first_root = first_root->parent; ++first_depth; }
    while (second_root->parent != nullptr) { second_root = second_root->parent; ++second_depth; }
    if (first_root != second_root) {
        return static_cast<std::uint16_t>(1 | 32 |
            (std::less<const DomNode*>{}(first_root, second_root) ? 4 : 2));
    }
    if (is_self_or_ancestor(node, other)) return 4 | 16;
    if (is_self_or_ancestor(other, node)) return 2 | 8;
    const DomNode* first_branch = node;
    const DomNode* second_branch = other;
    while (first_depth > second_depth) { first_branch = first_branch->parent; --first_depth; }
    while (second_depth > first_depth) { second_branch = second_branch->parent; --second_depth; }
    while (first_branch->parent != second_branch->parent) {
        first_branch = first_branch->parent;
        second_branch = second_branch->parent;
    }
    for (const DomNode* sibling : first_branch->parent->children) {
        if (sibling == first_branch) return 4;
        if (sibling == second_branch) return 2;
    }
    return 0;
}

void DocumentHost::remember_node_owner(DomNode* node, DocumentHost* owner) {
    if (node == nullptr || owner == nullptr) return;
    if (owner == this) forwarded_node_owners_.erase(node);
    else forwarded_node_owners_[node] = owner;
}

DocumentHost* DocumentHost::forwarded_node_owner(DomNode* node) const {
    auto found = forwarded_node_owners_.find(node);
    if (found == forwarded_node_owners_.end()) return nullptr;
    DocumentHost* owner = found->second;
    for (int depth = 0; depth < 1024; ++depth) {
        const auto next = owner->forwarded_node_owners_.find(node);
        if (next == owner->forwarded_node_owners_.end() || next->second == owner) return owner;
        owner = next->second;
    }
    return owner;
}

void DocumentHost::borrow_mutation_observer(MutationObserverState* observer) {
    if (observer == nullptr || observer->owner == this) return;
    if (std::find(borrowed_observers_.begin(), borrowed_observers_.end(), observer) == borrowed_observers_.end()) {
        borrowed_observers_.push_back(observer);
    }
}

std::vector<MutationObserverState*> DocumentHost::mutation_observer_states() const {
    std::vector<MutationObserverState*> states;
    states.reserve(observers_.size() + borrowed_observers_.size());
    for (const auto& observer : observers_) states.push_back(observer.get());
    for (auto* observer : borrowed_observers_) {
        if (std::find(states.begin(), states.end(), observer) == states.end()) states.push_back(observer);
    }
    return states;
}

void DocumentHost::remember_record_owners(MutationObserverState* observer,
                                         const MutationRecordData& record) {
    if (observer == nullptr || observer->owner == nullptr || observer->owner == this) return;
    auto* owner = observer->owner;
    owner->remember_node_owner(record.target, this);
    owner->remember_node_owner(record.previous_sibling, this);
    owner->remember_node_owner(record.next_sibling, this);
    for (auto* node : record.added) owner->remember_node_owner(node, this);
    for (auto* node : record.removed) owner->remember_node_owner(node, this);
}

bool DocumentHost::normalize_node(v8::Isolate* isolate,
                                  v8::Local<v8::Context> context, DomNode* node) {
    for (std::size_t index = 0; index < node->children.size();) {
        DomNode* child = node->children[index];
        if (child->kind != NodeKind::Text) {
            if (!normalize_node(isolate, context, child)) return false;
            ++index;
            continue;
        }
        if (child->data.empty()) {
            std::string error;
            if (!remove_dom_node(isolate, context, node, child, &error)) return false;
            continue;
        }
        while (index + 1 < node->children.size() &&
               node->children[index + 1]->kind == NodeKind::Text) {
            DomNode* following = node->children[index + 1];
            if (!following->data.empty()) {
                const auto old_value = child->data;
                child->data += following->data;
                queue_character_data(isolate, context, child, old_value);
            }
            std::string error;
            if (!remove_dom_node(isolate, context, node, following, &error)) return false;
        }
        ++index;
    }
    return true;
}

v8::MaybeLocal<v8::Value> DocumentHost::adopt_node(v8::Isolate* isolate,
    v8::Local<v8::Context> context, NodeHost* source_host) {
    auto* source = source_host->document();
    auto* node = source_host->node();
    if (node->kind == NodeKind::Document) {
        throw_node_error(isolate, context, "NotSupportedError", "A Document cannot be adopted");
        return {};
    }
    if (source == nullptr || source->page_ != page_) {
        throw_node_error(isolate, context, "WrongDocumentError", "Node belongs to another Page");
        return {};
    }
    std::vector<DomNode*> subtree = {node};
    for (std::size_t index = 0; index < subtree.size(); ++index) {
        subtree.insert(subtree.end(), subtree[index]->children.begin(), subtree[index]->children.end());
    }
    const std::unordered_set<DomNode*> moved(subtree.begin(), subtree.end());
    if (node->parent != nullptr) {
        std::string error;
        if (!source->remove_dom_node(isolate, context, node->parent, node, &error)) return {};
    }
    if (source == this) return wrap_node(isolate, context, node);

    // All documents in a Page share one isolate/generation and operation guard.
    // Move native ownership, retaining each original HostObject and JS wrapper.
    move_owned_values(source->pool_, detached_pool_, [&](DomNode* value) { return moved.contains(value); });
    move_owned_values(source->detached_pool_, detached_pool_, [&](DomNode* value) { return moved.contains(value); });
    move_owned_values(source->node_hosts_, node_hosts_, [&](NodeHost* host) {
        if (!moved.contains(host->node())) return false;
        host->set_document_owner(this);
        if (host->node_kind() == NodeKind::Element) {
            static_cast<ElementHost*>(host)->set_html_document_mode(html_document_);
        }
        return true;
    });
    for (auto* child : subtree) {
        remember_node_owner(child, this);
        source->remember_node_owner(child, this);
        auto host = source->wrappers_.extract(child);
        if (!host.empty()) wrappers_.insert(std::move(host));
        auto wrapper = source->node_objects_.extract(child);
        if (!wrapper.empty()) node_objects_.insert(std::move(wrapper));
    }
    move_owned_values(source->collections_, collections_, [&](LiveCollectionBinding* binding) {
        if (binding->root == nullptr || !moved.contains(binding->root)) return false;
        binding->document = this;
        return true;
    });
    move_owned_values(source->class_list_bindings_, class_list_bindings_, [&](ClassListBinding* binding) {
        if (!moved.contains(binding->node)) return false;
        binding->document = this;
        return true;
    });
    transfer_attribute_wrappers(source, this, subtree);
    for (auto* observer : source->mutation_observer_states()) {
        const bool follows = std::any_of(observer->registrations.begin(), observer->registrations.end(),
            [&](const MutationObserverRegistration& registration) {
                return !registration.target_is_document && moved.contains(registration.target);
            });
        if (follows) borrow_mutation_observer(observer);
    }
    if (moved.contains(source->title_node_)) {
        source->title_node_ = source->find_tag("title");
        source->title_ = source->title_node_ == nullptr ? std::string() : text_content_of(source->title_node_);
    }
    return wrap_node(isolate, context, node);
}

bool DocumentHost::call_node_operation(v8::Isolate* isolate,
    v8::Local<v8::Context> context, DomNode* target, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& args, v8::Local<v8::Value>& out) {
    if (name == "getRootNode") {
        if (args.Length() > 0 && args[0]->IsObject()) {
            v8::Local<v8::Value> composed;
            if (!args[0].As<v8::Object>()->Get(context,
                    v8::String::NewFromUtf8Literal(isolate, "composed")).ToLocal(&composed)) {
                out = v8::Undefined(isolate);
                return true;
            }
        }
        out = wrap_node(isolate, context, node_root(target));
        return true;
    }
    if (name == "normalize") {
        (void)normalize_node(isolate, context, target);
        out = v8::Undefined(isolate);
        return true;
    }
    if (name != "compareDocumentPosition" && name != "importNode" && name != "adoptNode") return false;
    out = v8::Undefined(isolate);
    if (name != "compareDocumentPosition" && target->kind != NodeKind::Document) return false;
    if (name != "compareDocumentPosition" && handle_attribute_node_operation(isolate, context, this, name, args, out)) return true;
    auto* backing = args.Length() > 0 ? host_object_backing(args[0]) : nullptr;
    if (backing == nullptr || !backing->is_node()) {
        throw_node_error(isolate, context, "TypeError", "A Node argument is required");
        return true;
    }
    auto* other = static_cast<NodeHost*>(backing);
    v8::Local<v8::Context> creation_context;
    if (!args[0].As<v8::Object>()->GetCreationContext().ToLocal(&creation_context) || creation_context != context ||
        other->document()->page_state() != page_) {
        throw_node_error(isolate, context, "WrongDocumentError", "Node belongs to another context");
        return true;
    }
    if (name == "compareDocumentPosition") {
        out = v8::Integer::NewFromUnsigned(isolate, compare_node_position(target, other->node()));
        return true;
    }
    if (other->node()->kind == NodeKind::Document) {
        throw_node_error(isolate, context, "NotSupportedError", "A Document cannot be imported or adopted");
        return true;
    }
    if (name == "importNode") {
        const bool deep = args.Length() > 1 && args[1]->BooleanValue(isolate);
        out = wrap_node(isolate, context, clone_detached(other->node(), deep));
    } else {
        (void)adopt_node(isolate, context, other).ToLocal(&out);
    }
    return true;
}

}  // namespace iv8::browser
