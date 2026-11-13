#pragma once

#include "iv8/v8_headers.h"

namespace iv8::browser {

void crypto_get_random_values_callback(const v8::FunctionCallbackInfo<v8::Value>& info);
void crypto_random_uuid_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

}  // namespace iv8::browser
