#pragma once

#include "iv8/v8_headers.h"

namespace iv8::browser {

extern void window_post_message_dispatch(const v8::FunctionCallbackInfo<v8::Value>& info);

// Internal native hooks captured by the existing JS host implementations.
// They are deleted before page scripts run and never invoke Python callbacks.
bool install_script_call_trace_hooks(v8::Isolate* isolate,
    v8::Local<v8::Context> context);
bool clear_script_call_trace_hooks(v8::Isolate* isolate,
    v8::Local<v8::Context> context);
bool make_script_call_trace_hooks(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Function>& begin,
    v8::Local<v8::Function>& end, v8::Local<v8::Function>& data_property,
    v8::Local<v8::Function>& user_operation);

}  // namespace iv8::browser
