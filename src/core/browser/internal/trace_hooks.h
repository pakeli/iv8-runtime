#pragma once

#include <cstdint>
#include <string>

#include "iv8/access_trace.h"

namespace iv8::browser {

void watch_record(v8::Isolate* isolate, const std::string& receiver,
                  const std::string& method);
std::uint64_t access_trace_record(v8::Isolate* isolate, const void* identity,
                                  const char* object_type, const char* label,
                                  const char* member, AccessOperation operation);

}  // namespace iv8::browser
