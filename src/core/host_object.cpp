#include "iv8/host_object.h"

#include <algorithm>
#include <cstdio>
#include <string>
#include <utility>
#include <vector>

#include "iv8/access_trace.h"

namespace iv8 {

namespace {

// Tag for the host-object back-pointer stored in internal field 0 (V8 15.0
// requires a tag on aligned-pointer internal fields; the default suffices for
// the single host-object pointer type M2-1 stores).
constexpr v8::EmbedderDataTypeTag kHostObjectTag = v8::kEmbedderDataTypeTagDefault;

v8::Local<v8::String> v8str(v8::Isolate* isolate, const std::string& s) {
    return v8::String::NewFromUtf8(isolate, s.c_str(), v8::NewStringType::kNormal,
                                   static_cast<int>(s.size()))
        .ToLocalChecked();
}

// Recover the native HostObject* stored in the receiver's aligned-pointer
// internal field. Fails safe (nullptr) if the receiver is not a host object
// (e.g. a derived object without the internal field), so a stray access returns
// JS undefined rather than dereferencing garbage.
HostObject* backing_of(v8::Local<v8::Object> self) {
    if (self.IsEmpty() || self->InternalFieldCount() < 1) {
        return nullptr;
    }
    return static_cast<HostObject*>(
        self->GetAlignedPointerFromInternalField(0, kHostObjectTag));
}

// Record a native host entry before dispatch when the Page-level access trace is
// enabled. The fast path is one isolate-data lookup plus an atomic check; labels
// and type strings are built only while tracing. This is intentionally separate
// from the old watch_apis path.
AccessTrace* active_trace(v8::Isolate* isolate) {
    AccessTrace* trace = access_trace_from_isolate(isolate);
    return trace != nullptr && trace->enabled() ? trace : nullptr;
}

std::uint64_t record_access(AccessTrace* trace, HostObject* host,
                            const std::string& member,
                            AccessOperation operation) {
    if (trace == nullptr) {
        return 0;
    }
    return trace->record(host, host->trace_object_type(), host->trace_label(),
                         member, operation);
}

// Step 6a value summaries. Only primitive/type inspection is performed: no
// toString/valueOf, property Get, or other user code can run here.
constexpr int kMaxSummaryBytes = 64;
constexpr int kMaxConvertedStringBytes = 4096;
constexpr int kMaxArgumentSummaries = 8;

std::string summarize_string(v8::Isolate* isolate,
                             v8::Local<v8::String> value) {
    const int utf8_length = value->Utf8Length(isolate);
    std::string summary = "string:" + std::to_string(utf8_length) + ":";
    if (utf8_length > kMaxConvertedStringBytes) {
        summary += "...";
        return summary;
    }
    v8::String::Utf8Value utf8(isolate, value);
    if (*utf8 == nullptr) {
        summary += "...";
        return summary;
    }
    const std::size_t length = static_cast<std::size_t>(utf8.length());
    std::size_t cut = std::min(length, static_cast<std::size_t>(kMaxSummaryBytes));
    if (cut < length) {
        // Do not split a UTF-8 continuation byte; Python must receive valid
        // UTF-8 from the report.
        while (cut > 0 &&
               (static_cast<unsigned char>((*utf8)[cut]) & 0xC0) == 0x80) {
            --cut;
        }
    }
    const auto* bytes = reinterpret_cast<const unsigned char*>(*utf8);
    for (std::size_t i = 0; i < cut; ++i) {
        const unsigned char c = bytes[i];
        switch (c) {
            case '\\':
                summary += "\\\\";
                break;
            case '\n':
                summary += "\\n";
                break;
            case '\r':
                summary += "\\r";
                break;
            case '\t':
                summary += "\\t";
                break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof(escaped), "\\x%02x", c);
                    summary += escaped;
                } else {
                    summary += static_cast<char>(c);
                }
                break;
        }
    }
    if (cut < length) {
        summary += "...";
    }
    return summary;
}

std::string summarize_value(v8::Isolate* isolate, v8::Local<v8::Value> value) {
    if (value.IsEmpty() || value->IsUndefined()) {
        return "undefined";
    }
    if (value->IsNull()) {
        return "null";
    }
    if (value->IsBoolean()) {
        return value->BooleanValue(isolate) ? "boolean:true" : "boolean:false";
    }
    if (value->IsNumber()) {
        char buffer[40];
        std::snprintf(buffer, sizeof(buffer), "%.17g",
                      value.As<v8::Number>()->Value());
        return std::string("number:") + buffer;
    }
    if (value->IsString()) {
        return summarize_string(isolate, value.As<v8::String>());
    }
    if (value->IsBigInt()) {
        return "bigint";
    }
    if (value->IsSymbol()) {
        return "symbol";
    }
    if (value->IsFunction()) {
        return "function";
    }
    if (value->IsArray()) {
        return "array";
    }
    if (value->IsObject()) {
        return host_object_backing(value) != nullptr ? "host_object" : "object";
    }
    return "unknown";
}

std::vector<std::string> summarize_arguments(
    v8::Isolate* isolate,
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    const int count = std::min(info.Length(), kMaxArgumentSummaries);
    std::vector<std::string> summaries;
    summaries.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        summaries.push_back(summarize_value(isolate, info[i]));
    }
    return summaries;
}

// Keep existence/attribute queries on the normal lookup path. Without a query
// handler V8 may use the getter interceptor for `in`, hasOwnProperty, and
// descriptor operations; a no-op query prevents those from being mistaken for
// ordinary property reads.
v8::Intercepted named_property_query(
    v8::Local<v8::Name>,
    const v8::PropertyCallbackInfo<v8::Integer>&) {
    return v8::Intercepted::kNo;
}

// Walk real own-property descriptors without invoking getters or interceptors.
// A Proxy or a host-object prototype is conservatively treated as present: we
// suppress a probe rather than report a false missing property.
bool property_is_present(v8::Local<v8::Object> holder,
                         v8::Local<v8::Context> context,
                         v8::Local<v8::Name> property) {
    v8::Local<v8::Object> current = holder;
    for (int depth = 0; depth < 1000; depth++) {
        if (current.IsEmpty() || current->IsProxy()) {
            return true;
        }
        if (current->HasRealNamedProperty(context, property).FromMaybe(true)) {
            return true;
        }
        v8::Local<v8::Value> prototype = current->GetPrototype();
        if (prototype->IsNullOrUndefined()) {
            return false;
        }
        if (!prototype->IsObject()) {
            return false;
        }
        current = prototype.As<v8::Object>();
        if (host_object_backing(current) != nullptr) {
            return true;
        }
    }
    return true;  // cyclic/unexpected chain: do not assert missing
}

// Named-property interceptor for absent string keys. kNonMasking keeps the
// interceptor out of the path for existing own/prototype properties; returning
// kNo hands the lookup back to V8, so the script still sees undefined and all
// descriptors/prototype/`in` behavior is unchanged.
v8::Intercepted named_property_getter(
    v8::Local<v8::Name> property,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    AccessTrace* trace = access_trace_from_isolate(isolate);
    if (trace == nullptr || !trace->enabled() || !property->IsString()) {
        return v8::Intercepted::kNo;
    }
    HostObject* host = backing_of(info.Holder());
    if (host == nullptr) {
        return v8::Intercepted::kNo;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    // kNonMasking should make this branch unnecessary, but keep the guard so a
    // V8 callback-order change cannot turn an existing property into a missing
    // probe. This is a real-property check, not a Get: it does not run getters.
    if (property_is_present(info.Holder(), context, property)) {
        return v8::Intercepted::kNo;
    }
    v8::String::Utf8Value utf8(isolate, property);
    if (*utf8 == nullptr) {
        return v8::Intercepted::kNo;
    }
    const std::string key(*utf8, static_cast<std::size_t>(utf8.length()));
    trace->record(host, host->trace_object_type(), host->trace_label(), key,
                  AccessOperation::kGetProbe, "missing");
    return v8::Intercepted::kNo;
}

// One generic accessor for every native property: dispatches by the property
// name to the backing object's get_property().
void property_getter(v8::Local<v8::Name> property,
                     const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    HostObject* host = backing_of(info.Holder());
    if (host == nullptr) {
        return;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::String::Utf8Value name(isolate, property);
    const std::string key(*name != nullptr ? *name : "");
    AccessTrace* trace = active_trace(isolate);
    const std::uint64_t seq =
        record_access(trace, host, key, AccessOperation::kGet);
    v8::Local<v8::Value> result = host->get_property(isolate, context, key);
    info.GetReturnValue().Set(result);
    if (seq != 0) {
        try {
            AccessCompletion completion;
            completion.outcome =
                isolate->HasPendingException() ? "threw" : "returned";
            if (completion.outcome == "returned") {
                completion.value_summary = summarize_value(isolate, result);
            }
            trace->complete(seq, std::move(completion));
        } catch (...) {
            // Summaries are best-effort and must never affect JS behavior.
        }
    }
}

// Generic setter for writable properties (M2-8): dispatch to set_property().
void property_setter(v8::Local<v8::Name> property, v8::Local<v8::Value> value,
                     const v8::PropertyCallbackInfo<void>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    HostObject* host = backing_of(info.Holder());
    if (host == nullptr) {
        return;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::String::Utf8Value name(isolate, property);
    const std::string key(*name != nullptr ? *name : "");
    AccessTrace* trace = active_trace(isolate);
    const std::uint64_t seq =
        record_access(trace, host, key, AccessOperation::kSet);
    host->set_property(isolate, context, key, value);
    if (seq != 0) {
        try {
            AccessCompletion completion;
            completion.outcome =
                isolate->HasPendingException() ? "threw" : "returned";
            if (completion.outcome == "returned") {
                completion.value_summary = summarize_value(isolate, value);
            }
            trace->complete(seq, std::move(completion));
        } catch (...) {
            // Summaries are best-effort and must never affect JS behavior.
        }
    }
}

// One generic method trampoline: the method name is carried as the function's
// Data, dispatched to the backing object's call_method().
void method_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    HostObject* host = backing_of(info.This());
    if (host == nullptr) {
        return;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::String::Utf8Value name(isolate, info.Data());
    const std::string key(*name != nullptr ? *name : "");
    AccessTrace* trace = active_trace(isolate);
    const std::uint64_t seq =
        record_access(trace, host, key, AccessOperation::kCall);
    AccessCompletion completion;
    if (seq != 0) {
        try {
            completion.argument_count =
                static_cast<std::uint64_t>(info.Length());
            completion.argument_summaries =
                summarize_arguments(isolate, info);
        } catch (...) {
            // Keep the argument count if only the summaries failed.
        }
    }
    v8::Local<v8::Value> result =
        host->call_method(isolate, context, key, info);
    info.GetReturnValue().Set(result);
    if (seq != 0) {
        try {
            completion.outcome =
                isolate->HasPendingException() ? "threw" : "returned";
            if (completion.outcome == "returned") {
                completion.return_summary = summarize_value(isolate, result);
            }
            trace->complete(seq, std::move(completion));
        } catch (...) {
            // Summaries are best-effort and must never affect JS behavior.
        }
    }
}

}  // namespace

v8::Local<v8::Object> make_host_object(v8::Isolate* isolate,
                                       v8::Local<v8::Context> context,
                                       HostObject* host) {
    v8::Local<v8::ObjectTemplate> tmpl = v8::ObjectTemplate::New(isolate);
    tmpl->SetInternalFieldCount(1);  // slot 0 = HostObject* (aligned pointer)

    // Observe absent string-key reads without masking own/prototype properties.
    // The callback only records when tracing is enabled and otherwise takes the
    // same normal lookup path as an object without an interceptor.
    const auto handler_flags = static_cast<v8::PropertyHandlerFlags>(
        static_cast<int>(v8::PropertyHandlerFlags::kNonMasking) |
        static_cast<int>(v8::PropertyHandlerFlags::kOnlyInterceptStrings));
    tmpl->SetHandler(v8::NamedPropertyHandlerConfiguration(
        &named_property_getter, nullptr, &named_property_query, nullptr,
        nullptr, v8::Local<v8::Value>(), handler_flags));

    // Data properties are getter-computed. Read-only ones use ReadOnly|DontDelete
    // (a JS write is a no-op in sloppy mode / TypeError in strict). Writable ones
    // (host->writable_property_names(), M2-8) additionally get a setter and drop
    // ReadOnly so assignment dispatches to set_property.
    const std::vector<std::string> writable = host->writable_property_names();
    const auto is_writable = [&writable](const std::string& name) {
        return std::find(writable.begin(), writable.end(), name) != writable.end();
    };
    for (const std::string& property : host->property_names()) {
        if (is_writable(property)) {
            tmpl->SetNativeDataProperty(
                v8str(isolate, property), &property_getter, &property_setter,
                v8::Local<v8::Value>(), v8::DontDelete);
        } else {
            tmpl->SetNativeDataProperty(
                v8str(isolate, property), &property_getter, nullptr,
                v8::Local<v8::Value>(),
                static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete));
        }
    }
    for (const std::string& method : host->method_names()) {
        v8::Local<v8::String> mname = v8str(isolate, method);
        v8::Local<v8::FunctionTemplate> fn =
            v8::FunctionTemplate::New(isolate, &method_callback, mname);
        tmpl->Set(mname, fn);
    }

    v8::Local<v8::Object> object = tmpl->NewInstance(context).ToLocalChecked();
    object->SetAlignedPointerInInternalField(0, host, kHostObjectTag);
    return object;
}

void install_host_object(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         HostObject* host) {
    v8::Local<v8::Object> object = make_host_object(isolate, context, host);
    (void)context->Global()->Set(context, v8str(isolate, host->global_name()),
                                 object);
}

HostObject* host_object_backing(v8::Local<v8::Value> value) {
    if (value.IsEmpty() || !value->IsObject()) {
        return nullptr;
    }
    // backing_of() fails safe (nullptr) if the object has no host-object internal
    // field, so a plain JS object / non-host object returns nullptr.
    return backing_of(value.As<v8::Object>());
}

}  // namespace iv8
