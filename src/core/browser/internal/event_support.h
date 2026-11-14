#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../internal/common.h"
#include "iv8/page_state.h"
#include "iv8/host_object.h"
#include "iv8/access_summary.h"
#include "iv8/web_interface.h"
#include "iv8/browser_compatibility.h"

namespace iv8 {

namespace browser {

extern bool event_bubbles(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> event);

extern v8::Local<v8::Private> event_stopped_key(v8::Isolate* isolate);

extern v8::Local<v8::Private> event_immediate_stopped_key(v8::Isolate* isolate);

extern v8::Local<v8::Private> event_passive_key(v8::Isolate* isolate);

extern bool event_in_passive_listener(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> event);

extern void set_event_in_passive_listener(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> event, bool passive);

extern void reset_event_stopped(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> event);

extern bool event_is_stopped(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> event);

extern bool event_is_immediately_stopped(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> event);

extern void stop_propagation_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void stop_immediate_propagation_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void prevent_default_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern bool event_default_prevented(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> event);

}  // namespace browser
}  // namespace iv8
