#pragma once

#include <cstdint>
#include <string_view>
#include <memory>
#include <vector>

#include "iv8/access_trace.h"
#include "iv8/access_summary.h"
#include "iv8/v8_headers.h"

namespace iv8 { class HostObject; }
namespace iv8::browser {

// Window globals use the same generation-owned Window HostObject identity as
// native functions and real accessors. Disabled tracing performs no V8 reads.
std::uint64_t window_trace_record(v8::Isolate* isolate, const char* member,
                                  AccessOperation operation, const char* resolution = "");

// Native data callbacks retain a data descriptor and the original stored value
// (including exact function identity). V8 also evaluates the native getter for
// descriptor queries. Redefinition to a JS accessor ends this native data path.
bool install_traced_window_data_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> target,
    std::string_view name, v8::Local<v8::Value> value,
    v8::PropertyAttribute attributes = v8::None);

// Known runtime-owned namespace data members use the same shape-preserving
// native data path with their own stable identity and readable receiver label.
bool install_traced_api_data_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> target,
    std::string_view name, v8::Local<v8::Value> value, const void* identity,
    std::string_view object_type, std::string_view label,
    v8::PropertyAttribute attributes = v8::None, bool receiver_identity = false);
bool install_traced_api_named_data_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> target,
    v8::Local<v8::Name> name, v8::Local<v8::Value> value, const void* identity,
    std::string_view object_type, std::string_view label,
    v8::PropertyAttribute attributes = v8::None, bool receiver_identity = false);
bool trace_native_api_data_members(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Object> object, const char* label);

bool capture_window_intrinsic_names(v8::Isolate* isolate,
    v8::Local<v8::Context> context);
bool install_window_public_data_tracing(v8::Isolate* isolate,
    v8::Local<v8::Context> context);

bool install_native_api_trace_store(v8::Isolate* isolate,
    v8::Local<v8::Context> context, std::vector<std::unique_ptr<HostObject>>& owners);
const void* native_api_identity(v8::Isolate* isolate, v8::Local<v8::Object> object);
std::uint64_t native_api_trace_record(v8::Isolate* isolate, v8::Local<v8::Object> object,
    const char* label, const char* member, AccessOperation operation);

class NativeApiCallTrace {
public:
    NativeApiCallTrace(const v8::FunctionCallbackInfo<v8::Value>& info,
        const char* label, const char* member);
private:
    AccessCallCompletion completion_;
};

class NativeApiPropertyTrace {
public:
    NativeApiPropertyTrace(const v8::FunctionCallbackInfo<v8::Value>& info,
        const char* label, const char* member, AccessOperation operation);
private:
    AccessFunctionPropertyCompletion completion_;
};

}  // namespace iv8::browser
