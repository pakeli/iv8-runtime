#include "event_support.h"

#include <cmath>
#include <cstring>
#include <unordered_set>

#include "iv8/access_origin.h"
#include "iv8/query_parameters.h"
#include "iv8/url_parser.h"
#include "iv8/web_method.h"
#include "iv8/host_object.h"
#include "iv8/web_interface.h"
#include "../window/window_trace_support.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {

void apply_prevent_default(const v8::FunctionCallbackInfo<v8::Value>& info);

namespace {

// Plain descriptors and real own properties on known non-masking HostObjects
// can be inspected without invoking user accessors. Private interface metadata
// distinguishes original host methods from user replacements. Unknown/proxy
// paths conservatively retain user attribution without diagnostic trap calls.
bool runtime_field_has_user_accessor(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> object,
    v8::Local<v8::Name> name, bool writing, bool method_call = false) {
    v8::TryCatch diagnostic(isolate);
    const auto unknown = [&]() {
        if (diagnostic.HasCaught()) {
            if (!diagnostic.CanContinue()) diagnostic.ReThrow();
            else diagnostic.Reset();
        }
        return true;
    };
    auto holder = object;
    for (int depth = 0; depth < 128; ++depth) {
        if (holder->IsProxy()) return true;
        if (holder->HasNamedLookupInterceptor() || holder->HasIndexedLookupInterceptor() ||
            holder->InternalFieldCount() != 0) {
            if (host_object_backing(holder) == nullptr) return true;
            // Real-property checks bypass interceptors. Non-masking host
            // handlers leave an existing own JS accessor to the engine.
            const auto own = holder->HasRealNamedProperty(context, name);
            if (own.IsNothing()) return unknown();
            if (!own.FromJust()) {
                const auto parent = holder->GetPrototype();
                if (!parent->IsObject()) return false;
                holder = parent.As<v8::Object>();
                continue;
            }
        }
        v8::Local<v8::Value> descriptor_value;
        if (!holder->GetOwnPropertyDescriptor(context, name).ToLocal(&descriptor_value))
            return unknown();
        if (descriptor_value->IsObject()) {
            const auto descriptor = descriptor_value.As<v8::Object>();
            const auto accessor_name = writing
                ? v8::String::NewFromUtf8Literal(isolate, "set")
                : v8::String::NewFromUtf8Literal(isolate, "get");
            const auto has_accessor = descriptor->HasOwnProperty(context, accessor_name);
            if (has_accessor.IsNothing()) return unknown();
            if (!has_accessor.FromJust()) {
                if (!method_call) return false;
                v8::Local<v8::Value> value;
                if (!descriptor->Get(context, v8::String::NewFromUtf8Literal(isolate, "value")).ToLocal(&value)) return unknown();
                if (!value->IsFunction()) return false;
                if (auto* host = host_object_backing(object); host != nullptr && name->IsString()) {
                    v8::String::Utf8Value member(isolate, name);
                    if (*member == nullptr) return unknown();
                    const auto interface = web_member_interface(host->prototype_key(), *member, true);
                    const auto prototype = web_interface_prototype(isolate, context, interface);
                    v8::Local<v8::String> key;
                    v8::Local<v8::Value> original;
                    const std::string private_name = std::string("iv8.webidl.method.") + *member;
                    if (prototype.IsEmpty() ||
                        !v8::String::NewFromUtf8(isolate, private_name.c_str()).ToLocal(&key) ||
                        !prototype->GetPrivate(context, v8::Private::ForApi(isolate, key)).ToLocal(&original)) return unknown();
                    if (original->StrictEquals(value)) return false;
                }
                return true;
            }
            v8::Local<v8::Value> accessor;
            if (!descriptor->Get(context, accessor_name).ToLocal(&accessor)) return unknown();
            if (!accessor->IsFunction()) return false;
            const auto* trace = access_trace_from_isolate(isolate);
            return trace == nullptr ||
                !trace->runtime_script(accessor.As<v8::Function>()->ScriptId());
        }
        const auto parent = holder->GetPrototype();
        if (!parent->IsObject()) return false;
        holder = parent.As<v8::Object>();
    }
    return true;
}

v8::Local<v8::Private> event_state_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8::event-state"));
}

bool state_field(v8::Isolate* isolate, v8::Local<v8::Context> context,
                 v8::Local<v8::Object> state, const char* name,
                 v8::Local<v8::Value>* value) {
    v8::Local<v8::String> key;
    return v8::String::NewFromUtf8(isolate, name).ToLocal(&key) &&
        state->Get(context, key).ToLocal(value);
}

bool set_state_field(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Object> state, const char* name,
                     v8::Local<v8::Value> value) {
    v8::Local<v8::String> key;
    return v8::String::NewFromUtf8(isolate, name).ToLocal(&key) &&
        state->CreateDataProperty(context, key, value).FromMaybe(false);
}

v8::Local<v8::Object> get_event_state(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> event,
    bool require_brand, bool require_ui = false) {
    v8::Local<v8::Value> value;
    if (!event->GetPrivate(context, event_state_key(isolate)).ToLocal(&value)) return {};
    if (value->IsObject()) {
        auto state = value.As<v8::Object>();
        v8::Local<v8::Value> branded;
        v8::Local<v8::Value> ui;
        if ((!require_brand || (state_field(isolate, context, state, "brand", &branded) && branded->IsTrue())) &&
            (!require_ui || (state_field(isolate, context, state, "ui", &ui) && ui->IsTrue()))) return state;
    }
    if (require_brand) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Illegal invocation")));
    }
    return {};
}

v8::Local<v8::Object> create_event_state(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> event,
    bool brand, bool composed) {
    auto state = v8::Object::New(isolate);
    const auto* runtime = state_from_isolate(isolate);
    const double timestamp = runtime == nullptr ? 0 : static_cast<double>(runtime->event_loop_time_ms());
    if (!state->SetPrototype(context, v8::Null(isolate)).FromMaybe(false) ||
        !set_state_field(isolate, context, state, "brand", v8::Boolean::New(isolate, brand)) ||
        !set_state_field(isolate, context, state, "ui", v8::False(isolate)) ||
        !set_state_field(isolate, context, state, "composed", v8::Boolean::New(isolate, composed)) ||
        !set_state_field(isolate, context, state, "timeStamp", v8::Number::New(isolate, timestamp)) ||
        !set_state_field(isolate, context, state, "dispatching", v8::False(isolate)) ||
        !set_state_field(isolate, context, state, "path", v8::Array::New(isolate)) ||
        !set_state_field(isolate, context, state, "view", v8::Null(isolate)) ||
        !set_state_field(isolate, context, state, "detail", v8::Integer::New(isolate, 0)) ||
        !set_state_field(isolate, context, state, "which", v8::Integer::New(isolate, 0)) ||
        !event->SetPrivate(context, event_state_key(isolate), state).FromMaybe(false)) return {};
    return state;
}

bool dispatch_in_progress(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Object> state) {
    v8::Local<v8::Value> value;
    return state_field(isolate, context, state, "dispatching", &value) && value->IsTrue();
}

bool set_event_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Object> event, const char* name,
                     v8::Local<v8::Value> value) {
    return set_runtime_api_field(isolate, context, event, name, value).FromMaybe(false);
}

bool legacy_event_type(const v8::FunctionCallbackInfo<v8::Value>& info,
                       v8::Local<v8::String>* type) {
    auto isolate = info.GetIsolate();
    if (info.Length() < 1) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "An event type is required")));
        return false;
    }
    return access_user_operation(isolate, [&] { return info[0]->ToString(isolate->GetCurrentContext()); }).ToLocal(type);
}

bool apply_legacy_event(const v8::FunctionCallbackInfo<v8::Value>& info,
                        v8::Local<v8::Object> state, v8::Local<v8::String> type) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    if (dispatch_in_progress(isolate, context, state)) return false;
    if (!set_event_value(isolate, context, info.This(), "type", type) ||
        !set_event_value(isolate, context, info.This(), "bubbles",
            v8::Boolean::New(isolate, info.Length() > 1 && info[1]->BooleanValue(isolate))) ||
        !set_event_value(isolate, context, info.This(), "cancelable",
            v8::Boolean::New(isolate, info.Length() > 2 && info[2]->BooleanValue(isolate))) ||
        !set_event_value(isolate, context, info.This(), "defaultPrevented", v8::False(isolate))) return false;
    reset_event_stopped(isolate, context, info.This());
    return true;
}

bool initialize_legacy_event(const v8::FunctionCallbackInfo<v8::Value>& info,
                             v8::Local<v8::Object> state) {
    v8::Local<v8::String> type;
    return legacy_event_type(info, &type) && apply_legacy_event(info, state, type);
}

}  // namespace

bool runtime_api_field_has_user_accessor(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> object,
    v8::Local<v8::Name> name, bool writing, bool method_call) {
    auto runtime_origin = access_runtime_scope(isolate);
    return runtime_field_has_user_accessor(isolate, context, object, name, writing, method_call);
}

v8::MaybeLocal<v8::Value> get_runtime_api_field(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> object, const char* name) {
    auto runtime_origin = access_runtime_scope(isolate);
    v8::Local<v8::String> key;
    if (!v8::String::NewFromUtf8(isolate, name).ToLocal(&key)) return {};
    const auto* trace = access_trace_from_isolate(isolate);
    if (trace != nullptr && trace->enabled() &&
        runtime_field_has_user_accessor(isolate, context, object, key, false)) {
        return access_user_operation(isolate, [&] { return object->Get(context, key); });
    }
    return object->Get(context, key);
}

v8::Maybe<bool> set_runtime_api_field(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> object, const char* name,
    v8::Local<v8::Value> value) {
    auto runtime_origin = access_runtime_scope(isolate);
    v8::Local<v8::String> key;
    if (!v8::String::NewFromUtf8(isolate, name).ToLocal(&key)) return v8::Nothing<bool>();
    const auto* trace = access_trace_from_isolate(isolate);
    if (trace != nullptr && trace->enabled() &&
        runtime_field_has_user_accessor(isolate, context, object, key, true)) {
        return access_user_operation(isolate, [&] { return object->Set(context, key, value); });
    }
    return object->Set(context, key, value);
}

bool is_native_event(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Value> value) {
    if (!value->IsObject() || value->IsProxy()) return false;
    auto state = get_event_state(isolate, context, value.As<v8::Object>(), false);
    v8::Local<v8::Value> brand;
    return !state.IsEmpty() &&
        state_field(isolate, context, state, "brand", &brand) && brand->IsTrue();
}

bool initialize_event_state(v8::Isolate* isolate, v8::Local<v8::Context> context,
                            v8::Local<v8::Object> event, bool composed) {
    return !create_event_state(isolate, context, event, true, composed).IsEmpty() &&
        define_event_trusted_accessor(isolate, context, event);
}

bool initialize_ui_event_state(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               v8::Local<v8::Object> event,
                               v8::Local<v8::Value> view, double detail) {
    auto state = get_event_state(isolate, context, event, true);
    return !state.IsEmpty() &&
        set_state_field(isolate, context, state, "ui", v8::True(isolate)) &&
        set_state_field(isolate, context, state, "view", view) &&
        set_state_field(isolate, context, state, "detail", v8::Number::New(isolate, detail));
}

bool set_ui_event_which(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> event, double which) {
    auto state = get_event_state(isolate, context, event, true, true);
    return !state.IsEmpty() && set_state_field(isolate, context, state, "which", v8::Number::New(isolate, which));
}

bool begin_event_dispatch(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Object> event,
                          const std::vector<v8::Local<v8::Value>>& path) {
    auto state = get_event_state(isolate, context, event, false);
    if (state.IsEmpty()) state = create_event_state(isolate, context, event, false, false);
    if (state.IsEmpty()) return false;
    if (dispatch_in_progress(isolate, context, state)) {
        auto error = v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate, "Event is already being dispatched"));
        if (error->IsObject() && error.As<v8::Object>()->CreateDataProperty(context,
                v8::String::NewFromUtf8Literal(isolate, "name"),
                v8::String::NewFromUtf8Literal(isolate, "InvalidStateError")).FromMaybe(false)) {
            isolate->ThrowException(error);
        }
        return false;
    }
    auto stored_path = v8::Array::New(isolate);
    for (std::size_t index = 0; index < path.size(); ++index) {
        if (!stored_path->CreateDataProperty(context, static_cast<std::uint32_t>(index), path[index]).FromMaybe(false)) return false;
    }
    return set_state_field(isolate, context, state, "path", stored_path) &&
        set_state_field(isolate, context, state, "dispatching", v8::True(isolate));
}

void end_event_dispatch(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> event) {
    auto state = get_event_state(isolate, context, event, false);
    if (!state.IsEmpty() &&
        (!set_state_field(isolate, context, state, "path", v8::Array::New(isolate)) ||
         !set_state_field(isolate, context, state, "dispatching", v8::False(isolate)))) return;
    reset_event_stopped(isolate, context, event);
    set_event_in_passive_listener(isolate, context, event, false);
}

void event_extra_getter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    v8::String::Utf8Value field(isolate, info.Data());
    if (*field == nullptr) return;
    NativeApiPropertyTrace tracing(info, "Event", *field, AccessOperation::kGet);
    auto state = get_event_state(isolate, context, info.This(), true);
    if (state.IsEmpty()) return;
    const std::string name(*field, static_cast<std::size_t>(field.length()));
    if (name == "isTrusted") { info.GetReturnValue().Set(false); return; }
    if (name == "cancelBubble") { info.GetReturnValue().Set(event_is_stopped(isolate, context, info.This())); return; }
    if (name == "returnValue") { info.GetReturnValue().Set(!event_default_prevented(isolate, context, info.This())); return; }
    v8::Local<v8::Value> value;
    if (name == "srcElement") {
        if (get_runtime_api_field(isolate, context, info.This(), "target").ToLocal(&value)) info.GetReturnValue().Set(value);
    } else if (state_field(isolate, context, state, name.c_str(), &value)) info.GetReturnValue().Set(value);
}

void event_extra_setter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    v8::String::Utf8Value field(isolate, info.Data());
    if (*field == nullptr) return;
    NativeApiPropertyTrace tracing(info, "Event", *field, AccessOperation::kSet);
    if (get_event_state(isolate, context, info.This(), true).IsEmpty()) return;
    const bool value = info.Length() > 0 && info[0]->BooleanValue(isolate);
    if (std::strcmp(*field, "cancelBubble") == 0 && value) {
        if (!info.This()->SetPrivate(context, event_stopped_key(isolate), v8::True(isolate)).FromMaybe(false)) return;
    } else if (std::strcmp(*field, "returnValue") == 0 && !value) {
        apply_prevent_default(info);
    }
}

void event_init_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    NativeApiCallTrace tracing(info, "Event", "initEvent");
    auto state = get_event_state(info.GetIsolate(), info.GetIsolate()->GetCurrentContext(), info.This(), true);
    if (!state.IsEmpty()) (void)initialize_legacy_event(info, state);
}

void event_composed_path_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    NativeApiCallTrace tracing(info, "Event", "composedPath");
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto state = get_event_state(isolate, context, info.This(), true);
    if (state.IsEmpty()) return;
    v8::Local<v8::Value> value;
    if (!state_field(isolate, context, state, "path", &value) || !value->IsArray()) return;
    auto stored = value.As<v8::Array>();
    auto copy = v8::Array::New(isolate);
    for (std::uint32_t index = 0; index < stored->Length(); ++index) {
        v8::Local<v8::Value> entry;
        if (!stored->Get(context, index).ToLocal(&entry) ||
            !copy->CreateDataProperty(context, index, entry).FromMaybe(false)) return;
    }
    info.GetReturnValue().Set(copy);
}

void ui_event_extra_getter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    v8::String::Utf8Value field(isolate, info.Data());
    if (*field == nullptr) return;
    NativeApiPropertyTrace tracing(info, "UIEvent", *field, AccessOperation::kGet);
    auto state = get_event_state(isolate, context, info.This(), true, true);
    if (state.IsEmpty()) return;
    if (std::strcmp(*field, "sourceCapabilities") == 0) { info.GetReturnValue().Set(v8::Null(isolate)); return; }
    v8::Local<v8::Value> value;
    if (state_field(isolate, context, state, *field, &value)) info.GetReturnValue().Set(value);
}

void ui_event_init_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    NativeApiCallTrace tracing(info, "UIEvent", "initUIEvent");
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto state = get_event_state(isolate, context, info.This(), true, true);
    v8::Local<v8::String> type;
    if (state.IsEmpty() || !legacy_event_type(info, &type)) return;
    auto view = info.Length() > 3 ? info[3] : v8::Local<v8::Value>(v8::Null(isolate));
    if (view->IsUndefined()) view = v8::Null(isolate);
    if (!view->IsNull() && !view->StrictEquals(context->Global())) {
        isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(isolate, "view must be a Window or null")));
        return;
    }
    std::int32_t detail = 0;
    if (info.Length() > 4) {
        auto converted = access_user_operation(isolate, [&] { return info[4]->Int32Value(context); });
        if (converted.IsNothing()) return;
        detail = converted.FromMaybe(0);
    }
    if (!apply_legacy_event(info, state, type)) return;
    if (!set_state_field(isolate, context, state, "view", view) ||
        !set_state_field(isolate, context, state, "detail", v8::Number::New(isolate, detail))) return;
}

bool define_event_trusted_accessor(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   v8::Local<v8::Object> event) {
    v8::Local<v8::Function> getter;
    if (!make_web_method(isolate, context, "get isTrusted", 0, &event_extra_getter,
            v8::String::NewFromUtf8Literal(isolate, "isTrusted")).ToLocal(&getter)) return false;
    v8::PropertyDescriptor descriptor(getter, v8::Undefined(isolate));
    descriptor.set_enumerable(true);
    descriptor.set_configurable(false);
    return event->DefineProperty(context, v8::String::NewFromUtf8Literal(isolate, "isTrusted"), descriptor).FromMaybe(false);
}

// --- M4-A-7 minimal bubbling helpers -----------------------------------------
// Whether `event.bubbles` is truthy (missing -> false).
bool event_bubbles(v8::Isolate* isolate, v8::Local<v8::Context> context,
                   v8::Local<v8::Object> event) {
    v8::Local<v8::Value> value;
    if (get_runtime_api_field(isolate, context, event, "bubbles").ToLocal(&value)) {
        return value->BooleanValue(isolate);
    }
    return false;
}

// Internal "propagation stopped" / "immediate propagation stopped" flags,
// stored as V8 Privates on the event so they are invisible to normal property
// access (not part of the public Event face).
v8::Local<v8::Private> event_stopped_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate, v8::String::NewFromUtf8Literal(isolate, "iv8::event-stopped"));
}

v8::Local<v8::Private> event_immediate_stopped_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8::event-immediate-stopped"));
}

v8::Local<v8::Private> event_passive_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate, v8::String::NewFromUtf8Literal(isolate, "iv8::event-passive"));
}

bool event_in_passive_listener(v8::Isolate* isolate,
                               v8::Local<v8::Context> context,
                               v8::Local<v8::Object> event) {
    v8::Local<v8::Value> value;
    if (event->GetPrivate(context, event_passive_key(isolate)).ToLocal(&value)) {
        return value->BooleanValue(isolate);
    }
    return false;
}

void set_event_in_passive_listener(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   v8::Local<v8::Object> event, bool passive) {
    (void)event->SetPrivate(context, event_passive_key(isolate),
                            v8::Boolean::New(isolate, passive));
}

void reset_event_stopped(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         v8::Local<v8::Object> event) {
    (void)event->SetPrivate(context, event_stopped_key(isolate),
                            v8::False(isolate));
    (void)event->SetPrivate(context, event_immediate_stopped_key(isolate),
                            v8::False(isolate));
}

bool event_is_stopped(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Object> event) {
    v8::Local<v8::Value> value;
    if (event->GetPrivate(context, event_stopped_key(isolate)).ToLocal(&value)) {
        return value->BooleanValue(isolate);
    }
    return false;
}

bool event_is_immediately_stopped(v8::Isolate* isolate,
                                  v8::Local<v8::Context> context,
                                  v8::Local<v8::Object> event) {
    v8::Local<v8::Value> value;
    if (event->GetPrivate(context, event_immediate_stopped_key(isolate))
            .ToLocal(&value)) {
        return value->BooleanValue(isolate);
    }
    return false;
}

// event.stopPropagation(): set the private flag on the receiver event. Stops
// propagation to LATER targets only; remaining listeners on the current target
// still run.
void stop_propagation_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    NativeApiCallTrace tracing(info, "Event", "stopPropagation");
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    (void)info.This()->SetPrivate(context, event_stopped_key(isolate),
                                  v8::True(isolate));
}

// event.stopImmediatePropagation(): stop later listeners on this target and all
// later targets. Setting the ordinary stop flag as well keeps the existing
// bubbling checks correct without touching every call site.
void stop_immediate_propagation_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    NativeApiCallTrace tracing(info, "Event", "stopImmediatePropagation");
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    (void)info.This()->SetPrivate(context, event_stopped_key(isolate),
                                  v8::True(isolate));
    (void)info.This()->SetPrivate(context, event_immediate_stopped_key(isolate),
                                  v8::True(isolate));
}

// event.preventDefault(): no-op unless the event is cancelable; otherwise mark
// defaultPrevented (an ordinary own field, visible to scripts and to the
// dispatchEvent return value).
void prevent_default_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    NativeApiCallTrace tracing(info, "Event", "preventDefault");
    apply_prevent_default(info);
}

void apply_prevent_default(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (event_in_passive_listener(isolate, context, info.This())) {
        return;  // passive listeners cannot cancel
    }
    v8::Local<v8::Value> cancelable;
    if (!get_runtime_api_field(isolate, context, info.This(), "cancelable")
             .ToLocal(&cancelable) ||
        !cancelable->BooleanValue(isolate)) {
        return;
    }
    (void)set_runtime_api_field(isolate, context, info.This(), "defaultPrevented",
                           v8::True(isolate));
}

bool event_default_prevented(v8::Isolate* isolate,
                             v8::Local<v8::Context> context,
                             v8::Local<v8::Object> event) {
    v8::Local<v8::Value> value;
    if (get_runtime_api_field(isolate, context, event, "defaultPrevented")
            .ToLocal(&value)) {
        return value->BooleanValue(isolate);
    }
    return false;
}

}  // namespace iv8::browser
