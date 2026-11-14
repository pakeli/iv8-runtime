#include "event_constructors.h"

#include <cmath>
#include <cstring>
#include <unordered_set>

#include "iv8/query_parameters.h"
#include "iv8/url_parser.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {

// M3-3 / M4-A-7 / M11-9 minimal Event constructor. `new Event(type, init?)`
// yields a plain JS object with `type` (string, coerced from the first
// argument), `bubbles` / `cancelable` (from init: truthy -> true; a missing /
// non-object init -> false), `defaultPrevented` (false), `target` /
// `currentTarget` (null until a dispatch sets them), and the propagation /
// cancellation methods `stopPropagation`, `stopImmediatePropagation`, and
// `preventDefault`. Still no capture phase, eventPhase / composed / timeStamp,
// or listener options.
void event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Object> self = info.This();
    std::string type;
    if (info.Length() > 0) {
        v8::Local<v8::String> str;
        if (info[0]->ToString(context).ToLocal(&str)) {
            v8::String::Utf8Value utf8(isolate, str);
            if (*utf8 != nullptr) {
                type.assign(*utf8, static_cast<std::size_t>(utf8.length()));
            }
        }
    }
    // M4-A-7 / M11-9: bubbles / cancelable from init (only when it is an object).
    bool bubbles = false;
    bool cancelable = false;
    if (info.Length() > 1 && info[1]->IsObject()) {
        v8::Local<v8::Value> value;
        if (info[1].As<v8::Object>()
                ->Get(context, v8_string(isolate, "bubbles"))
                .ToLocal(&value)) {
            bubbles = value->BooleanValue(isolate);
        }
        if (info[1].As<v8::Object>()
                ->Get(context, v8_string(isolate, "cancelable"))
                .ToLocal(&value)) {
            cancelable = value->BooleanValue(isolate);
        }
    }
    (void)self->Set(context, v8_string(isolate, "type"), v8_string(isolate, type));
    (void)self->Set(context, v8_string(isolate, "bubbles"),
                    v8::Boolean::New(isolate, bubbles));
    (void)self->Set(context, v8_string(isolate, "cancelable"),
                    v8::Boolean::New(isolate, cancelable));
    (void)self->Set(context, v8_string(isolate, "defaultPrevented"),
                    v8::False(isolate));
    // M11-11: NONE outside dispatch; 1/2/3 during capturing/at-target/bubbling.
    (void)self->Set(context, v8_string(isolate, "eventPhase"),
                    v8::Integer::New(isolate, 0));
    (void)self->Set(context, v8_string(isolate, "target"), v8::Null(isolate));
    (void)self->Set(context, v8_string(isolate, "currentTarget"),
                    v8::Null(isolate));
    // Propagation / cancellation methods.
    v8::Local<v8::Function> stop_fn =
        v8::FunctionTemplate::New(isolate, &stop_propagation_callback)
            ->GetFunction(context)
            .ToLocalChecked();
    (void)self->Set(context, v8_string(isolate, "stopPropagation"), stop_fn);
    v8::Local<v8::Function> stop_immediate_fn =
        v8::FunctionTemplate::New(isolate, &stop_immediate_propagation_callback)
            ->GetFunction(context)
            .ToLocalChecked();
    (void)self->Set(context, v8_string(isolate, "stopImmediatePropagation"),
                    stop_immediate_fn);
    v8::Local<v8::Function> prevent_default_fn =
        v8::FunctionTemplate::New(isolate, &prevent_default_callback)
            ->GetFunction(context)
            .ToLocalChecked();
    (void)self->Set(context, v8_string(isolate, "preventDefault"),
                    prevent_default_fn);
}

bool event_construct_check(const v8::FunctionCallbackInfo<v8::Value>& info,
                           const char* name) {
    if (info.IsConstructCall()) {
        return true;
    }
    v8::Isolate* isolate = info.GetIsolate();
    const std::string message =
        std::string(name) + " constructor requires 'new'";
    isolate->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8(isolate, message.c_str()).ToLocalChecked()));
    return false;
}

// Reads init[name] when init is an object; absent / non-object init yields
// undefined. Returns false only when the getter threw (exception left pending).
bool event_init_field(v8::Local<v8::Context> context,
                      const v8::FunctionCallbackInfo<v8::Value>& info,
                      const char* name, v8::Local<v8::Value>* out) {
    v8::Isolate* isolate = info.GetIsolate();
    if (info.Length() < 2 || !info[1]->IsObject()) {
        *out = v8::Undefined(isolate);
        return true;
    }
    return info[1]
        .As<v8::Object>()
        ->Get(context, v8_string(isolate, name))
        .ToLocal(out);
}

// Subclass fields are own read-only data properties (sloppy assignment is a
// silent no-op; strict assignment throws). The inherited Event fields stay
// writable because dispatch still sets target / currentTarget on the object.
void set_event_field(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Object> self, const char* name,
                     v8::Local<v8::Value> value) {
    (void)self->DefineOwnProperty(
        context, v8::String::NewFromUtf8(isolate, name).ToLocalChecked(), value,
        v8::ReadOnly);
}

// WebIDL-lite integer conversion: ToNumber, non-finite -> 0, truncate toward
// zero. Deliberately no 32-bit wrap-around (see the boundary note).
bool event_int_field(v8::Local<v8::Context> context, v8::Local<v8::Value> value,
                     double* out) {
    v8::Local<v8::Number> number;
    if (!value->ToNumber(context).ToLocal(&number)) {
        return false;
    }
    const double number_value = number->Value();
    *out = std::isfinite(number_value) ? std::trunc(number_value) : 0.0;
    return true;
}

bool event_string_field(v8::Local<v8::Context> context, v8::Isolate* isolate,
                        v8::Local<v8::Value> value, std::string* out) {
    if (value->IsUndefined()) {
        out->clear();
        return true;
    }
    v8::Local<v8::String> str;
    if (!value->ToString(context).ToLocal(&str)) {
        return false;
    }
    v8::String::Utf8Value utf8(isolate, str);
    *out = (*utf8 != nullptr)
               ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
               : std::string();
    return true;
}

void custom_event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!event_construct_check(info, "CustomEvent")) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    event_constructor(info);
    v8::Local<v8::Value> detail;
    if (!event_init_field(context, info, "detail", &detail)) {
        return;
    }
    if (detail->IsUndefined()) {
        detail = v8::Null(isolate);
    }
    set_event_field(isolate, context, info.This(), "detail", detail);
}

void mouse_event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!event_construct_check(info, "MouseEvent")) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    event_constructor(info);
    v8::Local<v8::Object> self = info.This();
    const char* const fields[] = {"clientX", "clientY", "button", "buttons"};
    for (const char* field : fields) {
        v8::Local<v8::Value> value;
        if (!event_init_field(context, info, field, &value)) {
            return;
        }
        double number = 0;
        if (!event_int_field(context, value, &number)) {
            return;
        }
        set_event_field(isolate, context, self, field,
                        v8::Number::New(isolate, number));
    }
}

void keyboard_event_constructor(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!event_construct_check(info, "KeyboardEvent")) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    event_constructor(info);
    v8::Local<v8::Object> self = info.This();
    const char* const fields[] = {"key", "code"};
    for (const char* field : fields) {
        v8::Local<v8::Value> value;
        if (!event_init_field(context, info, field, &value)) {
            return;
        }
        std::string text;
        if (!event_string_field(context, isolate, value, &text)) {
            return;
        }
        set_event_field(isolate, context, self, field, v8_string(isolate, text));
    }
    v8::Local<v8::Value> repeat_value;
    if (!event_init_field(context, info, "repeat", &repeat_value)) {
        return;
    }
    set_event_field(isolate, context, self, "repeat",
                    v8::Boolean::New(
                        isolate, repeat_value->BooleanValue(isolate)));
}

void focus_event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!event_construct_check(info, "FocusEvent")) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    event_constructor(info);
    v8::Local<v8::Value> related_target;
    if (!event_init_field(context, info, "relatedTarget", &related_target)) {
        return;
    }
    if (related_target->IsUndefined()) {
        related_target = v8::Null(isolate);
    }
    set_event_field(isolate, context, info.This(), "relatedTarget",
                    related_target);
}

// M11-52: MessageEvent. Event fields come from event_constructor; the init
// dictionary adds data / origin / lastEventId / source / ports. ports is copied
// into a fresh array (no transferables or frozen-array semantics). No cloning
// happens here: window.postMessage clones before constructing the event.
void message_event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!event_construct_check(info, "MessageEvent")) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    event_constructor(info);

    v8::Local<v8::Value> data;
    if (!event_init_field(context, info, "data", &data)) {
        return;
    }
    if (data->IsUndefined()) {
        data = v8::Null(isolate);
    }
    set_event_field(isolate, context, info.This(), "data", data);

    v8::Local<v8::Value> origin_value;
    if (!event_init_field(context, info, "origin", &origin_value)) {
        return;
    }
    std::string origin;
    if (!event_string_field(context, isolate, origin_value, &origin)) {
        return;
    }
    set_event_field(isolate, context, info.This(), "origin",
                    v8_string(isolate, origin));

    v8::Local<v8::Value> last_event_id_value;
    if (!event_init_field(context, info, "lastEventId", &last_event_id_value)) {
        return;
    }
    std::string last_event_id;
    if (!event_string_field(context, isolate, last_event_id_value,
                            &last_event_id)) {
        return;
    }
    set_event_field(isolate, context, info.This(), "lastEventId",
                    v8_string(isolate, last_event_id));

    v8::Local<v8::Value> source;
    if (!event_init_field(context, info, "source", &source)) {
        return;
    }
    if (source->IsUndefined()) {
        source = v8::Null(isolate);
    }
    set_event_field(isolate, context, info.This(), "source", source);

    v8::Local<v8::Value> ports_value;
    if (!event_init_field(context, info, "ports", &ports_value)) {
        return;
    }
    v8::Local<v8::Array> ports = v8::Array::New(isolate);
    if (ports_value->IsArray()) {
        v8::Local<v8::Array> source_ports = ports_value.As<v8::Array>();
        const std::uint32_t length = source_ports->Length();
        for (std::uint32_t i = 0; i < length; ++i) {
            v8::Local<v8::Value> item;
            if (!source_ports->Get(context, i).ToLocal(&item) ||
                ports->Set(context, i, item).IsEmpty()) {
                return;
            }
        }
    }
    set_event_field(isolate, context, info.This(), "ports", ports);
}

}  // namespace iv8::browser
