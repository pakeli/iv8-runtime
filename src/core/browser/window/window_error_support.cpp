#include "window_error_support.h"
#include "window_trace_support.h"

#include "iv8/web_method.h"
#include "iv8/access_summary.h"
#include "../internal/trace_hooks.h"
#include "../host/event_target.h"
#include "../web/event_constructors.h"

namespace iv8::browser {

namespace {

v8::Local<v8::Private> window_current_event_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8::window-current-event"));
}

bool window_receiver(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.This()->StrictEquals(info.GetIsolate()->GetCurrentContext()->Global())) return true;
    info.GetIsolate()->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8Literal(info.GetIsolate(), "Illegal invocation")));
    return false;
}

EventTargetHost* window_event_target(v8::Isolate* isolate,
                                    v8::Local<v8::Context> context) {
    v8::Local<v8::Value> backing;
    if (!context->Global()->GetPrivate(context, v8::Private::ForApi(isolate,
            v8::String::NewFromUtf8Literal(isolate, "iv8.webidl.window.host")))
                .ToLocal(&backing) || !backing->IsExternal()) return nullptr;
    auto* host = static_cast<HostObject*>(backing.As<v8::External>()->Value(
        v8::kExternalPointerTypeTagDefault));
    return host == nullptr ? nullptr : static_cast<EventTargetHost*>(host);
}

}  // namespace

WindowEventScope::WindowEventScope(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> event)
    : isolate_(isolate), context_(context) {
    if (!context_->Global()->GetPrivate(context_, window_current_event_key(isolate_))
            .ToLocal(&previous_)) return;
    active_ = context_->Global()->SetPrivate(context_, window_current_event_key(isolate_), event)
        .FromMaybe(false);
}

WindowEventScope::~WindowEventScope() noexcept {
    if (active_ && !context_->Global()->SetPrivate(context_,
            window_current_event_key(isolate_), previous_).FromMaybe(false)) return;
}

void window_event_getter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    const auto sequence = window_trace_record(info.GetIsolate(), "event", AccessOperation::kGet);
    AccessFunctionPropertyCompletion completion(info.GetIsolate(), sequence, info, AccessOperation::kGet);
    if (!window_receiver(info)) return;
    auto context = info.GetIsolate()->GetCurrentContext();
    v8::Local<v8::Value> event;
    if (context->Global()->GetPrivate(context, window_current_event_key(info.GetIsolate()))
            .ToLocal(&event)) info.GetReturnValue().Set(event);
}

void window_event_setter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    const auto sequence = window_trace_record(info.GetIsolate(), "event", AccessOperation::kSet);
    AccessFunctionPropertyCompletion completion(info.GetIsolate(), sequence, info, AccessOperation::kSet);
    if (!window_receiver(info)) return;
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto value = info.Length() > 0 ? info[0] : v8::Local<v8::Value>(v8::Undefined(isolate));
    // Chrome's replaceable setter creates an ordinary own data property. Event
    // scopes continue to update the hidden value, leaving that user shadow intact.
    if (!install_traced_window_data_property(isolate, context, context->Global(), "event", value)) return;
}

void window_report_error_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    const auto sequence = window_trace_record(isolate, "reportError", AccessOperation::kCall);
    AccessCallCompletion completion(isolate, sequence, info);
    watch_record(isolate, "window", "reportError");
    auto context = isolate->GetCurrentContext();
    if (info.Length() < 1) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "reportError requires an error argument")));
        return;
    }
    auto* target = window_event_target(isolate, context);
    if (target == nullptr) return;
    v8::Local<v8::Object> event;
    if (!create_runtime_event(isolate, context, "error", false, true).ToLocal(&event)) return;
    const auto message = v8::Exception::CreateMessage(isolate, info[0]);
    auto filename = message->GetScriptResourceName();
    if (!filename->IsString()) filename = v8::String::Empty(isolate);
    const int line = message->GetLineNumber(context).FromMaybe(0);
    const int column = message->GetStartColumn(context).FromMaybe(-1);
    if (!event->CreateDataProperty(context, v8::String::NewFromUtf8Literal(isolate, "message"),
            message->Get()).FromMaybe(false) ||
        !event->CreateDataProperty(context, v8::String::NewFromUtf8Literal(isolate, "error"),
            info[0]).FromMaybe(false) ||
        !event->CreateDataProperty(context, v8::String::NewFromUtf8Literal(isolate, "filename"),
            filename).FromMaybe(false) ||
        !event->CreateDataProperty(context, v8::String::NewFromUtf8Literal(isolate, "lineno"),
            v8::Integer::New(isolate, line)).FromMaybe(false) ||
        !event->CreateDataProperty(context, v8::String::NewFromUtf8Literal(isolate, "colno"),
            v8::Integer::New(isolate, column < 0 ? 0 : column + 1)).FromMaybe(false)) return;
    target->dispatch_event(isolate, context, event, context->Global());
}

bool install_window_error_support(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context) {
    v8::Local<v8::Function> getter;
    v8::Local<v8::Function> setter;
    v8::Local<v8::Function> report;
    if (!make_web_method(isolate, context, "get event", 0, &window_event_getter).ToLocal(&getter) ||
        !make_web_method(isolate, context, "set event", 1, &window_event_setter).ToLocal(&setter) ||
        !make_web_method(isolate, context, "reportError", 1, &window_report_error_callback).ToLocal(&report)) return false;
    v8::PropertyDescriptor descriptor(getter, setter);
    descriptor.set_enumerable(true);
    descriptor.set_configurable(true);
    return context->Global()->DefineProperty(context,
        v8::String::NewFromUtf8Literal(isolate, "event"), descriptor).FromMaybe(false) &&
        install_traced_window_data_property(isolate, context, context->Global(), "reportError", report);
}

}  // namespace iv8::browser
