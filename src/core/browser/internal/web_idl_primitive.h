#pragma once

#include <cstdint>
#include <string>

#include "iv8/v8_headers.h"

namespace iv8::browser {

// Shared by the active vibration, legacy quota, DOMError/DOMException, and
// BOM-04..08 slices. Preserve actual user conversion calls and thrown values;
// prefix only generated errors.
bool web_idl_to_primitive(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, bool string_hint, const std::string& error_prefix,
    v8::Local<v8::Value>* primitive);

bool web_idl_dom_string_u16(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, const std::string& error_prefix,
    std::u16string* output);

bool web_idl_dom_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, const std::string& error_prefix,
    std::string* output);

bool web_idl_usv_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, const std::string& error_prefix,
    std::string* output);

bool web_idl_enforce_range_ull(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, const std::string& error_prefix,
    std::uint64_t* output);

}  // namespace iv8::browser
