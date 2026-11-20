#include "web_idl_primitive.h"

#include "iv8/access_origin.h"
#include "common.h"

namespace iv8::browser {
namespace {

bool conversion_error(v8::Isolate* isolate, const std::string& prefix,
    const std::string& message) {
    const auto text = prefix + message;
    v8::Local<v8::String> value;
    if (v8::String::NewFromUtf8(isolate, text.data(), v8::NewStringType::kNormal,
            static_cast<int>(text.size())).ToLocal(&value)) {
        isolate->ThrowException(v8::Exception::TypeError(value));
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
    v8::Local<v8::Context> context, v8::Local<v8::Value> hook,
    const std::string& error_prefix) {
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
    return conversion_error(isolate, error_prefix, message);
}

}  // namespace

bool web_idl_to_primitive(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, bool string_hint, const std::string& error_prefix,
    v8::Local<v8::Value>* primitive) {
    if (!input->IsObject()) { *primitive = input; return true; }
    auto object = input.As<v8::Object>();
    v8::Local<v8::Value> exotic;
    if (!user_get(isolate, context, object, v8::Symbol::GetToPrimitive(isolate)).ToLocal(&exotic)) return false;
    if (!exotic->IsNullOrUndefined()) {
        if (!callable(exotic)) return noncallable_primitive_hook(isolate, context, exotic, error_prefix);
        v8::Local<v8::Value> hint[] = {v8_string(isolate, string_hint ? "string" : "number")};
        if (!user_call(isolate, context, exotic, input, 1, hint).ToLocal(primitive)) return false;
        if ((*primitive)->IsObject()) return conversion_error(isolate, error_prefix, "Cannot convert object to primitive value");
        return true;
    }
    for (const char* name : {string_hint ? "toString" : "valueOf", string_hint ? "valueOf" : "toString"}) {
        v8::Local<v8::Value> method;
        if (!user_get(isolate, context, object, v8_string(isolate, name)).ToLocal(&method)) return false;
        if (!callable(method)) continue;
        if (!user_call(isolate, context, method, input).ToLocal(primitive)) return false;
        if (!(*primitive)->IsObject()) return true;
    }
    return conversion_error(isolate, error_prefix, "Cannot convert object to primitive value");
}

}  // namespace iv8::browser
