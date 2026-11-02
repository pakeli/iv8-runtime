#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "iv8/access_trace.h"
#include "iv8/v8_headers.h"

namespace iv8 {

// Internal helpers shared by the generic HostObject callbacks and the
// Page-owned native BOM callbacks. All summaries are primitive/type-only and
// must never execute user JavaScript.
std::string access_value_summary(v8::Isolate* isolate,
                                 v8::Local<v8::Value> value);
std::vector<std::string> access_argument_summaries(
    v8::Isolate* isolate, const v8::FunctionCallbackInfo<v8::Value>& info);

// Fill exception_name/message/stack/location from a caught TryCatch. Uses only
// V8 message/stack metadata; it does not read user-defined `stack` properties
// or invoke getters.
void access_capture_exception(v8::Isolate* isolate,
                              v8::Local<v8::Context> context,
                              v8::TryCatch& try_catch,
                              AccessCompletion& completion);

// RAII completion for a recorded native function callback. The constructor
// installs a TryCatch (only when seq != 0), captures argument summaries, and
// the destructor attaches returned/threw outcome, return summary, and safe
// exception metadata before rethrowing a caught exception unchanged.
class AccessCallCompletion {
public:
    AccessCallCompletion(v8::Isolate* isolate, std::uint64_t seq,
                         const v8::FunctionCallbackInfo<v8::Value>& info);
    ~AccessCallCompletion() noexcept;

    AccessCallCompletion(const AccessCallCompletion&) = delete;
    AccessCallCompletion& operator=(const AccessCallCompletion&) = delete;

private:
    v8::Isolate* isolate_ = nullptr;
    std::uint64_t seq_ = 0;
    const v8::FunctionCallbackInfo<v8::Value>* info_ = nullptr;
    std::optional<v8::TryCatch> try_catch_;
    AccessCompletion completion_;
};

// RAII completion for a recorded native property getter.
class AccessPropertyCompletion {
public:
    AccessPropertyCompletion(v8::Isolate* isolate, std::uint64_t seq,
                             const v8::PropertyCallbackInfo<v8::Value>& info);
    ~AccessPropertyCompletion() noexcept;

    AccessPropertyCompletion(const AccessPropertyCompletion&) = delete;
    AccessPropertyCompletion& operator=(const AccessPropertyCompletion&) = delete;

private:
    v8::Isolate* isolate_ = nullptr;
    std::uint64_t seq_ = 0;
    const v8::PropertyCallbackInfo<v8::Value>* info_ = nullptr;
    std::optional<v8::TryCatch> try_catch_;
    AccessCompletion completion_;
};

// RAII completion for a recorded native property setter.
class AccessPropertySetCompletion {
public:
    AccessPropertySetCompletion(
        v8::Isolate* isolate, std::uint64_t seq,
        const v8::PropertyCallbackInfo<void>& info,
        v8::Local<v8::Value> value);
    ~AccessPropertySetCompletion() noexcept;

    AccessPropertySetCompletion(const AccessPropertySetCompletion&) = delete;
    AccessPropertySetCompletion& operator=(const AccessPropertySetCompletion&) = delete;

private:
    v8::Isolate* isolate_ = nullptr;
    std::uint64_t seq_ = 0;
    std::optional<v8::TryCatch> try_catch_;
    AccessCompletion completion_;
};

}  // namespace iv8
