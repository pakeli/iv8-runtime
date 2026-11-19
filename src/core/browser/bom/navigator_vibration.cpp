#include "navigator_vibration.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

#include "iv8/access_origin.h"
#include "../internal/common.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {
namespace {

bool conversion_error(v8::Isolate* isolate, const std::string& message) {
    const auto prefixed = "Failed to execute 'vibrate' on 'Navigator': " + message;
    v8::Local<v8::String> text;
    if (v8::String::NewFromUtf8(isolate, prefixed.data(), v8::NewStringType::kNormal,
            static_cast<int>(prefixed.size())).ToLocal(&text)) {
        isolate->ThrowException(v8::Exception::TypeError(text));
    }
    return false;
}

bool callable(v8::Local<v8::Value> value) {
    return value->IsObject() && value.As<v8::Object>()->IsCallable();
}

v8::MaybeLocal<v8::Value> user_get(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> object,
    v8::Local<v8::Value> key) {
    return access_user_operation(isolate, [&] { return object->Get(context, key); });
}

v8::MaybeLocal<v8::Value> user_call(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> method,
    v8::Local<v8::Value> receiver, int count = 0,
    v8::Local<v8::Value>* arguments = nullptr) {
    return access_user_operation(isolate, [&] {
        return method.As<v8::Object>()->CallAsFunction(context, receiver, count, arguments);
    });
}

bool noncallable_primitive_hook(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> hook) {
    // Ask V8 to format its own non-callable-hook error on an internal holder.
    // The hook is known not callable: this cannot invoke it, inspect its fields,
    // or repeat the original object's Symbol.toPrimitive getter.
    std::string message = "Cannot convert object to primitive value";
    {
        v8::TryCatch diagnostic(isolate);
        auto holder = v8::Object::New(isolate);
        if (!holder->CreateDataProperty(context, v8::Symbol::GetToPrimitive(isolate), hook).FromMaybe(false))
            return false;
        (void)holder->NumberValue(context);
        if (diagnostic.HasCaught()) {
            v8::Local<v8::Value> text;
            if (diagnostic.Exception()->IsObject() && diagnostic.Exception().As<v8::Object>()
                    ->GetRealNamedProperty(context, v8_string(isolate, "message").As<v8::String>()).ToLocal(&text)
                    && text->IsString()) {
                v8::String::Utf8Value utf8(isolate, text);
                if (*utf8 != nullptr) message.assign(*utf8, utf8.length());
            }
            if (!diagnostic.CanContinue()) { diagnostic.ReThrow(); return false; }
            diagnostic.Reset();
        }
    }
    // The installed intrinsic stringifier observer changes this engine's
    // diagnostic rendering of our internal holder. Match Chrome's object slots
    // only in this newly generated diagnostic; never rewrite a caught user
    // exception or an identically spelled string-valued hook.
    const std::string holder_slot = " of object '[object Object]' is not a function";
    if (message.ends_with(holder_slot)) {
        message.replace(message.size() - holder_slot.size(), holder_slot.size(),
            " of object '#<Object>' is not a function");
    }
    const std::string object_hook_slot = "'[object Object]' returned for property 'Symbol(Symbol.toPrimitive)'";
    if (hook->IsObject() && message.starts_with(object_hook_slot)) {
        message.replace(0, object_hook_slot.size(),
            "'#<Object>' returned for property 'Symbol(Symbol.toPrimitive)'");
    }
    return conversion_error(isolate, message);
}

bool number_primitive(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, v8::Local<v8::Value>* primitive) {
    if (!input->IsObject()) { *primitive = input; return true; }
    auto object = input.As<v8::Object>();
    v8::Local<v8::Value> exotic;
    if (!user_get(isolate, context, object, v8::Symbol::GetToPrimitive(isolate)).ToLocal(&exotic)) return false;
    if (!exotic->IsNullOrUndefined()) {
        if (!callable(exotic)) return noncallable_primitive_hook(isolate, context, exotic);
        v8::Local<v8::Value> hint[] = {v8_string(isolate, "number")};
        if (!user_call(isolate, context, exotic, input, 1, hint).ToLocal(primitive)) return false;
        if ((*primitive)->IsObject()) return conversion_error(isolate, "Cannot convert object to primitive value");
        return true;
    }
    for (const char* name : {"valueOf", "toString"}) {
        v8::Local<v8::Value> method;
        if (!user_get(isolate, context, object, v8_string(isolate, name)).ToLocal(&method)) return false;
        if (!callable(method)) continue;
        if (!user_call(isolate, context, method, input).ToLocal(primitive)) return false;
        if (!(*primitive)->IsObject()) return true;
    }
    return conversion_error(isolate, "Cannot convert object to primitive value");
}

bool pattern_number(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, bool clamp) {
    v8::Local<v8::Value> primitive;
    if (!number_primitive(isolate, context, input, &primitive)) return false;
    if (primitive->IsSymbol()) return conversion_error(isolate, "Cannot convert a Symbol value to a number");
    if (primitive->IsBigInt()) return conversion_error(isolate, "Cannot convert a BigInt value to a number");
    if (!clamp) {
        return access_user_operation(isolate, [&] { return primitive->Uint32Value(context); }).IsJust();
    }
    const auto converted = access_user_operation(isolate, [&] { return primitive->NumberValue(context); });
    if (converted.IsNothing()) return false;
    const double number = converted.FromJust();
    // Chromium's scalar overload uses [Clamp]; sequence items use modulo
    // unsigned-long conversion. Neither result enters a hardware backend here.
    const double bounded = std::isnan(number) ? 0 : std::clamp(number, 0.0, 4294967295.0);
    const double lower = std::floor(bounded);
    const double fraction = bounded - lower;
    const auto rounded = static_cast<std::uint32_t>(lower +
        (fraction > 0.5 || (fraction == 0.5 && std::fmod(lower, 2.0) != 0) ? 1 : 0));
    (void)rounded;
    return true;
}

bool array_pattern(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Array> array) {
    // Chrome's real-Array overload bypasses Symbol.iterator, and observes a
    // changed length after each element conversion rather than snapshotting it.
    for (std::uint32_t index = 0; index < array->Length(); ++index) {
        v8::Local<v8::Value> item;
        if (!access_user_operation(isolate, [&] { return array->Get(context, index); }).ToLocal(&item) ||
            !pattern_number(isolate, context, item, false)) return false;
    }
    return true;
}

bool iterable_pattern(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Object> input) {
    // Overload selection and sequence conversion perform separate lookups in
    // the browser. Do not reuse the first result when converting an iterable.
    v8::Local<v8::Value> method, iterator_value, next;
    if (!user_get(isolate, context, input, v8::Symbol::GetIterator(isolate)).ToLocal(&method)) return false;
    if (method->IsNullOrUndefined()) return conversion_error(isolate, "The object must have a callable @@iterator property.");
    if (!callable(method)) return conversion_error(isolate, "@@iterator must be a callable.");
    if (!user_call(isolate, context, method, input).ToLocal(&iterator_value)) return false;
    if (!iterator_value->IsObject()) return conversion_error(isolate, "Iterator object must be an object.");
    auto iterator = iterator_value.As<v8::Object>();
    if (!user_get(isolate, context, iterator, v8_string(isolate, "next")).ToLocal(&next)) return false;
    if (!callable(next)) return conversion_error(isolate, "Expected next() function on iterator.");
    for (;;) {
        v8::Local<v8::Value> result, item, done;
        if (!user_call(isolate, context, next, iterator).ToLocal(&result)) return false;
        if (!result->IsObject()) return conversion_error(isolate, "Expected iterator.next() to return an Object.");
        auto record = result.As<v8::Object>();
        // Blink unpacks value before done, even when done is true. Conversion
        // failure propagates directly; the browser never closes this iterator.
        if (!user_get(isolate, context, record, v8_string(isolate, "value")).ToLocal(&item) ||
            !user_get(isolate, context, record, v8_string(isolate, "done")).ToLocal(&done)) return false;
        if (done->BooleanValue(isolate)) return true;
        if (!pattern_number(isolate, context, item, false)) return false;
    }
}

}  // namespace

v8::Local<v8::Value> navigator_vibrate(v8::Isolate* isolate,
    v8::Local<v8::Context> context,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, "navigator", "vibrate");
    if (arguments.Length() == 0) {
        conversion_error(isolate, "1 argument required, but only 0 present.");
        return v8::Undefined(isolate);
    }
    const auto input = arguments[0];
    bool valid = false;
    if (input->IsArray()) {
        valid = array_pattern(isolate, context, input.As<v8::Array>());
    } else if (input->IsObject()) {
        v8::Local<v8::Value> iterator;
        if (!user_get(isolate, context, input.As<v8::Object>(), v8::Symbol::GetIterator(isolate))
                .ToLocal(&iterator)) return v8::Undefined(isolate);
        if (!iterator->IsNullOrUndefined()) {
            if (!callable(iterator)) {
                conversion_error(isolate, "@@iterator must be a function");
                return v8::Undefined(isolate);
            }
            valid = iterable_pattern(isolate, context, input.As<v8::Object>());
        } else {
            valid = pattern_number(isolate, context, input, true);
        }
    } else {
        valid = pattern_number(isolate, context, input, true);
    }
    // All conversion/iteration effects run before the no-trusted-activation
    // decision. No pattern storage, timer, device polling or activation exists.
    return valid ? v8::Local<v8::Value>(v8::False(isolate)) : v8::Local<v8::Value>(v8::Undefined(isolate));
}

}  // namespace iv8::browser
