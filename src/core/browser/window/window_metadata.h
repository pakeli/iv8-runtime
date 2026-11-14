#pragma once

#include "iv8/v8_headers.h"

namespace iv8 {

extern const v8::ExternalPointerTypeTag kWindowNameTag;

extern void window_name_getter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info);

extern void window_name_setter(v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info);

extern void window_status_getter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info);

extern void window_status_setter(v8::Local<v8::Name>, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info);

extern const v8::ExternalPointerTypeTag kWindowAliasTag;

extern void window_alias_getter(v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info);

extern void window_alias_setter(v8::Local<v8::Name> property, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info);

extern const v8::ExternalPointerTypeTag kWindowTraceTag;

extern void window_metadata_getter(v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info);

}  // namespace iv8
