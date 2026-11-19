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
#include "../internal/event_support.h"

namespace iv8 {

namespace browser {

extern void event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info);
// Public constructor entry dispatch; internal base initialization calls retain
// their original callbacks so one public construction is not counted twice.
void traced_event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info);

void ui_event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info);
bool install_event_prototype_members(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     v8::Local<v8::Function> constructor,
                                     v8::Local<v8::Object> prototype);
bool install_ui_event_prototype_members(v8::Isolate* isolate,
                                        v8::Local<v8::Context> context,
                                        v8::Local<v8::Object> prototype);
// Construct hosted synthetic events through the generation's registered Event,
// independently of a script-replaced global Event binding.
v8::MaybeLocal<v8::Object> create_runtime_event(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& type,
    bool bubbles = false, bool cancelable = false);

extern bool event_construct_check(const v8::FunctionCallbackInfo<v8::Value>& info, const char* name);

extern bool event_init_field(v8::Local<v8::Context> context, const v8::FunctionCallbackInfo<v8::Value>& info, const char* name, v8::Local<v8::Value>* out);

extern void set_event_field(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> self, const char* name, v8::Local<v8::Value> value);

extern bool event_int_field(v8::Local<v8::Context> context, v8::Local<v8::Value> value, double* out);

extern bool event_string_field(v8::Local<v8::Context> context, v8::Isolate* isolate, v8::Local<v8::Value> value, std::string* out);

extern void custom_event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void mouse_event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void keyboard_event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void focus_event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void message_event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info);

}  // namespace browser
}  // namespace iv8
