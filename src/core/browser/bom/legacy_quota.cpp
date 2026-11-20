#include "legacy_quota.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "iv8/access_origin.h"
#include "iv8/page_state.h"
#include "iv8/url_parser.h"
#include "iv8/web_interface.h"
#include "../internal/common.h"
#include "../internal/trace_hooks.h"
#include "../internal/web_idl_primitive.h"
#include "../window/window_trace_support.h"

namespace iv8::browser {
namespace {

bool fail(v8::Isolate* isolate, const std::string& prefix, const std::string& detail) {
    const auto message = prefix + detail;
    v8::Local<v8::String> text;
    if (v8::String::NewFromUtf8(isolate, message.data(), v8::NewStringType::kNormal,
            static_cast<int>(message.size())).ToLocal(&text)) {
        isolate->ThrowException(v8::Exception::TypeError(text));
    }
    return false;
}

bool callback_value(v8::Isolate* isolate, const std::string& prefix,
    v8::Local<v8::Value> value, int position, bool nullable) {
    if (nullable && value->IsNullOrUndefined()) return true;
    if (value->IsObject() && value.As<v8::Object>()->IsCallable()) return true;
    return fail(isolate, prefix, "parameter " + std::to_string(position) +
        " is not of type 'Function'.");
}

void invoke_callback(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> callback, int count, v8::Local<v8::Value>* values) {
    if (callback->IsNullOrUndefined()) return;
    v8::TryCatch caught(isolate);
    (void)access_user_operation(isolate, [&] {
        return callback.As<v8::Object>()->CallAsFunction(
            context, v8::Undefined(isolate), count, values);
    });
    if (caught.HasCaught()) {
        if (!caught.CanContinue()) { caught.ReThrow(); return; }
        if (auto* state = state_from_isolate(isolate))
            state->record_async_error(isolate, context, caught, "storage_quota");
        caught.Reset();
    }
}

void deliver_quota_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> callback, first, second;
    if (!data->Get(context, 0).ToLocal(&callback) ||
        !data->Get(context, 1).ToLocal(&first)) return;
    v8::Local<v8::Value> values[] = {first, v8::Undefined(isolate)};
    if (data->Length() == 3) {
        if (!data->Get(context, 2).ToLocal(&second)) return;
        values[1] = second;
    }
    invoke_callback(isolate, context, callback, data->Length() == 3 ? 2 : 1, values);
}

bool schedule_callback(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> callback, int count, v8::Local<v8::Value>* values) {
    if (callback->IsNullOrUndefined()) return true;
    auto* state = state_from_isolate(isolate);
    if (!state) return false;
    auto data = v8::Array::New(isolate, count + 1);
    if (!data->CreateDataProperty(context, 0, callback).FromMaybe(false)) return false;
    for (int index = 0; index < count; ++index) {
        if (!data->CreateDataProperty(context, index + 1, values[index]).FromMaybe(false)) return false;
    }
    v8::Local<v8::Function> delivery;
    if (!v8::Function::New(context, &deliver_quota_callback, data, 0,
            v8::ConstructorBehavior::kThrow).ToLocal(&delivery)) return false;
    (void)state->register_timer(delivery, 0, false, false);
    return true;
}

bool quota_amount(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, const std::string& prefix, double* amount) {
    v8::Local<v8::Value> primitive;
    if (!web_idl_to_primitive(isolate, context, input, false, prefix, &primitive)) return false;
    if (primitive->IsSymbol()) return fail(isolate, prefix, "Cannot convert a Symbol value to a number");
    if (primitive->IsBigInt()) return fail(isolate, prefix, "Cannot convert a BigInt value to a number");
    const auto number = primitive->NumberValue(context);
    if (number.IsNothing()) return false;
    const double value = number.FromJust();
    if (!std::isfinite(value) || value == 0) { *amount = 0; return true; }
    // WebIDL unsigned long long: truncate, then reduce modulo 2^64. The API
    // reports only min(converted, available quota), avoiding an overflowing cast.
    const double modulus = 18446744073709551616.0;
    double remainder = std::fmod(std::trunc(value), modulus);
    if (remainder == 0) { *amount = 0; return true; }
    if (remainder < 0) remainder += modulus;
    *amount = std::min(remainder, kOfflineStorageQuota);
    return true;
}

bool dom_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, std::u16string* output) {
    const std::string prefix = "Failed to construct 'DOMError': ";
    v8::Local<v8::Value> primitive;
    if (!web_idl_to_primitive(isolate, context, input, true, prefix, &primitive)) return false;
    if (primitive->IsSymbol()) return fail(isolate, prefix, "Cannot convert a Symbol value to a string");
    v8::Local<v8::String> text;
    if (!primitive->ToString(context).ToLocal(&text)) return false;
    v8::String::Value units(isolate, text);
    if (!units.length()) { output->clear(); return true; }
    if (!*units && units.length()) return false;
    output->assign(reinterpret_cast<const char16_t*>(*units), static_cast<std::size_t>(units.length()));
    return true;
}

}  // namespace

LegacyQuotaHost::LegacyQuotaHost(PageState* owner, const std::string& base_url)
    : owner_(owner) {
    const auto protocol = ascii_lower(decompose_url(base_url).protocol);
    opaque_ = protocol != "http:" && protocol != "https:" && protocol != "ws:" &&
        protocol != "wss:" && protocol != "ftp:" && protocol != "file:";
}

v8::Local<v8::Value> DOMErrorHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name) {
    if (name != "name" && name != "message") return v8::Undefined(isolate);
    const auto& units = name == "name" ? name_ : message_;
    v8::Local<v8::String> value;
    if (!v8::String::NewFromTwoByte(isolate, reinterpret_cast<const std::uint16_t*>(units.data()),
            v8::NewStringType::kNormal, static_cast<int>(units.size())).ToLocal(&value))
        return v8::Undefined(isolate);
    return value;
}

v8::Local<v8::Value> DOMErrorHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string&,
    const v8::FunctionCallbackInfo<v8::Value>&) { return v8::Undefined(isolate); }

v8::Local<v8::Value> LegacyQuotaHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string&) { return v8::Undefined(isolate); }

v8::Local<v8::Value> LegacyQuotaHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    if (name != "queryUsageAndQuota" && name != "requestQuota") return v8::Undefined(isolate);
    watch_record(isolate, trace_label(), name);
    watch_record(isolate, "navigator.webkitTemporaryStorage", name);
    watch_record(isolate, "navigator.webkitPersistentStorage", name);
    const std::string prefix = "Failed to execute '" + name + "' on 'DeprecatedStorageQuota': ";
    if (arguments.Length() < 1) {
        fail(isolate, prefix, "1 argument required, but only 0 present.");
        return v8::Undefined(isolate);
    }
    const bool query = name == "queryUsageAndQuota";
    const auto at = [&](int index) -> v8::Local<v8::Value> {
        return arguments.Length() > index ? arguments[index] : v8::Local<v8::Value>(v8::Undefined(isolate));
    };
    double requested = 0;
    if (!query && !quota_amount(isolate, context, at(0), prefix, &requested)) return v8::Undefined(isolate);
    auto success = at(query ? 0 : 1);
    auto error = at(query ? 1 : 2);
    if (!callback_value(isolate, prefix, success, query ? 1 : 2, !query) ||
        !callback_value(isolate, prefix, error, query ? 2 : 3, true)) return v8::Undefined(isolate);
    if (opaque_) {
        if (error->IsNullOrUndefined()) return v8::Undefined(isolate);
        auto failure = owner_->create_dom_error(isolate, context,
            query ? u"NotSupportedError" : u"AbortError",
            query ? u"The implementation did not support the requested type of object or operation."
                  : u"The user aborted a request.");
        if (failure.IsEmpty()) return v8::Undefined(isolate);
        if (query) (void)schedule_callback(isolate, context, error, 1, &failure);
        else invoke_callback(isolate, context, error, 1, &failure);
    } else {
        v8::Local<v8::Value> values[] = {v8::Number::New(isolate, query ? 0 : requested),
            v8::Number::New(isolate, kOfflineStorageQuota)};
        (void)schedule_callback(isolate, context, success, query ? 2 : 1, values);
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> construct_dom_error(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner, v8::Local<v8::Array> arguments) {
    if (!arguments->Length()) {
        fail(isolate, "Failed to construct 'DOMError': ", "1 argument required, but only 0 present.");
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> name, message;
    std::u16string name_text, message_text;
    if (!arguments->Get(context, 0).ToLocal(&name) ||
        !dom_string(isolate, context, name, &name_text)) return v8::Undefined(isolate);
    if (arguments->Length() > 1) {
        if (!arguments->Get(context, 1).ToLocal(&message)) return v8::Undefined(isolate);
        if (!message->IsUndefined() && !dom_string(isolate, context, message, &message_text))
            return v8::Undefined(isolate);
    }
    return owner->create_dom_error(isolate, context, name_text, message_text);
}

bool install_legacy_quota_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner) {
    LegacyQuotaHost quota(owner, "about:blank");
    DOMErrorHost error(u"", u"");
    install_host_interface(isolate, context, &quota);
    install_host_interface(isolate, context, &error);
    auto prototype = web_interface_prototype(isolate, context, "DOMError");
    if (prototype.IsEmpty()) return false;
    auto key = v8::String::NewFromUtf8Literal(isolate, "constructor");
    v8::Local<v8::Value> constructor;
    if (!prototype->Get(context, key).ToLocal(&constructor) ||
        !prototype->Delete(context, key).FromMaybe(false)) return false;
    return install_traced_api_data_property(isolate, context, prototype, "constructor", constructor,
        native_api_identity(isolate, prototype), "host_object", "DOMError.prototype", v8::DontEnum);
}

}  // namespace iv8::browser
