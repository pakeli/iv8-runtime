#include "navigator_capabilities.h"

#include "iv8/access_origin.h"
#include "iv8/web_interface.h"
#include "../internal/common.h"
#include "../internal/trace_hooks.h"
#include "../window/window_trace_support.h"

namespace iv8::browser {

bool install_navigator_capability_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context) {
    SchedulingHost scheduling;
    install_host_interface(isolate, context, &scheduling);
    auto prototype = web_interface_prototype(isolate, context, "Scheduling");
    if (prototype.IsEmpty() || !prototype->HasOwnProperty(context,
            v8_string(isolate, "isInputPending").As<v8::String>()).FromMaybe(false)) return false;
    // Chrome orders the interface operation before its constructor. Move only
    // this newly created interface's constructor, retaining its native function
    // identity and the shared writable/configurable/non-enumerable descriptor.
    v8::Local<v8::Value> constructor;
    auto name = v8_string(isolate, "constructor").As<v8::String>();
    if (!prototype->Get(context, name).ToLocal(&constructor) ||
        !prototype->Delete(context, name).FromMaybe(false) ||
        !install_traced_api_data_property(isolate, context, prototype, "constructor", constructor,
            native_api_identity(isolate, prototype), "host_object", "Scheduling.prototype",
            v8::DontEnum)) return false;
    return true;
}

v8::Local<v8::Value> empty_gamepad_snapshot(v8::Isolate* isolate,
    v8::Local<v8::Context> context) {
    watch_record(isolate, "navigator", "getGamepads");
    constexpr int kEmptyGamepadSlots = 8;
    auto snapshot = v8::Array::New(isolate, kEmptyGamepadSlots);
    for (int index = 0; index < kEmptyGamepadSlots; ++index) {
        // Native sequence conversion creates own array entries. An inherited
        // JavaScript index setter must not run while constructing the snapshot.
        if (!snapshot->CreateDataProperty(context, index, v8::Null(isolate)).FromMaybe(false))
            return v8::Undefined(isolate);
    }
    return snapshot;
}

v8::Local<v8::Value> SchedulingHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string&) {
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> SchedulingHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    if (name != "isInputPending") return v8::Undefined(isolate);
    watch_record(isolate, "navigator.scheduling", name);
    if (arguments.Length() != 0 && !arguments[0]->IsNullOrUndefined()) {
        if (!arguments[0]->IsObject()) {
            isolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8Literal(isolate,
                    "Failed to execute 'isInputPending' on 'Scheduling': "
                    "The provided value is not of type 'IsInputPendingOptions'.")));
            return v8::Undefined(isolate);
        }
        v8::Local<v8::Value> include_continuous;
        if (!access_user_operation(isolate, [&] {
                return arguments[0].As<v8::Object>()->Get(context,
                    v8_string(isolate, "includeContinuous"));
            }).ToLocal(&include_continuous)) return v8::Undefined(isolate);
        // WebIDL boolean conversion invokes no valueOf/toString callbacks.
        (void)include_continuous->BooleanValue(isolate);
    }
    // This Page profile has no trusted user-input source. Synthetic events and
    // pending JavaScript tasks do not become native user interaction tasks.
    return v8::False(isolate);
}

}  // namespace iv8::browser
