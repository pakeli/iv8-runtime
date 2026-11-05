#include "iv8/host_object.h"

#include <algorithm>
#include <memory>
#include <optional>
#include <cstdio>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "iv8/access_summary.h"
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

// Keep existence/attribute queries on the normal lookup path. Without a query
// handler V8 may use the getter interceptor for `in`, hasOwnProperty, and
// descriptor operations; a no-op query prevents those from being mistaken for
// ordinary property reads.
v8::Intercepted named_property_query(
    v8::Local<v8::Name>,
    const v8::PropertyCallbackInfo<v8::Integer>&) {
    return v8::Intercepted::kNo;
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
    return v8::Intercepted::kNo;
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
    info.GetReturnValue().Set(v8::Array::New(isolate, 0));
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
HostObject* idl_receiver(const v8::FunctionCallbackInfo<v8::Value>& info,
                          std::string& key) {
    v8::Isolate* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> member;
    v8::Local<v8::Value> interface;
    if (!data->Get(context, 0).ToLocal(&member) ||
        !data->Get(context, 1).ToLocal(&interface)) return nullptr;
    v8::String::Utf8Value member_text(isolate, member);
    v8::String::Utf8Value interface_text(isolate, interface);
    key = *member_text != nullptr ? *member_text : "";
    HostObject* host = backing_of(info.This());
    bool compatible = host != nullptr && *interface_text != nullptr &&
        host->prototype_key() == *interface_text;
    // Node/HTMLElement attributes remain valid on sibling interfaces. Tag-
    // specific attributes (form.action, script.src, input.value, ...) retain
    // their stricter interface brand instead of accepting arbitrary hosts.
    if (host != nullptr && *interface_text != nullptr &&
        std::string_view(*interface_text).starts_with("Element:")) {
        constexpr std::string_view node_properties[] = {
            "nodeName", "nodeType", "nodeValue", "textContent", "parentNode",
            "parentElement", "childNodes", "ownerDocument", "isConnected",
            "firstChild", "lastChild", "previousSibling", "nextSibling"};
        constexpr std::string_view element_properties[] = {
            "id", "className", "tagName", "innerHTML", "outerHTML", "children",
            "previousElementSibling", "nextElementSibling", "firstElementChild",
            "lastElementChild", "childElementCount", "classList", "innerText", "style"};
        const auto contains = [&key](const auto& names) {
            return std::find(std::begin(names), std::end(names), key) != std::end(names);
        };
        if (contains(node_properties)) compatible = host->is_node();
        else if (contains(element_properties)) compatible = host->node_kind() == NodeKind::Element;
    }
    if (!compatible) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8str(isolate, "Illegal invocation")));
        return nullptr;
    }
    return host;
}

void idl_getter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    std::string key;
    HostObject* host = idl_receiver(info, key);
    if (host == nullptr) return;
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    AccessTrace* trace = active_trace(isolate);
    const auto seq = record_access(trace, host, key, AccessOperation::kGet);
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
    std::string key;
    HostObject* host = idl_receiver(info, key);
    if (host == nullptr) return;
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    v8::Local<v8::Value> value = info.Length() ? info[0] :
        v8::Local<v8::Value>(v8::Undefined(isolate));
    AccessTrace* trace = active_trace(isolate);
    const auto seq = record_access(trace, host, key, AccessOperation::kSet);
    v8::TryCatch caught(isolate);
    host->set_property(isolate, context, key, value);
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
    HostObject* host = backing_of(info.This());
    if (host == nullptr) {
        return;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::String::Utf8Value name(isolate, info.Data());
    const std::string key(*name != nullptr ? *name : "");
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
            if (!v8::FunctionTemplate::New(isolate, &method_callback, method_name)
                     ->GetFunction(context)
                     .ToLocal(&fn)) {
                return;
            }
            (void)holder->SetPrivate(context, slot, fn);
            result = fn;
        }
    } else {
        v8::Local<v8::Function> fn;
        if (!v8::FunctionTemplate::New(isolate, &method_callback, method_name)
                 ->GetFunction(context)
                 .ToLocal(&fn)) {
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

}  // namespace

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
    // Every callback returns kNo (or an empty enumerator list) so normal lookup,
    // descriptors, deletion, and enumeration remain authoritative.
    const auto handler_flags = static_cast<v8::PropertyHandlerFlags>(
        static_cast<int>(v8::PropertyHandlerFlags::kOnlyInterceptStrings));
    tmpl->SetHandler(v8::NamedPropertyHandlerConfiguration(
        &named_property_getter, nullptr, &named_property_query,
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
    const auto prototype_properties = host->prototype_property_names();
    for (const std::string& property : host->property_names()) {
        if (std::find(prototype_properties.begin(), prototype_properties.end(),
                      property) != prototype_properties.end()) continue;
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
        v8::Local<v8::Private> slot = v8::Private::New(isolate, mname);
        tmpl->SetNativeDataProperty(
            mname, &method_getter, &method_setter,
            v8::Local<v8::Value>::Cast(slot),
            v8::PropertyAttribute::None);
    }

    v8::Local<v8::Object> object = tmpl->NewInstance(context).ToLocalChecked();
    object->SetAlignedPointerInInternalField(0, host, kHostObjectTag);
    if (!prototype_properties.empty()) {
        auto slot = v8::Private::ForApi(isolate, v8str(isolate,
            "iv8.idl.prototype." + host->prototype_key()));
        v8::Local<v8::Value> cached;
        if (!context->Global()->GetPrivate(context, slot).ToLocal(&cached))
            return object;
        v8::Local<v8::Object> prototype;
        if (cached->IsObject()) {
            prototype = cached.As<v8::Object>();
        } else {
            prototype = v8::Object::New(isolate);
            for (const auto& property : prototype_properties) {
                auto data = v8::Array::New(isolate, 2);
                (void)data->Set(context, 0, v8str(isolate, property));
                (void)data->Set(context, 1, v8str(isolate, host->prototype_key()));
                v8::Local<v8::Function> getter;
                if (!v8::Function::New(context, &idl_getter, data, 0,
                        v8::ConstructorBehavior::kThrow).ToLocal(&getter))
                    return object;
                getter->SetName(v8str(isolate, "get " + property));
                v8::Local<v8::Function> setter;
                if (is_writable(property) &&
                    !v8::Function::New(context, &idl_setter, data, 1,
                        v8::ConstructorBehavior::kThrow).ToLocal(&setter))
                    return object;
                if (!setter.IsEmpty()) setter->SetName(v8str(isolate, "set " + property));
                v8::PropertyDescriptor descriptor(getter, setter.IsEmpty()
                    ? v8::Local<v8::Value>(v8::Undefined(isolate))
                    : v8::Local<v8::Value>(setter));
                descriptor.set_enumerable(true);
                descriptor.set_configurable(true);
                (void)prototype->DefineProperty(context, v8str(isolate, property),
                                                 descriptor);
            }
            (void)context->Global()->SetPrivate(context, slot, prototype);
        }
        (void)object->SetPrototype(context, prototype);
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
