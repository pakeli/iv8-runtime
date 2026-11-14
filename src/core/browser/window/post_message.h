#pragma once

#include "iv8/v8_headers.h"

namespace iv8::browser {

extern void window_post_message_dispatch(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void window_post_message_record(const v8::FunctionCallbackInfo<v8::Value>& info);

}  // namespace iv8::browser
