#include "event_constructors.h"

#include <cmath>
#include <cstring>
#include <unordered_set>

#include "iv8/query_parameters.h"
#include "iv8/url_parser.h"
#include "iv8/web_method.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {

namespace {

bool define_event_accessor(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           v8::Local<v8::Object> prototype, const char* name,
                           v8::FunctionCallback callback, bool writable = false) {
    v8::Local<v8::String> property;
    if (!v8::String::NewFromUtf8(isolate, name).ToLocal(&property)) return false;
    v8::Local<v8::Function> getter;
    if (!make_web_method(isolate, context, std::string("get ") + name, 0,
            callback, property).ToLocal(&getter)) return false;
    v8::Local<v8::Value> setter = v8::Undefined(isolate);
    if (writable) {
        v8::Local<v8::Function> function;
        if (!make_web_method(isolate, context, std::string("set ") + name, 1,
                &event_extra_setter, property).ToLocal(&function)) return false;
        setter = function;
    }
    v8::PropertyDescriptor descriptor(getter, setter);
    descriptor.set_enumerable(true);
    descriptor.set_configurable(true);
    return prototype->DefineProperty(context, property, descriptor).FromMaybe(false);
}

bool define_event_method(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         v8::Local<v8::Object> prototype, const char* name,
                         int length, v8::FunctionCallback callback) {
    v8::Local<v8::String> property;
    v8::Local<v8::Function> function;
    return v8::String::NewFromUtf8(isolate, name).ToLocal(&property) &&
        make_web_method(isolate, context, name, length, callback).ToLocal(&function) &&
        prototype->DefineOwnProperty(context, property, function).FromMaybe(false);
}

bool initialize_event_from_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::TryCatch caught(info.GetIsolate());
    event_constructor(info);
    if (caught.HasCaught()) { caught.ReThrow(); return false; }
    return true;
}

bool initialize_ui_event_from_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!initialize_event_from_constructor(info)) return false;
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    v8::Local<v8::Value> view;
    v8::Local<v8::Value> detail_value;
    if (!event_init_field(context, info, "view", &view) ||
        !event_init_field(context, info, "detail", &detail_value)) return false;
    if (view->IsUndefined()) view = v8::Null(isolate);
    if (!view->IsNull() && !view->StrictEquals(context->Global())) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "view must be a Window or null")));
        return false;
    }
    std::int32_t detail = 0;
    if (!detail_value->IsUndefined()) {
        const auto converted = detail_value->Int32Value(context);
        if (converted.IsNothing()) return false;
        detail = converted.FromMaybe(0);
    }
    return initialize_ui_event_state(isolate, context, info.This(), view, detail);
}

}  // namespace

bool install_event_prototype_members(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     v8::Local<v8::Function> constructor,
                                     v8::Local<v8::Object> prototype) {
    const std::pair<const char*, int> constants[] = {
        {"NONE", 0}, {"CAPTURING_PHASE", 1}, {"AT_TARGET", 2}, {"BUBBLING_PHASE", 3}};
    for (const auto& [name, value] : constants) {
        v8::Local<v8::String> property;
        if (!v8::String::NewFromUtf8(isolate, name).ToLocal(&property)) return false;
        for (auto target : {v8::Local<v8::Object>(constructor), prototype}) {
            if (!target->DefineOwnProperty(context, property, v8::Integer::New(isolate, value),
                    static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete)).FromMaybe(false)) return false;
        }
    }
    for (const char* name : {"composed", "timeStamp", "srcElement", "cancelBubble", "returnValue"}) {
        const bool writable = std::strcmp(name, "cancelBubble") == 0 || std::strcmp(name, "returnValue") == 0;
        if (!define_event_accessor(isolate, context, prototype, name, &event_extra_getter, writable)) return false;
    }
    return define_event_method(isolate, context, prototype, "initEvent", 1, &event_init_callback) &&
        define_event_method(isolate, context, prototype, "composedPath", 0, &event_composed_path_callback);
}

bool install_ui_event_prototype_members(v8::Isolate* isolate,
                                        v8::Local<v8::Context> context,
                                        v8::Local<v8::Object> prototype) {
    for (const char* name : {"view", "detail", "which", "sourceCapabilities"}) {
        if (!define_event_accessor(isolate, context, prototype, name, &ui_event_extra_getter)) return false;
    }
    return define_event_method(isolate, context, prototype, "initUIEvent", 1, &ui_event_init_callback);
}

v8::MaybeLocal<v8::Object> create_runtime_event(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& type,
    bool bubbles, bool cancelable) {
    const auto prototype_slot = v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.webidl.prototype.Event"));
    const auto constructor_slot = v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.webidl.constructor"));
    v8::Local<v8::Value> prototype_value;
    v8::Local<v8::Value> constructor_value;
    if (!context->Global()->GetPrivate(context, prototype_slot).ToLocal(&prototype_value) ||
        !prototype_value->IsObject()) return {};
    const auto prototype = prototype_value.As<v8::Object>();
    if (!prototype->GetPrivate(context, constructor_slot).ToLocal(&constructor_value) ||
        !constructor_value->IsFunction()) return {};
    v8::Local<v8::String> event_type;
    if (!v8::String::NewFromUtf8(isolate, type.data(), v8::NewStringType::kNormal,
            static_cast<int>(type.size())).ToLocal(&event_type)) return {};
    auto init = v8::Object::New(isolate);
    if (!init->SetPrototype(context, v8::Null(isolate)).FromMaybe(false) ||
        !init->CreateDataProperty(context, v8::String::NewFromUtf8Literal(isolate, "bubbles"),
            v8::Boolean::New(isolate, bubbles)).FromMaybe(false) ||
        !init->CreateDataProperty(context, v8::String::NewFromUtf8Literal(isolate, "cancelable"),
            v8::Boolean::New(isolate, cancelable)).FromMaybe(false) ||
        !init->CreateDataProperty(context, v8::String::NewFromUtf8Literal(isolate, "composed"),
            v8::False(isolate)).FromMaybe(false)) return {};
    v8::Local<v8::Value> arguments[] = {event_type, init};
    v8::Local<v8::Object> event;
    if (!constructor_value.As<v8::Function>()->NewInstance(context, 2, arguments).ToLocal(&event) ||
        !event->SetPrototype(context, prototype).FromMaybe(false)) return {};
    return event;
}

// M3-3 / M4-A-7 / M11-9 minimal Event constructor. `new Event(type, init?)`
// yields a plain JS object with `type` (string, coerced from the first
// argument), `bubbles` / `cancelable` (from init: truthy -> true; a missing /
// non-object init -> false), `defaultPrevented` (false), `target` /
// `currentTarget` (null until a dispatch sets them), and the propagation /
// cancellation methods `stopPropagation`, `stopImmediatePropagation`, and
// `preventDefault`. Still no capture phase, eventPhase / composed / timeStamp,
// or listener options.
void event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!event_construct_check(info, "Event")) return;
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Object> self = info.This();
    std::string type;
    if (info.Length() > 0) {
        v8::Local<v8::String> str;
        if (!info[0]->ToString(context).ToLocal(&str)) return;
        v8::String::Utf8Value utf8(isolate, str);
        if (*utf8 != nullptr) {
            type.assign(*utf8, static_cast<std::size_t>(utf8.length()));
        }
    }
    // M4-A-7 / M11-9: bubbles / cancelable from init (only when it is an object).
    bool bubbles = false;
    bool cancelable = false;
    if (info.Length() > 1 && info[1]->IsObject()) {
        v8::Local<v8::Value> value;
        if (info[1].As<v8::Object>()
                ->Get(context, v8_string(isolate, "bubbles"))
                .ToLocal(&value)) bubbles = value->BooleanValue(isolate);
        else return;
        if (info[1].As<v8::Object>()
                ->Get(context, v8_string(isolate, "cancelable"))
                .ToLocal(&value)) cancelable = value->BooleanValue(isolate);
        else return;
    }
    v8::Local<v8::Value> composed_value;
    if (!event_init_field(context, info, "composed", &composed_value) ||
        !initialize_event_state(isolate, context, self, composed_value->BooleanValue(isolate))) return;
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

void ui_event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!event_construct_check(info, "UIEvent")) return;
    if (info.Length() < 1) {
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(info.GetIsolate(), "An event type is required")));
        return;
    }
    (void)initialize_ui_event_from_constructor(info);
}

bool event_construct_check(const v8::FunctionCallbackInfo<v8::Value>& info,
                           const char* name) {
    if (info.IsConstructCall()) {
        return true;
    }
    v8::Isolate* isolate = info.GetIsolate();
    const std::string message =
        std::string(name) + " constructor requires 'new'";
    v8::Local<v8::String> exception_message;
    if (v8::String::NewFromUtf8(isolate, message.c_str()).ToLocal(&exception_message)) {
        isolate->ThrowException(v8::Exception::TypeError(exception_message));
    }
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
    v8::Local<v8::String> property;
    if (!v8::String::NewFromUtf8(isolate, name).ToLocal(&property)) return;
    if (!self->DefineOwnProperty(context, property, value, v8::ReadOnly).FromMaybe(false)) return;
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
    if (!initialize_event_from_constructor(info)) return;
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
    if (!initialize_ui_event_from_constructor(info)) return;
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
        if (std::strcmp(field, "button") == 0 &&
            !set_ui_event_which(isolate, context, self, number >= 0 ? number + 1 : 0)) return;
    }
}

void keyboard_event_constructor(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!event_construct_check(info, "KeyboardEvent")) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!initialize_ui_event_from_constructor(info)) return;
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
    if (!initialize_ui_event_from_constructor(info)) return;
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
    if (!initialize_event_from_constructor(info)) return;

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
