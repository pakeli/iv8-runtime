#pragma once

#include "iv8/v8_headers.h"

namespace iv8::browser {

// Observe calls while delegating raw receivers to the original V8 intrinsics.
// Install once before user scripts so cached aliases share the same boundary.
bool install_to_string_monitoring(v8::Isolate* isolate,
    v8::Local<v8::Context> context);

}  // namespace iv8::browser
