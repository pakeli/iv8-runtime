#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "iv8/v8_headers.h"

namespace iv8::browser {

bool serialize_history_state(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> value, const std::string& operation,
    std::vector<std::uint8_t>* output);

}  // namespace iv8::browser
