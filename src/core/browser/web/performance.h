#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "../internal/common.h"
#include "../host/event_target.h"
#include "iv8/page_state.h"
#include "iv8/host_object.h"
#include "iv8/web_interface.h"

namespace iv8 {

struct PerformanceEntryRecord {
    std::string name;
    std::string entry_type;
    double start_time = 0.0;
    double duration = 0.0;
    v8::Global<v8::Object> wrapper;
};

struct PerformanceStore {
    std::vector<PerformanceEntryRecord> entries;
    std::uint32_t resource_timing_buffer_size = 250;

    void release_v8_handles() noexcept {
        for (auto& entry : entries) {
            entry.wrapper.Reset();
        }
        entries.clear();
    }
};

namespace browser {

class PerformanceEntryHost : public HostObject {
public:
    PerformanceEntryHost(std::string name, std::string entry_type,
        double start_time, double duration)
        : name_(std::move(name)),
          entry_type_(std::move(entry_type)),
          start_time_(start_time),
          duration_(duration) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "PerformanceEntry"; }
    std::string trace_object_type() const override { return "host_object"; }
    std::string trace_label() const override { return "PerformanceEntry"; }
    std::vector<std::string> property_names() const override {
        return {"name", "entryType", "startTime", "duration", "navigationId"};
    }
    std::vector<std::string> method_names() const override {
        return {"toJSON"};
    }

    const std::string& entry_name() const noexcept { return name_; }
    const std::string& entry_type() const noexcept { return entry_type_; }
    double start_time() const noexcept { return start_time_; }
    double duration() const noexcept { return duration_; }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

protected:
    std::string name_;
    std::string entry_type_;
    double start_time_ = 0.0;
    double duration_ = 0.0;
};

class PerformanceMarkHost : public PerformanceEntryHost {
public:
    PerformanceMarkHost(std::string name, double start_time)
        : PerformanceEntryHost(std::move(name), "mark", start_time, 0.0) {}

    std::string prototype_key() const override { return "PerformanceMark"; }
    std::string trace_label() const override { return "PerformanceEntry"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override { return {}; }
};

class PerformanceMeasureHost : public PerformanceEntryHost {
public:
    PerformanceMeasureHost(std::string name, double start_time, double duration)
        : PerformanceEntryHost(std::move(name), "measure", start_time, duration) {}

    std::string prototype_key() const override { return "PerformanceMeasure"; }
    std::string trace_label() const override { return "PerformanceEntry"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override { return {}; }
};

class PerformanceNavigationHost : public HostObject {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "PerformanceNavigation"; }
    std::string trace_object_type() const override { return "host_object"; }
    std::string trace_label() const override { return "PerformanceNavigation"; }
    std::vector<std::string> property_names() const override {
        return {"type", "redirectCount"};
    }
    std::vector<std::string> method_names() const override {
        return {"toJSON"};
    }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;
};

class PerformanceTimingHost : public HostObject {
public:
    explicit PerformanceTimingHost(bool https_scheme)
        : https_scheme_(https_scheme) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "PerformanceTiming"; }
    std::string trace_object_type() const override { return "host_object"; }
    std::string trace_label() const override { return "PerformanceTiming"; }
    std::vector<std::string> property_names() const override;
    std::vector<std::string> method_names() const override {
        return {"toJSON"};
    }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    bool https_scheme_ = false;
};

class EventCountsHost : public HostObject {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "EventCounts"; }
    std::string trace_object_type() const override { return "host_object"; }
    std::string trace_label() const override { return "EventCounts"; }
    std::vector<std::string> property_names() const override {
        return {"size"};
    }
    std::vector<std::string> method_names() const override {
        return {"entries", "forEach", "get", "has", "keys", "values"};
    }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;
};

class PerformanceHost : public EventTargetHost {
public:
    PerformanceHost(PageState* owner, PerformanceStore* store, bool https_scheme)
        : owner_(owner), store_(store), https_scheme_(https_scheme) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "Performance"; }
    std::string trace_object_type() const override { return "performance"; }
    std::string trace_label() const override { return "performance"; }
    std::vector<std::string> property_names() const override {
        return {
            "timeOrigin", "onresourcetimingbufferfull",
            "timing", "navigation", "memory", "eventCounts", "interactionCount"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"onresourcetimingbufferfull"};
    }
    std::vector<std::string> method_names() const override {
        auto names = event_method_names();
        const char* own[] = {
            "clearMarks", "clearMeasures", "clearResourceTimings",
            "getEntries", "getEntriesByName", "getEntriesByType",
            "mark", "measure", "setResourceTimingBufferSize",
            "toJSON", "now"};
        for (const char* m : own) names.emplace_back(m);
        return names;
    }
    void release_v8_handles() noexcept override {
        EventTargetHost::release_v8_handles();
        timing_object_.Reset();
        navigation_object_.Reset();
        event_counts_object_.Reset();
        if (store_ != nullptr) {
            store_->release_v8_handles();
        }
    }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name, v8::Local<v8::Value> value) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    v8::Local<v8::Object> ensure_timing(v8::Isolate* isolate,
        v8::Local<v8::Context> context);
    v8::Local<v8::Object> ensure_navigation(v8::Isolate* isolate,
        v8::Local<v8::Context> context);
    v8::Local<v8::Object> ensure_event_counts(v8::Isolate* isolate,
        v8::Local<v8::Context> context);

    PageState* owner_;
    PerformanceStore* store_;
    bool https_scheme_ = false;
    v8::Global<v8::Object> timing_object_;
    v8::Global<v8::Object> navigation_object_;
    v8::Global<v8::Object> event_counts_object_;
};

std::int64_t performance_clock_ms(v8::Isolate* isolate);

v8::Local<v8::Object> create_memory_info_object(v8::Isolate* isolate,
    v8::Local<v8::Context> context);

v8::Local<v8::Value> construct_performance_mark(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    v8::Local<v8::Array> arguments);

bool install_performance_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    PerformanceStore* store, const std::string& base_url);

}  // namespace browser
}  // namespace iv8
