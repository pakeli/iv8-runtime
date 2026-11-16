#pragma once

#include <utility>

#include "iv8/access_trace.h"
#include "iv8/v8_headers.h"

namespace iv8 {

inline AccessTrace::OriginScope access_runtime_scope(v8::Isolate* isolate) noexcept {
    return AccessTrace::OriginScope(access_trace_from_isolate(isolate), isolate,
                                    AccessOriginKind::kRuntime);
}

inline AccessTrace::OriginScope access_user_scope(v8::Isolate* isolate) noexcept {
    return AccessTrace::OriginScope(access_trace_from_isolate(isolate), isolate,
                                    AccessOriginKind::kUser);
}

inline AccessTrace::OriginScope access_runtime_bridge_scope(v8::Isolate* isolate) noexcept {
    return AccessTrace::OriginScope(access_trace_from_isolate(isolate), isolate,
                                    AccessOriginKind::kRuntimeBridge);
}

template <class Operation>
decltype(auto) access_user_operation(v8::Isolate* isolate, Operation&& operation) {
    auto origin = access_user_scope(isolate);
    return std::forward<Operation>(operation)();
}

}  // namespace iv8
