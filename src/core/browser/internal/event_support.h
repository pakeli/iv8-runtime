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

// Internal Event/FileReader-style field maintenance keeps runtime attribution,
// while an application-provided accessor (including a bound native function)
// re-enters user attribution. Diagnostics never inspect Proxy/interceptor paths.
v8::MaybeLocal<v8::Value> get_runtime_api_field(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> object, const char* name);
v8::Maybe<bool> set_runtime_api_field(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> object, const char* name,
    v8::Local<v8::Value> value);
bool runtime_api_field_has_user_accessor(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> object,
    v8::Local<v8::Name> name, bool writing, bool method_call = false);

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

// New Event/UIEvent state is kept in V8 private slots, owned by the generation.
// Existing public own fields remain on the historical dispatch path.
// Read the native brand without consulting public properties or unwrapping a
// Proxy. Selected Web IDL hosts use this before their bounded dispatch path.
bool is_native_event(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Value> value);
bool initialize_event_state(v8::Isolate* isolate, v8::Local<v8::Context> context,
                            v8::Local<v8::Object> event, bool composed);
bool initialize_ui_event_state(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Object> event,
                               v8::Local<v8::Value> view, double detail);
bool set_ui_event_which(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> event, double which);
bool begin_event_dispatch(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Object> event,
                          const std::vector<v8::Local<v8::Value>>& path);
void end_event_dispatch(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> event);

void event_extra_getter(const v8::FunctionCallbackInfo<v8::Value>& info);
void event_extra_setter(const v8::FunctionCallbackInfo<v8::Value>& info);
void event_init_callback(const v8::FunctionCallbackInfo<v8::Value>& info);
void event_composed_path_callback(const v8::FunctionCallbackInfo<v8::Value>& info);
void ui_event_extra_getter(const v8::FunctionCallbackInfo<v8::Value>& info);
void ui_event_init_callback(const v8::FunctionCallbackInfo<v8::Value>& info);
bool define_event_trusted_accessor(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   v8::Local<v8::Object> event);

}  // namespace browser
}  // namespace iv8
