#include "window_event_handlers.h"

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
#include "../window/window_event_target.h"
#include "window_trace_support.h"

namespace iv8::browser {

constexpr v8::ExternalPointerTypeTag kWindowEventsTag =
    v8::kExternalPointerTypeTagDefault;

const char* const kWindowEventHandlerNames[] = {
    "onload",        "onunload",        "onbeforeunload", "onerror",
    "onresize",      "onscroll",        "onfocus",        "onblur",
    "onhashchange",  "onpopstate",      "onmessage",      "onstorage",
    "ononline",      "onoffline",       "onpagehide",     "onpageshow",
    "onunhandledrejection",              "onrejectionhandled",
};

std::string window_handler_name(v8::Isolate* isolate,
                                v8::Local<v8::Name> property) {
    v8::String::Utf8Value utf8(isolate, property);
    return *utf8 != nullptr
               ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
               : std::string();
}

void window_event_handler_getter(
    v8::Local<v8::Name> property,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* target = static_cast<WindowEventTarget*>(
        info.Data().As<v8::External>()->Value(kWindowEventsTag));
    info.GetReturnValue().Set(
        target->get_event_handler(isolate,
                                  window_handler_name(isolate, property)));
}

void window_event_handler_setter(
    v8::Local<v8::Name> property, v8::Local<v8::Value> value,
    const v8::PropertyCallbackInfo<void>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* target = static_cast<WindowEventTarget*>(
        info.Data().As<v8::External>()->Value(kWindowEventsTag));
    target->set_event_handler(isolate, window_handler_name(isolate, property),
                              value);
}

namespace {

bool function_handler_data(const v8::FunctionCallbackInfo<v8::Value>& info,
                            WindowEventTarget** target, std::string* name) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!info.Data()->IsArray()) {
        return false;
    }
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> external;
    v8::Local<v8::Value> handler_name;
    if (!data->Get(context, 0).ToLocal(&external) ||
        !data->Get(context, 1).ToLocal(&handler_name) ||
        !external->IsExternal() || !handler_name->IsString()) {
        return false;
    }
    *target = static_cast<WindowEventTarget*>(
        external.As<v8::External>()->Value(kWindowEventsTag));
    v8::String::Utf8Value utf8(isolate, handler_name);
    if (*target == nullptr || *utf8 == nullptr) {
        return false;
    }
    name->assign(*utf8, static_cast<std::size_t>(utf8.length()));
    return true;
}

}  // namespace

void window_event_handler_function_getter(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    WindowEventTarget* target = nullptr;
    std::string name;
    if (!function_handler_data(info, &target, &name)) {
        return;
    }
    AccessFunctionPropertyCompletion completion(info.GetIsolate(),
        window_trace_record(info.GetIsolate(), name.c_str(), AccessOperation::kGet),
        info, AccessOperation::kGet);
    info.GetReturnValue().Set(target->get_event_handler(info.GetIsolate(), name));
}

void window_event_handler_function_setter(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    WindowEventTarget* target = nullptr;
    std::string name;
    if (!function_handler_data(info, &target, &name)) {
        return;
    }
    AccessFunctionPropertyCompletion completion(info.GetIsolate(),
        window_trace_record(info.GetIsolate(), name.c_str(), AccessOperation::kSet),
        info, AccessOperation::kSet);
    v8::Local<v8::Value> value = info.Length() > 0
        ? info[0] : v8::Local<v8::Value>(v8::Undefined(info.GetIsolate()));
    target->set_event_handler(info.GetIsolate(), name, value);
}

}  // namespace iv8::browser
