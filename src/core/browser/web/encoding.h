#pragma once

#include "iv8/v8_headers.h"

namespace iv8::browser {

void text_encoder_constructor(const v8::FunctionCallbackInfo<v8::Value>& info);
void text_encoder_encode(const v8::FunctionCallbackInfo<v8::Value>& info);
void text_decoder_constructor(const v8::FunctionCallbackInfo<v8::Value>& info);
void text_decoder_decode(const v8::FunctionCallbackInfo<v8::Value>& info);

}  // namespace iv8::browser
