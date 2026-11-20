#pragma once

#include <string>

#include "iv8/v8_headers.h"

namespace iv8::browser {

// Shared by the active vibration and legacy quota/DOMError slices. Preserve
// actual user conversion calls and thrown values; prefix only generated errors.
bool web_idl_to_primitive(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, bool string_hint, const std::string& error_prefix,
    v8::Local<v8::Value>* primitive);

}  // namespace iv8::browser
