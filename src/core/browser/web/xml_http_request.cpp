#include "xml_http_request.h"

#include <algorithm>
#include <array>
#include <initializer_list>
#include <string_view>

#include "iv8/access_origin.h"
#include "iv8/web_interface.h"
#include "../internal/common.h"
#include "../internal/trace_hooks.h"
#include "../window/window_trace_support.h"

namespace iv8::browser {
namespace {

constexpr std::array<std::string_view, 7> kEventHandlerNames = {
    "onloadstart", "onprogress", "onabort", "onerror", "onload", "ontimeout", "onloadend",
};

bool is_shared_handler(const std::string& name) {
    return std::find(kEventHandlerNames.begin(), kEventHandlerNames.end(), name) !=
        kEventHandlerNames.end();
}

std::vector<std::string> shared_handler_names() {
    std::vector<std::string> names;
    names.reserve(kEventHandlerNames.size());
    for (const auto name : kEventHandlerNames) names.emplace_back(name);
    return names;
}

bool is_request_method(const std::string& name) {
    return name == "open" || name == "setRequestHeader" || name == "send" ||
        name == "abort" || name == "getResponseHeader" || name == "getAllResponseHeaders" ||
        name == "overrideMimeType" || name == "setAttributionReporting" ||
        name == "setPrivateToken";
}

int required_arguments(const std::string& name) {
    if (name == "open" || name == "setRequestHeader") return 2;
    if (name == "getResponseHeader" || name == "overrideMimeType" ||
        name == "setAttributionReporting" || name == "setPrivateToken") return 1;
    return 0;
}

bool check_arity(v8::Isolate* isolate, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    const int required = required_arguments(name);
    if (arguments.Length() >= required) return true;
    const auto message = "Failed to execute '" + name + "' on 'XMLHttpRequest': " +
        std::to_string(required) + (required == 1 ? " argument required, but only " : " arguments required, but only ") +
        std::to_string(arguments.Length()) + " present.";
    isolate->ThrowException(v8::Exception::TypeError(v8_string(isolate, message).As<v8::String>()));
    return false;
}

void throw_response_type_error(v8::Isolate* isolate, v8::Local<v8::Context> context,
    const std::string& member, const std::string& allowed_type, const std::string& actual_type) {
    const auto message = "Failed to read the '" + member +
        "' property from 'XMLHttpRequest': The value is only accessible if the object's "
        "'responseType' is '' or '" + allowed_type + "' (was '" + actual_type + "').";
    auto error = v8::Exception::Error(v8_string(isolate, message).As<v8::String>());
    if (!error.As<v8::Object>()->CreateDataProperty(context,
            v8::String::NewFromUtf8Literal(isolate, "name"),
            v8::String::NewFromUtf8Literal(isolate, "InvalidStateError")).FromMaybe(false) ||
        !error.As<v8::Object>()->CreateDataProperty(context,
            v8::String::NewFromUtf8Literal(isolate, "code"), v8::Integer::New(isolate, 11)).FromMaybe(false)) return;
    isolate->ThrowException(error);
}

constexpr std::array<std::string_view, 5> kStateNames = {
    "UNSENT", "OPENED", "HEADERS_RECEIVED", "LOADING", "DONE",
};

int state_constant(std::string_view name) {
    const auto found = std::find(kStateNames.begin(), kStateNames.end(), name);
    return found == kStateNames.end() ? -1 : static_cast<int>(found - kStateNames.begin());
}

struct InterfaceProperty {
    std::string_view name;
    v8::Local<v8::String> key;
    v8::Local<v8::Value> value;
    v8::Local<v8::Value> getter;
    v8::Local<v8::Value> setter;
    v8::PropertyAttribute attributes = v8::None;
    bool data = false;
};

bool capture_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Object> prototype, std::string_view name, InterfaceProperty& property) {
    property.name = name;
    if (!v8::String::NewFromUtf8(isolate, name.data(), v8::NewStringType::kNormal,
            static_cast<int>(name.size())).ToLocal(&property.key)) return false;
    v8::Local<v8::Value> descriptor_value;
    if (!prototype->GetOwnPropertyDescriptor(context, property.key).ToLocal(&descriptor_value) ||
        !descriptor_value->IsObject()) return false;
    auto descriptor = descriptor_value.As<v8::Object>();
    v8::Local<v8::Value> configurable, enumerable;
    if (!descriptor->Get(context, v8::String::NewFromUtf8Literal(isolate, "configurable")).ToLocal(&configurable) ||
        !configurable->IsTrue() ||
        !descriptor->Get(context, v8::String::NewFromUtf8Literal(isolate, "enumerable")).ToLocal(&enumerable)) return false;
    if (!enumerable->IsTrue()) property.attributes = v8::DontEnum;
    const auto has_value = descriptor->HasOwnProperty(context,
        v8::String::NewFromUtf8Literal(isolate, "value"));
    if (has_value.IsNothing()) return false;
    property.data = has_value.FromJust();
    if (property.data) {
        v8::Local<v8::Value> writable;
        if (!descriptor->Get(context, v8::String::NewFromUtf8Literal(isolate, "value")).ToLocal(&property.value) ||
            !descriptor->Get(context, v8::String::NewFromUtf8Literal(isolate, "writable")).ToLocal(&writable)) return false;
        if (!writable->IsTrue()) property.attributes = static_cast<v8::PropertyAttribute>(property.attributes | v8::ReadOnly);
        return true;
    }
    return descriptor->Get(context, v8::String::NewFromUtf8Literal(isolate, "get")).ToLocal(&property.getter) &&
        descriptor->Get(context, v8::String::NewFromUtf8Literal(isolate, "set")).ToLocal(&property.setter);
}

bool reorder_interface(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Object> prototype, const char* interface,
    const std::vector<std::string_view>& order, bool add_constants = false) {
    std::vector<InterfaceProperty> properties;
    properties.reserve(order.size());
    for (const auto name : order) {
        if (add_constants && state_constant(name) >= 0) continue;
        InterfaceProperty property;
        if (!capture_property(isolate, context, prototype, name, property)) return false;
        properties.push_back(property);
    }
    for (const auto& property : properties) {
        if (!prototype->Delete(context, property.key).FromMaybe(false)) return false;
    }
    const auto* identity = native_api_identity(isolate, prototype);
    const auto label = std::string(interface) + ".prototype";
    for (const auto name : order) {
        const int constant = add_constants ? state_constant(name) : -1;
        if (constant >= 0) {
            if (!install_traced_api_data_property(isolate, context, prototype, name,
                    v8::Integer::New(isolate, constant), identity, "host_object", label,
                    static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete))) return false;
            continue;
        }
        const auto found = std::find_if(properties.begin(), properties.end(),
            [&](const auto& property) { return property.name == name; });
        if (found == properties.end()) return false;
        if (found->data) {
            if (!install_traced_api_data_property(isolate, context, prototype, name,
                    found->value, identity, "host_object", label, found->attributes)) return false;
        } else {
            v8::PropertyDescriptor descriptor(found->getter, found->setter);
            descriptor.set_enumerable((found->attributes & v8::DontEnum) == 0);
            descriptor.set_configurable(true);
            if (!prototype->DefineProperty(context, found->key, descriptor).FromMaybe(false)) return false;
        }
    }
    return true;
}

}  // namespace

std::vector<std::string> XMLHttpRequestEventTargetHost::property_names() const {
    return shared_handler_names();
}

std::vector<std::string> XMLHttpRequestEventTargetHost::writable_property_names() const {
    return shared_handler_names();
}

std::vector<std::string> XMLHttpRequestEventTargetHost::method_names() const {
    return event_method_names();
}

v8::Local<v8::Value> XMLHttpRequestEventTargetHost::get_handler_value(
    v8::Isolate* isolate, const std::string& name) {
    const auto found = handler_values_.find(name);
    return found == handler_values_.end() ? v8::Local<v8::Value>(v8::Null(isolate)) : found->second.Get(isolate);
}

void XMLHttpRequestEventTargetHost::set_handler_value(v8::Isolate* isolate,
    const std::string& name, v8::Local<v8::Value> value) {
    const auto found = handler_values_.find(name);
    if (!value->IsObject()) {
        if (found != handler_values_.end()) {
            found->second.Reset();
            handler_values_.erase(found);
        }
    } else if (found == handler_values_.end()) {
        handler_values_.emplace(name, v8::Global<v8::Value>(isolate, value));
    } else {
        found->second.Reset(isolate, value);
    }
    set_event_handler(isolate, name, value);
}

v8::Local<v8::Value> XMLHttpRequestEventTargetHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name) {
    return is_shared_handler(name) ? get_handler_value(isolate, name) : v8::Local<v8::Value>(v8::Undefined(isolate));
}

void XMLHttpRequestEventTargetHost::set_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name, v8::Local<v8::Value> value) {
    if (is_shared_handler(name)) set_handler_value(isolate, name, value);
}

v8::Local<v8::Value> XMLHttpRequestEventTargetHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    v8::Local<v8::Value> result;
    if (handle_event_method(isolate, context, name, arguments, result)) return result;
    return v8::Undefined(isolate);
}

void XMLHttpRequestEventTargetHost::release_v8_handles() noexcept {
    for (auto& entry : handler_values_) entry.second.Reset();
    handler_values_.clear();
    EventTargetHost::release_v8_handles();
}

XMLHttpRequestHost::XMLHttpRequestHost(v8::Isolate* isolate, v8::Local<v8::Object> upload,
    bool secure_context) : secure_context_(secure_context) {
    if (!upload.IsEmpty()) upload_.Reset(isolate, upload);
}

std::vector<std::string> XMLHttpRequestHost::property_names() const {
    std::vector<std::string> names = {
        "onreadystatechange", "readyState", "timeout", "withCredentials", "upload", "responseURL",
        "status", "statusText", "responseType", "response", "responseText", "responseXML",
    };
    const auto handlers = shared_handler_names();
    names.insert(names.end(), handlers.begin(), handlers.end());
    return names;
}

std::vector<std::string> XMLHttpRequestHost::writable_property_names() const {
    std::vector<std::string> names = {"onreadystatechange", "timeout", "withCredentials", "responseType"};
    const auto handlers = shared_handler_names();
    names.insert(names.end(), handlers.begin(), handlers.end());
    return names;
}

std::vector<std::string> XMLHttpRequestHost::method_names() const {
    auto names = event_method_names();
    for (const auto* name : {"abort", "getAllResponseHeaders", "getResponseHeader", "open",
            "overrideMimeType", "send", "setRequestHeader"}) {
        names.emplace_back(name);
    }
    if (secure_context_) {
        names.emplace_back("setAttributionReporting");
        names.emplace_back("setPrivateToken");
    }
    return names;
}

v8::Local<v8::Value> XMLHttpRequestHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name) {
    if (name == "onreadystatechange") return get_handler_value(isolate, name);
    if (name == "readyState" || name == "status") return v8::Integer::New(isolate, 0);
    if (name == "timeout") return v8::Integer::NewFromUnsigned(isolate, timeout_);
    if (name == "withCredentials") return v8::Boolean::New(isolate, with_credentials_);
    if (name == "upload") return upload_.IsEmpty() ? v8::Local<v8::Value>(v8::Null(isolate)) : upload_.Get(isolate);
    if (name == "responseType") return v8_string(isolate, response_type_);
    if (name == "responseURL" || name == "statusText") return v8::String::Empty(isolate);
    if (name == "response") {
        return response_type_.empty() || response_type_ == "text"
            ? v8::Local<v8::Value>(v8::String::Empty(isolate)) : v8::Local<v8::Value>(v8::Null(isolate));
    }
    if (name == "responseText") {
        if (!response_type_.empty() && response_type_ != "text") {
            throw_response_type_error(isolate, context, name, "text", response_type_);
            return v8::Undefined(isolate);
        }
        return v8::String::Empty(isolate);
    }
    if (name == "responseXML") {
        if (!response_type_.empty() && response_type_ != "document") {
            throw_response_type_error(isolate, context, name, "document", response_type_);
            return v8::Undefined(isolate);
        }
        return v8::Null(isolate);
    }
    return XMLHttpRequestEventTargetHost::get_property(isolate, context, name);
}

void XMLHttpRequestHost::set_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name, v8::Local<v8::Value> value) {
    if (name == "onreadystatechange") {
        set_handler_value(isolate, name, value);
        return;
    }
    if (name == "withCredentials") {
        with_credentials_ = value->BooleanValue(isolate);
        return;
    }
    if (name == "timeout") {
        if (value->IsBigInt()) {
            isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(
                isolate, "Failed to set the 'timeout' property on 'XMLHttpRequest': "
                "Cannot convert a BigInt value to a number")));
            return;
        }
        const auto converted = access_user_operation(isolate, [&] { return value->Uint32Value(context); });
        if (!converted.IsNothing()) timeout_ = converted.FromJust();
        return;
    }
    if (name == "responseType") {
        v8::Local<v8::String> converted;
        if (!access_user_operation(isolate, [&] { return value->ToString(context); }).ToLocal(&converted)) return;
        v8::String::Utf8Value text(isolate, converted);
        if (*text == nullptr) return;
        const std::string type(*text, static_cast<std::size_t>(text.length()));
        if (type.empty() || type == "text" || type == "json" || type == "document" ||
            type == "arraybuffer" || type == "blob") response_type_ = type;
        return;
    }
    XMLHttpRequestEventTargetHost::set_property(isolate, context, name, value);
}

v8::Local<v8::Value> XMLHttpRequestHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    if (!is_request_method(name)) return XMLHttpRequestEventTargetHost::call_method(isolate, context, name, arguments);
    watch_record(isolate, "XMLHttpRequest", name);
    if (!check_arity(isolate, name, arguments)) return v8::Undefined(isolate);
    if (name == "getResponseHeader") return v8::Null(isolate);
    if (name == "getAllResponseHeaders") return v8::String::Empty(isolate);
    // Arguments are intentionally left untouched: the placeholder neither
    // serializes request bodies nor coerces headers/URLs to start transport.
    return v8::Undefined(isolate);
}

void XMLHttpRequestHost::release_v8_handles() noexcept {
    upload_.Reset();
    XMLHttpRequestEventTargetHost::release_v8_handles();
}

bool install_xml_http_request_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, bool secure_context) {
    auto runtime_origin = access_runtime_scope(isolate);
    auto request_prototype = web_interface_prototype(isolate, context, "XMLHttpRequest");
    if (request_prototype.IsEmpty()) return false;
    const auto marker = v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.xhr.interfaces-installed"));
    v8::Local<v8::Value> installed;
    if (!request_prototype->GetPrivate(context, marker).ToLocal(&installed)) return false;
    if (installed->IsBoolean()) return installed->IsTrue() == secure_context;

    XMLHttpRequestEventTargetHost event_target;
    XMLHttpRequestUploadHost upload;
    XMLHttpRequestHost request(isolate, {}, secure_context);
    install_host_interface(isolate, context, &event_target);
    install_host_interface(isolate, context, &upload);
    install_host_interface(isolate, context, &request);
    auto event_target_prototype = web_interface_prototype(isolate, context, "XMLHttpRequestEventTarget");
    auto upload_prototype = web_interface_prototype(isolate, context, "XMLHttpRequestUpload");
    if (event_target_prototype.IsEmpty() || upload_prototype.IsEmpty()) return false;

    v8::Local<v8::Value> constructor_value;
    if (!request_prototype->GetPrivate(context, v8::Private::ForApi(isolate,
            v8::String::NewFromUtf8Literal(isolate, "iv8.webidl.constructor"))).ToLocal(&constructor_value) ||
        !constructor_value->IsFunction()) return false;
    auto constructor = constructor_value.As<v8::Object>();
    // Constants become non-configurable. A repeated successful installation is
    // handled by the private marker; partial/foreign constants are never deleted.
    for (const auto name : kStateNames) {
        v8::Local<v8::String> key;
        if (!v8::String::NewFromUtf8(isolate, name.data(), v8::NewStringType::kNormal,
                static_cast<int>(name.size())).ToLocal(&key)) return false;
        for (const auto target : {constructor, request_prototype}) {
            const auto present = target->HasOwnProperty(context, key);
            if (present.IsNothing() || present.FromJust()) return false;
        }
    }

    std::vector<std::string_view> handler_order(kEventHandlerNames.begin(), kEventHandlerNames.end());
    handler_order.emplace_back("constructor");
    if (!reorder_interface(isolate, context, event_target_prototype,
            "XMLHttpRequestEventTarget", handler_order) ||
        !reorder_interface(isolate, context, upload_prototype, "XMLHttpRequestUpload", {"constructor"})) return false;
    std::vector<std::string_view> request_order = {
        "onreadystatechange", "readyState", "timeout", "withCredentials", "upload", "responseURL",
        "status", "statusText", "responseType", "response", "responseText",
        "UNSENT", "OPENED", "HEADERS_RECEIVED", "LOADING", "DONE",
        "abort", "getAllResponseHeaders", "getResponseHeader", "open", "overrideMimeType", "send",
        "setRequestHeader", "constructor", "responseXML",
    };
    if (secure_context) {
        request_order.emplace_back("setAttributionReporting");
        request_order.emplace_back("setPrivateToken");
    }
    if (!reorder_interface(isolate, context, request_prototype,
            "XMLHttpRequest", request_order, true)) return false;
    const auto* constructor_identity = native_api_identity(isolate, constructor);
    for (std::size_t index = 0; index < kStateNames.size(); ++index) {
        if (!install_traced_api_data_property(isolate, context, constructor, kStateNames[index],
                v8::Integer::New(isolate, static_cast<int>(index)), constructor_identity,
                "host_object", "XMLHttpRequest",
                static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete))) return false;
    }
    return request_prototype->SetPrivate(context, marker,
        v8::Boolean::New(isolate, secure_context)).FromMaybe(false);
}

}  // namespace iv8::browser
