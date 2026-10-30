#include "iv8/access_trace.h"

#include <algorithm>
#include <utility>

#include "iv8/context_host.h"

namespace iv8 {

namespace {

const char* operation_name(AccessOperation operation) {
    switch (operation) {
        case AccessOperation::kGet:
            return "get";
        case AccessOperation::kSet:
            return "set";
        case AccessOperation::kCall:
            return "call";
        case AccessOperation::kGetProbe:
            return "get_probe";
    }
    return "unknown";
}

}  // namespace

void AccessTrace::start(
    std::size_t max_events,
    std::optional<std::vector<std::string>> object_types) {
    std::lock_guard<std::mutex> lock(mutex_);
    max_events_ = max_events;
    dropped_events_ = 0;
    seq_ = 0;
    events_.clear();
    filter_object_types_.reset();
    if (object_types.has_value()) {
        std::vector<std::string> deduped;
        for (const std::string& type : *object_types) {
            if (std::find(deduped.begin(), deduped.end(), type) == deduped.end()) {
                deduped.push_back(type);
            }
        }
        filter_object_types_ = std::move(deduped);
    }
    current_resource_.clear();
    current_source_kind_ = "unknown";
    enabled_.store(true, std::memory_order_release);
}

void AccessTrace::stop() noexcept {
    enabled_.store(false, std::memory_order_release);
}

void AccessTrace::begin_generation(std::uint64_t generation) noexcept {
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        generation_ = generation;
        object_ids_.clear();
        current_resource_.clear();
        current_source_kind_ = "unknown";
    } catch (...) {
        // Lifecycle setup must never throw.
    }
}

void AccessTrace::record(const void* identity, std::string_view object_type,
                         std::string_view label, std::string_view member,
                         AccessOperation operation,
                         std::string_view resolution) noexcept {
    if (identity == nullptr || !enabled_.load(std::memory_order_acquire)) {
        return;
    }
    try {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!enabled_.load(std::memory_order_relaxed)) {
            return;
        }
        if (filter_object_types_.has_value()) {
            bool allowed = false;
            for (const std::string& type : *filter_object_types_) {
                if (std::string_view(type) == object_type) {
                    allowed = true;
                    break;
                }
            }
            if (!allowed) {
                return;  // filtered events are not drops
            }
        }
        if (events_.size() >= max_events_) {
            ++dropped_events_;
            return;
        }

        std::uint64_t object_id = 0;
        const auto found = object_ids_.find(identity);
        if (found == object_ids_.end()) {
            object_id = ++next_object_id_;
            object_ids_.emplace(identity, object_id);
        } else {
            object_id = found->second;
        }

        AccessEvent event;
        event.seq = ++seq_;
        event.generation = generation_;
        event.object_id = object_id;
        event.object_type = std::string(object_type);
        event.label = std::string(label);
        event.member = std::string(member);
        event.operation = operation_name(operation);
        event.phase = "enter";
        event.resolution = resolution;
        event.resource_name = current_resource_;
        event.source_kind = current_source_kind_;
        events_.push_back(std::move(event));
    } catch (...) {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            ++dropped_events_;
        } catch (...) {
            // Tracing must never surface an allocation/lock failure to JS.
        }
    }
}

AccessSnapshot AccessTrace::snapshot(bool clear) {
    std::lock_guard<std::mutex> lock(mutex_);
    AccessSnapshot snapshot;
    snapshot.enabled = enabled_.load(std::memory_order_acquire);
    snapshot.generation = generation_;
    snapshot.max_events = max_events_;
    snapshot.dropped_events = dropped_events_;
    snapshot.filter_object_types = filter_object_types_;
    if (clear) {
        snapshot.events = std::move(events_);
        events_.clear();
        dropped_events_ = 0;
    } else {
        snapshot.events = events_;
    }
    return snapshot;
}

AccessTrace::SourceScope::SourceScope(AccessTrace* trace, std::string resource,
                                      std::string source_kind) noexcept
    : trace_(trace) {
    if (trace_ == nullptr) {
        return;
    }
    try {
        std::lock_guard<std::mutex> lock(trace_->mutex_);
        previous_resource_ = std::move(trace_->current_resource_);
        previous_source_kind_ = std::move(trace_->current_source_kind_);
        trace_->current_resource_ = std::move(resource);
        trace_->current_source_kind_ = std::move(source_kind);
        active_ = true;
    } catch (...) {
        active_ = false;
    }
}

AccessTrace::SourceScope::~SourceScope() noexcept {
    if (trace_ == nullptr || !active_) {
        return;
    }
    try {
        std::lock_guard<std::mutex> lock(trace_->mutex_);
        trace_->current_resource_ = std::move(previous_resource_);
        trace_->current_source_kind_ = std::move(previous_source_kind_);
    } catch (...) {
        // Destructors must not throw during eval unwinding.
    }
}

const std::vector<std::string>& AccessTrace::supported_object_types() {
    static const std::vector<std::string> types = {
        "connection", "console", "crypto", "document", "element",
        "host_object", "host_probe", "location", "media_query_list",
        "navigator", "orientation", "performance", "permission_status",
        "permissions", "screen", "storage", "storage_manager",
        "visual_viewport", "window",
    };
    return types;
}

const std::vector<std::string>& AccessTrace::not_covered() {
    static const std::vector<std::string> gaps = {
        "remaining performance/crypto/Storage property reads (their native method calls and performance.timeOrigin are covered)",
        "other window/global data properties",
        "permission-status missing probes/onchange and MediaQueryList onchange/missing probes",
        "arrays and plain values returned by host APIs",
        "user-created plain JS objects",
        "method-reference reads",
        "non-HostObject missing-property probes/in/delete/enumeration/descriptors",
        "read-only write attempts",
        "arguments/returns/stacks",
    };
    return gaps;
}

AccessTrace* access_trace_from_isolate(v8::Isolate* isolate) {
    if (isolate == nullptr) {
        return nullptr;
    }
    auto* state =
        static_cast<ContextState*>(isolate->GetData(kIsolateStateSlot));
    return state != nullptr ? state->access_trace() : nullptr;
}

}  // namespace iv8
