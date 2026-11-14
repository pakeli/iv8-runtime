#include "event_support.h"

#include <cstring>
#include <unordered_set>

#include "iv8/query_parameters.h"
#include "iv8/url_parser.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {

// --- M4-A-7 minimal bubbling helpers -----------------------------------------
// Whether `event.bubbles` is truthy (missing -> false).
bool event_bubbles(v8::Isolate* isolate, v8::Local<v8::Context> context,
                   v8::Local<v8::Object> event) {
    v8::Local<v8::Value> value;
    if (event->Get(context, v8_string(isolate, "bubbles")).ToLocal(&value)) {
        return value->BooleanValue(isolate);
    }
    return false;
}

// Internal "propagation stopped" / "immediate propagation stopped" flags,
// stored as V8 Privates on the event so they are invisible to normal property
// access (not part of the public Event face).
v8::Local<v8::Private> event_stopped_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate, v8::String::NewFromUtf8Literal(isolate, "iv8::event-stopped"));
}

v8::Local<v8::Private> event_immediate_stopped_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8::event-immediate-stopped"));
}

v8::Local<v8::Private> event_passive_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate, v8::String::NewFromUtf8Literal(isolate, "iv8::event-passive"));
}

bool event_in_passive_listener(v8::Isolate* isolate,
                               v8::Local<v8::Context> context,
                               v8::Local<v8::Object> event) {
    v8::Local<v8::Value> value;
    if (event->GetPrivate(context, event_passive_key(isolate)).ToLocal(&value)) {
        return value->BooleanValue(isolate);
    }
    return false;
}

void set_event_in_passive_listener(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   v8::Local<v8::Object> event, bool passive) {
    (void)event->SetPrivate(context, event_passive_key(isolate),
                            v8::Boolean::New(isolate, passive));
}

void reset_event_stopped(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         v8::Local<v8::Object> event) {
    (void)event->SetPrivate(context, event_stopped_key(isolate),
                            v8::False(isolate));
    (void)event->SetPrivate(context, event_immediate_stopped_key(isolate),
                            v8::False(isolate));
}

bool event_is_stopped(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Object> event) {
    v8::Local<v8::Value> value;
    if (event->GetPrivate(context, event_stopped_key(isolate)).ToLocal(&value)) {
        return value->BooleanValue(isolate);
    }
    return false;
}

bool event_is_immediately_stopped(v8::Isolate* isolate,
                                  v8::Local<v8::Context> context,
                                  v8::Local<v8::Object> event) {
    v8::Local<v8::Value> value;
    if (event->GetPrivate(context, event_immediate_stopped_key(isolate))
            .ToLocal(&value)) {
        return value->BooleanValue(isolate);
    }
    return false;
}

// event.stopPropagation(): set the private flag on the receiver event. Stops
// propagation to LATER targets only; remaining listeners on the current target
// still run.
void stop_propagation_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    (void)info.This()->SetPrivate(context, event_stopped_key(isolate),
                                  v8::True(isolate));
}

// event.stopImmediatePropagation(): stop later listeners on this target and all
// later targets. Setting the ordinary stop flag as well keeps the existing
// bubbling checks correct without touching every call site.
void stop_immediate_propagation_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    (void)info.This()->SetPrivate(context, event_stopped_key(isolate),
                                  v8::True(isolate));
    (void)info.This()->SetPrivate(context, event_immediate_stopped_key(isolate),
                                  v8::True(isolate));
}

// event.preventDefault(): no-op unless the event is cancelable; otherwise mark
// defaultPrevented (an ordinary own field, visible to scripts and to the
// dispatchEvent return value).
void prevent_default_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (event_in_passive_listener(isolate, context, info.This())) {
        return;  // passive listeners cannot cancel
    }
    v8::Local<v8::Value> cancelable;
    if (!info.This()
             ->Get(context, v8_string(isolate, "cancelable"))
             .ToLocal(&cancelable) ||
        !cancelable->BooleanValue(isolate)) {
        return;
    }
    (void)info.This()->Set(context, v8_string(isolate, "defaultPrevented"),
                           v8::True(isolate));
}

bool event_default_prevented(v8::Isolate* isolate,
                             v8::Local<v8::Context> context,
                             v8::Local<v8::Object> event) {
    v8::Local<v8::Value> value;
    if (event->Get(context, v8_string(isolate, "defaultPrevented"))
            .ToLocal(&value)) {
        return value->BooleanValue(isolate);
    }
    return false;
}

}  // namespace iv8::browser
