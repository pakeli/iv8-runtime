#include "navigator_identity.h"

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "iv8/access_origin.h"
#include "../internal/common.h"

namespace iv8::browser {
namespace {

v8::MaybeLocal<v8::Array> brand_array(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::vector<UserAgentBrand>& brands,
    bool frozen) {
    auto array = v8::Array::New(isolate, static_cast<int>(brands.size()));
    for (std::size_t index = 0; index < brands.size(); ++index) {
        auto item = v8::Object::New(isolate);
        if (!item->CreateDataProperty(context, v8_string(isolate, "brand").As<v8::String>(),
                v8_string(isolate, brands[index].brand)).FromMaybe(false) ||
            !item->CreateDataProperty(context, v8_string(isolate, "version").As<v8::String>(),
                v8_string(isolate, brands[index].version)).FromMaybe(false) ||
            !access_user_operation(isolate, [&] {
                return array->Set(context, static_cast<std::uint32_t>(index), item);
            }).FromMaybe(false)) {
            return {};
        }
    }
    if (frozen && !array->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen).FromMaybe(false)) return {};
    return array;
}

v8::MaybeLocal<v8::Object> low_entropy_values(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const HostEnvironment& environment) {
    v8::Local<v8::Array> brands;
    if (!brand_array(isolate, context, environment.user_agent_data_brands, false).ToLocal(&brands)) return {};
    auto object = v8::Object::New(isolate);
    if (!object->CreateDataProperty(context, v8_string(isolate, "brands").As<v8::String>(), brands).FromMaybe(false) ||
        !object->CreateDataProperty(context, v8_string(isolate, "mobile").As<v8::String>(),
            v8::Boolean::New(isolate, environment.user_agent_data_mobile)).FromMaybe(false) ||
        !object->CreateDataProperty(context, v8_string(isolate, "platform").As<v8::String>(),
            v8_string(isolate, environment.user_agent_data_platform)).FromMaybe(false)) return {};
    return object;
}

bool read_hints(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> value, std::unordered_set<std::string>* hints) {
    const auto fail = [&]() {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate,
                "Failed to execute 'getHighEntropyValues' on 'NavigatorUAData': "
                "The provided value cannot be converted to a sequence.")));
        return false;
    };
    if (!value->IsObject()) return fail();
    auto input = value.As<v8::Object>();
    v8::Local<v8::Value> iterator_method;
    if (!access_user_operation(isolate, [&] {
            return input->Get(context, v8::Symbol::GetIterator(isolate));
        }).ToLocal(&iterator_method)) return false;
    if (!iterator_method->IsFunction()) return fail();
    v8::Local<v8::Value> iterator_value;
    if (!access_user_operation(isolate, [&] {
            return iterator_method.As<v8::Function>()->Call(context, input, 0, nullptr);
        }).ToLocal(&iterator_value)) return false;
    if (!iterator_value->IsObject()) return fail();
    auto iterator = iterator_value.As<v8::Object>();
    v8::Local<v8::Value> next;
    if (!access_user_operation(isolate, [&] {
            return iterator->Get(context, v8_string(isolate, "next"));
        }).ToLocal(&next)) return false;
    if (!next->IsFunction()) return fail();
    while (true) {
        v8::Local<v8::Value> result_value;
        if (!access_user_operation(isolate, [&] {
                return next.As<v8::Function>()->Call(context, iterator, 0, nullptr);
            }).ToLocal(&result_value)) return false;
        if (!result_value->IsObject()) return fail();
        auto result = result_value.As<v8::Object>();
        v8::Local<v8::Value> done;
        if (!access_user_operation(isolate, [&] {
                return result->Get(context, v8_string(isolate, "done"));
            }).ToLocal(&done)) return false;
        if (done->BooleanValue(isolate)) return true;
        v8::Local<v8::Value> item;
        v8::Local<v8::String> text;
        if (!access_user_operation(isolate, [&] {
                return result->Get(context, v8_string(isolate, "value"));
            }).ToLocal(&item) ||
            !access_user_operation(isolate, [&] {
                return item->ToString(context);
            }).ToLocal(&text)) return false;
        v8::String::Utf8Value utf8(isolate, text);
        if (*utf8 == nullptr) return false;
        const std::string hint(*utf8, static_cast<std::size_t>(utf8.length()));
        if (hint == "architecture" || hint == "bitness" || hint == "model" ||
            hint == "platformVersion" || hint == "uaFullVersion" ||
            hint == "fullVersionList" || hint == "wow64") {
            hints->insert(hint);
        }
    }
}

}  // namespace

void install_navigator_identity_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const HostEnvironment* environment) {
    NavigatorUADataHost user_agent_data(environment);
    UserActivationHost user_activation;
    DOMStringListHost ancestor_origins;
    install_host_interface(isolate, context, &user_agent_data);
    install_host_interface(isolate, context, &user_activation);
    install_host_interface(isolate, context, &ancestor_origins);
}

v8::Local<v8::Value> NavigatorUADataHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name) {
    if (name == "mobile") return v8::Boolean::New(isolate, environment_->user_agent_data_mobile);
    if (name == "platform") return v8_string(isolate, environment_->user_agent_data_platform);
    if (name == "brands") {
        v8::Local<v8::Array> brands;
        if (brand_array(isolate, context, environment_->user_agent_data_brands, true).ToLocal(&brands)) return brands;
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> NavigatorUADataHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    if (name == "toJSON") {
        v8::Local<v8::Object> value;
        if (low_entropy_values(isolate, context, *environment_).ToLocal(&value)) return value;
        return v8::Undefined(isolate);
    }
    if (name != "getHighEntropyValues") return v8::Undefined(isolate);
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) return v8::Undefined(isolate);
    v8::TryCatch caught(isolate);
    std::unordered_set<std::string> hints;
    const auto input = arguments.Length() ? arguments[0] : v8::Local<v8::Value>(v8::Undefined(isolate));
    bool valid_hints = false;
    if (arguments.Length() == 0) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate,
                "Failed to execute 'getHighEntropyValues' on 'NavigatorUAData': "
                "1 argument required, but only 0 present.")));
    } else {
        valid_hints = read_hints(isolate, context, input, &hints);
    }
    if (!valid_hints) {
        if (caught.HasCaught()) {
            auto error = caught.Exception();
            caught.Reset();
            if (!resolver->Reject(context, error).FromMaybe(false)) return v8::Undefined(isolate);
        }
        return resolver->GetPromise();
    }
    v8::Local<v8::Object> result;
    if (!low_entropy_values(isolate, context, *environment_).ToLocal(&result)) return v8::Undefined(isolate);
    const auto add_string = [&](const char* key, const std::string& value) {
        return !hints.contains(key) || result->CreateDataProperty(context,
            v8_string(isolate, key).As<v8::String>(), v8_string(isolate, value)).FromMaybe(false);
    };
    if (!add_string("architecture", environment_->user_agent_data_architecture) ||
        !add_string("bitness", environment_->user_agent_data_bitness) ||
        !add_string("model", environment_->user_agent_data_model) ||
        !add_string("platformVersion", environment_->user_agent_data_platform_version) ||
        !add_string("uaFullVersion", environment_->user_agent_data_full_version)) return v8::Undefined(isolate);
    if (hints.contains("fullVersionList")) {
        v8::Local<v8::Array> brands;
        if (!brand_array(isolate, context, environment_->user_agent_data_full_version_list, false).ToLocal(&brands) ||
            !result->CreateDataProperty(context, v8_string(isolate, "fullVersionList").As<v8::String>(), brands).FromMaybe(false)) return v8::Undefined(isolate);
    }
    if (hints.contains("wow64") && !result->CreateDataProperty(context,
            v8_string(isolate, "wow64").As<v8::String>(),
            v8::Boolean::New(isolate, environment_->user_agent_data_wow64)).FromMaybe(false)) return v8::Undefined(isolate);
    if (!access_user_operation(isolate, [&] {
            return resolver->Resolve(context, result);
        }).FromMaybe(false)) return v8::Undefined(isolate);
    return resolver->GetPromise();
}

v8::Local<v8::Value> UserActivationHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name) {
    if (name == "hasBeenActive" || name == "isActive") return v8::False(isolate);
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> UserActivationHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string&,
    const v8::FunctionCallbackInfo<v8::Value>&) {
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> DOMStringListHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name) {
    if (name == "length") return v8::Integer::New(isolate, 0);
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> DOMStringListHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    if (arguments.Length() == 0) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8_string(isolate, "Failed to execute '" + name +
                "' on 'DOMStringList': 1 argument required, but only 0 present.").As<v8::String>()));
        return v8::Undefined(isolate);
    }
    if (name == "item") {
        if (access_user_operation(isolate, [&] {
                return arguments[0]->Uint32Value(context);
            }).IsNothing()) return v8::Undefined(isolate);
        return v8::Null(isolate);
    }
    if (name == "contains") {
        v8::Local<v8::String> text;
        if (!access_user_operation(isolate, [&] {
                return arguments[0]->ToString(context);
            }).ToLocal(&text)) return v8::Undefined(isolate);
        return v8::False(isolate);
    }
    return v8::Undefined(isolate);
}

}  // namespace iv8::browser
