#pragma once

#include <string_view>

#include "iv8/v8_headers.h"

namespace iv8 {

// Required-argument metadata for the operations currently exposed by Page.
int web_method_length(std::string_view receiver, std::string_view name);

// Regular Web API operations expose native functions that reject construction.
v8::MaybeLocal<v8::Function> make_web_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    std::string_view name, int length, v8::FunctionCallback callback,
    v8::Local<v8::Value> data = v8::Local<v8::Value>());

}  // namespace iv8
