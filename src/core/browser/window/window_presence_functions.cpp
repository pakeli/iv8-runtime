#include "window_presence_functions.h"

#include <cstdint>
#include <string>

#include "iv8/web_interface.h"
#include "iv8/web_method.h"
#include "../internal/common.h"
#include "../internal/trace_hooks.h"
#include "../web/capabilities.h"

namespace iv8::browser {
namespace {

std::string operation_name(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::String::Utf8Value text(info.GetIsolate(), info.Data());
    return *text == nullptr ? std::string() : std::string(*text, static_cast<std::size_t>(text.length()));
}

void unsupported_operation(const v8::FunctionCallbackInfo<v8::Value>& info) {
    const auto name = operation_name(info);
    watch_record(info.GetIsolate(), "window", name);
    info.GetIsolate()->ThrowException(unsupported_capability(info.GetIsolate(), name));
}

void rejected_operation(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    const auto name = operation_name(info);
    watch_record(isolate, "window", name);
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver) ||
        !resolver->Reject(context, unsupported_capability(isolate, name)).FromMaybe(false)) return;
    info.GetReturnValue().Set(resolver->GetPromise());
}

void profile_noop(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", operation_name(info));
}

void prefixed_animation_frame(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto* state = state_from_isolate(isolate);
    const auto name = operation_name(info);
    watch_record(isolate, "window", name);
    if (state == nullptr || info.Length() == 0) return;
    if (name == "webkitRequestAnimationFrame") {
        if (!info[0]->IsFunction()) return;
        info.GetReturnValue().Set(static_cast<double>(state->register_animation_frame(info[0].As<v8::Function>())));
    } else {
        const auto handle = info[0]->IntegerValue(isolate->GetCurrentContext());
        if (handle.IsNothing()) return;
        state->cancel_animation_frame(handle.FromJust());
    }
}

bool window_receiver(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.This() == info.GetIsolate()->GetCurrentContext()->Global()) return true;
    info.GetIsolate()->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8Literal(info.GetIsolate(), "Illegal invocation")));
    return false;
}

void profile_flag_getter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (window_receiver(info)) info.GetReturnValue().Set(info.Data());
}

void replaceable_flag_setter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!window_receiver(info)) return;
    auto value = info.Length() > 0 ? info[0] : v8::Local<v8::Value>(v8::Undefined(info.GetIsolate()));
    (void)info.This()->DefineOwnProperty(info.GetIsolate()->GetCurrentContext(),
        info.Data().As<v8::String>(), value);
}

bool install_flag(v8::Isolate* isolate, v8::Local<v8::Context> context,
    const char* name, bool value, bool replaceable = false, bool enumerable = true) {
    v8::Local<v8::String> key;
    v8::Local<v8::Function> getter;
    if (!v8::String::NewFromUtf8(isolate, name).ToLocal(&key) ||
        !make_web_method(isolate, context, std::string("get ") + name, 0,
            &profile_flag_getter, v8::Boolean::New(isolate, value)).ToLocal(&getter)) return false;
    v8::Local<v8::Value> setter = v8::Undefined(isolate);
    if (replaceable) {
        v8::Local<v8::Function> function;
        if (!make_web_method(isolate, context, std::string("set ") + name, 1,
                &replaceable_flag_setter, key).ToLocal(&function)) return false;
        setter = function;
    }
    v8::PropertyDescriptor descriptor(getter, setter);
    descriptor.set_enumerable(enumerable);
    descriptor.set_configurable(true);
    return context->Global()->DefineProperty(context, key, descriptor).FromMaybe(false);
}

bool install_operation(v8::Isolate* isolate, v8::Local<v8::Context> context,
    const char* name, int length, v8::FunctionCallback callback) {
    v8::Local<v8::String> key;
    v8::Local<v8::Function> function;
    return v8::String::NewFromUtf8(isolate, name).ToLocal(&key) &&
        make_web_method(isolate, context, name, length, callback, key).ToLocal(&function) &&
        context->Global()->DefineOwnProperty(context, key, function).FromMaybe(false);
}

}  // namespace

bool install_window_presence_functions(v8::Isolate* isolate,
    v8::Local<v8::Context> context) {
    for (const char* name : {"open", "find", "getSelection"}) {
        if (!install_operation(isolate, context, name, 0, &unsupported_operation)) return false;
    }
    for (const char* name : {"getComputedStyle", "fetchLater", "when"}) {
        if (!install_operation(isolate, context, name, 1, &unsupported_operation)) return false;
    }
    if (!install_operation(isolate, context, "webkitRequestFileSystem", 3, &unsupported_operation) ||
        !install_operation(isolate, context, "webkitResolveLocalFileSystemURL", 2, &unsupported_operation)) return false;
    for (const char* name : {"getScreenDetails", "queryLocalFonts", "showDirectoryPicker",
            "showOpenFilePicker", "showSaveFilePicker"}) {
        if (!install_operation(isolate, context, name, 0, &rejected_operation)) return false;
    }
    if (!install_operation(isolate, context, "createImageBitmap", 1, &rejected_operation)) return false;
    for (const char* name : {"captureEvents", "releaseEvents", "close", "requestResize"}) {
        if (!install_operation(isolate, context, name, 0, &profile_noop)) return false;
    }
    for (const char* name : {"webkitRequestAnimationFrame", "webkitCancelAnimationFrame"}) {
        if (!install_operation(isolate, context, name, 1, &prefixed_animation_frame)) return false;
    }
    if (!install_flag(isolate, context, "credentialless", false) ||
        !install_flag(isolate, context, "originAgentCluster", true) ||
        !install_flag(isolate, context, "offscreenBuffering", true, true, false)) return false;
    auto prototype = web_interface_prototype(isolate, context, "Window");
    return !prototype.IsEmpty() && prototype->DefineOwnProperty(context,
        v8::String::NewFromUtf8Literal(isolate, "PERSISTENT"), v8::Integer::New(isolate, 1),
        static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete)).FromMaybe(false) &&
        prototype->DefineOwnProperty(context, v8::String::NewFromUtf8Literal(isolate, "TEMPORARY"),
            v8::Integer::New(isolate, 0),
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete)).FromMaybe(false);
}

}  // namespace iv8::browser
