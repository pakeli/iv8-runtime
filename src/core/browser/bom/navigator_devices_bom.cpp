#include "navigator_devices_bom.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "iv8/access_origin.h"
#include "iv8/dom_url.h"
#include "iv8/page_state.h"
#include "iv8/web_interface.h"
#include "../internal/common.h"
#include "../internal/trace_hooks.h"
#include "../internal/web_idl_primitive.h"
#include "../web/dom_exception.h"
#include "../window/window_trace_support.h"
#include "navigator_parts.h"

namespace iv8::browser {
namespace {

bool throw_type_error(v8::Isolate* isolate, const std::string& prefix,
    const std::string& detail) {
    const auto message = prefix + detail;
    v8::Local<v8::String> text;
    if (v8::String::NewFromUtf8(isolate, message.data(), v8::NewStringType::kNormal,
            static_cast<int>(message.size())).ToLocal(&text)) {
        isolate->ThrowException(v8::Exception::TypeError(text));
    }
    return false;
}

v8::Local<v8::Value> rejected_promise(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> reason) {
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver) ||
        !resolver->Reject(context, reason).FromMaybe(false)) {
        return v8::Undefined(isolate);
    }
    return resolver->GetPromise();
}

v8::Local<v8::Value> reject_from_try_catch(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::TryCatch& caught) {
    if (!caught.CanContinue()) {
        caught.ReThrow();
        return v8::Undefined(isolate);
    }
    auto exception = caught.Exception();
    caught.Reset();
    return rejected_promise(isolate, context, exception);
}

bool is_callable_object(v8::Local<v8::Value> value) {
    return value->IsObject() && value.As<v8::Object>()->IsCallable();
}

v8::MaybeLocal<v8::Value> user_get(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> object,
    v8::Local<v8::Value> key) {
    return access_user_operation(isolate, [&] {
        return object->Get(context, key);
    });
}

v8::MaybeLocal<v8::Value> user_call(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> callable,
    v8::Local<v8::Value> receiver, int count = 0,
    v8::Local<v8::Value>* arguments = nullptr) {
    return access_user_operation(isolate, [&] {
        return callable.As<v8::Object>()->CallAsFunction(
            context, receiver, count, arguments);
    });
}

template <typename ItemConverter>
bool convert_sequence(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, const std::string& prefix,
    ItemConverter&& convert_item) {
    if (!input->IsObject()) {
        return throw_type_error(isolate, prefix,
            "The provided value cannot be converted to a sequence.");
    }
    auto object = input.As<v8::Object>();
    v8::Local<v8::Value> method;
    if (!user_get(isolate, context, object, v8::Symbol::GetIterator(isolate))
            .ToLocal(&method)) {
        return false;
    }
    if (method->IsNullOrUndefined()) {
        return throw_type_error(isolate, prefix,
            "The object must have a callable @@iterator property.");
    }
    if (!is_callable_object(method)) {
        return throw_type_error(isolate, prefix, "@@iterator must be a callable.");
    }
    v8::Local<v8::Value> iterator_value;
    if (!user_call(isolate, context, method, input).ToLocal(&iterator_value)) {
        return false;
    }
    if (!iterator_value->IsObject()) {
        return throw_type_error(isolate, prefix,
            "Iterator object must be an object.");
    }
    auto iterator = iterator_value.As<v8::Object>();
    v8::Local<v8::Value> next;
    if (!user_get(isolate, context, iterator, v8_string(isolate, "next"))
            .ToLocal(&next)) {
        return false;
    }
    if (!is_callable_object(next)) {
        return throw_type_error(isolate, prefix,
            "Expected next() function on iterator.");
    }
    for (;;) {
        v8::Local<v8::Value> step;
        if (!user_call(isolate, context, next, iterator).ToLocal(&step)) {
            return false;
        }
        if (!step->IsObject()) {
            return throw_type_error(isolate, prefix,
                "Expected iterator.next() to return an Object.");
        }
        auto record = step.As<v8::Object>();
        v8::Local<v8::Value> item;
        v8::Local<v8::Value> done;
        // Blink's UnpackIteratorResult reads `value` before `done`.
        if (!user_get(isolate, context, record, v8_string(isolate, "value"))
                .ToLocal(&item) ||
            !user_get(isolate, context, record, v8_string(isolate, "done"))
                .ToLocal(&done)) {
            return false;
        }
        if (done->BooleanValue(isolate)) {
            return true;
        }
        if (!convert_item(item)) {
            return false;
        }
    }
}

void deliver_empty_array_promise(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto resolver = info.Data().As<v8::Promise::Resolver>();
    auto empty = v8::Array::New(isolate, 0);
    (void)access_user_operation(isolate, [&] {
        return resolver->Resolve(context, empty);
    });
}

v8::Local<v8::Value> schedule_empty_array_promise(
    v8::Isolate* isolate, v8::Local<v8::Context> context) {
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        return v8::Undefined(isolate);
    }
    auto* state = state_from_isolate(isolate);
    if (state == nullptr) return v8::Undefined(isolate);
    v8::Local<v8::Function> delivery;
    if (!v8::Function::New(context, &deliver_empty_array_promise, resolver, 0,
            v8::ConstructorBehavior::kThrow).ToLocal(&delivery)) {
        return v8::Undefined(isolate);
    }
    (void)state->register_timer(delivery, 0, false, false);
    return resolver->GetPromise();
}

void deliver_task_value(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> resolver_val;
    v8::Local<v8::Value> is_reject_val;
    v8::Local<v8::Value> payload;
    if (!data->Get(context, 0).ToLocal(&resolver_val) ||
        !data->Get(context, 1).ToLocal(&is_reject_val) ||
        !data->Get(context, 2).ToLocal(&payload)) {
        return;
    }
    auto resolver = resolver_val.As<v8::Promise::Resolver>();
    (void)access_user_operation(isolate, [&] {
        return is_reject_val->BooleanValue(isolate)
            ? resolver->Reject(context, payload)
            : resolver->Resolve(context, payload);
    });
}

v8::Local<v8::Value> schedule_task_value(v8::Isolate* isolate,
    v8::Local<v8::Context> context, bool reject, v8::Local<v8::Value> payload) {
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        return v8::Undefined(isolate);
    }
    auto* state = state_from_isolate(isolate);
    if (state == nullptr) return v8::Undefined(isolate);
    auto data = v8::Array::New(isolate, 3);
    (void)data->CreateDataProperty(context, 0, resolver);
    (void)data->CreateDataProperty(context, 1, v8::Boolean::New(isolate, reject));
    (void)data->CreateDataProperty(context, 2, payload);
    v8::Local<v8::Function> delivery;
    if (!v8::Function::New(context, &deliver_task_value, data, 0,
            v8::ConstructorBehavior::kThrow).ToLocal(&delivery)) {
        return v8::Undefined(isolate);
    }
    (void)state->register_timer(delivery, 0, false, false);
    return resolver->GetPromise();
}

void forward_microtask_false(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto resolver = info.Data().As<v8::Promise::Resolver>();
    (void)access_user_operation(isolate, [&] {
        return resolver->Resolve(context, v8::False(isolate));
    });
}

bool convert_filter_object_sequence(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> input,
    const std::string& prefix, const char* filter_type_name) {
    return convert_sequence(isolate, context, input, prefix,
        [&](v8::Local<v8::Value> item) {
            if (item->IsNullOrUndefined()) return true;
            if (!item->IsObject()) {
                return throw_type_error(isolate, prefix,
                    std::string("The provided value is not of type '") +
                    filter_type_name + "'.");
            }
            auto dict = item.As<v8::Object>();
            auto read_num = [&](const char* k) {
                v8::Local<v8::Value> v;
                if (!user_get(isolate, context, dict, v8_string(isolate, k)).ToLocal(&v))
                    return false;
                if (v->IsUndefined()) return true;
                v8::Local<v8::Number> n;
                return access_user_operation(isolate, [&] {
                    return v->ToNumber(context);
                }).ToLocal(&n);
            };
            auto read_str = [&](const char* k) {
                v8::Local<v8::Value> v;
                if (!user_get(isolate, context, dict, v8_string(isolate, k)).ToLocal(&v))
                    return false;
                if (v->IsUndefined()) return true;
                std::string out;
                return web_idl_dom_string(isolate, context, v, prefix, &out);
            };
            const std::string_view ft(filter_type_name);
            if (ft == "USBDeviceFilter") {
                return read_num("classCode") && read_num("productId") &&
                    read_num("protocolCode") && read_str("serialNumber") &&
                    read_num("subclassCode") && read_num("vendorId");
            }
            if (ft == "HIDDeviceFilter") {
                return read_num("productId") && read_num("usage") &&
                    read_num("usagePage") && read_num("vendorId");
            }
            if (ft == "SerialPortFilter") {
                v8::Local<v8::Value> bt;
                if (!user_get(isolate, context, dict,
                        v8_string(isolate, "bluetoothServiceClassId")).ToLocal(&bt)) {
                    return false;
                }
                if (!bt->IsUndefined() && !bt->IsNumber()) {
                    std::string s;
                    if (!web_idl_dom_string(isolate, context, bt, prefix, &s)) return false;
                }
                return read_num("usbProductId") && read_num("usbVendorId");
            }
            if (ft == "BluetoothLEScanFilterInit") {
                v8::Local<v8::Value> mfg;
                if (!user_get(isolate, context, dict,
                        v8_string(isolate, "manufacturerData")).ToLocal(&mfg)) {
                    return false;
                }
                if (!mfg->IsUndefined() &&
                    !convert_sequence(isolate, context, mfg, prefix,
                        [&](v8::Local<v8::Value>) { return true; })) {
                    return false;
                }
                if (!read_str("name") || !read_str("namePrefix")) return false;
                v8::Local<v8::Value> svcs;
                if (!user_get(isolate, context, dict,
                        v8_string(isolate, "services")).ToLocal(&svcs)) {
                    return false;
                }
                if (!svcs->IsUndefined() &&
                    !convert_sequence(isolate, context, svcs, prefix,
                        [&](v8::Local<v8::Value>) { return true; })) {
                    return false;
                }
            }
            return true;
        });
}

v8::Local<v8::Private> pristine_array_iterator_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.bom06.arrayValues"));
}

constexpr std::array<const char*, 13> kWgslLanguageFeatures = {{
    "packed_4x8_integer_dot_product",
    "subgroup_uniformity",
    "immediate_address_space",
    "subgroup_id",
    "linear_indexing",
    "readonly_and_readwrite_storage_textures",
    "swizzle_assignment",
    "unrestricted_pointer_parameters",
    "buffer_view",
    "texture_formats_tier1",
    "texture_and_sampler_let",
    "pointer_composite_access",
    "uniform_buffer_standard_layout",
}};

class PresentationReceiverDeclHost : public HostObject {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "PresentationReceiver"; }
    std::vector<std::string> property_names() const override {
        return {"connectionList"};
    }
    std::vector<std::string> method_names() const override { return {}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&) override {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        return v8::Undefined(isolate);
    }
};

bool move_constructor_to_end(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> prototype,
    const std::string& interface_name) {
    auto key = v8::String::NewFromUtf8Literal(isolate, "constructor");
    v8::Local<v8::Value> constructor;
    if (!prototype->Get(context, key).ToLocal(&constructor) ||
        !prototype->Delete(context, key).FromMaybe(false)) {
        return false;
    }
    return install_traced_api_data_property(isolate, context, prototype,
        "constructor", constructor, native_api_identity(isolate, prototype),
        "host_object", interface_name + ".prototype", v8::DontEnum);
}

bool move_method_after_constructor(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> prototype,
    const std::string& interface_name, const char* trailing_method) {
    auto method_key = v8_string(isolate, trailing_method).As<v8::String>();
    v8::Local<v8::Value> method_fn;
    if (!prototype->Get(context, method_key).ToLocal(&method_fn) ||
        !prototype->Delete(context, method_key).FromMaybe(false) ||
        !move_constructor_to_end(isolate, context, prototype, interface_name)) {
        return false;
    }
    return install_traced_api_data_property(isolate, context, prototype,
        trailing_method, method_fn, native_api_identity(isolate, prototype),
        "host_object", interface_name + ".prototype");
}

}  // namespace

// --- Bluetooth ---

v8::Local<v8::Value> BluetoothHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&) {
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> BluetoothHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (!validate_bom_event_dispatch(isolate, context, name, arguments)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> event_result;
    if (handle_event_method(isolate, context, name, arguments, event_result)) {
        return event_result;
    }
    if (name == "getAvailability") {
        return schedule_task_value(isolate, context, false, v8::True(isolate));
    }
    if (name == "requestDevice") {
        const std::string prefix = "Failed to execute 'requestDevice' on 'Bluetooth': ";
        v8::TryCatch caught(isolate);
        if (arguments.Length() > 0 && !arguments[0]->IsNullOrUndefined()) {
            if (!arguments[0]->IsObject()) {
                throw_type_error(isolate, prefix,
                    "The provided value is not of type 'RequestDeviceOptions'.");
                return reject_from_try_catch(isolate, context, caught);
            }
            auto opts = arguments[0].As<v8::Object>();
            for (const char* key : {"acceptAllDevices", "exclusionFilters", "filters",
                                    "optionalManufacturerData", "optionalServices"}) {
                v8::Local<v8::Value> val;
                if (!user_get(isolate, context, opts, v8_string(isolate, key))
                        .ToLocal(&val)) {
                    return reject_from_try_catch(isolate, context, caught);
                }
                if (std::string_view(key) == "acceptAllDevices") {
                    (void)val->BooleanValue(isolate);
                } else if (!val->IsUndefined()) {
                    const std::string f_prefix = prefix + "Failed to read the '" +
                        key + "' property from 'RequestDeviceOptions': ";
                    if (std::string_view(key) == "filters" ||
                        std::string_view(key) == "exclusionFilters") {
                        if (!convert_filter_object_sequence(isolate, context, val,
                                f_prefix, "BluetoothLEScanFilterInit")) {
                            return reject_from_try_catch(isolate, context, caught);
                        }
                    } else {
                        if (!convert_sequence(isolate, context, val, f_prefix,
                                [&](v8::Local<v8::Value>) { return true; })) {
                            return reject_from_try_catch(isolate, context, caught);
                        }
                    }
                }
            }
        }
        auto err = make_dom_exception(isolate, context, owner_, "SecurityError",
            prefix + "Must be handling a user gesture to show a permission request.");
        return rejected_promise(isolate, context, err);
    }
    return v8::Undefined(isolate);
}

// --- HID ---

v8::Local<v8::Value> HIDHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name) {
    if (name == "onconnect" || name == "ondisconnect") {
        return get_event_handler(isolate, name);
    }
    return v8::Undefined(isolate);
}

void HIDHost::set_property(v8::Isolate* isolate, v8::Local<v8::Context>,
    const std::string& name, v8::Local<v8::Value> value) {
    if (name == "onconnect" || name == "ondisconnect") {
        set_event_handler(isolate, name, value);
    }
}

v8::Local<v8::Value> HIDHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (!validate_bom_event_dispatch(isolate, context, name, arguments)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> event_result;
    if (handle_event_method(isolate, context, name, arguments, event_result)) {
        return event_result;
    }
    if (name == "getDevices") {
        return schedule_empty_array_promise(isolate, context);
    }
    if (name == "requestDevice") {
        const std::string prefix = "Failed to execute 'requestDevice' on 'HID': ";
        v8::TryCatch caught(isolate);
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix,
                "1 argument required, but only 0 present.");
            return reject_from_try_catch(isolate, context, caught);
        }
        if (!arguments[0]->IsObject()) {
            throw_type_error(isolate, prefix,
                "The provided value is not of type 'HIDDeviceRequestOptions'.");
            return reject_from_try_catch(isolate, context, caught);
        }
        auto opts = arguments[0].As<v8::Object>();
        v8::Local<v8::Value> excl;
        v8::Local<v8::Value> filters;
        if (!user_get(isolate, context, opts, v8_string(isolate, "exclusionFilters"))
                .ToLocal(&excl)) {
            return reject_from_try_catch(isolate, context, caught);
        }
        if (!excl->IsUndefined() &&
            !convert_filter_object_sequence(isolate, context, excl,
                prefix + "Failed to read the 'exclusionFilters' property from 'HIDDeviceRequestOptions': ",
                "HIDDeviceFilter")) {
            return reject_from_try_catch(isolate, context, caught);
        }
        if (!user_get(isolate, context, opts, v8_string(isolate, "filters"))
                .ToLocal(&filters)) {
            return reject_from_try_catch(isolate, context, caught);
        }
        if (filters->IsUndefined()) {
            throw_type_error(isolate, prefix,
                "Failed to read the 'filters' property from 'HIDDeviceRequestOptions': "
                "Required member is undefined.");
            return reject_from_try_catch(isolate, context, caught);
        }
        if (!convert_filter_object_sequence(isolate, context, filters,
                prefix + "Failed to read the 'filters' property from 'HIDDeviceRequestOptions': ",
                "HIDDeviceFilter")) {
            return reject_from_try_catch(isolate, context, caught);
        }
        auto err = make_dom_exception(isolate, context, owner_, "SecurityError",
            prefix + "Must be handling a user gesture to show a permission request.");
        return rejected_promise(isolate, context, err);
    }
    return v8::Undefined(isolate);
}

// --- Serial ---

v8::Local<v8::Value> SerialHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name) {
    if (name == "onconnect" || name == "ondisconnect") {
        return get_event_handler(isolate, name);
    }
    return v8::Undefined(isolate);
}

void SerialHost::set_property(v8::Isolate* isolate, v8::Local<v8::Context>,
    const std::string& name, v8::Local<v8::Value> value) {
    if (name == "onconnect" || name == "ondisconnect") {
        set_event_handler(isolate, name, value);
    }
}

v8::Local<v8::Value> SerialHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (!validate_bom_event_dispatch(isolate, context, name, arguments)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> event_result;
    if (handle_event_method(isolate, context, name, arguments, event_result)) {
        return event_result;
    }
    if (name == "getPorts") {
        return schedule_empty_array_promise(isolate, context);
    }
    if (name == "requestPort") {
        const std::string prefix = "Failed to execute 'requestPort' on 'Serial': ";
        v8::TryCatch caught(isolate);
        if (arguments.Length() > 0 && !arguments[0]->IsNullOrUndefined()) {
            if (!arguments[0]->IsObject()) {
                throw_type_error(isolate, prefix,
                    "The provided value is not of type 'SerialPortRequestOptions'.");
                return reject_from_try_catch(isolate, context, caught);
            }
            auto opts = arguments[0].As<v8::Object>();
            v8::Local<v8::Value> bt_ids;
            v8::Local<v8::Value> filters;
            if (!user_get(isolate, context, opts,
                    v8_string(isolate, "allowedBluetoothServiceClassIds")).ToLocal(&bt_ids)) {
                return reject_from_try_catch(isolate, context, caught);
            }
            if (!bt_ids->IsUndefined() &&
                !convert_sequence(isolate, context, bt_ids,
                    prefix + "Failed to read the 'allowedBluetoothServiceClassIds' property from 'SerialPortRequestOptions': ",
                    [&](v8::Local<v8::Value>) { return true; })) {
                return reject_from_try_catch(isolate, context, caught);
            }
            if (!user_get(isolate, context, opts, v8_string(isolate, "filters"))
                    .ToLocal(&filters)) {
                return reject_from_try_catch(isolate, context, caught);
            }
            if (!filters->IsUndefined() &&
                !convert_filter_object_sequence(isolate, context, filters,
                    prefix + "Failed to read the 'filters' property from 'SerialPortRequestOptions': ",
                    "SerialPortFilter")) {
                return reject_from_try_catch(isolate, context, caught);
            }
        }
        auto err = make_dom_exception(isolate, context, owner_, "SecurityError",
            prefix + "Must be handling a user gesture to show a permission request.");
        return rejected_promise(isolate, context, err);
    }
    return v8::Undefined(isolate);
}

// --- USB ---

v8::Local<v8::Value> USBHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name) {
    if (name == "onconnect" || name == "ondisconnect") {
        return get_event_handler(isolate, name);
    }
    return v8::Undefined(isolate);
}

void USBHost::set_property(v8::Isolate* isolate, v8::Local<v8::Context>,
    const std::string& name, v8::Local<v8::Value> value) {
    if (name == "onconnect" || name == "ondisconnect") {
        set_event_handler(isolate, name, value);
    }
}

v8::Local<v8::Value> USBHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (!validate_bom_event_dispatch(isolate, context, name, arguments)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> event_result;
    if (handle_event_method(isolate, context, name, arguments, event_result)) {
        return event_result;
    }
    if (name == "getDevices") {
        return schedule_empty_array_promise(isolate, context);
    }
    if (name == "requestDevice") {
        const std::string prefix = "Failed to execute 'requestDevice' on 'USB': ";
        v8::TryCatch caught(isolate);
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix,
                "1 argument required, but only 0 present.");
            return reject_from_try_catch(isolate, context, caught);
        }
        if (!arguments[0]->IsObject()) {
            throw_type_error(isolate, prefix,
                "The provided value is not of type 'USBDeviceRequestOptions'.");
            return reject_from_try_catch(isolate, context, caught);
        }
        auto opts = arguments[0].As<v8::Object>();
        v8::Local<v8::Value> excl;
        v8::Local<v8::Value> filters;
        if (!user_get(isolate, context, opts, v8_string(isolate, "exclusionFilters"))
                .ToLocal(&excl)) {
            return reject_from_try_catch(isolate, context, caught);
        }
        if (!excl->IsUndefined() &&
            !convert_filter_object_sequence(isolate, context, excl,
                prefix + "Failed to read the 'exclusionFilters' property from 'USBDeviceRequestOptions': ",
                "USBDeviceFilter")) {
            return reject_from_try_catch(isolate, context, caught);
        }
        if (!user_get(isolate, context, opts, v8_string(isolate, "filters"))
                .ToLocal(&filters)) {
            return reject_from_try_catch(isolate, context, caught);
        }
        if (filters->IsUndefined()) {
            throw_type_error(isolate, prefix,
                "Failed to read the 'filters' property from 'USBDeviceRequestOptions': "
                "Required member is undefined.");
            return reject_from_try_catch(isolate, context, caught);
        }
        if (!convert_filter_object_sequence(isolate, context, filters,
                prefix + "Failed to read the 'filters' property from 'USBDeviceRequestOptions': ",
                "USBDeviceFilter")) {
            return reject_from_try_catch(isolate, context, caught);
        }
        auto err = make_dom_exception(isolate, context, owner_, "SecurityError",
            prefix + "Must be handling a user gesture to show a permission request.");
        return rejected_promise(isolate, context, err);
    }
    return v8::Undefined(isolate);
}

// --- XRSystem ---

v8::Local<v8::Value> XRSystemHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name) {
    if (name == "ondevicechange") return get_event_handler(isolate, name);
    return v8::Undefined(isolate);
}

void XRSystemHost::set_property(v8::Isolate* isolate, v8::Local<v8::Context>,
    const std::string& name, v8::Local<v8::Value> value) {
    if (name == "ondevicechange") set_event_handler(isolate, name, value);
}

v8::Local<v8::Value> XRSystemHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (!validate_bom_event_dispatch(isolate, context, name, arguments)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> event_result;
    if (handle_event_method(isolate, context, name, arguments, event_result)) {
        return event_result;
    }
    const std::string prefix = "Failed to execute '" + name + "' on 'XRSystem': ";
    v8::TryCatch caught(isolate);
    if (name == "isSessionSupported" || name == "requestSession") {
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix,
                "1 argument required, but only 0 present.");
            return reject_from_try_catch(isolate, context, caught);
        }
        std::string mode;
        if (!web_idl_dom_string(isolate, context, arguments[0], prefix, &mode)) {
            return reject_from_try_catch(isolate, context, caught);
        }
        if (mode != "inline" && mode != "immersive-vr" && mode != "immersive-ar") {
            throw_type_error(isolate, prefix,
                "The provided value '" + mode +
                "' is not a valid enum value of type XRSessionMode.");
            return reject_from_try_catch(isolate, context, caught);
        }
        if (name == "isSessionSupported") {
            if (mode == "inline") {
                return resolved_promise(isolate, context, v8::True(isolate));
            }
            if (mode == "immersive-vr") {
                v8::Local<v8::Promise::Resolver> outer;
                v8::Local<v8::Promise::Resolver> hop;
                if (!v8::Promise::Resolver::New(context).ToLocal(&outer) ||
                    !v8::Promise::Resolver::New(context).ToLocal(&hop) ||
                    !hop->Resolve(context, v8::Undefined(isolate)).FromMaybe(false)) {
                    return v8::Undefined(isolate);
                }
                v8::Local<v8::Function> next_fn;
                if (v8::Function::New(context, &forward_microtask_false, outer, 0,
                        v8::ConstructorBehavior::kThrow).ToLocal(&next_fn)) {
                    (void)hop->GetPromise()->Then(context, next_fn);
                }
                return outer->GetPromise();
            }
            return resolved_promise(isolate, context, v8::False(isolate));
        }
        if (arguments.Length() > 1 && !arguments[1]->IsNullOrUndefined()) {
            if (!arguments[1]->IsObject()) {
                throw_type_error(isolate, prefix,
                    "The provided value is not of type 'XRSessionInit'.");
                return reject_from_try_catch(isolate, context, caught);
            }
            auto init = arguments[1].As<v8::Object>();
            for (const char* key : {"depthSensing", "domOverlay",
                                    "optionalFeatures", "requiredFeatures"}) {
                v8::Local<v8::Value> val;
                if (!user_get(isolate, context, init, v8_string(isolate, key))
                        .ToLocal(&val)) {
                    return reject_from_try_catch(isolate, context, caught);
                }
                if ((std::string_view(key) == "optionalFeatures" ||
                     std::string_view(key) == "requiredFeatures") &&
                    !val->IsUndefined()) {
                    const std::string f_prefix = prefix + "Failed to read the '" +
                        key + "' property from 'XRSessionInit': ";
                    if (!convert_sequence(isolate, context, val, f_prefix,
                            [&](v8::Local<v8::Value> item) {
                                std::string feat;
                                return web_idl_dom_string(
                                    isolate, context, item, f_prefix, &feat);
                            })) {
                        return reject_from_try_catch(isolate, context, caught);
                    }
                }
            }
        }
        if (mode == "inline") {
            auto err = make_dom_exception(isolate, context, owner_,
                "NotSupportedError",
                "The specified session configuration is not supported.");
            return schedule_task_value(isolate, context, true, err);
        }
        auto err = make_dom_exception(isolate, context, owner_, "SecurityError",
            prefix + "The requested session requires user activation.");
        return rejected_promise(isolate, context, err);
    }
    return v8::Undefined(isolate);
}

// --- WGSLLanguageFeatures & GPU ---

v8::Local<v8::Value> WGSLLanguageFeaturesHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name) {
    if (name == "size") {
        return v8::Integer::New(isolate, static_cast<int>(kWgslLanguageFeatures.size()));
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> WGSLLanguageFeaturesHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    const std::string prefix =
        "Failed to execute '" + name + "' on 'WGSLLanguageFeatures': ";
    if (name == "has") {
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix,
                "1 argument required, but only 0 present.");
            return v8::Undefined(isolate);
        }
        std::string token;
        if (!web_idl_dom_string(isolate, context, arguments[0], prefix, &token)) {
            return v8::Undefined(isolate);
        }
        for (const char* feat : kWgslLanguageFeatures) {
            if (token == feat) return v8::True(isolate);
        }
        return v8::False(isolate);
    }
    if (name == "forEach") {
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix,
                "1 argument required, but only 0 present.");
            return v8::Undefined(isolate);
        }
        if (!is_callable_object(arguments[0])) {
            throw_type_error(isolate, prefix,
                "parameter 1 is not of type 'Function'.");
            return v8::Undefined(isolate);
        }
        const v8::Local<v8::Value> this_arg = arguments.Length() > 1
            ? arguments[1] : v8::Local<v8::Value>(v8::Undefined(isolate));
        for (const char* feat : kWgslLanguageFeatures) {
            auto s = v8_string(isolate, feat);
            v8::Local<v8::Value> cb_args[] = {s, s, arguments.This()};
            if (user_call(isolate, context, arguments[0], this_arg, 3, cb_args).IsEmpty()) {
                return v8::Undefined(isolate);
            }
        }
        return v8::Undefined(isolate);
    }
    if (name == "entries" || name == "keys" || name == "values") {
        auto arr = v8::Array::New(isolate, static_cast<int>(kWgslLanguageFeatures.size()));
        for (std::uint32_t i = 0; i < kWgslLanguageFeatures.size(); ++i) {
            auto s = v8_string(isolate, kWgslLanguageFeatures[i]);
            if (name == "entries") {
                auto pair = v8::Array::New(isolate, 2);
                (void)pair->CreateDataProperty(context, 0, s);
                (void)pair->CreateDataProperty(context, 1, s);
                (void)arr->CreateDataProperty(context, i, pair);
            } else {
                (void)arr->CreateDataProperty(context, i, s);
            }
        }
        v8::Local<v8::Value> iter_fn;
        v8::Local<v8::Value> iter;
        if (!context->Global()
                 ->GetPrivate(context, pristine_array_iterator_key(isolate))
                 .ToLocal(&iter_fn) ||
            !iter_fn->IsFunction() ||
            !iter_fn.As<v8::Function>()->Call(context, arr, 0, nullptr).ToLocal(&iter)) {
            return v8::Undefined(isolate);
        }
        return iter;
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> GPUHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name) {
    if (name == "wgslLanguageFeatures") {
        if (features_object_.IsEmpty()) {
            features_object_.Reset(
                isolate, make_host_object(isolate, context, &features_));
        }
        return features_object_.Get(isolate);
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> GPUHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (name == "getPreferredCanvasFormat") {
        return v8_string(isolate, "bgra8unorm");
    }
    if (name == "requestAdapter") {
        const std::string prefix = "Failed to execute 'requestAdapter' on 'GPU': ";
        v8::TryCatch caught(isolate);
        if (arguments.Length() > 0 && !arguments[0]->IsNullOrUndefined()) {
            if (!arguments[0]->IsObject()) {
                throw_type_error(isolate, prefix,
                    "The provided value is not of type 'GPURequestAdapterOptions'.");
                return reject_from_try_catch(isolate, context, caught);
            }
            auto opts = arguments[0].As<v8::Object>();
            for (const char* key : {"compatibilityMode", "featureLevel",
                                    "forceFallbackAdapter", "powerPreference",
                                    "xrCompatible"}) {
                v8::Local<v8::Value> val;
                if (!user_get(isolate, context, opts, v8_string(isolate, key))
                        .ToLocal(&val)) {
                    return reject_from_try_catch(isolate, context, caught);
                }
                if (std::string_view(key) == "powerPreference" && !val->IsUndefined()) {
                    const std::string p_prefix = prefix +
                        "Failed to read the 'powerPreference' property from 'GPURequestAdapterOptions': ";
                    std::string pref;
                    if (!web_idl_dom_string(isolate, context, val, p_prefix, &pref)) {
                        return reject_from_try_catch(isolate, context, caught);
                    }
                    if (pref != "low-power" && pref != "high-performance") {
                        throw_type_error(isolate, p_prefix,
                            "The provided value '" + pref +
                            "' is not a valid enum value of type GPUPowerPreference.");
                        return reject_from_try_catch(isolate, context, caught);
                    }
                } else if (std::string_view(key) == "featureLevel" && !val->IsUndefined()) {
                    std::string level;
                    if (!web_idl_dom_string(isolate, context, val, prefix, &level)) {
                        return reject_from_try_catch(isolate, context, caught);
                    }
                } else {
                    (void)val->BooleanValue(isolate);
                }
            }
        }
        return resolved_promise(isolate, context, v8::Null(isolate));
    }
    return v8::Undefined(isolate);
}

// --- Presentation & PresentationRequest ---

v8::Local<v8::Value> PresentationRequestHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name) {
    if (name == "onconnectionavailable") return get_event_handler(isolate, name);
    return v8::Undefined(isolate);
}

void PresentationRequestHost::set_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name, v8::Local<v8::Value> value) {
    if (name == "onconnectionavailable") set_event_handler(isolate, name, value);
}

v8::Local<v8::Value> PresentationRequestHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (!validate_bom_event_dispatch(isolate, context, name, arguments)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> event_result;
    if (handle_event_method(isolate, context, name, arguments, event_result)) {
        return event_result;
    }
    const std::string prefix =
        "Failed to execute '" + name + "' on 'PresentationRequest': ";
    v8::TryCatch caught(isolate);
    if (name == "getAvailability") {
        auto err = make_dom_exception(isolate, context, owner_,
            "NotSupportedError", "Availability monitoring is not supported.");
        return schedule_task_value(isolate, context, true, err);
    }
    if (name == "start") {
        auto err = make_dom_exception(isolate, context, owner_,
            "InvalidAccessError",
            prefix + "PresentationRequest::start() requires user gesture.");
        return rejected_promise(isolate, context, err);
    }
    if (name == "reconnect") {
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix,
                "1 argument required, but only 0 present.");
            return reject_from_try_catch(isolate, context, caught);
        }
        std::string id;
        if (!web_idl_usv_string(isolate, context, arguments[0], prefix, &id)) {
            return reject_from_try_catch(isolate, context, caught);
        }
        auto err = make_dom_exception(isolate, context, owner_,
            "NotFoundError", "No presentation connection found for id.");
        return schedule_task_value(isolate, context, true, err);
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> construct_presentation_request(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    const std::string& base_url, v8::Local<v8::Array> arguments) {
    const std::string prefix = "Failed to construct 'PresentationRequest': ";
    if (arguments->Length() < 1) {
        throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> arg0;
    if (!arguments->Get(context, 0).ToLocal(&arg0)) return v8::Undefined(isolate);
    std::vector<std::string> raw_urls;
    bool as_sequence = false;
    if (arg0->IsObject()) {
        v8::Local<v8::Value> iter;
        if (!user_get(isolate, context, arg0.As<v8::Object>(),
                v8::Symbol::GetIterator(isolate)).ToLocal(&iter)) {
            return v8::Undefined(isolate);
        }
        // Blink performs two separate Symbol.iterator lookups here: one for
        // overload resolution and one inside sequence conversion (verified in Chrome).
        if (!iter->IsNullOrUndefined()) {
            as_sequence = true;
            if (!convert_sequence(isolate, context, arg0, prefix,
                    [&](v8::Local<v8::Value> item) {
                        std::string u;
                        if (!web_idl_usv_string(isolate, context, item, prefix, &u)) {
                            return false;
                        }
                        raw_urls.push_back(std::move(u));
                        return true;
                    })) {
                return v8::Undefined(isolate);
            }
        }
    }
    if (!as_sequence) {
        std::string single;
        if (!web_idl_usv_string(isolate, context, arg0, prefix, &single)) {
            return v8::Undefined(isolate);
        }
        raw_urls.push_back(std::move(single));
    }
    if (raw_urls.empty()) {
        auto err = make_dom_exception(isolate, context, owner,
            "NotSupportedError",
            prefix + "Do not support empty sequence of URLs.");
        isolate->ThrowException(err);
        return v8::Undefined(isolate);
    }
    std::vector<std::string> resolved_urls;
    for (const auto& raw : raw_urls) {
        const DomUrl parsed = resolve_dom_url(raw, base_url);
        if (!parsed.valid) {
            auto err = make_dom_exception(isolate, context, owner,
                "SyntaxError",
                prefix + "'" + raw + "' can't be resolved to a valid URL.");
            isolate->ThrowException(err);
            return v8::Undefined(isolate);
        }
        if (parsed.protocol != "http:" && parsed.protocol != "https:" &&
            parsed.protocol != "cast:" && parsed.protocol != "cast-dial:") {
            auto err = make_dom_exception(isolate, context, owner,
                "NotSupportedError",
                prefix + "Do not support URL '" + parsed.href + "'.");
            isolate->ThrowException(err);
            return v8::Undefined(isolate);
        }
        resolved_urls.push_back(parsed.href);
    }
    return owner->adopt_host_object(isolate, context,
        std::make_unique<PresentationRequestHost>(owner, std::move(resolved_urls)));
}

v8::Local<v8::Value> PresentationHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name) {
    if (name == "defaultRequest") {
        return default_request_.IsEmpty()
            ? v8::Local<v8::Value>(v8::Null(isolate))
            : v8::Local<v8::Value>(default_request_.Get(isolate));
    }
    if (name == "receiver") {
        return v8::Null(isolate);
    }
    return v8::Undefined(isolate);
}

void PresentationHost::set_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name, v8::Local<v8::Value> value) {
    if (name != "defaultRequest") return;
    if (value->IsNullOrUndefined()) {
        default_request_.Reset();
        return;
    }
    auto* host = host_object_backing(value);
    if (host == nullptr || host->prototype_key() != "PresentationRequest") {
        throw_type_error(isolate,
            "Failed to set the 'defaultRequest' property on 'Presentation': ",
            "Failed to convert value to 'PresentationRequest'.");
        return;
    }
    default_request_.Reset(isolate, value.As<v8::Object>());
}

v8::Local<v8::Value> PresentationHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&,
    const v8::FunctionCallbackInfo<v8::Value>&) {
    return v8::Undefined(isolate);
}

// --- Interface installation ---

bool install_navigator_bom06_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner, bool secure_context) {
    if (!secure_context) {
        return true;
    }
    auto sample_array = v8::Array::New(isolate, 0);
    v8::Local<v8::Value> array_iter;
    if (!sample_array->Get(context, v8::Symbol::GetIterator(isolate))
             .ToLocal(&array_iter) ||
        !array_iter->IsFunction() ||
        !context->Global()
             ->SetPrivate(context, pristine_array_iterator_key(isolate), array_iter)
             .FromMaybe(false)) {
        return false;
    }
    BluetoothHost bluetooth(owner);
    XRSystemHost xr(owner);
    GPUHost gpu;
    WGSLLanguageFeaturesHost wgsl;
    PresentationHost presentation;
    PresentationRequestHost pres_req(owner, {});
    PresentationReceiverDeclHost pres_recv;

    for (HostObject* host : {static_cast<HostObject*>(&bluetooth),
                             static_cast<HostObject*>(&xr),
                             static_cast<HostObject*>(&gpu),
                             static_cast<HostObject*>(&wgsl),
                             static_cast<HostObject*>(&presentation),
                             static_cast<HostObject*>(&pres_req),
                             static_cast<HostObject*>(&pres_recv)}) {
        auto proto = web_interface_prototype(isolate, context, host->prototype_key());
        if (proto.IsEmpty()) return false;
        install_host_interface(isolate, context, host);
        if (!move_constructor_to_end(isolate, context, proto, host->prototype_key())) {
            return false;
        }
    }

    // HID, Serial, and USB expose requestDevice/requestPort after constructor.
    HIDHost hid(owner);
    SerialHost serial(owner);
    USBHost usb(owner);
    for (const auto& [host, trailing] :
        std::initializer_list<std::pair<HostObject*, const char*>>{
            {&hid, "requestDevice"},
            {&serial, "requestPort"},
            {&usb, "requestDevice"}}) {
        auto proto = web_interface_prototype(isolate, context, host->prototype_key());
        if (proto.IsEmpty()) return false;
        install_host_interface(isolate, context, host);
        if (!move_method_after_constructor(
                isolate, context, proto, host->prototype_key(), trailing)) {
            return false;
        }
    }

    // WGSLLanguageFeatures is a setlike<DOMString>: @@iterator === values.
    auto wgsl_proto = web_interface_prototype(isolate, context, "WGSLLanguageFeatures");
    v8::Local<v8::Value> values_fn;
    if (wgsl_proto.IsEmpty() ||
        !wgsl_proto->Get(context, v8_string(isolate, "values")).ToLocal(&values_fn) ||
        !install_traced_api_named_data_property(isolate, context, wgsl_proto,
            v8::Symbol::GetIterator(isolate), values_fn,
            native_api_identity(isolate, wgsl_proto), "host_object",
            "WGSLLanguageFeatures.prototype", v8::DontEnum)) {
        return false;
    }
    return true;
}

}  // namespace iv8::browser
