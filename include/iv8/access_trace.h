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
};

struct AccessEvent {
    std::uint64_t seq = 0;
    std::uint64_t generation = 0;
    std::uint64_t object_id = 0;
    std::string object_type;
    std::string label;
    std::string member;
    std::string operation;  // "get" | "set" | "call"
    std::string phase;      // "enter"
    std::string resource_name;
    std::string source_kind;
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

    // Record an event before the concrete host operation runs. Never throws:
    // allocation/other failures increment the dropped counter when possible.
    void record(const void* identity, std::string_view object_type,
                std::string_view label, std::string_view member,
                AccessOperation operation) noexcept;

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
