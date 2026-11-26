#include "iv8/host_object.h"

#include <algorithm>
#include <limits>
#include <memory>
#include <optional>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "iv8/access_summary.h"
#include "iv8/access_trace.h"
#include "iv8/access_origin.h"
#include "iv8/web_interface.h"
#include "iv8/web_method.h"
#include "browser/window/window_trace_support.h"
#include "browser/internal/trace_hooks.h"

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
    if (self.IsEmpty() || self->InternalFieldCount() != 1) {
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

enum class NamedPropertyLookup { kAbsent, kFound, kException };

NamedPropertyLookup lookup_named_property(
    HostObject* host, v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Name> property, std::string& member,
    v8::Local<v8::Value>& result) {
    if (host == nullptr || !host->has_named_properties() || !property->IsString()) {
        return NamedPropertyLookup::kAbsent;
    }
    auto runtime_origin = access_runtime_scope(isolate);
    v8::TryCatch caught(isolate);
    v8::String::Utf8Value name(isolate, property);
    bool found = false;
    if (*name != nullptr) {
        member.assign(*name, static_cast<std::size_t>(name.length()));
        found = host->named_property(isolate, context, member).ToLocal(&result);
    }
    if (caught.HasCaught()) {
        caught.ReThrow();
        return NamedPropertyLookup::kException;
    }
    return found ? NamedPropertyLookup::kFound : NamedPropertyLookup::kAbsent;
}

// Legacy form names are configurable, non-enumerable, read-only own properties.
// Other hosts keep queries on their ordinary lookup path; these queries must
// not be reported as missing-property reads by the access trace.
v8::Intercepted named_property_query(
    v8::Local<v8::Name> property,
    const v8::PropertyCallbackInfo<v8::Integer>& info) {
    std::string member;
    v8::Local<v8::Value> result;
    const auto lookup = lookup_named_property(backing_of(info.Holder()),
        info.GetIsolate(), info.GetIsolate()->GetCurrentContext(), property, member, result);
    if (lookup == NamedPropertyLookup::kAbsent) return v8::Intercepted::kNo;
    if (lookup == NamedPropertyLookup::kFound) {
        info.GetReturnValue().Set(static_cast<int>(v8::ReadOnly | v8::DontEnum));
    }
    return v8::Intercepted::kYes;
}

void record_operation(v8::Isolate* isolate, v8::Local<v8::Object> holder,
                      v8::Local<v8::Name> property,
                      AccessOperation operation) {
    AccessTrace* trace = active_trace(isolate);
    if (trace == nullptr || property.IsEmpty() || !property->IsString()) {
        return;
    }
    HostObject* host = backing_of(holder);
    if (host == nullptr) {
        return;
    }
    v8::String::Utf8Value utf8(isolate, property);
    if (*utf8 == nullptr) {
        return;
    }
    const std::string key(*utf8, static_cast<std::size_t>(utf8.length()));
    trace->record(host, host->trace_object_type(), host->trace_label(), key,
                  operation);
}

v8::Intercepted named_property_deleter(
    v8::Local<v8::Name> property,
    const v8::PropertyCallbackInfo<v8::Boolean>& info) {
    record_operation(info.GetIsolate(), info.Holder(), property,
                     AccessOperation::kDelete);
    return v8::Intercepted::kNo;
}

v8::Intercepted named_property_definer(
    v8::Local<v8::Name> property, const v8::PropertyDescriptor&,
    const v8::PropertyCallbackInfo<v8::Boolean>& info) {
    record_operation(info.GetIsolate(), info.Holder(), property,
                     AccessOperation::kDefineProperty);
    auto isolate = info.GetIsolate();
    auto runtime_origin = access_runtime_scope(isolate);
    auto* host = backing_of(info.Holder());
    std::string member;
    v8::Local<v8::Value> result;
    const auto lookup = lookup_named_property(host, isolate,
        isolate->GetCurrentContext(), property, member, result);
    if (lookup == NamedPropertyLookup::kAbsent) return v8::Intercepted::kNo;
    if (lookup == NamedPropertyLookup::kFound) {
        const auto message = "Failed to set a named property '" + member +
            "' on '" + host->prototype_key() + "': Named property setter is not supported.";
        v8::Local<v8::String> exception_message;
        if (v8::String::NewFromUtf8(isolate, message.data(), v8::NewStringType::kNormal,
                static_cast<int>(message.size())).ToLocal(&exception_message)) {
            isolate->ThrowException(v8::Exception::TypeError(exception_message));
        }
    }
    return v8::Intercepted::kYes;
}

void named_property_enumerator(
    const v8::PropertyCallbackInfo<v8::Array>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    AccessTrace* trace = active_trace(isolate);
    if (trace != nullptr) {
        HostObject* host = backing_of(info.Holder());
        if (host != nullptr) {
            trace->record(host, host->trace_object_type(), host->trace_label(),
                          std::string(), AccessOperation::kOwnKeys);
        }
    }
    auto runtime_origin = access_runtime_scope(isolate);
    auto* host = backing_of(info.Holder());
    const auto names = host != nullptr ? host->named_property_names() : std::vector<std::string>{};
    if (names.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
        isolate->ThrowException(v8::Exception::RangeError(
            v8::String::NewFromUtf8Literal(isolate, "Too many named properties")));
        return;
    }
    auto result = v8::Array::New(isolate, static_cast<int>(names.size()));
    for (std::uint32_t index = 0; index < names.size(); ++index) {
        v8::Local<v8::String> name;
        if (!v8::String::NewFromUtf8(isolate, names[index].data(), v8::NewStringType::kNormal,
                static_cast<int>(names[index].size())).ToLocal(&name) ||
            !result->CreateDataProperty(isolate->GetCurrentContext(), index, name).FromMaybe(false)) return;
    }
    info.GetReturnValue().Set(result);
}

// Walk real own-property descriptors without invoking getters or interceptors.
// A Proxy or a host-object prototype is conservatively treated as present: we
// suppress a probe rather than report a false missing property.
bool property_is_present(v8::Isolate* isolate, v8::Local<v8::Object> holder,
                         v8::Local<v8::Context> context,
                         v8::Local<v8::Name> property) {
    auto runtime_origin = access_runtime_scope(isolate);
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

bool is_interface_method(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         v8::Local<v8::Object> object, HostObject* host,
                         const std::string& member, v8::Local<v8::Value>& result) {
    auto runtime_origin = access_runtime_scope(isolate);
    if (host->prototype_key().empty()) return false;
    auto interface = web_member_interface(host->prototype_key(), member, true);
    auto prototype = web_interface_prototype(isolate, context, interface);
    if (prototype.IsEmpty()) return false;
    v8::Local<v8::Value> canonical;
    if (!prototype->GetPrivate(context, v8::Private::ForApi(isolate,
            v8str(isolate, "iv8.webidl.method." + member))).ToLocal(&canonical) ||
        !canonical->IsFunction()) return false;
    auto key = v8str(isolate, member);
    for (int depth = 0; depth < 1000 && !object.IsEmpty() && !object->IsProxy(); ++depth) {
        if (object->HasRealNamedProperty(context, key).FromMaybe(false)) {
            v8::Local<v8::Value> descriptor;
            v8::Local<v8::Value> value;
            if (!object->GetOwnPropertyDescriptor(context, key).ToLocal(&descriptor) ||
                !descriptor->IsObject() ||
                !descriptor.As<v8::Object>()->HasOwnProperty(context, v8str(isolate, "value")).FromMaybe(false) ||
                !descriptor.As<v8::Object>()->Get(context, v8str(isolate, "value")).ToLocal(&value))
                return false;
            if (value != canonical) return false;
            result = value;
            return true;
        }
        auto parent = object->GetPrototype();
        if (!parent->IsObject()) return false;
        object = parent.As<v8::Object>();
    }
    return false;
}

// Form names can mask inherited IDL members. Other hosts retain the existing
// method-reference and missing-property trace paths without changing lookup.
v8::Intercepted named_property_getter(
    v8::Local<v8::Name> property,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* named_host = backing_of(info.Holder());
    std::string named_member;
    v8::Local<v8::Value> named_value;
    const auto lookup = lookup_named_property(named_host, isolate,
        isolate->GetCurrentContext(), property, named_member, named_value);
    if (lookup == NamedPropertyLookup::kException) return v8::Intercepted::kYes;
    if (lookup == NamedPropertyLookup::kFound) {
        auto* trace = active_trace(isolate);
        const auto sequence = record_access(trace, named_host, named_member, AccessOperation::kGet);
        if (sequence != 0) {
            try {
                AccessCompletion completion;
                completion.outcome = "returned";
                completion.value_summary = access_value_summary(isolate, named_value);
                trace->complete(sequence, std::move(completion));
            } catch (...) {
                // Diagnostics must not change the named property's result.
            }
        }
        info.GetReturnValue().Set(named_value);
        return v8::Intercepted::kYes;
    }
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
    v8::String::Utf8Value utf8(isolate, property);
    if (*utf8 == nullptr) return v8::Intercepted::kNo;
    const std::string key(*utf8, static_cast<std::size_t>(utf8.length()));
    if (property_is_present(isolate, info.Holder(), context, property)) {
        v8::Local<v8::Value> method;
        if (is_interface_method(isolate, context, info.Holder(), host, key, method)) {
            record_access(trace, host, key, AccessOperation::kGetMethod);
            info.GetReturnValue().Set(method);
            return v8::Intercepted::kYes;
        }
        return v8::Intercepted::kNo;
    }
    trace->record(host, host->trace_object_type(), host->trace_label(), key,
                  AccessOperation::kGetProbe, "missing");
    return v8::Intercepted::kNo;
}

v8::Intercepted named_property_setter(
    v8::Local<v8::Name> property, v8::Local<v8::Value> value,
    const v8::PropertyCallbackInfo<v8::Boolean>& info) {
    auto isolate = info.GetIsolate();
    auto* host = backing_of(info.Holder());
    auto* trace = active_trace(isolate);
    auto context = isolate->GetCurrentContext();
    std::string named_member;
    v8::Local<v8::Value> named_value;
    const auto lookup = lookup_named_property(host, isolate, context, property,
        named_member, named_value);
    if (lookup == NamedPropertyLookup::kException) return v8::Intercepted::kYes;
    if (lookup == NamedPropertyLookup::kFound) {
        auto prototype_value = info.Holder()->GetPrototype();
        for (int depth = 0; depth < 1000 && prototype_value->IsObject(); ++depth) {
            auto prototype = prototype_value.As<v8::Object>();
            v8::Local<v8::Value> descriptor;
            if (!prototype->GetOwnPropertyDescriptor(context, property).ToLocal(&descriptor)) return v8::Intercepted::kYes;
            if (descriptor->IsObject()) {
                auto descriptor_object = descriptor.As<v8::Object>();
                const auto setter_key = v8::String::NewFromUtf8Literal(isolate, "set");
                const auto has_setter = descriptor_object->HasOwnProperty(context, setter_key);
                if (has_setter.IsNothing()) return v8::Intercepted::kYes;
                if (has_setter.FromMaybe(false)) {
                    v8::Local<v8::Value> setter;
                    if (!descriptor_object->Get(context, setter_key).ToLocal(&setter)) return v8::Intercepted::kYes;
                    if (setter->IsFunction()) {
                        v8::Local<v8::Value> arguments[] = {value};
                        v8::Local<v8::Value> ignored;
                        if (!access_user_operation(isolate, [&] {
                                return setter.As<v8::Function>()->Call(context, info.Holder(), 1, arguments);
                            }).ToLocal(&ignored)) return v8::Intercepted::kYes;
                        info.GetReturnValue().Set(true);
                        return v8::Intercepted::kYes;
                    }
                }
                break;
            }
            prototype_value = prototype->GetPrototype();
        }
        info.GetReturnValue().Set(false);
        return v8::Intercepted::kYes;
    }
    if (host == nullptr || trace == nullptr || !property->IsString()) return v8::Intercepted::kNo;
    v8::String::Utf8Value text(isolate, property);
    const std::string member(*text != nullptr ? *text : "");
    v8::Local<v8::Value> original_method;
    if (is_interface_method(isolate, isolate->GetCurrentContext(), info.Holder(), host, member, original_method)) {
        auto seq = record_access(trace, host, member, AccessOperation::kSet);
        AccessCompletion completion;
        completion.outcome = "returned";
        completion.value_summary = access_value_summary(isolate, value);
        trace->complete(seq, std::move(completion));
    }
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
    auto runtime_origin = access_runtime_scope(isolate);
    std::optional<v8::TryCatch> try_catch;
    if (seq != 0) {
        try {
            try_catch.emplace(isolate);
        } catch (...) {
            try_catch.reset();
        }
    }
    v8::Local<v8::Value> result = host->get_property(isolate, context, key);
    const bool caught = try_catch && try_catch->HasCaught();
    if (seq != 0) {
        try {
            AccessCompletion completion;
            if (caught) {
                completion.outcome = "threw";
                access_capture_exception(isolate, context, *try_catch,
                                         completion);
            } else {
                completion.outcome = "returned";
                completion.value_summary = access_value_summary(isolate, result);
            }
            trace->complete(seq, std::move(completion));
        } catch (...) {
            // Summaries are best-effort and must never affect JS behavior.
        }
    }
    if (caught) {
        try_catch->ReThrow();
        return;
    }
    info.GetReturnValue().Set(result);
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
    auto runtime_origin = access_runtime_scope(isolate);
    std::optional<v8::TryCatch> try_catch;
    if (seq != 0) {
        try {
            try_catch.emplace(isolate);
        } catch (...) {
            try_catch.reset();
        }
    }
    host->set_property(isolate, context, key, value);
    const bool caught = try_catch && try_catch->HasCaught();
    if (seq != 0) {
        try {
            AccessCompletion completion;
            if (caught) {
                completion.outcome = "threw";
                access_capture_exception(isolate, context, *try_catch,
                                         completion);
            } else {
                completion.outcome = "returned";
                completion.value_summary = access_value_summary(isolate, value);
            }
            trace->complete(seq, std::move(completion));
        } catch (...) {
            // Summaries are best-effort and must never affect JS behavior.
        }
    }
    if (caught) {
        try_catch->ReThrow();
    }
}

// Function accessors expose real Web IDL descriptors on a shared prototype.
// Callback data contains the member and expected interface; no native pointer is
// captured, so copying a getter cannot outlive or bypass its receiver's brand.
HostObject* method_receiver(v8::Isolate* isolate, v8::Local<v8::Object> receiver) {
    if (auto* host = backing_of(receiver)) return host;
    auto context = isolate->GetCurrentContext();
    if (receiver != context->Global()) return nullptr;
    v8::Local<v8::Value> value;
    if (!receiver->GetPrivate(context, v8::Private::ForApi(isolate,
            v8str(isolate, "iv8.webidl.window.host"))).ToLocal(&value) ||
        !value->IsExternal()) return nullptr;
    return static_cast<HostObject*>(value.As<v8::External>()->Value(
        v8::kExternalPointerTypeTagDefault));
}

std::uint64_t record_rejected_access(v8::Isolate* isolate,
    v8::Local<v8::Array> data, const std::string& interface,
    const std::string& key, AccessOperation operation, HostObject* legacy_owner = nullptr) {
    auto* trace = active_trace(isolate);
    if (trace == nullptr || !trace->user_entry()) return 0;
    auto context = isolate->GetCurrentContext();
    v8::TryCatch diagnostic(isolate);
    const void* identity = nullptr;
    std::string type;
    std::string label;
    try {
        {
            auto runtime_origin = access_runtime_scope(isolate);
            HostObject* owner = legacy_owner;
            v8::Local<v8::Value> target;
            if (owner == nullptr && data->Length() > 2 && data->Get(context, 2).ToLocal(&target)) {
                if (target->IsExternal()) {
                    owner = static_cast<HostObject*>(target.As<v8::External>()->Value(
                        v8::kExternalPointerTypeTagDefault));
                } else if (target->IsObject()) {
                    owner = backing_of(target.As<v8::Object>());
                    if (owner == nullptr) {
                        identity = browser::native_api_identity(isolate, target.As<v8::Object>());
                        type = "host_object";
                        label = interface + ".prototype";
                    }
                }
            }
            if (owner != nullptr) {
                identity = owner;
                type = owner->trace_object_type();
                label = owner->trace_label();
            }
        }
        if (diagnostic.HasCaught()) {
            if (!diagnostic.CanContinue()) diagnostic.ReThrow();
            else diagnostic.Reset();
            return 0;
        }
        return trace->record(identity, type, label, key, operation);
    } catch (...) {
        // Failed diagnostics must not replace the original brand error.
        if (diagnostic.HasCaught()) {
            if (!diagnostic.CanContinue()) diagnostic.ReThrow();
            else diagnostic.Reset();
        }
        return 0;
    }
}

HostObject* idl_receiver(const v8::FunctionCallbackInfo<v8::Value>& info,
                          std::string& key, std::string& interface_name) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> member;
    v8::Local<v8::Value> interface;
    if (!data->Get(context, 0).ToLocal(&member) ||
        !data->Get(context, 1).ToLocal(&interface)) return nullptr;
    v8::String::Utf8Value member_text(isolate, member);
    v8::String::Utf8Value interface_text(isolate, interface);
    key = *member_text != nullptr ? *member_text : "";
    interface_name = *interface_text != nullptr ? *interface_text : "";
    HostObject* host = method_receiver(isolate, info.This());
    if (host == nullptr || *interface_text == nullptr ||
        !web_interface_inherits(host->prototype_key(), *interface_text)) {
        return nullptr;
    }
    return host;
}

void idl_getter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    std::string key;
    std::string interface;
    HostObject* host = idl_receiver(info, key, interface);
    if (host == nullptr) {
        const auto sequence = record_rejected_access(isolate, info.Data().As<v8::Array>(),
            interface, key, AccessOperation::kGet);
        AccessFunctionPropertyCompletion completion(isolate, sequence, info, AccessOperation::kGet);
        if (!isolate->IsExecutionTerminating()) {
            isolate->ThrowException(v8::Exception::TypeError(v8str(isolate, "Illegal invocation")));
        }
        return;
    }
    auto context = isolate->GetCurrentContext();
    AccessTrace* trace = active_trace(isolate);
    const auto seq = record_access(trace, host, key, AccessOperation::kGet);
    auto runtime_origin = access_runtime_scope(isolate);
    v8::TryCatch caught(isolate);
    auto result = host->get_property(isolate, context, key);
    if (seq != 0) {
        try {
            AccessCompletion completion;
            completion.outcome = caught.HasCaught() ? "threw" : "returned";
            if (caught.HasCaught()) {
                access_capture_exception(isolate, context, caught, completion);
            } else {
                completion.value_summary = access_value_summary(isolate, result);
            }
            trace->complete(seq, std::move(completion));
        } catch (...) {}
    }
    if (caught.HasCaught()) { caught.ReThrow(); return; }
    info.GetReturnValue().Set(result);
}

void idl_setter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    std::string key;
    std::string interface;
    HostObject* host = idl_receiver(info, key, interface);
    if (host == nullptr) {
        const auto sequence = record_rejected_access(isolate, info.Data().As<v8::Array>(),
            interface, key, AccessOperation::kSet);
        AccessFunctionPropertyCompletion completion(isolate, sequence, info, AccessOperation::kSet);
        if (!isolate->IsExecutionTerminating()) {
            isolate->ThrowException(v8::Exception::TypeError(v8str(isolate, "Illegal invocation")));
        }
        return;
    }
    auto context = isolate->GetCurrentContext();
    v8::Local<v8::Value> value = info.Length() ? info[0] :
        v8::Local<v8::Value>(v8::Undefined(isolate));
    AccessTrace* trace = active_trace(isolate);
    const auto seq = record_access(trace, host, key, AccessOperation::kSet);
    auto runtime_origin = access_runtime_scope(isolate);
    v8::TryCatch caught(isolate);
    auto native_value = value;
    if (key == "nodeValue" || key == "textContent") {
        if (native_value->IsNullOrUndefined()) {
            native_value = v8str(isolate, "");
        } else {
            v8::Local<v8::String> text;
            if (access_user_operation(isolate, [&] {
                    return native_value->ToString(context);
                }).ToLocal(&text)) native_value = text;
        }
    }
    if (!caught.HasCaught()) host->set_property(isolate, context, key, native_value);
    if (seq != 0) {
        try {
            AccessCompletion completion;
            completion.outcome = caught.HasCaught() ? "threw" : "returned";
            if (caught.HasCaught()) {
                access_capture_exception(isolate, context, caught, completion);
            } else {
                completion.value_summary = access_value_summary(isolate, value);
            }
            trace->complete(seq, std::move(completion));
        } catch (...) {}
    }
    if (caught.HasCaught()) caught.ReThrow();
}

// One generic method trampoline: the method name is carried as the function's
// Data, dispatched to the backing object's call_method().
void method_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> member;
    v8::Local<v8::Value> owner_value;
    if (!data->Get(context, 0).ToLocal(&member) ||
        !data->Get(context, 1).ToLocal(&owner_value)) return;
    v8::String::Utf8Value name(isolate, member);
    const std::string key(*name != nullptr ? *name : "");
    HostObject* host = method_receiver(isolate, info.This());
    bool compatible = false;
    std::string interface_name;
    HostObject* legacy_owner = nullptr;
    if (owner_value->IsString()) {
        v8::String::Utf8Value interface(isolate, owner_value);
        interface_name = *interface != nullptr ? *interface : "";
        compatible = host != nullptr && *interface != nullptr &&
            web_interface_inherits(host->prototype_key(), *interface);
    } else {
        auto* owner = static_cast<HostObject*>(owner_value.As<v8::External>()->Value(
            v8::kExternalPointerTypeTagDefault));
        legacy_owner = owner;
        if (owner->trace_object_type() == "console") host = owner;
        compatible = host != nullptr && owner->trace_object_type() == host->trace_object_type();
    }
    if (!compatible) {
        const auto sequence = record_rejected_access(isolate, data, interface_name,
            key, AccessOperation::kCall, legacy_owner);
        AccessCallCompletion completion(isolate, sequence, info);
        if (!isolate->IsExecutionTerminating()) {
            if (interface_name == "DeprecatedStorageQuota") {
                // Preserve the former bootstrap's call-attempt watch channel,
                // including receiver rejection, for both native alias selectors.
                browser::watch_record(isolate, "DeprecatedStorageQuota", key);
                browser::watch_record(isolate, "navigator.webkitTemporaryStorage", key);
                browser::watch_record(isolate, "navigator.webkitPersistentStorage", key);
            }
            // These selected Promise-returning operations report receiver
            // failures as rejections. Trace only their synchronous Promise return.
            const bool navigator_promise = interface_name == "Navigator" &&
                (key == "getInstalledRelatedApps" || key == "share" ||
                 key == "setAppBadge" || key == "clearAppBadge" ||
                 key == "requestMIDIAccess" || key == "requestMediaKeySystemAccess");
            const bool eme_promise =
                (interface_name == "MediaKeySystemAccess" && key == "createMediaKeys") ||
                (interface_name == "MediaKeys" &&
                    (key == "setServerCertificate" || key == "getStatusForPolicy")) ||
                (interface_name == "MediaKeySession" &&
                    (key == "close" || key == "generateRequest" || key == "load" ||
                     key == "remove" || key == "update"));
            const bool bom05_promise =
                (interface_name == "Clipboard" &&
                    (key == "read" || key == "readText" ||
                     key == "write" || key == "writeText")) ||
                (interface_name == "ClipboardItem" && key == "getType") ||
                (interface_name == "CredentialsContainer" &&
                    (key == "create" || key == "get" ||
                     key == "preventSilentAccess" || key == "store")) ||
                (interface_name == "LockManager" &&
                    (key == "query" || key == "request")) ||
                (interface_name == "WakeLock" && key == "request") ||
                (interface_name == "WakeLockSentinel" && key == "release") ||
                (interface_name == "Keyboard" &&
                    (key == "getLayoutMap" || key == "lock")) ||
                (interface_name == "MediaCapabilities" &&
                    (key == "decodingInfo" || key == "encodingInfo"));
            const bool bom06_promise =
                (interface_name == "Bluetooth" &&
                    (key == "getAvailability" || key == "requestDevice")) ||
                (interface_name == "HID" &&
                    (key == "getDevices" || key == "requestDevice")) ||
                (interface_name == "Serial" &&
                    (key == "getPorts" || key == "requestPort")) ||
                (interface_name == "USB" &&
                    (key == "getDevices" || key == "requestDevice")) ||
                (interface_name == "XRSystem" &&
                    (key == "isSessionSupported" || key == "requestSession")) ||
                (interface_name == "GPU" && key == "requestAdapter") ||
                (interface_name == "PresentationRequest" &&
                    (key == "getAvailability" || key == "reconnect" || key == "start"));
            const bool bom07_promise =
                (interface_name == "MediaDevices" &&
                    (key == "enumerateDevices" || key == "getUserMedia" ||
                     key == "getDisplayMedia")) ||
                (interface_name == "ServiceWorkerContainer" &&
                    (key == "getRegistration" || key == "getRegistrations" ||
                     key == "register")) ||
                (interface_name == "ServiceWorkerRegistration" &&
                    (key == "unregister" || key == "update" ||
                     key == "getNotifications" || key == "showNotification")) ||
                (interface_name == "StorageBucketManager" &&
                    (key == "delete" || key == "keys" || key == "open")) ||
                (interface_name == "StorageBucket" &&
                    (key == "estimate" || key == "expires" || key == "getDirectory" ||
                     key == "persisted" || key == "setExpires" || key == "persist")) ||
                (interface_name == "NavigatorLogin" && key == "setStatus") ||
                (interface_name == "NavigatorManagedData" &&
                    key == "getManagedConfiguration") ||
                (interface_name == "Ink" && key == "requestPresenter");
            const bool bom08_promise =
                interface_name == "Observable" &&
                (key == "every" || key == "find" || key == "first" ||
                 key == "forEach" || key == "last" || key == "reduce" ||
                 key == "some" || key == "toArray");
            if ((interface_name == "ScreenOrientation" && key == "lock") ||
                navigator_promise || eme_promise || bom05_promise ||
                bom06_promise || bom07_promise || bom08_promise) {
                std::string watch_owner = interface_name;
                if (navigator_promise) watch_owner = "navigator";
                else if (interface_name == "ScreenOrientation") watch_owner = "screen.orientation";
                else if (interface_name == "Clipboard") watch_owner = "navigator.clipboard";
                else if (interface_name == "CredentialsContainer") watch_owner = "navigator.credentials";
                else if (interface_name == "LockManager") watch_owner = "navigator.locks";
                else if (interface_name == "WakeLock") watch_owner = "navigator.wakeLock";
                else if (interface_name == "Keyboard") watch_owner = "navigator.keyboard";
                else if (interface_name == "MediaCapabilities") watch_owner = "navigator.mediaCapabilities";
                else if (interface_name == "Bluetooth") watch_owner = "navigator.bluetooth";
                else if (interface_name == "HID") watch_owner = "navigator.hid";
                else if (interface_name == "Serial") watch_owner = "navigator.serial";
                else if (interface_name == "USB") watch_owner = "navigator.usb";
                else if (interface_name == "XRSystem") watch_owner = "navigator.xr";
                else if (interface_name == "GPU") watch_owner = "navigator.gpu";
                else if (interface_name == "MediaDevices") watch_owner = "navigator.mediaDevices";
                else if (interface_name == "ServiceWorkerContainer") watch_owner = "navigator.serviceWorker";
                else if (interface_name == "StorageBucketManager") watch_owner = "navigator.storageBuckets";
                else if (interface_name == "NavigatorLogin") watch_owner = "navigator.login";
                else if (interface_name == "NavigatorManagedData") watch_owner = "navigator.managed";
                else if (interface_name == "Ink") watch_owner = "navigator.ink";
                browser::watch_record(isolate, watch_owner, key);
                v8::Local<v8::Promise::Resolver> resolver;
                if (!v8::Promise::Resolver::New(context).ToLocal(&resolver) ||
                    !resolver->Reject(context, v8::Exception::TypeError(v8str(isolate,
                        "Failed to execute '" + key + "' on '" + interface_name + "': Illegal invocation")))
                        .FromMaybe(false)) return;
                info.GetReturnValue().Set(resolver->GetPromise());
                return;
            }
            isolate->ThrowException(v8::Exception::TypeError(v8str(isolate, "Illegal invocation")));
        }
        return;
    }
    AccessTrace* trace = active_trace(isolate);
    std::uint64_t seq = 0;
    if (trace != nullptr) {
        // A method call normally follows its property read; fold that read
        // into one call event. If other trace events intervened, keep both.
        seq = trace->promote_last_method_read(host, key);
        if (seq == 0) {
            seq = record_access(trace, host, key, AccessOperation::kCall);
        }
    }
    auto runtime_origin = access_runtime_scope(isolate);
    AccessCompletion completion;
    std::optional<v8::TryCatch> try_catch;
    if (seq != 0) {
        try {
            try_catch.emplace(isolate);
            completion.argument_count =
                static_cast<std::uint64_t>(info.Length());
            completion.argument_summaries =
                access_argument_summaries(isolate, info);
        } catch (...) {
            // Keep the arguments if only the TryCatch allocation failed.
        }
    }
    v8::Local<v8::Value> result =
        host->call_method(isolate, context, key, info);
    const bool caught = try_catch && try_catch->HasCaught();
    if (seq != 0) {
        try {
            if (caught) {
                completion.outcome = "threw";
                access_capture_exception(isolate, context, *try_catch,
                                         completion);
            } else {
                completion.outcome = "returned";
                completion.return_summary =
                    access_value_summary(isolate, result);
            }
            trace->complete(seq, std::move(completion));
        } catch (...) {
            // Summaries are best-effort and must never affect JS behavior.
        }
    }
    if (caught) {
        try_catch->ReThrow();
        return;
    }
    info.GetReturnValue().Set(result);
}

// Step 6b: method properties are accessors so a bare method reference is
// observable. The function is created lazily and cached in a V8 private slot on
// the holder, preserving identity and writable/overridable behavior. A normal
// `receiver.method(...)` read is folded into the following call event by
// method_callback when no other trace event intervenes.
void method_getter(v8::Local<v8::Name> property,
                   const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Object> holder = info.Holder();
    HostObject* host = backing_of(holder);
    if (host == nullptr || !property->IsString()) {
        return;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::String> method_name = property.As<v8::String>();
    v8::Local<v8::Value> result;
    if (!info.Data().IsEmpty() && info.Data()->IsPrivate()) {
        v8::Local<v8::Private> slot = info.Data().As<v8::Private>();
        v8::Maybe<bool> has = holder->HasPrivate(context, slot);
        if (has.IsNothing()) {
            return;
        }
        if (has.FromJust()) {
            if (!holder->GetPrivate(context, slot).ToLocal(&result)) {
                return;
            }
        } else {
            v8::Local<v8::Function> fn;
            auto data = v8::Array::New(isolate, 2);
            (void)data->Set(context, 0, method_name);
            (void)data->Set(context, 1, v8::External::New(isolate, host,
                v8::kExternalPointerTypeTagDefault));
            v8::String::Utf8Value name(isolate, method_name);
            const std::string member(*name != nullptr ? *name : "");
            if (!make_web_method(isolate, context, member,
                    web_method_length(host->trace_object_type(), member),
                    &method_callback, data).ToLocal(&fn)) {
                return;
            }
            (void)holder->SetPrivate(context, slot, fn);
            result = fn;
        }
    } else {
        v8::Local<v8::Function> fn;
        auto data = v8::Array::New(isolate, 2);
        (void)data->Set(context, 0, method_name);
        (void)data->Set(context, 1, v8::External::New(isolate, host,
            v8::kExternalPointerTypeTagDefault));
        v8::String::Utf8Value name(isolate, method_name);
        const std::string member(*name != nullptr ? *name : "");
        if (!make_web_method(isolate, context, member,
                web_method_length(host->trace_object_type(), member),
                &method_callback, data).ToLocal(&fn)) {
            return;
        }
        result = fn;
    }
    // A method-named property whose current value is not a function (script
    // override) is an ordinary property read, not a method reference.
    if (result->IsFunction()) {
        AccessTrace* trace = active_trace(isolate);
        if (trace != nullptr) {
            v8::String::Utf8Value utf8(isolate, method_name);
            const std::string key(*utf8 != nullptr ? *utf8 : "");
            record_access(trace, host, key, AccessOperation::kGetMethod);
        }
    }
    info.GetReturnValue().Set(result);
}

void method_setter(v8::Local<v8::Name> property, v8::Local<v8::Value> value,
                   const v8::PropertyCallbackInfo<void>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Object> holder = info.Holder();
    HostObject* host = backing_of(holder);
    if (host == nullptr || !property->IsString()) {
        return;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::String::Utf8Value utf8(isolate, property);
    const std::string key(*utf8 != nullptr ? *utf8 : "");
    AccessTrace* trace = active_trace(isolate);
    const std::uint64_t seq =
        record_access(trace, host, key, AccessOperation::kSet);
    auto runtime_origin = access_runtime_scope(isolate);
    std::optional<v8::TryCatch> try_catch;
    if (seq != 0) {
        try {
            try_catch.emplace(isolate);
        } catch (...) {
            try_catch.reset();
        }
    }
    if (!info.Data().IsEmpty() && info.Data()->IsPrivate()) {
        (void)holder->SetPrivate(context, info.Data().As<v8::Private>(), value);
    }
    const bool caught = try_catch && try_catch->HasCaught();
    if (seq != 0) {
        try {
            AccessCompletion completion;
            if (caught) {
                completion.outcome = "threw";
                access_capture_exception(isolate, context, *try_catch,
                                         completion);
            } else {
                completion.outcome = "returned";
                completion.value_summary = access_value_summary(isolate, value);
            }
            trace->complete(seq, std::move(completion));
        } catch (...) {
            // Summaries are best-effort and must never affect JS behavior.
        }
    }
    if (caught) {
        try_catch->ReThrow();
    }
}

v8::Local<v8::Array> member_data(v8::Isolate* isolate, v8::Local<v8::Context> context,
    const std::string& member, const std::string& interface, v8::Local<v8::Value> owner) {
    auto data = v8::Array::New(isolate, 3);
    if (!data->CreateDataProperty(context, 0, v8str(isolate, member)).FromMaybe(false) ||
        !data->CreateDataProperty(context, 1, v8str(isolate, interface)).FromMaybe(false) ||
        !data->CreateDataProperty(context, 2, owner).FromMaybe(false)) return {};
    return data;
}

bool define_idl_attribute(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Object> target, const std::string& member,
                          const std::string& interface, bool writable, bool configurable) {
    auto* owner_host = backing_of(target);
    // An own accessor may outlive its JS receiver. Its diagnostic owner is a
    // generation-owned native host, not a captured receiver object.
    v8::Local<v8::Value> owner = owner_host != nullptr
        ? v8::Local<v8::Value>(v8::External::New(isolate, owner_host, v8::kExternalPointerTypeTagDefault))
        : v8::Local<v8::Value>(target);
    auto data = member_data(isolate, context, member, interface, owner);
    if (data.IsEmpty()) return false;
    v8::Local<v8::Function> getter;
    v8::Local<v8::Function> setter;
    if (!make_web_method(isolate, context, "get " + member, 0, &idl_getter, data)
            .ToLocal(&getter)) return false;
    if (writable && !make_web_method(isolate, context, "set " + member, 1, &idl_setter, data)
            .ToLocal(&setter)) return false;
    v8::PropertyDescriptor descriptor(getter, setter.IsEmpty()
        ? v8::Local<v8::Value>(v8::Undefined(isolate)) : v8::Local<v8::Value>(setter));
    descriptor.set_enumerable(true);
    descriptor.set_configurable(configurable);
    return target->DefineProperty(context, v8str(isolate, member), descriptor).FromMaybe(false);
}

}  // namespace

void set_window_host(v8::Isolate* isolate, v8::Local<v8::Context> context, HostObject* host) {
    (void)context->Global()->SetPrivate(context, v8::Private::ForApi(isolate,
        v8str(isolate, "iv8.webidl.window.host")), v8::External::New(isolate, host, v8::kExternalPointerTypeTagDefault));
}

void install_host_interface(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             HostObject* host) {
    const auto name = host->prototype_key();
    if (name.empty()) return;
    const auto writable = host->writable_property_names();
    if (name != "Location") {
        for (const auto& member : host->property_names()) {
            if (member == "location" && web_interface_inherits(name, "Document")) continue;
            const auto interface = web_member_interface(name, member, false);
            auto prototype = web_interface_prototype(isolate, context, interface);
            if (prototype.IsEmpty()) return;
            auto registered = v8::Private::ForApi(isolate, v8str(isolate, "iv8.webidl.attribute." + member));
            if (prototype->HasPrivate(context, registered).FromMaybe(true)) continue;
            if (!define_idl_attribute(isolate, context, prototype, member, interface,
                    std::find(writable.begin(), writable.end(), member) != writable.end(), true)) return;
            (void)prototype->SetPrivate(context, registered, v8::True(isolate));
        }
    }
    for (const auto& member : host->method_names()) {
        const auto interface = web_member_interface(name, member, true);
        auto prototype = web_interface_prototype(isolate, context, interface);
        if (prototype.IsEmpty()) return;
        auto registered = v8::Private::ForApi(isolate, v8str(isolate, "iv8.webidl.method." + member));
        if (prototype->HasPrivate(context, registered).FromMaybe(true)) continue;
        // Location exposes own methods. Its owner is diagnostic only; the
        // shared callback still validates the actual invocation receiver.
        v8::Local<v8::Value> owner = name == "Location"
            ? v8::Local<v8::Value>(v8::External::New(isolate, host, v8::kExternalPointerTypeTagDefault))
            : v8::Local<v8::Value>(prototype);
        auto data = member_data(isolate, context, member, interface, owner);
        v8::Local<v8::Function> function;
        if (data.IsEmpty() || !make_web_method(isolate, context, member,
                web_method_length(host->trace_object_type(), member), &method_callback, data)
                .ToLocal(&function)) return;
        (void)prototype->SetPrivate(context, registered, function);
        if (name != "Location") {
            (void)browser::install_traced_api_data_property(isolate, context, prototype,
                member, function, browser::native_api_identity(isolate, prototype),
                "host_object", interface + ".prototype");
        }
    }
}

v8::Local<v8::Object> make_host_object(v8::Isolate* isolate,
                                       v8::Local<v8::Context> context,
                                       HostObject* host) {
    v8::Local<v8::ObjectTemplate> tmpl = v8::ObjectTemplate::New(isolate);
    tmpl->SetInternalFieldCount(1);  // slot 0 = HostObject* (aligned pointer)

    // Observe absent string-key reads without masking own/prototype properties.
    // The callback only records when tracing is enabled and otherwise takes the
    // same normal lookup path as an object without an interceptor.
    // Step 6c: keep the interceptor active for existing string properties too,
    // so delete, enumeration, and define operations can be observed. Descriptor
    // and HasProperty queries are intentionally not observed here: V8 invokes
    // the named descriptor callback for some internal property bookkeeping
    // (for example, adding the per-wrapper matchMedia onchange property), which
    // cannot be distinguished from an explicit descriptor query.
    // Named form controls additionally supply their legacy native property
    // semantics. Other hosts retain normal lookup, descriptors and enumeration.
    const auto handler_flags = static_cast<v8::PropertyHandlerFlags>(
        static_cast<int>(v8::PropertyHandlerFlags::kOnlyInterceptStrings));
    tmpl->SetHandler(v8::NamedPropertyHandlerConfiguration(
        &named_property_getter, &named_property_setter, &named_property_query,
        &named_property_deleter, &named_property_enumerator,
        &named_property_definer, nullptr,
        v8::Local<v8::Value>(), handler_flags));

    // Data properties are getter-computed. Read-only ones use ReadOnly|DontDelete
    // (a JS write is a no-op in sloppy mode / TypeError in strict). Writable ones
    // (host->writable_property_names(), M2-8) additionally get a setter and drop
    // ReadOnly so assignment dispatches to set_property.
    const std::vector<std::string> writable = host->writable_property_names();
    const auto is_writable = [&writable](const std::string& name) {
        return std::find(writable.begin(), writable.end(), name) != writable.end();
    };
    const bool idl = !host->prototype_key().empty();
    for (const std::string& property : host->property_names()) {
        if (idl) continue;
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
        if (idl) continue;
        v8::Local<v8::String> mname = v8str(isolate, method);
        v8::Local<v8::Private> slot = v8::Private::New(isolate, mname);
        tmpl->SetNativeDataProperty(
            mname, &method_getter, &method_setter,
            v8::Local<v8::Value>::Cast(slot),
            v8::PropertyAttribute::None);
    }

    v8::Local<v8::Object> object = tmpl->NewInstance(context).ToLocalChecked();
    object->SetAlignedPointerInInternalField(0, host, kHostObjectTag);
    if (idl) {
        install_host_interface(isolate, context, host);
        auto prototype = web_interface_prototype(isolate, context, host->prototype_key());
        if (prototype.IsEmpty()) return object;
        (void)object->SetPrototype(context, prototype);
        if (web_interface_inherits(host->prototype_key(), "Document")) {
            (void)define_idl_attribute(isolate, context, object, "location", "Document", true, false);
        }
        if (host->prototype_key() == "Location") {
            for (const auto& member : host->property_names()) {
                const bool writable_member = std::find(writable.begin(), writable.end(), member) != writable.end();
                (void)define_idl_attribute(isolate, context, object, member, "Location", writable_member, false);
            }
            for (const auto& member : host->method_names()) {
                v8::Local<v8::Value> function;
                if (!prototype->GetPrivate(context, v8::Private::ForApi(isolate,
                        v8str(isolate, "iv8.webidl.method." + member))).ToLocal(&function)) return object;
                (void)object->DefineOwnProperty(context, v8str(isolate, member), function,
                    static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete));
            }
            (void)object->DefineOwnProperty(context, v8::Symbol::GetToPrimitive(isolate),
                v8::Undefined(isolate), static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete));
        }
    }
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
