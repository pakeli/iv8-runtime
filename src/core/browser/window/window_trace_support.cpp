#include "window_trace_support.h"

#include <limits>
#include <string>

#include "iv8/access_summary.h"
#include "iv8/host_object.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {

namespace {

class ApiTraceStoreHost : public HostObject {
public:
    std::string global_name() const override { return {}; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override { return {}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate, v8::Local<v8::Context>,
        const std::string&) override { return v8::Undefined(isolate); }
    v8::Local<v8::Value> call_method(v8::Isolate* isolate, v8::Local<v8::Context>,
        const std::string&, const v8::FunctionCallbackInfo<v8::Value>&) override {
        return v8::Undefined(isolate);
    }
    const void* allocate() {
        tokens_.push_back(std::make_unique<char>(0));
        return tokens_.back().get();
    }
private:
    std::vector<std::unique_ptr<char>> tokens_;
};

v8::Local<v8::Private> api_trace_store_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.trace.api-object-store"));
}

std::string trace_member_name(v8::Isolate* isolate, v8::Local<v8::Name> name) {
    if (name->IsSymbol()) {
        auto description = name.As<v8::Symbol>()->Description(isolate);
        if (!description->IsString()) return "[Symbol]";
        v8::String::Utf8Value text(isolate, description);
        return *text == nullptr ? "[Symbol]" : "[" + std::string(*text, text.length()) + "]";
    }
    v8::String::Utf8Value text(isolate, name.As<v8::String>());
    return *text == nullptr ? std::string() : std::string(*text, text.length());
}

std::uint64_t data_trace_record(v8::Isolate* isolate, v8::Local<v8::Array> data,
    v8::Local<v8::Object> receiver, const char* member, AccessOperation operation) {
    if (data->Length() == 1) return window_trace_record(isolate, member, operation);
    auto* trace = access_trace_from_isolate(isolate);
    if (trace == nullptr || !trace->enabled()) return 0;
    auto context = isolate->GetCurrentContext();
    v8::Local<v8::Value> identity, type, label;
    if (!data->Get(context, 1).ToLocal(&identity) || !identity->IsExternal() ||
        !data->Get(context, 2).ToLocal(&type) || !data->Get(context, 3).ToLocal(&label)) return 0;
    v8::String::Utf8Value type_text(isolate, type), label_text(isolate, label);
    if (*type_text == nullptr || *label_text == nullptr) return 0;
    const void* native_identity = identity.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault);
    if (data->Length() > 4) {
        v8::Local<v8::Value> use_receiver;
        if (!data->Get(context, 4).ToLocal(&use_receiver)) return 0;
        if (use_receiver->IsTrue()) {
            if (const auto* resolved = native_api_identity(isolate, receiver)) native_identity = resolved;
        }
    }
    return trace->record(native_identity,
        *type_text, *label_text, member, operation);
}

void window_data_getter(v8::Local<v8::Name> property,
                         const v8::PropertyCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    const auto member = trace_member_name(isolate, property);
    const auto sequence = data_trace_record(isolate, info.Data().As<v8::Array>(),
        info.Holder(), member.c_str(), AccessOperation::kGet);
    AccessPropertyCompletion completion(isolate, sequence, info);
    v8::Local<v8::Value> value;
    if (info.Data().As<v8::Array>()->Get(context, 0).ToLocal(&value)) {
        info.GetReturnValue().Set(value);
    }
}

void window_data_setter(v8::Local<v8::Name> property,
                         v8::Local<v8::Value> value,
                         const v8::PropertyCallbackInfo<void>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    const auto member = trace_member_name(isolate, property);
    const auto sequence = data_trace_record(isolate, info.Data().As<v8::Array>(),
        info.Holder(), member.c_str(), AccessOperation::kSet);
    AccessPropertySetCompletion completion(isolate, sequence, info, value);
    if (!info.Data().As<v8::Array>()->CreateDataProperty(context, 0, value).FromMaybe(false)) return;
}

}  // namespace

bool install_native_api_trace_store(v8::Isolate* isolate,
    v8::Local<v8::Context> context, std::vector<std::unique_ptr<HostObject>>& owners) {
    auto store = std::make_unique<ApiTraceStoreHost>();
    if (!context->Global()->SetPrivate(context, api_trace_store_key(isolate),
            v8::External::New(isolate, store.get(), v8::kExternalPointerTypeTagDefault)).FromMaybe(false)) return false;
    owners.push_back(std::move(store));
    return true;
}

const void* native_api_identity(v8::Isolate* isolate, v8::Local<v8::Object> object) {
    if (object.IsEmpty()) return nullptr;
    // Engine-level target access executes no Proxy traps. CSSStyleDeclaration
    // uses a Proxy facade whose native observation identity is its stable target.
    for (int depth = 0; object->IsProxy() && depth < 100; ++depth) {
        auto target = object.As<v8::Proxy>()->GetTarget();
        if (!target->IsObject()) return nullptr;
        object = target.As<v8::Object>();
    }
    auto context = isolate->GetCurrentContext();
    const auto key = v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.trace.api-object-identity"));
    v8::Local<v8::Value> token;
    if (!object->GetPrivate(context, key).ToLocal(&token)) return nullptr;
    if (token->IsExternal()) return token.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault);
    v8::Local<v8::Value> store_value;
    if (!context->Global()->GetPrivate(context, api_trace_store_key(isolate)).ToLocal(&store_value) ||
        !store_value->IsExternal()) return nullptr;
    auto* store = static_cast<ApiTraceStoreHost*>(store_value.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    const void* identity = nullptr;
    try { identity = store->allocate(); } catch (...) { return nullptr; }
    if (!object->SetPrivate(context, key, v8::External::New(isolate,
            const_cast<void*>(identity), v8::kExternalPointerTypeTagDefault)).FromMaybe(false)) return nullptr;
    return identity;
}

std::uint64_t native_api_trace_record(v8::Isolate* isolate, v8::Local<v8::Object> object,
    const char* label, const char* member, AccessOperation operation) {
    auto* trace = access_trace_from_isolate(isolate);
    if (trace == nullptr || !trace->enabled()) return 0;
    v8::TryCatch diagnostic(isolate);
    const auto* identity = native_api_identity(isolate, object);
    if (diagnostic.HasCaught()) {
        if (!diagnostic.CanContinue()) { diagnostic.ReThrow(); return 0; }
        diagnostic.Reset();
        return 0;
    }
    return identity == nullptr ? 0 : trace->record(identity, "host_object", label, member, operation);
}

NativeApiCallTrace::NativeApiCallTrace(const v8::FunctionCallbackInfo<v8::Value>& info,
    const char* label, const char* member)
    : completion_(info.GetIsolate(), native_api_trace_record(info.GetIsolate(),
        info.This(), label, member, AccessOperation::kCall), info) {
    watch_record(info.GetIsolate(), label, member);
}

NativeApiPropertyTrace::NativeApiPropertyTrace(const v8::FunctionCallbackInfo<v8::Value>& info,
    const char* label, const char* member, AccessOperation operation)
    : completion_(info.GetIsolate(), native_api_trace_record(info.GetIsolate(),
        info.This(), label, member, operation), info, operation) {}

std::uint64_t window_trace_record(v8::Isolate* isolate, const char* member,
                                  AccessOperation operation, const char* resolution) {
    auto* trace = access_trace_from_isolate(isolate);
    if (trace == nullptr || !trace->enabled()) return 0;
    auto context = isolate->GetCurrentContext();
    v8::Local<v8::Value> value;
    if (!context->Global()->GetPrivate(context, v8::Private::ForApi(isolate,
            v8::String::NewFromUtf8Literal(isolate, "iv8.webidl.window.host")))
                .ToLocal(&value) || !value->IsExternal()) return 0;
    auto* host = static_cast<HostObject*>(value.As<v8::External>()->Value(
        v8::kExternalPointerTypeTagDefault));
    return host == nullptr ? 0 : trace->record(host, "window", "window", member, operation, resolution);
}

bool install_traced_window_data_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> target,
    std::string_view name, v8::Local<v8::Value> value,
    v8::PropertyAttribute attributes) {
    if (target.IsEmpty() || value.IsEmpty() ||
        name.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
    v8::Local<v8::String> property;
    if (!v8::String::NewFromUtf8(isolate, name.data(), v8::NewStringType::kNormal,
            static_cast<int>(name.size())).ToLocal(&property)) return false;
    auto data = v8::Array::New(isolate, 1);
    if (!data->CreateDataProperty(context, 0, value).FromMaybe(false)) return false;
    return target->SetNativeDataProperty(context, property, &window_data_getter,
        &window_data_setter, data, attributes).FromMaybe(false);
}

bool install_traced_api_data_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> target,
    std::string_view name, v8::Local<v8::Value> value, const void* identity,
    std::string_view object_type, std::string_view label, v8::PropertyAttribute attributes,
    bool receiver_identity) {
    if (target.IsEmpty() || value.IsEmpty() ||
        name.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        object_type.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        label.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
    v8::Local<v8::String> key;
    if (!v8::String::NewFromUtf8(isolate, name.data(), v8::NewStringType::kNormal,
            static_cast<int>(name.size())).ToLocal(&key)) return false;
    return install_traced_api_named_data_property(isolate, context, target, key, value,
        identity, object_type, label, attributes, receiver_identity);
}

bool install_traced_api_named_data_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> target,
    v8::Local<v8::Name> key, v8::Local<v8::Value> value, const void* identity,
    std::string_view object_type, std::string_view label, v8::PropertyAttribute attributes,
    bool receiver_identity) {
    if (target.IsEmpty() || key.IsEmpty() || value.IsEmpty() ||
        object_type.size() > static_cast<std::size_t>(std::numeric_limits<int>::max()) ||
        label.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
    if (identity == nullptr) return target->DefineOwnProperty(context, key, value, attributes).FromMaybe(false);
    v8::Local<v8::String> type_text, label_text;
    if (!v8::String::NewFromUtf8(isolate, object_type.data(), v8::NewStringType::kNormal,
            static_cast<int>(object_type.size())).ToLocal(&type_text) ||
        !v8::String::NewFromUtf8(isolate, label.data(), v8::NewStringType::kNormal,
            static_cast<int>(label.size())).ToLocal(&label_text)) return false;
    auto data = v8::Array::New(isolate, receiver_identity ? 5 : 4);
    if (!data->CreateDataProperty(context, 0, value).FromMaybe(false) ||
        !data->CreateDataProperty(context, 1, v8::External::New(isolate,
            const_cast<void*>(identity), v8::kExternalPointerTypeTagDefault)).FromMaybe(false) ||
        !data->CreateDataProperty(context, 2, type_text).FromMaybe(false) ||
        !data->CreateDataProperty(context, 3, label_text).FromMaybe(false)) return false;
    if (receiver_identity && !data->CreateDataProperty(context, 4, v8::True(isolate)).FromMaybe(false)) return false;
    return target->SetNativeDataProperty(context, key, &window_data_getter,
        &window_data_setter, data, attributes).FromMaybe(false);
}

bool trace_native_api_data_members(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Object> object, const char* label) {
    v8::Local<v8::Array> names;
    if (!object->GetOwnPropertyNames(context, v8::ALL_PROPERTIES, v8::KeyConversionMode::kConvertToString).ToLocal(&names)) return false;
    for (std::uint32_t index = 0; index < names->Length(); ++index) {
        v8::Local<v8::Value> key_value, descriptor_value;
        if (!names->Get(context, index).ToLocal(&key_value) || !key_value->IsName() ||
            !object->GetOwnPropertyDescriptor(context, key_value.As<v8::Name>()).ToLocal(&descriptor_value)) return false;
        if (!descriptor_value->IsObject()) continue;
        auto descriptor = descriptor_value.As<v8::Object>();
        const auto has_value = descriptor->HasOwnProperty(context,
            v8::String::NewFromUtf8Literal(isolate, "value"));
        if (has_value.IsNothing()) return false;
        if (!has_value.FromJust()) continue;
        v8::Local<v8::Value> value, writable, enumerable, configurable;
        if (!descriptor->Get(context, v8::String::NewFromUtf8Literal(isolate, "value")).ToLocal(&value) ||
            !descriptor->Get(context, v8::String::NewFromUtf8Literal(isolate, "writable")).ToLocal(&writable) ||
            !descriptor->Get(context, v8::String::NewFromUtf8Literal(isolate, "enumerable")).ToLocal(&enumerable) ||
            !descriptor->Get(context, v8::String::NewFromUtf8Literal(isolate, "configurable")).ToLocal(&configurable)) return false;
        if (!configurable->IsTrue()) continue;
        int attributes = v8::None;
        if (!writable->IsTrue()) attributes |= v8::ReadOnly;
        if (!enumerable->IsTrue()) attributes |= v8::DontEnum;
        if (!install_traced_api_named_data_property(isolate, context, object, key_value.As<v8::Name>(), value,
                native_api_identity(isolate, object), "host_object", label,
                static_cast<v8::PropertyAttribute>(attributes), true)) return false;
    }
    return true;
}

bool capture_window_intrinsic_names(v8::Isolate* isolate,
    v8::Local<v8::Context> context) {
    v8::Local<v8::Array> names;
    if (!context->Global()->GetOwnPropertyNames(context).ToLocal(&names)) return false;
    return context->Global()->SetPrivate(context, v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.trace.intrinsic-global-names")), names).FromMaybe(false);
}

bool install_window_public_data_tracing(v8::Isolate* isolate,
    v8::Local<v8::Context> context) {
    v8::Local<v8::Value> original;
    if (!context->Global()->GetPrivate(context, v8::Private::ForApi(isolate,
            v8::String::NewFromUtf8Literal(isolate, "iv8.trace.intrinsic-global-names")))
            .ToLocal(&original) || !original->IsArray()) return false;
    auto intrinsic_names = v8::Set::New(isolate);
    auto initial = original.As<v8::Array>();
    for (std::uint32_t index = 0; index < initial->Length(); ++index) {
        v8::Local<v8::Value> name;
        if (!initial->Get(context, index).ToLocal(&name) ||
            !intrinsic_names->Add(context, name).ToLocal(&intrinsic_names)) return false;
    }
    v8::Local<v8::Array> names;
    if (!context->Global()->GetOwnPropertyNames(context).ToLocal(&names)) return false;
    for (std::uint32_t index = 0; index < names->Length(); ++index) {
        v8::Local<v8::Value> name;
        if (!names->Get(context, index).ToLocal(&name) || !name->IsString()) return false;
        const auto intrinsic = intrinsic_names->Has(context, name);
        if (intrinsic.IsNothing()) return false;
        if (intrinsic.FromJust()) continue;
        v8::String::Utf8Value text(isolate, name);
        if (*text == nullptr) return false;
        const std::string member(*text, static_cast<std::size_t>(text.length()));
        if (member == "hostProbe" || member.starts_with("__iv8")) continue;
        // These native data properties already trace through live PageState
        // getters/setters. A descriptor exposes their current value, but
        // replacing that value would lose coercion, persistence or alias state.
        if (member == "name" || member == "status" || member == "parent" ||
            member == "frames" || member == "opener") continue;
        v8::Local<v8::Value> descriptor_value;
        if (!context->Global()->GetOwnPropertyDescriptor(context, name.As<v8::String>())
                .ToLocal(&descriptor_value) || !descriptor_value->IsObject()) return false;
        auto descriptor = descriptor_value.As<v8::Object>();
        const auto has_value = descriptor->HasOwnProperty(context,
            v8::String::NewFromUtf8Literal(isolate, "value"));
        if (has_value.IsNothing()) return false;
        if (!has_value.FromJust()) continue;
        v8::Local<v8::Value> value, configurable, enumerable, writable;
        if (!descriptor->Get(context, v8::String::NewFromUtf8Literal(isolate, "value")).ToLocal(&value) ||
            !descriptor->Get(context, v8::String::NewFromUtf8Literal(isolate, "configurable")).ToLocal(&configurable) ||
            !descriptor->Get(context, v8::String::NewFromUtf8Literal(isolate, "enumerable")).ToLocal(&enumerable) ||
            !descriptor->Get(context, v8::String::NewFromUtf8Literal(isolate, "writable")).ToLocal(&writable)) return false;
        // These templates install public data methods outside the generic
        // HostObject registry. Preserve the exact function/prototype objects.
        if (value->IsFunction() && (member == "Event" || member == "UIEvent" || member == "CustomEvent" ||
                member == "MouseEvent" || member == "KeyboardEvent" || member == "FocusEvent" ||
                member == "MessageEvent" || member == "TextEncoder" || member == "TextDecoder" ||
                member == "URL" || member == "URLSearchParams" || member == "Blob" || member == "File" ||
                member == "FileReader" || member == "MutationObserver")) {
            v8::Local<v8::Value> prototype;
            if (!value.As<v8::Function>()->Get(context,
                    v8::String::NewFromUtf8Literal(isolate, "prototype")).ToLocal(&prototype)) return false;
            if (prototype->IsObject() && !trace_native_api_data_members(isolate, context,
                    prototype.As<v8::Object>(), (member + ".prototype").c_str())) return false;
        }
        if (!configurable->IsTrue()) continue; // Immutable roots are instrumented at creation.
        int attributes = v8::None;
        if (!enumerable->IsTrue()) attributes |= v8::DontEnum;
        if (!writable->IsTrue()) attributes |= v8::ReadOnly;
        if (!install_traced_window_data_property(isolate, context, context->Global(), member,
                value, static_cast<v8::PropertyAttribute>(attributes))) return false;
    }
    return true;
}

}  // namespace iv8::browser
