#pragma once

#include <string>

#include "iv8/v8_headers.h"

namespace iv8 {

void install_browser_compatibility(v8::Isolate* isolate,
                                    v8::Local<v8::Context> context, const std::string& base_url);
v8::Local<v8::Value> make_inline_style(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> element);

}  // namespace iv8
