#pragma once

#include "iv8/v8_headers.h"

namespace iv8::browser {

v8::Local<v8::Value> navigator_vibrate(v8::Isolate* isolate,
    v8::Local<v8::Context> context,
    const v8::FunctionCallbackInfo<v8::Value>& arguments);

}  // namespace iv8::browser
