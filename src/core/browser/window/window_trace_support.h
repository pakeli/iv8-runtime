#pragma once

#include <cstdint>
#include <string_view>

#include "iv8/access_trace.h"
#include "iv8/v8_headers.h"

namespace iv8::browser {

// Window globals use the same generation-owned Window HostObject identity as
// native functions and real accessors. Disabled tracing performs no V8 reads.
std::uint64_t window_trace_record(v8::Isolate* isolate, const char* member,
                                  AccessOperation operation);

// Native data callbacks retain a data descriptor and the original stored value
// (including exact function identity). V8 also evaluates the native getter for
// descriptor queries. Redefinition to a JS accessor ends this native data path.
bool install_traced_window_data_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> target,
    std::string_view name, v8::Local<v8::Value> value,
    v8::PropertyAttribute attributes = v8::None);

}  // namespace iv8::browser
