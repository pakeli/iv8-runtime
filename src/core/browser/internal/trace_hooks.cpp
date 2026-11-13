#include "common.h"
#include "trace_hooks.h"

#include "iv8/watch_registry.h"

namespace iv8::browser {

// M9-2: record a host-method call hit if the "receiver.method" path is watched.
// Cheap no-op when watching is disabled (the common case): the enabled() check is
// a lock-free atomic read, and the path string is built only when it may match.
void watch_record(v8::Isolate* isolate, const std::string& receiver,
                  const std::string& method) {
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr) {
        return;
    }
    WatchRegistry* registry = state->watch_registry();
    if (registry == nullptr || !registry->enabled()) {
        return;
    }
    registry->maybe_record(receiver + "." + method);
}

// Step 5 call coverage for runtime-owned plain BOM objects. These are
// independent native callbacks, not HostObject methods, so they use explicit
// stable native tokens for object identity. Property reads on these objects are
// a later group and are not implied by these calls. kPerformanceTraceToken,
// kCryptoTraceToken and kStorageTraceToken are defined in
// browser/internal/common.h.

std::uint64_t access_trace_record(v8::Isolate* isolate, const void* identity,
                                  const char* object_type, const char* label,
                                  const char* member, AccessOperation operation) {
    AccessTrace* trace = access_trace_from_isolate(isolate);
    if (trace != nullptr && trace->enabled()) {
        return trace->record(identity, object_type, label, member, operation);
    }
    return 0;
}

}  // namespace iv8::browser
