#include "dom_exception.h"

#include <cstdint>
#include <utility>

#include "iv8/page_state.h"
#include "iv8/web_interface.h"
#include "../internal/common.h"
#include "../internal/trace_hooks.h"
#include "../internal/web_idl_primitive.h"
#include "../window/window_trace_support.h"

namespace iv8::browser {
namespace {

constexpr std::pair<const char*, int> kLegacyConstants[] = {
    {"INDEX_SIZE_ERR", 1},
    {"DOMSTRING_SIZE_ERR", 2},
    {"HIERARCHY_REQUEST_ERR", 3},
    {"WRONG_DOCUMENT_ERR", 4},
    {"INVALID_CHARACTER_ERR", 5},
    {"NO_DATA_ALLOWED_ERR", 6},
    {"NO_MODIFICATION_ALLOWED_ERR", 7},
    {"NOT_FOUND_ERR", 8},
    {"NOT_SUPPORTED_ERR", 9},
    {"INUSE_ATTRIBUTE_ERR", 10},
    {"INVALID_STATE_ERR", 11},
    {"SYNTAX_ERR", 12},
    {"INVALID_MODIFICATION_ERR", 13},
    {"NAMESPACE_ERR", 14},
    {"INVALID_ACCESS_ERR", 15},
    {"VALIDATION_ERR", 16},
    {"TYPE_MISMATCH_ERR", 17},
    {"SECURITY_ERR", 18},
    {"NETWORK_ERR", 19},
    {"ABORT_ERR", 20},
    {"URL_MISMATCH_ERR", 21},
    {"QUOTA_EXCEEDED_ERR", 22},
    {"TIMEOUT_ERR", 23},
    {"INVALID_NODE_TYPE_ERR", 24},
    {"DATA_CLONE_ERR", 25},
};

std::u16string utf8_to_u16(v8::Isolate* isolate, std::string_view text) {
    if (text.empty()) return {};
    v8::Local<v8::String> str;
    if (!v8::String::NewFromUtf8(isolate, text.data(), v8::NewStringType::kNormal,
            static_cast<int>(text.size())).ToLocal(&str)) return {};
    v8::String::Value units(isolate, str);
    if (!units.length() || *units == nullptr) return {};
    return std::u16string(reinterpret_cast<const char16_t*>(*units),
        static_cast<std::size_t>(units.length()));
}

}  // namespace

int dom_exception_legacy_code(std::u16string_view name) {
    if (name == u"IndexSizeError") return 1;
    if (name == u"DOMStringSizeError") return 2;
    if (name == u"HierarchyRequestError") return 3;
    if (name == u"WrongDocumentError") return 4;
    if (name == u"InvalidCharacterError") return 5;
    if (name == u"NoDataAllowedError") return 6;
    if (name == u"NoModificationAllowedError") return 7;
    if (name == u"NotFoundError") return 8;
    if (name == u"NotSupportedError") return 9;
    if (name == u"InUseAttributeError") return 10;
    if (name == u"InvalidStateError") return 11;
    if (name == u"SyntaxError") return 12;
    if (name == u"InvalidModificationError") return 13;
    if (name == u"NamespaceError") return 14;
    if (name == u"InvalidAccessError") return 15;
    if (name == u"ValidationError") return 16;
    if (name == u"TypeMismatchError") return 17;
    if (name == u"SecurityError") return 18;
    if (name == u"NetworkError") return 19;
    if (name == u"AbortError") return 20;
    if (name == u"URLMismatchError") return 21;
    if (name == u"QuotaExceededError") return 22;
    if (name == u"TimeoutError") return 23;
    if (name == u"InvalidNodeTypeError") return 24;
    if (name == u"DataCloneError") return 25;
    return 0;
}

v8::Local<v8::Value> DOMExceptionHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name) {
    if (name == "code") {
        return v8::Integer::New(isolate, code_);
    }
    if (name != "name" && name != "message") {
        return v8::Undefined(isolate);
    }
    const auto& units = name == "name" ? name_ : message_;
    v8::Local<v8::String> value;
    if (!v8::String::NewFromTwoByte(isolate,
            reinterpret_cast<const std::uint16_t*>(units.data()),
            v8::NewStringType::kNormal, static_cast<int>(units.size())).ToLocal(&value)) {
        return v8::Undefined(isolate);
    }
    return value;
}

v8::Local<v8::Value> DOMExceptionHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string&,
    const v8::FunctionCallbackInfo<v8::Value>&) {
    return v8::Undefined(isolate);
}

bool install_dom_exception_interface(v8::Isolate* isolate,
    v8::Local<v8::Context> context) {
    auto prototype = web_interface_prototype(isolate, context, "DOMException");
    if (prototype.IsEmpty()) return false;
    v8::Local<v8::Value> error_ctor;
    v8::Local<v8::Value> error_prototype;
    if (!context->Global()->Get(context, v8::String::NewFromUtf8Literal(isolate, "Error"))
            .ToLocal(&error_ctor) || !error_ctor->IsObject() ||
        !error_ctor.As<v8::Object>()->Get(context,
            v8::String::NewFromUtf8Literal(isolate, "prototype")).ToLocal(&error_prototype) ||
        !error_prototype->IsObject() ||
        !prototype->SetPrototype(context, error_prototype).FromMaybe(false)) {
        return false;
    }
    auto ctor_key = v8::String::NewFromUtf8Literal(isolate, "constructor");
    v8::Local<v8::Value> constructor_value;
    if (!prototype->Get(context, ctor_key).ToLocal(&constructor_value) ||
        !constructor_value->IsObject() ||
        !prototype->Delete(context, ctor_key).FromMaybe(false)) {
        return false;
    }
    auto constructor = constructor_value.As<v8::Object>();
    DOMExceptionHost declaration(u"Error", u"");
    install_host_interface(isolate, context, &declaration);
    const auto constant_attrs = static_cast<v8::PropertyAttribute>(
        v8::ReadOnly | v8::DontDelete);
    for (const auto& [member, value] : kLegacyConstants) {
        if (!install_traced_api_data_property(isolate, context, constructor, member,
                v8::Integer::New(isolate, value), native_api_identity(isolate, constructor),
                "host_object", "DOMException", constant_attrs) ||
            !install_traced_api_data_property(isolate, context, prototype, member,
                v8::Integer::New(isolate, value), native_api_identity(isolate, prototype),
                "host_object", "DOMException.prototype", constant_attrs)) {
            return false;
        }
    }
    return install_traced_api_data_property(isolate, context, prototype,
        "constructor", constructor, native_api_identity(isolate, prototype),
        "host_object", "DOMException.prototype", v8::DontEnum);
}

v8::Local<v8::Value> construct_dom_exception(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    v8::Local<v8::Array> arguments) {
    const std::string prefix = "Failed to construct 'DOMException': ";
    std::u16string message_text;
    std::u16string name_text = u"Error";
    if (arguments->Length() > 0) {
        v8::Local<v8::Value> message;
        if (!arguments->Get(context, 0).ToLocal(&message)) return v8::Undefined(isolate);
        if (!message->IsUndefined() &&
            !web_idl_dom_string_u16(isolate, context, message, prefix, &message_text)) {
            return v8::Undefined(isolate);
        }
    }
    if (arguments->Length() > 1) {
        v8::Local<v8::Value> name;
        if (!arguments->Get(context, 1).ToLocal(&name)) return v8::Undefined(isolate);
        if (!name->IsUndefined() &&
            !web_idl_dom_string_u16(isolate, context, name, prefix, &name_text)) {
            return v8::Undefined(isolate);
        }
    }
    return owner->create_dom_exception(isolate, context, name_text, message_text, false);
}

v8::Local<v8::Value> make_dom_exception(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    std::string_view name, std::string_view message) {
    return owner->create_dom_exception(isolate, context,
        utf8_to_u16(isolate, name), utf8_to_u16(isolate, message), true);
}

}  // namespace iv8::browser
