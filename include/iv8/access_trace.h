#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace v8 {
class Isolate;
}

namespace iv8 {

enum class AccessOperation {
    kGet,
    kSet,
    kCall,
    kGetProbe,
    kGetMethod,
    kHas,
    kDelete,
    kOwnKeys,
    kGetOwnPropertyDescriptor,
    kDefineProperty,
};

struct AccessEvent {
    std::uint64_t seq = 0;
    std::uint64_t generation = 0;
    std::uint64_t object_id = 0;
    std::string object_type;
    std::string label;
    std::string member;
    std::string operation;   // get | set | call | get_probe | get_method |
                             // has | delete | own_keys |
                             // get_own_property_descriptor | define_property
    std::string phase;       // "enter"
    std::string resolution;  // non-empty only for get_probe
    std::string resource_name;
    std::string source_kind;
    // Step 6a completion fields for generic HostObject get/set/call. They are
    // attached to the same event after dispatch; get_probe and independent
    // native callbacks leave them empty.
    std::string outcome;        // "returned" | "threw"
    std::string value_summary;  // get/set
    std::uint64_t argument_count = 0;
    std::vector<std::string> argument_summaries;  // first 8 arguments
    std::string return_summary;                   // call
    // Step 6h: best-effort synchronous exception metadata. These fields are
    // populated only when a traced native entry actually throws; asynchronous
    // failures may leave them absent.
    std::string exception_name;
    std::string exception_message;
    std::string exception_stack;
    std::string exception_location;
};

// Completion data attached to an already-recorded event. Kept separate so the
// enter path stays allocation-light and the record lock is not held during the
// host implementation.
struct AccessCompletion {
    std::string outcome;
    std::string value_summary;
    std::uint64_t argument_count = 0;
    std::vector<std::string> argument_summaries;
    std::string return_summary;
    std::string exception_name;
    std::string exception_message;
    std::string exception_stack;
    std::string exception_location;
};

struct AccessSnapshot {
    std::vector<AccessEvent> events;
    bool enabled = false;
    std::uint64_t generation = 0;
    std::size_t max_events = 0;
    std::uint64_t dropped_events = 0;
    std::optional<std::vector<std::string>> filter_object_types;
};

// Page-owned tracing channel for native HostObject get/set/call entry points.
// It deliberately has no V8 or Python dependency in its write path: host
// callbacks only record native strings/integers, and Python reads a snapshot
// after the trace mutex is released.
class AccessTrace {
public:
    AccessTrace() = default;
    ~AccessTrace() = default;

    AccessTrace(const AccessTrace&) = delete;
    AccessTrace& operator=(const AccessTrace&) = delete;

    // Start a new session. `max_events` is a hard capacity; on overflow the
    // earliest events are retained and later events are counted as dropped.
    // `object_types` is an optional list of object-type filters; an empty list
    // records nothing.
    void start(std::size_t max_events,
               std::optional<std::vector<std::string>> object_types);

    // Stop new recording while retaining events/configuration.
    void stop() noexcept;

    // Advance the Page generation and clear object-ID bindings. Events keep
    // their old generation; object IDs are never reused.
    void begin_generation(std::uint64_t generation) noexcept;

    bool enabled() const noexcept {
        return enabled_.load(std::memory_order_acquire);
    }

    // Record an event before the concrete host operation runs. Returns the
    // event's seq when it was retained, or 0 when tracing is disabled/filtered,
    // the capacity is full, or allocation failed. Never throws.
    std::uint64_t record(const void* identity, std::string_view object_type,
                         std::string_view label, std::string_view member,
                         AccessOperation operation,
                         std::string_view resolution = {}) noexcept;

    // Promote the immediately preceding get_method event for this host/member
    // to a call event. Returns the promoted event's seq, or 0 when the last
    // event is not a matching method read (the caller then records a new call).
    std::uint64_t promote_last_method_read(const void* identity,
                                           std::string_view member) noexcept;

    // Attach completion data to a retained event. No-op if the seq is no longer
    // present (cleared/stopped between enter and completion). Never throws and
    // never performs V8/Python work while holding the trace mutex.
    void complete(std::uint64_t seq, AccessCompletion completion) noexcept;

    // Copy the current report data. If `clear` is true, events and
    // dropped_events are cleared after the copy; sequence/configuration/
    // generation/object IDs are preserved.
    AccessSnapshot snapshot(bool clear);

    // RAII source attribution. Nested scopes restore the previous resource and
    // source kind. Resource/source strings are copied before returning.
    class SourceScope {
    public:
        SourceScope(AccessTrace* trace, std::string resource,
                    std::string source_kind) noexcept;
        ~SourceScope() noexcept;

        SourceScope(const SourceScope&) = delete;
        SourceScope& operator=(const SourceScope&) = delete;

    private:
        AccessTrace* trace_ = nullptr;
        std::string previous_resource_;
        std::string previous_source_kind_;
        bool active_ = false;
    };

    static const std::vector<std::string>& supported_object_types();
    static const std::vector<std::string>& not_covered();

private:
    mutable std::mutex mutex_;
    std::atomic<bool> enabled_{false};
    std::vector<AccessEvent> events_;
    std::size_t max_events_ = 0;
    std::uint64_t dropped_events_ = 0;
    std::uint64_t seq_ = 0;
    std::uint64_t generation_ = 0;
    std::uint64_t next_object_id_ = 0;
    std::unordered_map<const void*, std::uint64_t> object_ids_;
    std::optional<std::vector<std::string>> filter_object_types_;
    std::string current_resource_;
    std::string current_source_kind_ = "unknown";
};

// Recover the Page-owned trace from the isolate embedder slot. Returns nullptr
// for a bare JSContext (no Page) or a missing context.
AccessTrace* access_trace_from_isolate(v8::Isolate* isolate);

}  // namespace iv8
