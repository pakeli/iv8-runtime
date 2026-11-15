#pragma once

#include "iv8/v8_headers.h"

namespace iv8::browser {

bool install_window_presence_functions(v8::Isolate* isolate,
    v8::Local<v8::Context> context);

}  // namespace iv8::browser
