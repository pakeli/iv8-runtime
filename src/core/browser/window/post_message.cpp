#include "post_message.h"

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
#include "../host/event_target.h"
#include "../internal/trace_hooks.h"
#include "../window/window_event_handlers.h"

namespace iv8::browser {

void window_post_message_dispatch(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (info.Length() < 1 || !info[0]->IsObject()) {
        return;
    }
    auto* target = static_cast<EventTargetHost*>(
        info.Data().As<v8::External>()->Value(kWindowEventsTag));
    target->dispatch_event(isolate, context, info[0].As<v8::Object>(),
                           context->Global());
}

void window_post_message_record(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "postMessage");
}

}  // namespace iv8::browser
