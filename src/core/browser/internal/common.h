#pragma once

// Shared internals of the Page browser runtime (namespace iv8::browser).
//
// Private to src/core: not installed and not part of the public include/ tree.
// Stage 0.2 of docs/page_state_split_plan.md. Everything here must keep a single
// definition program-wide once page_state.cpp is split into several translation
// units. Header-defined helpers use `inline` and tokens use `inline constexpr`.
// Out-of-line helpers have one definition in common.cpp.

#include <array>
#include <cstddef>
#include <string>

#include "iv8/context_host.h"

namespace iv8::browser {

// --- Access-trace identity tokens -------------------------------------------
//
// The access trace (include/iv8/access_trace.h) distinguishes runtime-owned
// plain objects by the ADDRESS of these tokens, not their value. A non-inline
// namespace-scope `constexpr`/`static` variable has internal linkage, so each
// translation unit including it would get its own copy at a different address
// and identity checks would silently diverge. C++17 inline variables have exactly
// one address in the whole program. verify_trace_token_identity() (below) checks
// this at Page construction.
inline constexpr char kStorageManagerTraceToken = 0;
inline constexpr char kPermissionsTraceToken = 0;
inline constexpr char kJavaEnabledTraceToken = 0;
inline constexpr char kPerformanceTraceToken = 0;
inline constexpr char kCryptoTraceToken = 0;
inline constexpr char kStorageTraceToken = 0;

inline constexpr std::size_t kTraceTokenCount = 6;
using TraceTokenAddresses = std::array<const void*, kTraceTokenCount>;

// The token addresses as taken in common.cpp, a separate translation unit.
TraceTokenAddresses trace_token_addresses_in_common_tu() noexcept;

// Throws std::logic_error when the addresses taken by the calling translation
// unit differ from those taken in common.cpp. Must be called from (and inlined
// into) the consuming translation unit; see page_state.cpp.
void verify_trace_token_identity(const TraceTokenAddresses& local);

// Token addresses as taken by the translation unit that expands this macro.
// A macro rather than an inline function: an inline function would be merged
// across translation units and so could never observe a per-TU divergence.
#define IV8_BROWSER_LOCAL_TRACE_TOKEN_ADDRESSES()                              \
    (::iv8::browser::TraceTokenAddresses{                                     \
        &::iv8::browser::kStorageManagerTraceToken,                          \
        &::iv8::browser::kPermissionsTraceToken,                             \
        &::iv8::browser::kJavaEnabledTraceToken,                             \
        &::iv8::browser::kPerformanceTraceToken,                             \
        &::iv8::browser::kCryptoTraceToken,                                  \
        &::iv8::browser::kStorageTraceToken})

// Shared string normalization defined once in common.cpp.
extern std::string ascii_lower(std::string s);

// --- Shared V8 helpers -------------------------------------------------------

// Recovers the owning ContextState from the isolate embedder-data slot.
inline ContextState* state_from_isolate(v8::Isolate* isolate) {
    return static_cast<ContextState*>(isolate->GetData(kIsolateStateSlot));
}

inline v8::Local<v8::Value> v8_string(v8::Isolate* isolate, const std::string& s) {
    return v8::String::NewFromUtf8(isolate, s.c_str(), v8::NewStringType::kNormal,
                                   static_cast<int>(s.size()))
        .ToLocalChecked();
}

inline v8::Local<v8::Value> dom_exception(v8::Isolate* isolate, const char* name,
                                          const std::string& message) {
    auto error = v8::Exception::Error(v8_string(isolate, message).As<v8::String>());
    (void)error.As<v8::Object>()->Set(isolate->GetCurrentContext(),
        v8_string(isolate, "name"), v8_string(isolate, name));
    return error;
}

}  // namespace iv8::browser
