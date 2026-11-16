#include "live_collection.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "iv8/access_summary.h"
#include "iv8/web_method.h"
#include "../dom/dom_query.h"
#include "../dom/selector_match.h"
#include "../internal/trace_hooks.h"
#include "../window/window_trace_support.h"
#include "document_host.h"

namespace iv8::browser {

std::vector<DomNode*> DocumentHost::collection_nodes(const LiveCollectionBinding& binding) const {
    if (binding.kind == "window-name" || binding.kind == "form-name") {
        return named_collection_nodes(binding);
    }
    if (binding.kind == "tag-ns") {
        const auto predicate = [&binding](const DomNode* node) {
            return node->kind == NodeKind::Element &&
                (binding.namespace_uri == "*" || binding.namespace_uri == node->namespace_uri) &&
                (binding.query == "*" || binding.query == (node->local_name.empty() ? node->tag : node->local_name));
        };
        if (binding.root == nullptr) return collect(predicate);
        std::vector<DomNode*> nodes;
        for (auto* child : binding.root->children) collect_matching(child, predicate, nodes);
        return nodes;
    }
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

namespace {

const char* collection_label(v8::Local<v8::Object> object) {
    return object->InternalFieldCount() == 2 && object->GetInternalField(1).As<v8::Value>()->IsTrue()
        ? "HTMLAllCollection" : "HTMLCollection";
}

void collection_missing(v8::Isolate* isolate, LiveCollectionBinding* binding,
                         v8::Local<v8::Object> object, const std::string& member) {
    auto* trace = access_trace_from_isolate(isolate);
    if (trace != nullptr) (void)trace->record(binding, "host_object",
        collection_label(object), member, AccessOperation::kGetProbe, "missing");
}

std::uint64_t collection_method_property_record(v8::Isolate* isolate,
    v8::Local<v8::Object> receiver, v8::Local<v8::Array> data,
    const char* member, AccessOperation operation) {
    if (auto* binding = collection_binding(receiver)) {
        return access_trace_record(isolate, binding, "host_object",
            collection_label(receiver), member, operation);
    }
    v8::Local<v8::Value> label;
    if (!data->Get(isolate->GetCurrentContext(), 1).ToLocal(&label) || !label->IsString()) return 0;
    v8::String::Utf8Value text(isolate, label);
    return *text == nullptr ? 0 : native_api_trace_record(isolate, receiver, *text, member, operation);
}

void collection_method_getter(v8::Local<v8::Name> property,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto data = info.Data().As<v8::Array>();
    v8::String::Utf8Value member(isolate, property);
    const auto sequence = *member == nullptr ? 0 : collection_method_property_record(
        isolate, info.Holder(), data, *member, AccessOperation::kGet);
    AccessPropertyCompletion completion(isolate, sequence, info);
    v8::Local<v8::Value> value;
    if (data->Get(isolate->GetCurrentContext(), 0).ToLocal(&value)) info.GetReturnValue().Set(value);
}

void collection_method_setter(v8::Local<v8::Name> property,
    v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info) {
    auto isolate = info.GetIsolate();
    auto data = info.Data().As<v8::Array>();
    v8::String::Utf8Value member(isolate, property);
    const auto sequence = *member == nullptr ? 0 : collection_method_property_record(
        isolate, info.Holder(), data, *member, AccessOperation::kSet);
    AccessPropertySetCompletion completion(isolate, sequence, info, value);
    (void)data->CreateDataProperty(isolate->GetCurrentContext(), 0, value);
}

}  // namespace

v8::Intercepted collection_index(std::uint32_t index,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
    auto binding = collection_binding(info.Holder());
    if (binding == nullptr) return v8::Intercepted::kNo;
    auto nodes = binding->document->collection_nodes(*binding);
    const auto member = std::to_string(index);
    if (index >= nodes.size()) {
        collection_missing(info.GetIsolate(), binding, info.Holder(), member);
        return v8::Intercepted::kNo;
    }
    const auto sequence = access_trace_record(info.GetIsolate(), binding, "host_object",
        collection_label(info.Holder()), member.c_str(), AccessOperation::kGet);
    AccessPropertyCompletion completion(info.GetIsolate(), sequence, info);
    info.GetReturnValue().Set(binding->document->wrap_node(info.GetIsolate(),
        info.GetIsolate()->GetCurrentContext(), nodes[index]));
    return v8::Intercepted::kYes;
}

v8::Intercepted collection_index_query(std::uint32_t index,
    const v8::PropertyCallbackInfo<v8::Integer>& info) {
    auto binding = collection_binding(info.Holder());
    if (binding == nullptr) return v8::Intercepted::kNo;
    const auto member = std::to_string(index);
    (void)access_trace_record(info.GetIsolate(), binding, "host_object",
        collection_label(info.Holder()), member.c_str(), AccessOperation::kHas);
    if (index >= binding->document->collection_nodes(*binding).size())
        return v8::Intercepted::kNo;
    info.GetReturnValue().Set(static_cast<int>(v8::ReadOnly));
    return v8::Intercepted::kYes;
}

void collection_indices(const v8::PropertyCallbackInfo<v8::Array>& info) {
    auto binding = collection_binding(info.Holder());
    if (binding == nullptr) return;
    (void)access_trace_record(info.GetIsolate(), binding, "host_object",
        collection_label(info.Holder()), "", AccessOperation::kOwnKeys);
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
    const std::string member = *text != nullptr ? *text : "";
    auto node = collection_named(binding, member);
    if (node == nullptr) {
        collection_missing(info.GetIsolate(), binding, info.Holder(), member);
        return v8::Intercepted::kNo;
    }
    const auto sequence = access_trace_record(info.GetIsolate(), binding, "host_object",
        collection_label(info.Holder()), member.c_str(), AccessOperation::kGet);
    AccessPropertyCompletion completion(info.GetIsolate(), sequence, info);
    info.GetReturnValue().Set(binding->document->wrap_node(info.GetIsolate(),
        info.GetIsolate()->GetCurrentContext(), node));
    return v8::Intercepted::kYes;
}

void collection_length(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto binding = collection_binding(info.This());
    const auto sequence = binding == nullptr ? 0 : access_trace_record(info.GetIsolate(),
        binding, "host_object", collection_label(info.This()), "length", AccessOperation::kGet);
    AccessFunctionPropertyCompletion completion(info.GetIsolate(), sequence, info, AccessOperation::kGet);
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
    const char* member = info.Data()->IsTrue() ? "namedItem" : "item";
    const auto sequence = binding == nullptr ? 0 : access_trace_record(isolate, binding,
        "host_object", collection_label(info.This()), member, AccessOperation::kCall);
    AccessCallCompletion completion(isolate, sequence, info);
    watch_record(isolate, collection_label(info.This()), member);
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
    const auto sequence = access_trace_record(isolate, binding, "host_object",
        "HTMLAllCollection", "[[Call]]", AccessOperation::kCall);
    AccessCallCompletion completion(isolate, sequence, info);
    watch_record(isolate, "HTMLAllCollection", "[[Call]]");
    if (info.Length() == 0) { info.GetReturnValue().Set(v8::Null(isolate)); return; }
    v8::Local<v8::String> value;
    if (!info[0]->ToString(context).ToLocal(&value)) return;
    v8::String::Utf8Value name(isolate, value);
    DomNode* node = collection_named(binding, *name != nullptr ? *name : "");
    info.GetReturnValue().Set(binding->document->wrap_node(isolate, context, node));
}

v8::Local<v8::Object> DocumentHost::live_collection(v8::Isolate* isolate,
    v8::Local<v8::Context> context, DomNode* root, const std::string& kind,
    const std::string& query, bool all, const std::string& namespace_uri) {
    auto binding = std::make_unique<LiveCollectionBinding>(LiveCollectionBinding{this, root, kind, query, namespace_uri});
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
    object->SetInternalField(1, v8::Boolean::New(isolate, all));
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
            auto data = v8::Array::New(isolate, 2);
            if (!data->CreateDataProperty(context, 0, method).FromMaybe(false) ||
                !data->CreateDataProperty(context, 1, v8_string(isolate,
                    all ? "HTMLAllCollection.prototype" : "HTMLCollection.prototype")).FromMaybe(false)) return object;
            (void)prototype->SetNativeDataProperty(context, v8_string(isolate, name).As<v8::String>(),
                &collection_method_getter, &collection_method_setter, data);
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
