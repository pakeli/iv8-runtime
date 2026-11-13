#pragma once

#include "iv8/v8_headers.h"

namespace iv8::browser {

void btoa_callback(const v8::FunctionCallbackInfo<v8::Value>& info);
void atob_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

}  // namespace iv8::browser
