#include "performance.h"

#include <cstring>
#include <unordered_set>

#include "iv8/query_parameters.h"
#include "iv8/url_parser.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {

// --- M11-31/M11-37 virtual performance timeline ------------------------------
//
// performance.now() reports the M11-29/M11-30 facade's virtual clock. This is not
// a wall clock: it starts at 0, advances only through tick()/advance()/
// run_until_idle() (including timer/frame jumps), and resets with each load()
// generation. The legacy pumps do not advance it. Date remains V8's real clock;
// only this runtime-owned clock is virtual.
//
// M11-37 adds a minimal performance entry timeline: mark()/measure()/getEntries*
// and clearMarks()/clearMeasures(). Entries are plain read-only objects stored
// per generation; there is no PerformanceObserver, resource timing, navigation
// timing, or real timeline.
constexpr v8::ExternalPointerTypeTag kPerformanceTag =
    v8::kExternalPointerTypeTagDefault;

PerformanceStore* performance_store_from(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    return static_cast<PerformanceStore*>(
        info.Data().As<v8::External>()->Value(kPerformanceTag));
}

std::string performance_arg_string(
    const v8::FunctionCallbackInfo<v8::Value>& info, int index) {
    if (index >= info.Length()) {
        return std::string();
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::String> str;
    if (!info[index]->ToString(isolate->GetCurrentContext()).ToLocal(&str)) {
        return std::string();
    }
    v8::String::Utf8Value utf8(isolate, str);
    return *utf8 != nullptr
               ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
               : std::string();
}

v8::Local<v8::Value> make_performance_entry(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const PerformanceEntryRecord& entry) {
    v8::Local<v8::Object> object = v8::Object::New(isolate);
    const auto define = [&](const char* key, v8::Local<v8::Value> value) {
        (void)object->DefineOwnProperty(
            context,
            v8::String::NewFromUtf8(isolate, key).ToLocalChecked(), value,
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete));
    };
    define("name", v8_string(isolate, entry.name));
    define("entryType", v8_string(isolate, entry.entry_type));
    define("startTime",
           v8::Number::New(isolate, static_cast<double>(entry.start_time)));
    define("duration",
           v8::Number::New(isolate, static_cast<double>(entry.duration)));
    return object;
}

std::int64_t performance_clock_ms(v8::Isolate* isolate) {
    ContextState* state = state_from_isolate(isolate);
    return state != nullptr ? state->event_loop_time_ms() : 0;
}

void performance_time_origin_getter(
    v8::Local<v8::Name>,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    const std::uint64_t seq = access_trace_record(
        isolate, &kPerformanceTraceToken, "performance", "performance",
        "timeOrigin", AccessOperation::kGet);
    AccessPropertyCompletion completion(isolate, seq, info);
    info.GetReturnValue().Set(v8::Number::New(isolate, 0.0));
}

void performance_now_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "performance", "now");
    const std::uint64_t seq = access_trace_record(
        info.GetIsolate(), &kPerformanceTraceToken, "performance",
        "performance", "now", AccessOperation::kCall);  // M9-2 style
    AccessCallCompletion completion(info.GetIsolate(), seq, info);
    ContextState* state = state_from_isolate(info.GetIsolate());
    const double ms = state == nullptr
                          ? 0.0
                          : static_cast<double>(state->event_loop_time_ms());
    info.GetReturnValue().Set(ms);
}

const PerformanceEntryRecord* performance_find_mark(
    const PerformanceStore* store, const std::string& name) {
    for (auto it = store->entries.rbegin(); it != store->entries.rend(); ++it) {
        if (it->entry_type == "mark" && it->name == name) {
            return &*it;
        }
    }
    return nullptr;
}

v8::Local<v8::Array> performance_entries_array(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const PerformanceStore* store, const std::string& type,
    const std::string& name) {
    std::size_t count = 0;
    for (const PerformanceEntryRecord& entry : store->entries) {
        if ((type.empty() || entry.entry_type == type) &&
            (name.empty() || entry.name == name)) {
            count++;
        }
    }
    v8::Local<v8::Array> array =
        v8::Array::New(isolate, static_cast<int>(count));
    std::uint32_t index = 0;
    for (const PerformanceEntryRecord& entry : store->entries) {
        if ((type.empty() || entry.entry_type == type) &&
            (name.empty() || entry.name == name)) {
            (void)array->Set(context, index++,
                             make_performance_entry(isolate, context, entry));
        }
    }
    return array;
}

void performance_mark_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "performance", "mark");
    const std::uint64_t seq = access_trace_record(
        info.GetIsolate(), &kPerformanceTraceToken, "performance",
        "performance", "mark", AccessOperation::kCall);
    AccessCallCompletion completion(info.GetIsolate(), seq, info);
    PerformanceStore* store = performance_store_from(info);
    if (store == nullptr) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    PerformanceEntryRecord entry;
    entry.name = performance_arg_string(info, 0);
    entry.entry_type = "mark";
    entry.start_time = performance_clock_ms(isolate);
    store->entries.push_back(entry);
    info.GetReturnValue().Set(
        make_performance_entry(isolate, context, store->entries.back()));
}

void performance_measure_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "performance", "measure");
    const std::uint64_t seq = access_trace_record(
        info.GetIsolate(), &kPerformanceTraceToken, "performance",
        "performance", "measure", AccessOperation::kCall);
    AccessCallCompletion completion(info.GetIsolate(), seq, info);
    PerformanceStore* store = performance_store_from(info);
    if (store == nullptr) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    PerformanceEntryRecord entry;
    entry.name = performance_arg_string(info, 0);
    entry.entry_type = "measure";
    entry.start_time = 0;
    entry.duration = 0;
    std::int64_t end_time = performance_clock_ms(isolate);
    if (info.Length() > 1 && !info[1]->IsUndefined()) {
        const std::string start_mark = performance_arg_string(info, 1);
        const PerformanceEntryRecord* mark =
            performance_find_mark(store, start_mark);
        if (mark == nullptr) {
            isolate->ThrowException(v8::Exception::Error(
                v8::String::NewFromUtf8(
                    isolate,
                    ("measure: unknown start mark: " + start_mark).c_str())
                    .ToLocalChecked()));
            return;
        }
        entry.start_time = mark->start_time;
    }
    if (info.Length() > 2 && !info[2]->IsUndefined()) {
        const std::string end_mark = performance_arg_string(info, 2);
        const PerformanceEntryRecord* mark =
            performance_find_mark(store, end_mark);
        if (mark == nullptr) {
            isolate->ThrowException(v8::Exception::Error(
                v8::String::NewFromUtf8(
                    isolate,
                    ("measure: unknown end mark: " + end_mark).c_str())
                    .ToLocalChecked()));
            return;
        }
        end_time = mark->start_time;
    }
    entry.duration = end_time - entry.start_time;
    store->entries.push_back(entry);
    info.GetReturnValue().Set(
        make_performance_entry(isolate, context, store->entries.back()));
}

void performance_get_entries_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "performance", "getEntries");
    const std::uint64_t seq = access_trace_record(
        info.GetIsolate(), &kPerformanceTraceToken, "performance",
        "performance", "getEntries", AccessOperation::kCall);
    AccessCallCompletion completion(info.GetIsolate(), seq, info);
    PerformanceStore* store = performance_store_from(info);
    if (store == nullptr) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    info.GetReturnValue().Set(performance_entries_array(
        isolate, isolate->GetCurrentContext(), store, std::string(),
        std::string()));
}

void performance_get_entries_by_name_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "performance", "getEntriesByName");
    const std::uint64_t seq = access_trace_record(
        info.GetIsolate(), &kPerformanceTraceToken, "performance",
        "performance", "getEntriesByName", AccessOperation::kCall);
    AccessCallCompletion completion(info.GetIsolate(), seq, info);
    PerformanceStore* store = performance_store_from(info);
    if (store == nullptr) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    info.GetReturnValue().Set(performance_entries_array(
        isolate, isolate->GetCurrentContext(), store, std::string(),
        performance_arg_string(info, 0)));
}

void performance_get_entries_by_type_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "performance", "getEntriesByType");
    const std::uint64_t seq = access_trace_record(
        info.GetIsolate(), &kPerformanceTraceToken, "performance",
        "performance", "getEntriesByType", AccessOperation::kCall);
    AccessCallCompletion completion(info.GetIsolate(), seq, info);
    PerformanceStore* store = performance_store_from(info);
    if (store == nullptr) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    info.GetReturnValue().Set(performance_entries_array(
        isolate, isolate->GetCurrentContext(), store,
        performance_arg_string(info, 0), std::string()));
}

void performance_clear_entries(PerformanceStore* store, const std::string& type,
                               bool has_name, const std::string& name) {
    store->entries.erase(
        std::remove_if(store->entries.begin(), store->entries.end(),
                       [&](const PerformanceEntryRecord& entry) {
                           return entry.entry_type == type &&
                                  (!has_name || entry.name == name);
                       }),
        store->entries.end());
}

void performance_clear_marks_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "performance", "clearMarks");
    const std::uint64_t seq = access_trace_record(
        info.GetIsolate(), &kPerformanceTraceToken, "performance",
        "performance", "clearMarks", AccessOperation::kCall);
    AccessCallCompletion completion(info.GetIsolate(), seq, info);
    PerformanceStore* store = performance_store_from(info);
    if (store == nullptr) {
        return;
    }
    performance_clear_entries(store, "mark", info.Length() > 0,
                              performance_arg_string(info, 0));
}

void performance_clear_measures_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "performance", "clearMeasures");
    const std::uint64_t seq = access_trace_record(
        info.GetIsolate(), &kPerformanceTraceToken, "performance",
        "performance", "clearMeasures", AccessOperation::kCall);
    AccessCallCompletion completion(info.GetIsolate(), seq, info);
    PerformanceStore* store = performance_store_from(info);
    if (store == nullptr) {
        return;
    }
    performance_clear_entries(store, "measure", info.Length() > 0,
                              performance_arg_string(info, 0));
}

}  // namespace iv8::browser
