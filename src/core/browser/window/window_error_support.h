#pragma once

#include "iv8/v8_headers.h"

namespace iv8::browser {

// The current-event value belongs to a context and exists only while invoking
// one listener/handler. No persistent or process-wide V8 handle is retained.
class WindowEventScope {
public:
    WindowEventScope(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Object> event);
    ~WindowEventScope() noexcept;

    WindowEventScope(const WindowEventScope&) = delete;
    WindowEventScope& operator=(const WindowEventScope&) = delete;

private:
    v8::Isolate* isolate_;
    v8::Local<v8::Context> context_;
    v8::Local<v8::Value> previous_;
    bool active_ = false;
};

void window_event_getter(const v8::FunctionCallbackInfo<v8::Value>& info);
void window_event_setter(const v8::FunctionCallbackInfo<v8::Value>& info);
void window_report_error_callback(const v8::FunctionCallbackInfo<v8::Value>& info);
bool install_window_error_support(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context);

}  // namespace iv8::browser
