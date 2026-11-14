#include "window_functions.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "iv8/web_method.h"
#include "../internal/common.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {

void register_timer_callback(const v8::FunctionCallbackInfo<v8::Value>& info,
                             bool repeating) {
    v8::Isolate* isolate = info.GetIsolate();
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr || info.Length() < 1 || !info[0]->IsFunction()) {
        return;  // invalid call: no timer registered, returns undefined
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Function> callback = info[0].As<v8::Function>();
    std::int32_t delay = 0;
    if (info.Length() > 1) {
        delay = info[1]->Int32Value(context).FromMaybe(0);
    }
    const std::int64_t id = state->register_timer(callback, delay, repeating);
    info.GetReturnValue().Set(static_cast<double>(id));
}

void set_timeout_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "setTimeout");  // M9-2
    register_timer_callback(info, /*repeating=*/false);
}

void set_interval_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "setInterval");  // M9-2
    register_timer_callback(info, /*repeating=*/true);
}

void queue_microtask_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "queueMicrotask");  // M9-2
    v8::Isolate* isolate = info.GetIsolate();
    if (info.Length() < 1 || !info[0]->IsFunction()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(
                isolate, "queueMicrotask: callback must be a function")));
        return;
    }
    isolate->EnqueueMicrotask(info[0].As<v8::Function>());
}

void request_animation_frame_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "requestAnimationFrame");  // M9-2
    v8::Isolate* isolate = info.GetIsolate();
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr || info.Length() < 1 || !info[0]->IsFunction()) {
        return;  // invalid call: nothing queued, returns undefined
    }
    const std::int64_t handle =
        state->register_animation_frame(info[0].As<v8::Function>());
    info.GetReturnValue().Set(static_cast<double>(handle));
}

void cancel_animation_frame_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "cancelAnimationFrame");  // M9-2
    v8::Isolate* isolate = info.GetIsolate();
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr || info.Length() < 1) {
        return;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::int64_t handle =
        static_cast<std::int64_t>(info[0]->IntegerValue(context).FromMaybe(0));
    state->cancel_animation_frame(handle);
}

void request_idle_callback_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "requestIdleCallback");  // M9-2
    v8::Isolate* isolate = info.GetIsolate();
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr || info.Length() < 1 || !info[0]->IsFunction()) {
        return;  // invalid call: nothing queued, returns undefined
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::int32_t timeout = -1;  // absent
    if (info.Length() > 1 && !info[1]->IsUndefined() && !info[1]->IsNull()) {
        if (!info[1]->IsObject()) {
            isolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8Literal(
                    isolate, "requestIdleCallback: options must be an object")));
            return;
        }
        v8::Local<v8::Object> options = info[1].As<v8::Object>();
        v8::Local<v8::Value> value;
        if (!options->Get(context, v8_string(isolate, "timeout")).ToLocal(&value)) {
            return;  // the getter threw
        }
        if (!value->IsUndefined()) {
            if (!value->IsNumber()) {
                isolate->ThrowException(v8::Exception::TypeError(
                    v8::String::NewFromUtf8Literal(
                        isolate,
                        "requestIdleCallback: options.timeout must be a number")));
                return;
            }
            const double ms = value.As<v8::Number>()->Value();
            if (std::isnan(ms) || std::isinf(ms) || ms < 0.0) {
                isolate->ThrowException(v8::Exception::TypeError(
                    v8::String::NewFromUtf8Literal(
                        isolate,
                        "requestIdleCallback: options.timeout must be a "
                        "non-negative finite number")));
                return;
            }
            // Clamp to int32 range; the value is recorded only, never a deadline.
            timeout = static_cast<std::int32_t>(
                ms > 2147483647.0 ? 2147483647.0 : ms);
        }
    }
    const std::int64_t handle =
        state->register_idle_callback(info[0].As<v8::Function>(), timeout);
    info.GetReturnValue().Set(static_cast<double>(handle));
}

void cancel_idle_callback_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "cancelIdleCallback");  // M9-2
    v8::Isolate* isolate = info.GetIsolate();
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr || info.Length() < 1) {
        return;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::int64_t handle =
        static_cast<std::int64_t>(info[0]->IntegerValue(context).FromMaybe(0));
    state->cancel_idle_callback(handle);
}

void clear_timer_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr || info.Length() < 1) {
        return;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::int64_t id =
        static_cast<std::int64_t>(info[0]->IntegerValue(context).FromMaybe(0));
    state->clear_timer(id);
}

void alert_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "alert");
}

void confirm_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "confirm");
    info.GetReturnValue().Set(v8::False(info.GetIsolate()));
}

void prompt_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "prompt");
    info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
}

void print_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "print");
}

void window_scroll_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "scroll");
}

void window_scroll_to_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "scrollTo");
}

void window_scroll_by_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "scrollBy");
}

void window_focus_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "focus");
}

void window_blur_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "blur");
}

void window_move_by_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "moveBy");
}

void window_move_to_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "moveTo");
}

void window_resize_by_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "resizeBy");
}

void window_resize_to_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "resizeTo");
}

void window_stop_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "stop");
}

v8::Local<v8::Private> window_location_slot(v8::Isolate* isolate) {
    return v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.window.location"));
}

void window_location_get(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    if (info.This() != context->Global()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Illegal invocation")));
        return;
    }
    v8::Local<v8::Value> location;
    if (context->Global()->GetPrivate(context, window_location_slot(isolate)).ToLocal(&location))
        info.GetReturnValue().Set(location);
}

void window_location_set(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    if (info.This() != context->Global()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Illegal invocation")));
        return;
    }
    v8::Local<v8::Value> location;
    if (!context->Global()->GetPrivate(context, window_location_slot(isolate)).ToLocal(&location) ||
        !location->IsObject()) return;
    auto value = info.Length() ? info[0] : v8::Local<v8::Value>(v8::Undefined(isolate));
    (void)location.As<v8::Object>()->Set(context, v8_string(isolate, "href"), value);
}

void install_global_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             v8::Local<v8::Object> global, const std::string& name,
                             v8::FunctionCallback callback) {
    v8::Local<v8::Function> fn;
    if (!make_web_method(isolate, context, name,
            web_method_length("window", name), callback).ToLocal(&fn)) return;
    (void)global->Set(context, v8_string(isolate, name), fn);
}

}  // namespace iv8::browser
