#include "live_collection.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "iv8/web_method.h"
#include "../dom/dom_query.h"
#include "../dom/selector_match.h"
#include "../internal/trace_hooks.h"
#include "document_host.h"

namespace iv8::browser {

std::vector<DomNode*> DocumentHost::collection_nodes(const LiveCollectionBinding& binding) const {
    const auto tokens = split_class_tokens(binding.query);
    const auto predicate = binding.kind == "tag" ? tag_predicate(binding.query) :
        std::function<bool(const DomNode*)>([tokens](const DomNode* node) {
            return !tokens.empty() && has_all_class_tokens(node, tokens);
        });
    if (binding.root == nullptr) return collect(predicate);
    std::vector<DomNode*> nodes;
    for (auto child : binding.root->children) collect_matching(child, predicate, nodes);
    return nodes;
}

LiveCollectionBinding* collection_binding(v8::Local<v8::Object> object) {
    if (object->InternalFieldCount() != 2) return nullptr;
    return static_cast<LiveCollectionBinding*>(object->GetAlignedPointerFromInternalField(0, v8::kEmbedderDataTypeTagDefault));
}

v8::Intercepted collection_index(std::uint32_t index,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
    auto binding = collection_binding(info.Holder());
    if (binding == nullptr) return v8::Intercepted::kNo;
    auto nodes = binding->document->collection_nodes(*binding);
    if (index >= nodes.size()) return v8::Intercepted::kNo;
    info.GetReturnValue().Set(binding->document->wrap_node(info.GetIsolate(),
        info.GetIsolate()->GetCurrentContext(), nodes[index]));
    return v8::Intercepted::kYes;
}

v8::Intercepted collection_index_query(std::uint32_t index,
    const v8::PropertyCallbackInfo<v8::Integer>& info) {
    auto binding = collection_binding(info.Holder());
    if (binding == nullptr || index >= binding->document->collection_nodes(*binding).size())
        return v8::Intercepted::kNo;
    info.GetReturnValue().Set(static_cast<int>(v8::ReadOnly));
    return v8::Intercepted::kYes;
}

void collection_indices(const v8::PropertyCallbackInfo<v8::Array>& info) {
    auto binding = collection_binding(info.Holder());
    if (binding == nullptr) return;
    auto nodes = binding->document->collection_nodes(*binding);
    auto array = v8::Array::New(info.GetIsolate(), static_cast<int>(nodes.size()));
    for (std::uint32_t index = 0; index < nodes.size(); ++index)
        (void)array->Set(info.GetIsolate()->GetCurrentContext(), index,
            v8::Integer::NewFromUnsigned(info.GetIsolate(), index));
    info.GetReturnValue().Set(array);
}

DomNode* collection_named(LiveCollectionBinding* binding, const std::string& name) {
    if (name.empty()) return nullptr;
    for (auto node : binding->document->collection_nodes(*binding)) {
        auto found = node->attributes.find("name");
        if (node->id == name || (found != node->attributes.end() && found->second == name))
            return node;
    }
    return nullptr;
}

v8::Intercepted collection_name(v8::Local<v8::Name> name,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (!name->IsString()) return v8::Intercepted::kNo;
    auto binding = collection_binding(info.Holder());
    if (binding == nullptr) return v8::Intercepted::kNo;
    v8::String::Utf8Value text(info.GetIsolate(), name);
    auto node = collection_named(binding, *text != nullptr ? *text : "");
    if (node == nullptr) return v8::Intercepted::kNo;
    info.GetReturnValue().Set(binding->document->wrap_node(info.GetIsolate(),
        info.GetIsolate()->GetCurrentContext(), node));
    return v8::Intercepted::kYes;
}

void collection_length(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto binding = collection_binding(info.This());
    if (binding == nullptr) {
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(info.GetIsolate(), "Illegal invocation")));
        return;
    }
    info.GetReturnValue().Set(static_cast<std::uint32_t>(
        binding->document->collection_nodes(*binding).size()));
}

void collection_item(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto binding = collection_binding(info.This());
    if (binding == nullptr) {
        isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(isolate, "Illegal invocation")));
        return;
    }
    v8::Local<v8::Value> argument = info.Length() ? info[0] :
        v8::Local<v8::Value>(v8::Undefined(isolate));
    DomNode* node = nullptr;
    const bool named = info.Data()->IsTrue();
    if (named) {
        v8::Local<v8::String> name;
        if (!argument->ToString(context).ToLocal(&name)) return;
        v8::String::Utf8Value text(isolate, name);
        node = collection_named(binding, *text != nullptr ? *text : "");
    } else {
        auto index = argument->Uint32Value(context);
        if (index.IsNothing()) return;
        auto nodes = binding->document->collection_nodes(*binding);
        if (index.FromJust() < nodes.size()) node = nodes[index.FromJust()];
    }
    info.GetReturnValue().Set(binding->document->wrap_node(isolate, context, node));
}

void all_collection_call(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto binding = static_cast<LiveCollectionBinding*>(info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    if (info.Length() == 0) { info.GetReturnValue().Set(v8::Null(isolate)); return; }
    v8::Local<v8::String> value;
    if (!info[0]->ToString(context).ToLocal(&value)) return;
    v8::String::Utf8Value name(isolate, value);
    DomNode* node = collection_named(binding, *name != nullptr ? *name : "");
    info.GetReturnValue().Set(binding->document->wrap_node(isolate, context, node));
}

v8::Local<v8::Object> DocumentHost::live_collection(v8::Isolate* isolate,
    v8::Local<v8::Context> context, DomNode* root, const std::string& kind,
    const std::string& query, bool all) {
    auto binding = std::make_unique<LiveCollectionBinding>(LiveCollectionBinding{this, root, kind, query});
    auto raw = binding.get();
    collections_.push_back(std::move(binding));
    auto object_template = v8::ObjectTemplate::New(isolate);
    object_template->SetInternalFieldCount(2); // Distinct from a HostObject wrapper.
    object_template->SetHandler(v8::IndexedPropertyHandlerConfiguration(
        &collection_index, nullptr, &collection_index_query, nullptr, &collection_indices));
    object_template->SetHandler(v8::NamedPropertyHandlerConfiguration(&collection_name,
        nullptr, nullptr, nullptr, nullptr, v8::Local<v8::Value>(),
        v8::PropertyHandlerFlags::kNonMasking));
    if (all) {
        object_template->MarkAsUndetectable();
        object_template->SetCallAsFunctionHandler(&all_collection_call,
            v8::External::New(isolate, raw, v8::kExternalPointerTypeTagDefault));
    }
    v8::Local<v8::Object> object;
    if (!object_template->NewInstance(context).ToLocal(&object)) return v8::Object::New(isolate);
    object->SetAlignedPointerInInternalField(0, raw, v8::kEmbedderDataTypeTagDefault);
    auto cache_key = v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate,
        "iv8.collection.prototypes"));
    v8::Local<v8::Value> cached;
    if (!context->Global()->GetPrivate(context, cache_key).ToLocal(&cached)) return object;
    v8::Local<v8::Object> prototypes = cached->IsObject() ? cached.As<v8::Object>() : v8::Object::New(isolate);
    (void)context->Global()->SetPrivate(context, cache_key, prototypes);
    const auto class_name = v8_string(isolate, all ? "HTMLAllCollection" : "HTMLCollection").As<v8::String>();
    if (!prototypes->Get(context, class_name).ToLocal(&cached)) return object;
    v8::Local<v8::Object> prototype;
    if (cached->IsObject()) prototype = cached.As<v8::Object>();
    else {
        prototype = v8::Object::New(isolate);
        v8::Local<v8::Function> getter;
        if (!v8::Function::New(context, &collection_length).ToLocal(&getter)) return object;
        v8::PropertyDescriptor length(getter, v8::Undefined(isolate));
        length.set_enumerable(true); length.set_configurable(true);
        (void)prototype->DefineProperty(context, v8::String::NewFromUtf8Literal(isolate, "length"), length);
        for (const auto& name : {"item", "namedItem"}) {
            v8::Local<v8::Function> method;
            if (!v8::Function::New(context, &collection_item,
                    v8::Boolean::New(isolate, std::string(name) == "namedItem")).ToLocal(&method)) return object;
            (void)prototype->Set(context, v8_string(isolate, name), method);
        }
        (void)prototype->DefineOwnProperty(context, v8::Symbol::GetToStringTag(isolate),
            class_name, static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum));
        v8::Local<v8::Value> iterator;
        if (v8::Array::New(isolate)->Get(context, v8::Symbol::GetIterator(isolate)).ToLocal(&iterator))
            (void)prototype->DefineOwnProperty(context, v8::Symbol::GetIterator(isolate), iterator, v8::DontEnum);
        (void)prototypes->Set(context, class_name, prototype);
    }
    (void)object->SetPrototype(context, prototype);
    return object;
}

}  // namespace iv8::browser
