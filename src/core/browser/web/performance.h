#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../internal/common.h"
#include "iv8/page_state.h"
#include "iv8/host_object.h"
#include "iv8/access_summary.h"
#include "iv8/web_interface.h"
#include "iv8/browser_compatibility.h"

namespace iv8 {

// M11-37: per-generation performance timeline records. Defined in iv8 scope
// because PageState owns the store through the opaque forward declaration in
// page_state.h.
struct PerformanceEntryRecord {
    std::string name;
    std::string entry_type;
    std::int64_t start_time = 0;
    std::int64_t duration = 0;
};

struct PerformanceStore {
    std::vector<PerformanceEntryRecord> entries;
};

namespace browser {

extern const v8::ExternalPointerTypeTag kPerformanceTag;

extern PerformanceStore* performance_store_from(const v8::FunctionCallbackInfo<v8::Value>& info);

extern std::string performance_arg_string(const v8::FunctionCallbackInfo<v8::Value>& info, int index);

extern v8::Local<v8::Value> make_performance_entry(v8::Isolate* isolate, v8::Local<v8::Context> context, const PerformanceEntryRecord& entry);

extern std::int64_t performance_clock_ms(v8::Isolate* isolate);

extern void performance_time_origin_getter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info);

extern void performance_now_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern const PerformanceEntryRecord* performance_find_mark(const PerformanceStore* store, const std::string& name);

extern v8::Local<v8::Array> performance_entries_array(v8::Isolate* isolate, v8::Local<v8::Context> context, const PerformanceStore* store, const std::string& type, const std::string& name);

extern void performance_mark_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void performance_measure_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void performance_get_entries_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void performance_get_entries_by_name_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void performance_get_entries_by_type_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void performance_clear_entries(PerformanceStore* store, const std::string& type, bool has_name, const std::string& name);

extern void performance_clear_marks_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void performance_clear_measures_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

}  // namespace browser
}  // namespace iv8
