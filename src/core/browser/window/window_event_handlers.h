#pragma once

#include "iv8/v8_headers.h"

namespace iv8::browser {

extern const v8::ExternalPointerTypeTag kWindowEventsTag;

extern const char* const kWindowEventHandlerNames[18];

extern void window_event_handler_getter(v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info);

extern void window_event_handler_setter(v8::Local<v8::Name> property, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<void>& info);

}  // namespace iv8::browser
