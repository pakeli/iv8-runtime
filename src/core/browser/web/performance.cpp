#include "performance.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include "iv8/access_origin.h"
#include "iv8/access_summary.h"
#include "iv8/url_parser.h"
#include "iv8/web_method.h"
#include "../bom/navigator_parts.h"
#include "../internal/trace_hooks.h"
#include "../window/window_trace_support.h"
#include "dom_exception.h"
#include "structured_clone.h"

namespace iv8::browser {
namespace {

constexpr std::int64_t kDeterministicNavigationStart = 1700000000000LL;
constexpr double kDeterministicTotalHeapSize = 10000000.0;
constexpr double kDeterministicUsedHeapSize = 10000000.0;
constexpr double kDeterministicHeapSizeLimit = 2172649472.0;

const std::vector<const char*>& timing_attribute_names() {
    static const std::vector<const char*> names = {
        "navigationStart",
        "unloadEventStart",
        "unloadEventEnd",
        "redirectStart",
        "redirectEnd",
        "fetchStart",
        "domainLookupStart",
        "domainLookupEnd",
        "connectStart",
        "connectEnd",
        "secureConnectionStart",
        "requestStart",
        "responseStart",
        "responseEnd",
        "domLoading",
        "domInteractive",
        "domContentLoadedEventStart",
        "domContentLoadedEventEnd",
        "domComplete",
        "loadEventStart",
        "loadEventEnd",
    };
    return names;
}

const std::vector<const char*>& timing_to_json_names() {
    static const std::vector<const char*> names = {
        "connectStart",
        "secureConnectionStart",
        "unloadEventEnd",
        "domainLookupStart",
        "domainLookupEnd",
        "responseStart",
        "connectEnd",
        "responseEnd",
        "requestStart",
        "domLoading",
        "redirectStart",
        "loadEventEnd",
        "domComplete",
        "navigationStart",
        "loadEventStart",
        "domContentLoadedEventEnd",
        "unloadEventStart",
        "redirectEnd",
        "domInteractive",
        "fetchStart",
        "domContentLoadedEventStart",
    };
    return names;
}

const std::vector<const char*>& event_counts_keys() {
    static const std::vector<const char*> keys = {
        "pointerdown", "touchend", "input", "keydown",
        "mouseleave", "mouseenter", "drop", "beforeinput",
        "pointerenter", "dragend", "pointercancel", "compositionupdate",
        "mousedown", "dragleave", "dragover", "mouseup",
        "pointerover", "lostpointercapture", "mouseover", "gotpointercapture",
        "dblclick", "keyup", "keypress", "pointerup",
        "compositionstart", "auxclick", "dragstart", "touchstart",
        "compositionend", "pointerout", "dragenter", "touchcancel",
        "click", "contextmenu", "mouseout", "pointerleave",
    };
    return keys;
}

bool is_timing_attribute_name(const std::string& name) {
    for (const char* candidate : timing_attribute_names()) {
        if (name == candidate) return true;
    }
    return false;
}

std::int64_t deterministic_timing_value(bool https_scheme, const std::string& name) {
    if (name == "unloadEventStart" || name == "unloadEventEnd" ||
        name == "redirectStart" || name == "redirectEnd") {
        return 0;
    }
    if (name == "secureConnectionStart") {
        return https_scheme ? kDeterministicNavigationStart + 1 : 0;
    }
    if (name == "navigationStart") return kDeterministicNavigationStart;
    if (name == "fetchStart" || name == "domainLookupStart" ||
        name == "domainLookupEnd" || name == "connectStart" ||
        name == "connectEnd") {
        return kDeterministicNavigationStart + 1;
    }
    if (name == "requestStart") return kDeterministicNavigationStart + 2;
    if (name == "responseStart") return kDeterministicNavigationStart + 3;
    if (name == "responseEnd") return kDeterministicNavigationStart + 4;
    if (name == "domLoading") return kDeterministicNavigationStart + 5;
    if (name == "domInteractive") return kDeterministicNavigationStart + 6;
    if (name == "domContentLoadedEventStart" ||
        name == "domContentLoadedEventEnd") {
        return kDeterministicNavigationStart + 7;
    }
    if (name == "domComplete" || name == "loadEventStart" ||
        name == "loadEventEnd") {
        return kDeterministicNavigationStart + 8;
    }
    return 0;
}

bool is_https_base_url(const std::string& base_url) {
    const auto parsed = decompose_url(base_url);
    return parsed.protocol == "https:" || parsed.protocol == "wss:";
}

bool throw_perf_type_error(v8::Isolate* isolate, const std::string& prefix,
    const std::string& detail) {
    const auto message = prefix + detail;
    v8::Local<v8::String> text;
    if (v8::String::NewFromUtf8(isolate, message.data(), v8::NewStringType::kNormal,
            static_cast<int>(message.size())).ToLocal(&text)) {
        isolate->ThrowException(v8::Exception::TypeError(text));
    }
    return false;
}

v8::MaybeLocal<v8::Value> perf_user_get(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> object,
    v8::Local<v8::Value> key) {
    return access_user_operation(isolate, [&] {
        return object->Get(context, key);
    });
}

bool perf_to_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> value, std::string* out) {
    v8::Local<v8::String> str;
    if (!access_user_operation(isolate, [&] {
            return value->ToString(context);
        }).ToLocal(&str)) {
        return false;
    }
    v8::String::Utf8Value utf8(isolate, str);
    out->assign(*utf8 != nullptr ? *utf8 : "",
        *utf8 != nullptr ? static_cast<std::size_t>(utf8.length()) : 0);
    return true;
}

bool perf_finite_double(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> value, const std::string& prefix, double* out) {
    double number = 0.0;
    if (!access_user_operation(isolate, [&] {
            return value->NumberValue(context);
        }).To(&number)) {
        return false;
    }
    if (!std::isfinite(number)) {
        return throw_perf_type_error(isolate, prefix,
            "The provided double value is non-finite.");
    }
    *out = number;
    return true;
}

v8::Local<v8::Private> perf_entry_detail_slot(v8::Isolate* isolate) {
    return v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.bom09.perf_entry.detail"));
}

v8::Local<v8::Private> perf_entry_name_slot(v8::Isolate* isolate) {
    return v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.bom09.perf_entry.name"));
}

bool clone_performance_detail(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    v8::Local<v8::Value> input, const std::string& prefix,
    v8::Local<v8::Value>* out) {
    if (input.IsEmpty() || input->IsUndefined()) {
        *out = v8::Null(isolate);
        return true;
    }
    bool data_clone_error = false;
    {
        v8::TryCatch caught(isolate);
        v8::Local<v8::Value> cloned;
        if (structured_clone_value(isolate, context, input, v8::Map::New(isolate), 0,
                &data_clone_error)
                .ToLocal(&cloned)) {
            *out = cloned;
            return true;
        }
        if (!caught.HasCaught() || !caught.CanContinue() || !data_clone_error) {
            caught.ReThrow();
            return false;
        }
        caught.Reset();
    }
    isolate->ThrowException(make_dom_exception(isolate, context, owner,
        "DataCloneError",
        prefix + "The object could not be cloned."));
    return false;
}

bool parse_performance_mark_args(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    int argc, const std::function<v8::Local<v8::Value>(int)>& arg_at,
    const std::string& prefix, std::string* name_out,
    v8::Local<v8::String>* name_v8_out,
    double* start_time_out, v8::Local<v8::Value>* detail_out) {
    if (argc < 1) {
        return throw_perf_type_error(isolate, prefix,
            "1 argument required, but only 0 present.");
    }
    v8::Local<v8::String> name_str;
    if (!access_user_operation(isolate, [&] {
            return arg_at(0)->ToString(context);
        }).ToLocal(&name_str)) {
        return false;
    }
    std::string small_name;
    auto materialize_name = [&]() -> const std::string& {
        if (name_out != nullptr) {
            return *name_out;
        }
        if (small_name.empty() && name_str->Length() > 0) {
            v8::String::Utf8Value utf8(isolate, name_str);
            if (*utf8 != nullptr) {
                small_name.assign(*utf8, static_cast<std::size_t>(utf8.length()));
            }
        }
        return small_name;
    };
    if (name_out != nullptr) {
        v8::String::Utf8Value utf8(isolate, name_str);
        name_out->assign(*utf8 != nullptr ? *utf8 : "",
            *utf8 != nullptr ? static_cast<std::size_t>(utf8.length()) : 0);
    } else if (name_str->Length() <= 32) {
        v8::String::Utf8Value utf8(isolate, name_str);
        small_name.assign(*utf8 != nullptr ? *utf8 : "",
            *utf8 != nullptr ? static_cast<std::size_t>(utf8.length()) : 0);
    }
    double start_time = static_cast<double>(performance_clock_ms(isolate));
    bool has_start_time = false;
    v8::Local<v8::Value> raw_detail = v8::Undefined(isolate);

    if (argc >= 2) {
        auto opt_val = arg_at(1);
        if (!opt_val.IsEmpty() && !opt_val->IsNullOrUndefined()) {
            if (!opt_val->IsObject()) {
                return throw_perf_type_error(isolate, prefix,
                    "The provided value is not of type 'PerformanceMarkOptions'.");
            }
            auto opts = opt_val.As<v8::Object>();
            // Lexicographical WebIDL PerformanceMarkOptions order: detail, startTime.
            if (!perf_user_get(isolate, context, opts, v8_string(isolate, "detail"))
                    .ToLocal(&raw_detail)) {
                return false;
            }
            v8::Local<v8::Value> start_val;
            if (!perf_user_get(isolate, context, opts, v8_string(isolate, "startTime"))
                    .ToLocal(&start_val)) {
                return false;
            }
            if (!start_val->IsUndefined()) {
                if (!perf_finite_double(isolate, context, start_val,
                        prefix + "Failed to read the 'startTime' property from 'PerformanceMarkOptions': ",
                        &start_time)) {
                    return false;
                }
                has_start_time = true;
            }
        }
    }

    const std::string& candidate_timing =
        name_out != nullptr ? *name_out : small_name;
    if (name_str->Length() <= 32 && is_timing_attribute_name(candidate_timing)) {
        isolate->ThrowException(make_dom_exception(isolate, context, owner,
            "SyntaxError",
            prefix + "'" + candidate_timing +
            "' is part of the PerformanceTiming interface, and cannot be used as a mark name."));
        return false;
    }
    if (has_start_time && start_time < 0.0) {
        return throw_perf_type_error(isolate, prefix,
            "'" + materialize_name() + "' cannot have a negative start time.");
    }
    if (!clone_performance_detail(isolate, context, owner, raw_detail, prefix, detail_out)) {
        return false;
    }
    if (name_v8_out != nullptr) {
        *name_v8_out = name_str;
    }
    *start_time_out = start_time;
    return true;
}

bool resolve_measure_mark_or_time(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    const PerformanceStore* store, bool https_scheme,
    const std::string& measure_name, bool is_numeric, double numeric_val,
    const std::string& mark_name, const std::string& prefix, double* out) {
    if (is_numeric) {
        if (numeric_val < 0.0) {
            return throw_perf_type_error(isolate, prefix,
                "'" + measure_name + "' cannot have a negative time stamp.");
        }
        *out = numeric_val;
        return true;
    }
    if (is_timing_attribute_name(mark_name)) {
        const auto val = deterministic_timing_value(https_scheme, mark_name);
        if (val == 0) {
            isolate->ThrowException(make_dom_exception(isolate, context, owner,
                "InvalidAccessError",
                prefix + "'" + mark_name +
                "' is empty: either the event hasn't happened yet, or it would "
                "provide cross-origin timing information."));
            return false;
        }
        *out = static_cast<double>(val - kDeterministicNavigationStart);
        return true;
    }
    if (store != nullptr) {
        for (auto it = store->entries.rbegin(); it != store->entries.rend(); ++it) {
            if (it->entry_type == "mark" && it->name == mark_name) {
                *out = it->start_time;
                return true;
            }
        }
    }
    isolate->ThrowException(make_dom_exception(isolate, context, owner,
        "SyntaxError",
        prefix + "The mark '" + mark_name + "' does not exist."));
    return false;
}

// --- Private MemoryInfo ---

v8::Local<v8::Private> memory_info_brand_slot(v8::Isolate* isolate) {
    return v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.bom09.memory_info.brand"));
}

v8::Local<v8::Private> memory_info_proto_slot(v8::Isolate* isolate) {
    return v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.bom09.memory_info.proto"));
}

void memory_info_getter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    v8::String::Utf8Value prop_utf8(isolate, info.Data());
    const char* prop = *prop_utf8 != nullptr ? *prop_utf8 : "";
    if (!info.This()->IsObject() ||
        !info.This().As<v8::Object>()->HasPrivate(
            context, memory_info_brand_slot(isolate)).FromMaybe(false)) {
        const auto seq = access_trace_record(isolate,
            native_api_identity(isolate, info.This()->IsObject()
                ? info.This().As<v8::Object>() : context->Global()),
            "host_object", "MemoryInfo", prop, AccessOperation::kGet);
        AccessFunctionPropertyCompletion completion(
            isolate, seq, info, AccessOperation::kGet);
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Illegal invocation")));
        return;
    }
    auto receiver = info.This().As<v8::Object>();
    const auto seq = access_trace_record(isolate,
        native_api_identity(isolate, receiver),
        "host_object", "MemoryInfo", prop, AccessOperation::kGet);
    AccessFunctionPropertyCompletion completion(
        isolate, seq, info, AccessOperation::kGet);
    double value = kDeterministicHeapSizeLimit;
    if (std::strcmp(prop, "totalJSHeapSize") == 0) {
        value = kDeterministicTotalHeapSize;
    } else if (std::strcmp(prop, "usedJSHeapSize") == 0) {
        value = kDeterministicUsedHeapSize;
    }
    info.GetReturnValue().Set(v8::Number::New(isolate, value));
}

v8::Local<v8::Object> ensure_memory_info_prototype(v8::Isolate* isolate,
    v8::Local<v8::Context> context) {
    auto slot = memory_info_proto_slot(isolate);
    v8::Local<v8::Value> cached;
    if (context->Global()->GetPrivate(context, slot).ToLocal(&cached) &&
        cached->IsObject()) {
        return cached.As<v8::Object>();
    }
    auto proto = v8::Object::New(isolate);
    const char* fields[] = {
        "totalJSHeapSize", "usedJSHeapSize", "jsHeapSizeLimit"};
    for (const char* field : fields) {
        v8::Local<v8::Function> getter;
        if (!make_web_method(isolate, context, std::string("get ") + field, 0,
                &memory_info_getter, v8_string(isolate, field)).ToLocal(&getter)) {
            return {};
        }
        v8::PropertyDescriptor desc(getter, v8::Undefined(isolate));
        desc.set_enumerable(true);
        desc.set_configurable(true);
        if (!proto->DefineProperty(context, v8_string(isolate, field).As<v8::Name>(), desc)
                 .FromMaybe(false)) {
            return {};
        }
    }
    if (!install_traced_api_named_data_property(isolate, context, proto,
            v8::Symbol::GetToStringTag(isolate),
            v8::String::NewFromUtf8Literal(isolate, "MemoryInfo"),
            native_api_identity(isolate, proto),
            "host_object", "MemoryInfo.prototype",
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum)) ||
        !context->Global()->SetPrivate(context, slot, proto).FromMaybe(false)) {
        return {};
    }
    return proto;
}

// --- EventCounts Iterator ---

v8::Local<v8::Private> ec_iter_proto_slot(v8::Isolate* isolate) {
    return v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.bom09.ec_iter.proto"));
}

v8::Local<v8::Private> ec_iter_index_slot(v8::Isolate* isolate) {
    return v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.bom09.ec_iter.index"));
}

v8::Local<v8::Private> ec_iter_kind_slot(v8::Isolate* isolate) {
    return v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.bom09.ec_iter.kind"));
}

void ec_iter_next_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    if (!info.This()->IsObject() ||
        !info.This().As<v8::Object>()->HasPrivate(
            context, ec_iter_index_slot(isolate)).FromMaybe(false)) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Illegal invocation")));
        return;
    }
    auto self = info.This().As<v8::Object>();
    v8::Local<v8::Value> idx_val;
    v8::Local<v8::Value> kind_val;
    if (!self->GetPrivate(context, ec_iter_index_slot(isolate)).ToLocal(&idx_val) ||
        !self->GetPrivate(context, ec_iter_kind_slot(isolate)).ToLocal(&kind_val)) {
        return;
    }
    const int idx = idx_val->Int32Value(context).FromMaybe(0);
    const int kind = kind_val->Int32Value(context).FromMaybe(0);
    const auto& keys = event_counts_keys();
    auto step = v8::Object::New(isolate);
    if (idx < 0 || static_cast<std::size_t>(idx) >= keys.size()) {
        (void)step->CreateDataProperty(context,
            v8::String::NewFromUtf8Literal(isolate, "value"),
            v8::Undefined(isolate));
        (void)step->CreateDataProperty(context,
            v8::String::NewFromUtf8Literal(isolate, "done"),
            v8::True(isolate));
        info.GetReturnValue().Set(step);
        return;
    }
    (void)self->SetPrivate(context, ec_iter_index_slot(isolate),
        v8::Integer::New(isolate, idx + 1));
    v8::Local<v8::Value> out_val;
    if (kind == 0) {
        out_val = v8_string(isolate, keys[static_cast<std::size_t>(idx)]);
    } else if (kind == 1) {
        out_val = v8::Integer::New(isolate, 0);
    } else {
        auto pair = v8::Array::New(isolate, 2);
        (void)pair->CreateDataProperty(context, 0,
            v8_string(isolate, keys[static_cast<std::size_t>(idx)]));
        (void)pair->CreateDataProperty(context, 1, v8::Integer::New(isolate, 0));
        out_val = pair;
    }
    (void)step->CreateDataProperty(context,
        v8::String::NewFromUtf8Literal(isolate, "value"), out_val);
    (void)step->CreateDataProperty(context,
        v8::String::NewFromUtf8Literal(isolate, "done"), v8::False(isolate));
    info.GetReturnValue().Set(step);
}

v8::Local<v8::Object> ensure_event_counts_iterator_prototype(
    v8::Isolate* isolate, v8::Local<v8::Context> context) {
    auto runtime_origin = access_runtime_scope(isolate);
    auto pslot = ec_iter_proto_slot(isolate);
    v8::Local<v8::Value> cached_proto;
    if (context->Global()->GetPrivate(context, pslot).ToLocal(&cached_proto) &&
        cached_proto->IsObject()) {
        return cached_proto.As<v8::Object>();
    }
    auto iter_proto = v8::Object::New(isolate);
    auto empty_arr = v8::Array::New(isolate, 0);
    v8::Local<v8::Value> arr_iter_fn;
    v8::Local<v8::Value> arr_iter;
    if (empty_arr->Get(context, v8::Symbol::GetIterator(isolate)).ToLocal(&arr_iter_fn) &&
        arr_iter_fn->IsFunction() &&
        arr_iter_fn.As<v8::Function>()->Call(context, empty_arr, 0, nullptr).ToLocal(&arr_iter) &&
        arr_iter->IsObject()) {
        auto arr_iter_proto = arr_iter.As<v8::Object>()->GetPrototype();
        if (arr_iter_proto->IsObject()) {
            auto base_iter_proto = arr_iter_proto.As<v8::Object>()->GetPrototype();
            if (base_iter_proto->IsObject()) {
                (void)iter_proto->SetPrototype(context, base_iter_proto);
            }
        }
    }
    v8::Local<v8::Function> next_fn;
    if (!make_web_method(isolate, context, "next", 0, &ec_iter_next_callback)
            .ToLocal(&next_fn)) {
        return {};
    }
    (void)iter_proto->DefineOwnProperty(context,
        v8::String::NewFromUtf8Literal(isolate, "next"), next_fn, v8::None);
    (void)iter_proto->DefineOwnProperty(context,
        v8::Symbol::GetToStringTag(isolate),
        v8::String::NewFromUtf8Literal(isolate, "EventCounts Iterator"),
        static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum));
    (void)context->Global()->SetPrivate(context, pslot, iter_proto);
    return iter_proto;
}

v8::Local<v8::Value> create_event_counts_iterator(v8::Isolate* isolate,
    v8::Local<v8::Context> context, int kind) {
    auto runtime_origin = access_runtime_scope(isolate);
    auto iter_proto = ensure_event_counts_iterator_prototype(isolate, context);
    if (iter_proto.IsEmpty()) return v8::Undefined(isolate);

    auto obj = v8::Object::New(isolate);
    (void)obj->SetPrototype(context, iter_proto);
    (void)obj->SetPrivate(context, ec_iter_index_slot(isolate), v8::Integer::New(isolate, 0));
    (void)obj->SetPrivate(context, ec_iter_kind_slot(isolate), v8::Integer::New(isolate, kind));
    return obj;
}

bool reorder_prototype_properties(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> prototype,
    const std::string& interface_name, const std::vector<const char*>& ordered_names) {
    struct SavedDesc {
        std::string name;
        bool is_accessor = false;
        v8::Local<v8::Value> value;
        v8::Local<v8::Value> get;
        v8::Local<v8::Value> set;
        bool writable = true;
        bool enumerable = true;
        bool configurable = true;
    };
    std::vector<SavedDesc> saved;
    saved.reserve(ordered_names.size());
    for (const char* raw : ordered_names) {
        auto key = v8_string(isolate, raw);
        v8::Local<v8::Value> desc_val;
        if (!prototype->GetOwnPropertyDescriptor(context, key.As<v8::Name>())
                 .ToLocal(&desc_val) ||
            !desc_val->IsObject()) {
            return false;
        }
        auto desc_obj = desc_val.As<v8::Object>();
        SavedDesc item;
        item.name = raw;
        v8::Local<v8::Value> e_val;
        v8::Local<v8::Value> c_val;
        if (!desc_obj->Get(context, v8::String::NewFromUtf8Literal(isolate, "enumerable")).ToLocal(&e_val) ||
            !desc_obj->Get(context, v8::String::NewFromUtf8Literal(isolate, "configurable")).ToLocal(&c_val)) {
            return false;
        }
        item.enumerable = e_val->IsTrue();
        item.configurable = c_val->IsTrue();
        if (desc_obj->HasOwnProperty(context,
                v8::String::NewFromUtf8Literal(isolate, "get")).FromMaybe(false)) {
            item.is_accessor = true;
            if (!desc_obj->Get(context, v8::String::NewFromUtf8Literal(isolate, "get")).ToLocal(&item.get) ||
                !desc_obj->Get(context, v8::String::NewFromUtf8Literal(isolate, "set")).ToLocal(&item.set)) {
                return false;
            }
        } else {
            v8::Local<v8::Value> w_val;
            if (!desc_obj->Get(context, v8::String::NewFromUtf8Literal(isolate, "value")).ToLocal(&item.value) ||
                !desc_obj->Get(context, v8::String::NewFromUtf8Literal(isolate, "writable")).ToLocal(&w_val)) {
                return false;
            }
            item.writable = w_val->IsTrue();
        }
        if (!prototype->Delete(context, key).FromMaybe(false)) {
            return false;
        }
        saved.push_back(std::move(item));
    }

    for (const auto& item : saved) {
        if (item.is_accessor) {
            v8::PropertyDescriptor desc(item.get, item.set);
            desc.set_enumerable(item.enumerable);
            desc.set_configurable(item.configurable);
            if (!prototype->DefineProperty(context, v8_string(isolate, item.name).As<v8::Name>(), desc)
                     .FromMaybe(false)) {
                return false;
            }
        } else {
            int attrs = v8::None;
            if (!item.writable) attrs |= v8::ReadOnly;
            if (!item.enumerable) attrs |= v8::DontEnum;
            if (!item.configurable) attrs |= v8::DontDelete;
            if (!install_traced_api_data_property(isolate, context, prototype,
                    item.name, item.value, native_api_identity(isolate, prototype),
                    "host_object", interface_name + ".prototype",
                    static_cast<v8::PropertyAttribute>(attrs))) {
                return false;
            }
        }
    }
    return true;
}

}  // namespace

std::int64_t performance_clock_ms(v8::Isolate* isolate) {
    ContextState* state = state_from_isolate(isolate);
    return state != nullptr ? state->event_loop_time_ms() : 0;
}

v8::Local<v8::Object> create_memory_info_object(v8::Isolate* isolate,
    v8::Local<v8::Context> context) {
    auto runtime_origin = access_runtime_scope(isolate);
    auto proto = ensure_memory_info_prototype(isolate, context);
    if (proto.IsEmpty()) return v8::Object::New(isolate);
    auto obj = v8::Object::New(isolate);
    (void)obj->SetPrototype(context, proto);
    (void)obj->SetPrivate(context, memory_info_brand_slot(isolate), v8::True(isolate));
    return obj;
}

// --- PerformanceEntryHost / Mark / Measure ---

v8::Local<v8::Value> PerformanceEntryHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name) {
    if (name == "name") {
        if (weak_wrapper_ != nullptr && !weak_wrapper_->IsEmpty()) {
            auto runtime_origin = access_runtime_scope(isolate);
            v8::Local<v8::Value> val;
            if (weak_wrapper_->Get(isolate)->GetPrivate(
                    context, perf_entry_name_slot(isolate)).ToLocal(&val) &&
                !val.IsEmpty() && val->IsString()) {
                return val;
            }
        }
        return v8_string(isolate, "");
    }
    if (name == "entryType") return v8_string(isolate, entry_type_);
    if (name == "startTime") return v8::Number::New(isolate, start_time_);
    if (name == "duration") return v8::Number::New(isolate, duration_);
    if (name == "navigationId") return v8::Integer::New(isolate, 1);
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> PerformanceEntryHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, "PerformanceEntry", name);
    if (name != "toJSON") return v8::Undefined(isolate);
    auto runtime_origin = access_runtime_scope(isolate);
    v8::Local<v8::Value> name_val = v8_string(isolate, "");
    if (arguments.This()->IsObject()) {
        v8::Local<v8::Value> priv;
        if (arguments.This().As<v8::Object>()->GetPrivate(
                context, perf_entry_name_slot(isolate)).ToLocal(&priv) &&
            !priv.IsEmpty() && priv->IsString()) {
            name_val = priv;
        }
    }
    auto json = v8::Object::New(isolate);
    (void)json->CreateDataProperty(context, v8_string(isolate, "name").As<v8::Name>(), name_val);
    (void)json->CreateDataProperty(context, v8_string(isolate, "entryType").As<v8::Name>(), v8_string(isolate, entry_type_));
    (void)json->CreateDataProperty(context, v8_string(isolate, "startTime").As<v8::Name>(), v8::Number::New(isolate, start_time_));
    (void)json->CreateDataProperty(context, v8_string(isolate, "duration").As<v8::Name>(), v8::Number::New(isolate, duration_));
    (void)json->CreateDataProperty(context, v8_string(isolate, "navigationId").As<v8::Name>(), v8::Integer::New(isolate, 1));
    return json;
}

void attach_perf_entry_slots(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> wrapper,
    v8::Local<v8::String> entry_name, v8::Local<v8::Value> detail) {
    auto runtime_origin = access_runtime_scope(isolate);
    const v8::Local<v8::Value> stored_name = entry_name.IsEmpty()
        ? v8_string(isolate, "") : v8::Local<v8::Value>(entry_name);
    const v8::Local<v8::Value> stored_detail =
        detail.IsEmpty() || detail->IsUndefined()
            ? v8::Local<v8::Value>(v8::Null(isolate)) : detail;
    (void)wrapper->SetPrivate(context, perf_entry_name_slot(isolate), stored_name);
    (void)wrapper->SetPrivate(context, perf_entry_detail_slot(isolate), stored_detail);
}

void perf_entry_detail_getter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    v8::String::Utf8Value expected_utf8(isolate, info.Data());
    const std::string expected_iface(
        *expected_utf8 != nullptr ? *expected_utf8 : "");
    HostObject* host = host_object_backing(info.This());
    if (host == nullptr || expected_iface.empty() ||
        !web_interface_inherits(host->prototype_key(), expected_iface)) {
        const auto seq = access_trace_record(isolate,
            native_api_identity(isolate, info.This()->IsObject()
                ? info.This().As<v8::Object>() : context->Global()),
            "host_object", expected_iface.c_str(), "detail", AccessOperation::kGet);
        AccessFunctionPropertyCompletion completion(
            isolate, seq, info, AccessOperation::kGet);
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Illegal invocation")));
        return;
    }
    const auto obj_type = host->trace_object_type();
    const auto obj_label = host->trace_label();
    const auto seq = access_trace_record(isolate, host,
        obj_type.c_str(), obj_label.c_str(),
        "detail", AccessOperation::kGet);
    AccessFunctionPropertyCompletion completion(
        isolate, seq, info, AccessOperation::kGet);
    v8::Local<v8::Value> detail;
    if (info.This().As<v8::Object>()->GetPrivate(
            context, perf_entry_detail_slot(isolate)).ToLocal(&detail) &&
        !detail.IsEmpty()) {
        info.GetReturnValue().Set(detail);
        return;
    }
    info.GetReturnValue().Set(v8::Null(isolate));
}

bool install_perf_detail_accessor(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const char* interface_name) {
    auto proto = web_interface_prototype(isolate, context, interface_name);
    if (proto.IsEmpty()) return false;
    v8::Local<v8::Function> getter;
    if (!make_web_method(isolate, context, "get detail", 0,
            &perf_entry_detail_getter, v8_string(isolate, interface_name))
             .ToLocal(&getter)) {
        return false;
    }
    v8::PropertyDescriptor desc(getter, v8::Undefined(isolate));
    desc.set_enumerable(true);
    desc.set_configurable(true);
    return proto->DefineProperty(
        context, v8::String::NewFromUtf8Literal(isolate, "detail"), desc)
        .FromMaybe(false);
}

// --- PerformanceNavigationHost ---

v8::Local<v8::Value> PerformanceNavigationHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name) {
    if (name == "type" || name == "redirectCount") {
        return v8::Integer::New(isolate, 0);
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> PerformanceNavigationHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>&) {
    watch_record(isolate, "PerformanceNavigation", name);
    if (name != "toJSON") return v8::Undefined(isolate);
    auto runtime_origin = access_runtime_scope(isolate);
    auto json = v8::Object::New(isolate);
    (void)json->CreateDataProperty(context, v8_string(isolate, "type").As<v8::Name>(), v8::Integer::New(isolate, 0));
    (void)json->CreateDataProperty(context, v8_string(isolate, "redirectCount").As<v8::Name>(), v8::Integer::New(isolate, 0));
    return json;
}

// --- PerformanceTimingHost ---

std::vector<std::string> PerformanceTimingHost::property_names() const {
    std::vector<std::string> result;
    for (const char* n : timing_attribute_names()) result.emplace_back(n);
    return result;
}

v8::Local<v8::Value> PerformanceTimingHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name) {
    return v8::Number::New(isolate,
        static_cast<double>(deterministic_timing_value(https_scheme_, name)));
}

v8::Local<v8::Value> PerformanceTimingHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>&) {
    watch_record(isolate, "PerformanceTiming", name);
    if (name != "toJSON") return v8::Undefined(isolate);
    auto runtime_origin = access_runtime_scope(isolate);
    auto json = v8::Object::New(isolate);
    for (const char* field : timing_to_json_names()) {
        (void)json->CreateDataProperty(context, v8_string(isolate, field).As<v8::Name>(),
            v8::Number::New(isolate, static_cast<double>(
                deterministic_timing_value(https_scheme_, field))));
    }
    return json;
}

// --- EventCountsHost ---

v8::Local<v8::Value> EventCountsHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name) {
    if (name == "size") {
        return v8::Integer::New(isolate,
            static_cast<int>(event_counts_keys().size()));
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> EventCountsHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, "EventCounts", name);
    const std::string prefix =
        "Failed to execute '" + name + "' on 'EventCounts': ";
    const auto& keys = event_counts_keys();

    if (name == "keys") return create_event_counts_iterator(isolate, context, 0);
    if (name == "values") return create_event_counts_iterator(isolate, context, 1);
    if (name == "entries") return create_event_counts_iterator(isolate, context, 2);

    if (name == "has" || name == "get") {
        if (arguments.Length() < 1) {
            throw_perf_type_error(isolate, prefix,
                "1 argument required, but only 0 present.");
            return v8::Undefined(isolate);
        }
        std::string key;
        if (!perf_to_string(isolate, context, arguments[0], &key)) {
            return v8::Undefined(isolate);
        }
        const bool found = std::any_of(keys.begin(), keys.end(),
            [&](const char* k) { return key == k; });
        if (name == "has") return v8::Boolean::New(isolate, found);
        return found
            ? v8::Local<v8::Value>(v8::Integer::New(isolate, 0))
            : v8::Local<v8::Value>(v8::Undefined(isolate));
    }

    if (name == "forEach") {
        if (arguments.Length() < 1) {
            throw_perf_type_error(isolate, prefix,
                "1 argument required, but only 0 present.");
            return v8::Undefined(isolate);
        }
        if (!(arguments[0]->IsObject() &&
              arguments[0].As<v8::Object>()->IsCallable())) {
            throw_perf_type_error(isolate, prefix,
                "parameter 1 is not of type 'Function'.");
            return v8::Undefined(isolate);
        }
        auto cb = arguments[0].As<v8::Object>();
        const v8::Local<v8::Value> this_arg = arguments.Length() >= 2
            ? arguments[1] : v8::Local<v8::Value>(v8::Undefined(isolate));
        for (const char* k : keys) {
            v8::Local<v8::Value> argv[] = {
                v8::Integer::New(isolate, 0),
                v8_string(isolate, k),
                arguments.This(),
            };
            v8::Local<v8::Value> out;
            if (!access_user_operation(isolate, [&] {
                    return cb->CallAsFunction(context, this_arg, 3, argv);
                }).ToLocal(&out)) {
                return v8::Undefined(isolate);
            }
        }
        return v8::Undefined(isolate);
    }

    return v8::Undefined(isolate);
}

// --- PerformanceHost ---

v8::Local<v8::Object> PerformanceHost::ensure_timing(v8::Isolate* isolate,
    v8::Local<v8::Context> context) {
    if (timing_object_.IsEmpty()) {
        auto host = std::make_unique<PerformanceTimingHost>(https_scheme_);
        auto obj = owner_->adopt_host_object(isolate, context, std::move(host));
        timing_object_.Reset(isolate, obj);
    }
    return timing_object_.Get(isolate);
}

v8::Local<v8::Object> PerformanceHost::ensure_navigation(v8::Isolate* isolate,
    v8::Local<v8::Context> context) {
    if (navigation_object_.IsEmpty()) {
        auto host = std::make_unique<PerformanceNavigationHost>();
        auto obj = owner_->adopt_host_object(isolate, context, std::move(host));
        navigation_object_.Reset(isolate, obj);
    }
    return navigation_object_.Get(isolate);
}

v8::Local<v8::Object> PerformanceHost::ensure_event_counts(v8::Isolate* isolate,
    v8::Local<v8::Context> context) {
    if (event_counts_object_.IsEmpty()) {
        auto host = std::make_unique<EventCountsHost>();
        auto obj = owner_->adopt_host_object(isolate, context, std::move(host));
        event_counts_object_.Reset(isolate, obj);
    }
    return event_counts_object_.Get(isolate);
}

v8::Local<v8::Value> PerformanceHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name) {
    if (name == "timeOrigin") return v8::Number::New(isolate, 0.0);
    if (name == "onresourcetimingbufferfull") return get_event_handler(isolate, name);
    if (name == "timing") return ensure_timing(isolate, context);
    if (name == "navigation") return ensure_navigation(isolate, context);
    if (name == "memory") return create_memory_info_object(isolate, context);
    if (name == "eventCounts") return ensure_event_counts(isolate, context);
    if (name == "interactionCount") return v8::Integer::New(isolate, 0);
    return v8::Undefined(isolate);
}

void PerformanceHost::set_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name,
    v8::Local<v8::Value> value) {
    if (name == "onresourcetimingbufferfull") {
        set_event_handler(isolate, name, value);
    }
}

v8::Local<v8::Value> PerformanceHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, "performance", name);
    if (!validate_bom_event_dispatch(isolate, context, name, arguments)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> event_result;
    if (handle_event_method(isolate, context, name, arguments, event_result)) {
        return event_result;
    }

    const std::string prefix =
        "Failed to execute '" + name + "' on 'Performance': ";

    if (name == "now") {
        return v8::Number::New(isolate,
            static_cast<double>(performance_clock_ms(isolate)));
    }

    if (name == "toJSON") {
        auto runtime_origin = access_runtime_scope(isolate);
        auto json = v8::Object::New(isolate);
        auto timing_json = v8::Object::New(isolate);
        for (const char* field : timing_to_json_names()) {
            (void)timing_json->CreateDataProperty(context, v8_string(isolate, field).As<v8::Name>(),
                v8::Number::New(isolate, static_cast<double>(
                    deterministic_timing_value(https_scheme_, field))));
        }
        auto nav_json = v8::Object::New(isolate);
        (void)nav_json->CreateDataProperty(context, v8_string(isolate, "type").As<v8::Name>(),
            v8::Integer::New(isolate, 0));
        (void)nav_json->CreateDataProperty(context, v8_string(isolate, "redirectCount").As<v8::Name>(),
            v8::Integer::New(isolate, 0));
        (void)json->CreateDataProperty(context, v8_string(isolate, "timeOrigin").As<v8::Name>(),
            v8::Number::New(isolate, 0.0));
        (void)json->CreateDataProperty(context, v8_string(isolate, "timing").As<v8::Name>(), timing_json);
        (void)json->CreateDataProperty(context, v8_string(isolate, "navigation").As<v8::Name>(), nav_json);
        return json;
    }

    if (name == "clearResourceTimings") {
        return v8::Undefined(isolate);
    }

    if (name == "setResourceTimingBufferSize") {
        if (arguments.Length() < 1) {
            throw_perf_type_error(isolate, prefix,
                "1 argument required, but only 0 present.");
            return v8::Undefined(isolate);
        }
        std::uint32_t max_size = 0;
        if (!access_user_operation(isolate, [&] {
                return arguments[0]->Uint32Value(context);
            }).To(&max_size)) {
            return v8::Undefined(isolate);
        }
        if (store_ != nullptr) {
            store_->resource_timing_buffer_size = max_size;
        }
        return v8::Undefined(isolate);
    }

    if (name == "mark") {
        std::string mark_name;
        v8::Local<v8::String> mark_name_v8;
        double start_time = 0.0;
        v8::Local<v8::Value> detail;
        if (!parse_performance_mark_args(isolate, context, owner_,
                arguments.Length(),
                [&](int idx) { return arguments[idx]; },
                prefix, &mark_name, &mark_name_v8, &start_time, &detail)) {
            return v8::Undefined(isolate);
        }
        auto mark_host = std::make_unique<PerformanceMarkHost>(start_time);
        auto* mark_raw = mark_host.get();
        const v8::Global<v8::Object>* weak_slot = nullptr;
        auto mark_obj = owner_->adopt_ephemeral_host_object(
            isolate, context, std::move(mark_host), &weak_slot);
        mark_raw->set_weak_wrapper(weak_slot);
        attach_perf_entry_slots(isolate, context, mark_obj, mark_name_v8, detail);
        if (store_ != nullptr) {
            PerformanceEntryRecord rec;
            rec.name = std::move(mark_name);
            rec.entry_type = "mark";
            rec.start_time = start_time;
            rec.duration = 0.0;
            rec.wrapper.Reset(isolate, mark_obj);
            store_->entries.push_back(std::move(rec));
        }
        return mark_obj;
    }

    if (name == "measure") {
        if (arguments.Length() < 1) {
            throw_perf_type_error(isolate, prefix,
                "1 argument required, but only 0 present.");
            return v8::Undefined(isolate);
        }
        v8::Local<v8::String> measure_name_v8;
        if (!access_user_operation(isolate, [&] {
                return arguments[0]->ToString(context);
            }).ToLocal(&measure_name_v8)) {
            return v8::Undefined(isolate);
        }
        v8::String::Utf8Value measure_utf8(isolate, measure_name_v8);
        std::string measure_name(*measure_utf8 != nullptr ? *measure_utf8 : "",
            *measure_utf8 != nullptr ? static_cast<std::size_t>(measure_utf8.length()) : 0);

        v8::Local<v8::Value> raw_detail = v8::Undefined(isolate);
        bool has_detail = false;
        bool has_duration = false;
        double duration_opt = 0.0;
        bool has_start = false;
        bool start_is_num = false;
        double start_num = 0.0;
        std::string start_mark_str;
        bool has_end = false;
        bool end_is_num = false;
        double end_num = 0.0;
        std::string end_mark_str;
        bool options_non_empty = false;

        if (arguments.Length() >= 2 && !arguments[1]->IsNullOrUndefined()) {
            if (arguments[1]->IsObject()) {
                auto opts = arguments[1].As<v8::Object>();
                // Lexicographical WebIDL PerformanceMeasureOptions:
                // detail, duration, end, start.
                if (!perf_user_get(isolate, context, opts, v8_string(isolate, "detail"))
                        .ToLocal(&raw_detail)) {
                    return v8::Undefined(isolate);
                }
                has_detail = !raw_detail->IsUndefined();

                v8::Local<v8::Value> dur_val;
                if (!perf_user_get(isolate, context, opts, v8_string(isolate, "duration"))
                        .ToLocal(&dur_val)) {
                    return v8::Undefined(isolate);
                }
                if (!dur_val->IsUndefined()) {
                    if (!perf_finite_double(isolate, context, dur_val,
                            prefix + "Failed to read the 'duration' property from 'PerformanceMeasureOptions': ",
                            &duration_opt)) {
                        return v8::Undefined(isolate);
                    }
                    has_duration = true;
                }

                v8::Local<v8::Value> end_val;
                if (!perf_user_get(isolate, context, opts, v8_string(isolate, "end"))
                        .ToLocal(&end_val)) {
                    return v8::Undefined(isolate);
                }
                if (!end_val->IsUndefined()) {
                    has_end = true;
                    if (end_val->IsNumber()) {
                        end_is_num = true;
                        if (!perf_finite_double(isolate, context, end_val,
                                prefix + "Failed to read the 'end' property from 'PerformanceMeasureOptions': ",
                                &end_num)) {
                            return v8::Undefined(isolate);
                        }
                    } else if (!perf_to_string(isolate, context, end_val, &end_mark_str)) {
                        return v8::Undefined(isolate);
                    }
                }

                v8::Local<v8::Value> start_val;
                if (!perf_user_get(isolate, context, opts, v8_string(isolate, "start"))
                        .ToLocal(&start_val)) {
                    return v8::Undefined(isolate);
                }
                if (!start_val->IsUndefined()) {
                    has_start = true;
                    if (start_val->IsNumber()) {
                        start_is_num = true;
                        if (!perf_finite_double(isolate, context, start_val,
                                prefix + "Failed to read the 'start' property from 'PerformanceMeasureOptions': ",
                                &start_num)) {
                            return v8::Undefined(isolate);
                        }
                    } else if (!perf_to_string(isolate, context, start_val, &start_mark_str)) {
                        return v8::Undefined(isolate);
                    }
                }

                options_non_empty = has_detail || has_duration || has_start || has_end;
                if (options_non_empty) {
                    if (arguments.Length() >= 3 && !arguments[2]->IsUndefined()) {
                        throw_perf_type_error(isolate, prefix,
                            "If a non-empty PerformanceMeasureOptions object was passed, |end_mark| must not be passed.");
                        return v8::Undefined(isolate);
                    }
                    if (!has_start && !has_end) {
                        throw_perf_type_error(isolate, prefix,
                            "If a non-empty PerformanceMeasureOptions object was passed, at least one of its 'start' or 'end' properties must be present.");
                        return v8::Undefined(isolate);
                    }
                    if (has_start && has_duration && has_end) {
                        throw_perf_type_error(isolate, prefix,
                            "If a non-empty PerformanceMeasureOptions object was passed, it must not have all of its 'start', 'duration', and 'end' properties defined");
                        return v8::Undefined(isolate);
                    }
                }
            } else {
                has_start = true;
                start_is_num = false;
                if (!perf_to_string(isolate, context, arguments[1], &start_mark_str)) {
                    return v8::Undefined(isolate);
                }
            }
        }

        if (!options_non_empty && arguments.Length() >= 3 &&
            !arguments[2]->IsUndefined()) {
            has_end = true;
            end_is_num = false;
            if (!perf_to_string(isolate, context, arguments[2], &end_mark_str)) {
                return v8::Undefined(isolate);
            }
        }

        double resolved_start = 0.0;
        if (has_start) {
            if (!resolve_measure_mark_or_time(isolate, context, owner_, store_,
                    https_scheme_, measure_name, start_is_num, start_num,
                    start_mark_str, prefix, &resolved_start)) {
                return v8::Undefined(isolate);
            }
        }
        double resolved_end = 0.0;
        if (has_end) {
            if (!resolve_measure_mark_or_time(isolate, context, owner_, store_,
                    https_scheme_, measure_name, end_is_num, end_num,
                    end_mark_str, prefix, &resolved_end)) {
                return v8::Undefined(isolate);
            }
        }

        double final_end = 0.0;
        if (has_end) {
            final_end = resolved_end;
        } else if (has_start && has_duration) {
            final_end = resolved_start + duration_opt;
        } else {
            final_end = static_cast<double>(performance_clock_ms(isolate));
        }

        double final_start = 0.0;
        if (has_start) {
            final_start = resolved_start;
        } else if (has_end && has_duration) {
            final_start = final_end - duration_opt;
        } else {
            final_start = 0.0;
        }

        const double final_duration = has_duration
            ? duration_opt : (final_end - final_start);

        v8::Local<v8::Value> detail;
        if (!clone_performance_detail(isolate, context, owner_, raw_detail, prefix, &detail)) {
            return v8::Undefined(isolate);
        }
        auto meas_host = std::make_unique<PerformanceMeasureHost>(
            final_start, final_duration);
        auto* meas_raw = meas_host.get();
        const v8::Global<v8::Object>* weak_slot = nullptr;
        auto meas_obj = owner_->adopt_ephemeral_host_object(
            isolate, context, std::move(meas_host), &weak_slot);
        meas_raw->set_weak_wrapper(weak_slot);
        attach_perf_entry_slots(isolate, context, meas_obj, measure_name_v8, detail);
        if (store_ != nullptr) {
            PerformanceEntryRecord rec;
            rec.name = std::move(measure_name);
            rec.entry_type = "measure";
            rec.start_time = final_start;
            rec.duration = final_duration;
            rec.wrapper.Reset(isolate, meas_obj);
            store_->entries.push_back(std::move(rec));
        }
        return meas_obj;
    }

    if (name == "getEntries" || name == "getEntriesByName" ||
        name == "getEntriesByType") {
        std::string filter_name;
        bool has_name = false;
        std::string filter_type;
        bool has_type = false;

        if (name == "getEntriesByName") {
            if (arguments.Length() < 1) {
                throw_perf_type_error(isolate, prefix,
                    "1 argument required, but only 0 present.");
                return v8::Undefined(isolate);
            }
            if (!perf_to_string(isolate, context, arguments[0], &filter_name)) {
                return v8::Undefined(isolate);
            }
            has_name = true;
            if (arguments.Length() >= 2 && !arguments[1]->IsUndefined()) {
                if (!perf_to_string(isolate, context, arguments[1], &filter_type)) {
                    return v8::Undefined(isolate);
                }
                has_type = true;
            }
        } else if (name == "getEntriesByType") {
            if (arguments.Length() < 1) {
                throw_perf_type_error(isolate, prefix,
                    "1 argument required, but only 0 present.");
                return v8::Undefined(isolate);
            }
            if (!perf_to_string(isolate, context, arguments[0], &filter_type)) {
                return v8::Undefined(isolate);
            }
            has_type = true;
        }

        std::vector<const PerformanceEntryRecord*> matched;
        if (store_ != nullptr) {
            for (const auto& entry : store_->entries) {
                if ((has_name && entry.name != filter_name) ||
                    (has_type && entry.entry_type != filter_type)) {
                    continue;
                }
                if (!entry.wrapper.IsEmpty()) {
                    matched.push_back(&entry);
                }
            }
        }
        std::stable_sort(matched.begin(), matched.end(),
            [](const PerformanceEntryRecord* a, const PerformanceEntryRecord* b) {
                return a->start_time < b->start_time;
            });

        auto out = v8::Array::New(isolate, static_cast<int>(matched.size()));
        for (std::uint32_t i = 0; i < matched.size(); ++i) {
            (void)out->CreateDataProperty(context, i, matched[i]->wrapper.Get(isolate));
        }
        return out;
    }

    if (name == "clearMarks" || name == "clearMeasures") {
        const std::string target_type = name == "clearMarks" ? "mark" : "measure";
        bool has_name = false;
        std::string target_name;
        if (arguments.Length() >= 1 && !arguments[0]->IsUndefined()) {
            if (!perf_to_string(isolate, context, arguments[0], &target_name)) {
                return v8::Undefined(isolate);
            }
            has_name = true;
        }
        if (store_ != nullptr) {
            auto it = store_->entries.begin();
            while (it != store_->entries.end()) {
                if (it->entry_type == target_type &&
                    (!has_name || it->name == target_name)) {
                    it->wrapper.Reset();
                    it = store_->entries.erase(it);
                } else {
                    ++it;
                }
            }
        }
        return v8::Undefined(isolate);
    }

    return v8::Undefined(isolate);
}

// --- Constructor & Installation ---

v8::Local<v8::Value> construct_performance_mark(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    v8::Local<v8::Array> arguments) {
    const std::string prefix = "Failed to construct 'PerformanceMark': ";
    v8::Local<v8::String> mark_name_v8;
    double start_time = 0.0;
    v8::Local<v8::Value> detail;
    if (!parse_performance_mark_args(isolate, context, owner,
            static_cast<int>(arguments->Length()),
            [&](int idx) {
                v8::Local<v8::Value> val;
                if (!arguments->Get(context, static_cast<std::uint32_t>(idx)).ToLocal(&val)) {
                    return v8::Local<v8::Value>();
                }
                return val;
            },
            prefix, /*name_out=*/nullptr, &mark_name_v8, &start_time, &detail)) {
        return v8::Undefined(isolate);
    }
    auto mark_host = std::make_unique<PerformanceMarkHost>(start_time);
    auto* mark_raw = mark_host.get();
    const v8::Global<v8::Object>* weak_slot = nullptr;
    auto mark_obj = owner->adopt_ephemeral_host_object(
        isolate, context, std::move(mark_host), &weak_slot);
    mark_raw->set_weak_wrapper(weak_slot);
    attach_perf_entry_slots(isolate, context, mark_obj, mark_name_v8, detail);
    return mark_obj;
}

bool install_performance_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    PerformanceStore* store, const std::string& base_url) {
    const bool https_scheme = is_https_base_url(base_url);

    // Pre-initialize EventCounts Iterator prototype before user scripts run so
    // replacing Array.prototype[Symbol.iterator] never affects EventCounts.
    if (ensure_event_counts_iterator_prototype(isolate, context).IsEmpty()) {
        return false;
    }

    PerformanceEntryHost entry_decl("", 0.0, 0.0);
    PerformanceMarkHost mark_decl(0.0);
    PerformanceMeasureHost meas_decl(0.0, 0.0);
    PerformanceNavigationHost nav_decl;
    PerformanceTimingHost timing_decl(https_scheme);
    EventCountsHost ec_decl;
    PerformanceHost perf_decl(owner, store, https_scheme);

    for (HostObject* host : {static_cast<HostObject*>(&entry_decl),
                             static_cast<HostObject*>(&mark_decl),
                             static_cast<HostObject*>(&meas_decl),
                             static_cast<HostObject*>(&nav_decl),
                             static_cast<HostObject*>(&timing_decl),
                             static_cast<HostObject*>(&ec_decl),
                             static_cast<HostObject*>(&perf_decl)}) {
        auto proto = web_interface_prototype(isolate, context, host->prototype_key());
        if (proto.IsEmpty()) return false;
        install_host_interface(isolate, context, host);
    }
    if (!install_perf_detail_accessor(isolate, context, "PerformanceMark") ||
        !install_perf_detail_accessor(isolate, context, "PerformanceMeasure")) {
        return false;
    }

    // PerformanceNavigation static & prototype constants:
    // TYPE_NAVIGATE=0, TYPE_RELOAD=1, TYPE_BACK_FORWARD=2, TYPE_RESERVED=255.
    // On PerformanceNavigation.prototype, Chrome orders:
    // type, redirectCount, TYPE_*, toJSON, constructor, Symbol.toStringTag.
    auto nav_proto = web_interface_prototype(isolate, context, "PerformanceNavigation");
    v8::Local<v8::Value> nav_ctor;
    v8::Local<v8::Value> nav_to_json;
    if (nav_proto.IsEmpty() ||
        !nav_proto->Get(context, v8_string(isolate, "constructor")).ToLocal(&nav_ctor) ||
        !nav_ctor->IsFunction() ||
        !nav_proto->Get(context, v8_string(isolate, "toJSON")).ToLocal(&nav_to_json) ||
        !nav_proto->Delete(context, v8_string(isolate, "toJSON")).FromMaybe(false) ||
        !nav_proto->Delete(context, v8_string(isolate, "constructor")).FromMaybe(false)) {
        return false;
    }
    const std::pair<const char*, int> nav_consts[] = {
        {"TYPE_NAVIGATE", 0}, {"TYPE_RELOAD", 1},
        {"TYPE_BACK_FORWARD", 2}, {"TYPE_RESERVED", 255}};
    for (const auto& [cname, cval] : nav_consts) {
        for (auto target : {nav_ctor.As<v8::Object>(), nav_proto}) {
            if (!install_traced_api_data_property(isolate, context, target, cname,
                    v8::Integer::New(isolate, cval),
                    native_api_identity(isolate, target),
                    "host_object",
                    target == nav_proto ? "PerformanceNavigation.prototype" : "PerformanceNavigation",
                    static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete))) {
                return false;
            }
        }
    }
    if (!install_traced_api_data_property(isolate, context, nav_proto, "toJSON",
            nav_to_json, native_api_identity(isolate, nav_proto),
            "host_object", "PerformanceNavigation.prototype") ||
        !install_traced_api_data_property(isolate, context, nav_proto, "constructor",
            nav_ctor, native_api_identity(isolate, nav_proto),
            "host_object", "PerformanceNavigation.prototype", v8::DontEnum)) {
        return false;
    }

    // Reorder prototypes to match Chrome 158's exact own-property ordering.
    if (!reorder_prototype_properties(isolate, context,
            web_interface_prototype(isolate, context, "PerformanceEntry"),
            "PerformanceEntry",
            {"name", "entryType", "startTime", "duration", "navigationId", "toJSON", "constructor"}) ||
        !reorder_prototype_properties(isolate, context,
            web_interface_prototype(isolate, context, "PerformanceMark"),
            "PerformanceMark",
            {"detail", "constructor"}) ||
        !reorder_prototype_properties(isolate, context,
            web_interface_prototype(isolate, context, "PerformanceMeasure"),
            "PerformanceMeasure",
            {"detail", "constructor"})) {
        return false;
    }

    std::vector<const char*> timing_order = timing_attribute_names();
    timing_order.push_back("toJSON");
    timing_order.push_back("constructor");
    if (!reorder_prototype_properties(isolate, context,
            web_interface_prototype(isolate, context, "PerformanceTiming"),
            "PerformanceTiming", timing_order)) {
        return false;
    }

    auto ec_proto = web_interface_prototype(isolate, context, "EventCounts");
    if (!reorder_prototype_properties(isolate, context, ec_proto,
            "EventCounts",
            {"size", "entries", "forEach", "get", "has", "keys", "values", "constructor"})) {
        return false;
    }
    v8::Local<v8::Value> entries_fn;
    if (!ec_proto->Get(context, v8_string(isolate, "entries")).ToLocal(&entries_fn) ||
        !entries_fn->IsFunction() ||
        !install_traced_api_named_data_property(isolate, context, ec_proto,
            v8::Symbol::GetIterator(isolate), entries_fn,
            native_api_identity(isolate, ec_proto),
            "host_object", "EventCounts.prototype", v8::DontEnum)) {
        return false;
    }

    auto perf_proto = web_interface_prototype(isolate, context, "Performance");
    if (!reorder_prototype_properties(isolate, context, perf_proto,
            "Performance",
            {"timeOrigin", "onresourcetimingbufferfull",
             "clearMarks", "clearMeasures", "clearResourceTimings",
             "getEntries", "getEntriesByName", "getEntriesByType",
             "mark", "measure", "setResourceTimingBufferSize",
             "toJSON", "now", "constructor",
             "timing", "navigation", "memory", "eventCounts", "interactionCount"})) {
        return false;
    }

    // Install window.performance (ReadOnly | DontDelete).
    auto perf_host = std::make_unique<PerformanceHost>(owner, store, https_scheme);
    auto perf_obj = owner->adopt_host_object(isolate, context, std::move(perf_host));
    return install_traced_window_data_property(isolate, context, context->Global(),
        "performance", perf_obj,
        static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete));
}

}  // namespace iv8::browser
