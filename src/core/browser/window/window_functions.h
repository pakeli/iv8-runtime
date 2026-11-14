#pragma once

#include <string>

#include "iv8/v8_headers.h"

namespace iv8::browser {

extern void set_timeout_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void set_interval_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void queue_microtask_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void request_animation_frame_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void cancel_animation_frame_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void request_idle_callback_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void cancel_idle_callback_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void clear_timer_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void alert_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void confirm_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void prompt_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void print_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void window_scroll_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void window_scroll_to_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void window_scroll_by_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void window_focus_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void window_blur_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void window_move_by_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void window_move_to_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void window_resize_by_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void window_resize_to_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void window_stop_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern v8::Local<v8::Private> window_location_slot(v8::Isolate* isolate);

extern void window_location_get(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void window_location_set(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void install_global_function(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> global, const std::string& name, v8::FunctionCallback callback);

}  // namespace iv8::browser
