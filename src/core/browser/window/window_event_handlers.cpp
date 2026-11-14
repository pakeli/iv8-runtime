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

}  // namespace iv8::browser
