#include "to_string_monitoring.h"

#include <cstdint>
#include <initializer_list>
#include <optional>
#include <utility>

#include "iv8/access_origin.h"
#include "iv8/access_summary.h"
#include "iv8/host_object.h"
#include "iv8/web_method.h"
#include "window_trace_support.h"
#include "../internal/event_support.h"

namespace iv8::browser {
namespace {

// Dense private data: original functions, their observers, then owner tokens.
constexpr int kFunctionOriginal = 0;
constexpr int kObjectOriginal = 1;
constexpr int kFunctionObserver = 2;
constexpr int kObjectObserver = 3;
constexpr int kFunctionIdentity = 4;
constexpr int kObjectIdentity = 5;

enum class PropertyKind { kAbsent, kData, kAccessor, kProxy, kUnknown };
struct SafeProperty {
    PropertyKind kind = PropertyKind::kUnknown;
    v8::Local<v8::Value> value;
};

SafeProperty find_real_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Object> holder, v8::Local<v8::Name> name) {
    for (int depth = 0; depth < 128; ++depth) {
        if (holder->IsProxy()) return {PropertyKind::kProxy, holder};
        const auto own = holder->HasRealNamedProperty(context, name);
        if (own.IsNothing()) return {};
        if (own.FromJust()) {
            v8::Local<v8::Value> descriptor_value, value;
            if (!holder->GetOwnPropertyDescriptor(context, name).ToLocal(&descriptor_value) ||
                !descriptor_value->IsObject()) return {};
            auto descriptor = descriptor_value.As<v8::Object>();
            const auto data = descriptor->HasOwnProperty(context,
                v8::String::NewFromUtf8Literal(isolate, "value"));
            if (data.IsNothing()) return {};
            if (!descriptor->Get(context, data.FromJust()
                    ? v8::String::NewFromUtf8Literal(isolate, "value")
                    : v8::String::NewFromUtf8Literal(isolate, "get")).ToLocal(&value)) return {};
            return {data.FromJust() ? PropertyKind::kData : PropertyKind::kAccessor, value};
        }
        // Runtime named/indexed interceptors handle strings, not this Symbol.
        // A string trap lookup on an intercepted handler is less determinate.
        if (name->IsString() && (holder->HasNamedLookupInterceptor() ||
                holder->HasIndexedLookupInterceptor())) return {};
        auto parent = holder->GetPrototype();
        if (!parent->IsObject()) return {PropertyKind::kAbsent, {}};
        holder = parent.As<v8::Object>();
    }
    return {};
}

bool native_callable(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> value, int depth) {
    if (depth > 128) return true;
    if (value->IsProxy()) {
        auto proxy = value.As<v8::Proxy>();
        auto handler = proxy->GetHandler();
        if (!handler->IsObject()) return false;  // revoked; no callback can run
        const auto trap = find_real_property(isolate, context, handler.As<v8::Object>(),
            v8::String::NewFromUtf8Literal(isolate, "apply"));
        if (trap.kind == PropertyKind::kAbsent ||
            (trap.kind == PropertyKind::kData && trap.value->IsNullOrUndefined()) ||
            (trap.kind == PropertyKind::kAccessor && trap.value->IsUndefined()))
            return native_callable(isolate, context, proxy->GetTarget(), depth + 1);
        if (trap.kind == PropertyKind::kData)
            return native_callable(isolate, context, trap.value, depth + 1);
        return true;  // dynamic trap getter/Proxy: never execute it diagnostically
    }
    if (value->IsFunction()) return value.As<v8::Function>()->ScriptId() <= 0;
    return value->IsObject() && value.As<v8::Object>()->IsCallable();
}

bool tag_needs_native_reentry(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Object> receiver, int depth = 0) {
    if (depth > 128) return true;
    const auto property = find_real_property(isolate, context, receiver,
        v8::Symbol::GetToStringTag(isolate));
    if (property.kind == PropertyKind::kAbsent || property.kind == PropertyKind::kData) return false;
    if (property.kind == PropertyKind::kAccessor)
        return native_callable(isolate, context, property.value, depth + 1);
    if (property.kind != PropertyKind::kProxy) return true;
    auto proxy = property.value.As<v8::Proxy>();
    auto handler = proxy->GetHandler();
    if (!handler->IsObject()) return false;
    const auto trap = find_real_property(isolate, context, handler.As<v8::Object>(),
        v8::String::NewFromUtf8Literal(isolate, "get"));
    if (trap.kind == PropertyKind::kAbsent ||
        (trap.kind == PropertyKind::kData && trap.value->IsNullOrUndefined()) ||
        (trap.kind == PropertyKind::kAccessor && trap.value->IsUndefined())) {
        auto target = proxy->GetTarget();
        return target->IsObject() && tag_needs_native_reentry(isolate, context,
            target.As<v8::Object>(), depth + 1);
    }
    if (trap.kind == PropertyKind::kData) {
        const auto* trace = access_trace_from_isolate(isolate);
        if (!trap.value->IsProxy() && trap.value->IsFunction() && trace != nullptr &&
            trace->runtime_script(trap.value.As<v8::Function>()->ScriptId())) {
            // The CSS facade's registered runtime get trap forwards this
            // Symbol with Reflect.get. A user native/bound accessor behind
            // that facade still needs attribution without a new user frame.
            auto target = proxy->GetTarget();
            return target->IsObject() && tag_needs_native_reentry(isolate, context,
                target.As<v8::Object>(), depth + 1);
        }
        return native_callable(isolate, context, trap.value, depth + 1);
    }
    return true;  // a getter for handler.get may itself return a native trap
}

bool preserve_native_tag_callback(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> receiver) {
    if (receiver->IsNullOrUndefined()) return false;
    v8::TryCatch diagnostic(isolate);
    bool preserve = true;
    try {
        // Boxing is diagnostic only. The original builtin still receives raw
        // null/undefined/primitives, never this temporary receiver object.
        v8::Local<v8::Object> object;
        if (receiver->ToObject(context).ToLocal(&object))
            preserve = tag_needs_native_reentry(isolate, context, object);
    } catch (...) {
        // Unknown metadata retains user attribution without running user code.
    }
    if (diagnostic.HasCaught()) {
        preserve = true;
        if (!diagnostic.CanContinue()) diagnostic.ReThrow();
        else diagnostic.Reset();
    }
    return preserve;
}

void capture_call_inputs(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> receiver, v8::Local<v8::Array> arguments,
    AccessCompletion& completion) {
    v8::TryCatch diagnostic(isolate);
    try {
        completion.argument_count = arguments->Length();
        completion.receiver_summary = access_value_summary(isolate, receiver);
        if (!receiver->IsProxy()) {
            if (auto* host = host_object_backing(receiver)) completion.receiver_label = host->trace_label();
        }
        completion.argument_summaries.reserve(arguments->Length());
        for (std::uint32_t index = 0; index < arguments->Length(); ++index) {
            v8::Local<v8::Value> argument;
            if (!arguments->Get(context, index).ToLocal(&argument)) break;
            completion.argument_summaries.push_back(access_value_summary(isolate, argument));
        }
    } catch (...) {
        // Metadata allocation must not replace a builtin result/exception.
    }
    if (diagnostic.HasCaught() && !diagnostic.CanContinue()) diagnostic.ReThrow();
}

void to_string_apply(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    if (info.Length() != 3 || !info[0]->IsFunction() || !info[2]->IsArray()) return;
    auto data = info.Data().As<v8::Array>();
    auto original = info[0].As<v8::Function>();
    auto receiver = info[1];  // apply traps retain null, undefined and primitives.
    auto arguments = info[2].As<v8::Array>();
    v8::Local<v8::Value> function_original;
    if (!data->Get(context, kFunctionOriginal).ToLocal(&function_original)) return;
    const bool function_call = original->StrictEquals(function_original);
    const char* label = function_call ? "Function.prototype" : "Object.prototype";

    auto* trace = access_trace_from_isolate(isolate);
    const bool tracing = trace != nullptr && trace->enabled();
    std::uint64_t sequence = 0;
    if (tracing) {
        v8::Local<v8::Value> identity;
        if (data->Get(context, function_call ? kFunctionIdentity : kObjectIdentity)
                .ToLocal(&identity) && identity->IsExternal()) {
            sequence = trace->record(identity.As<v8::External>()->Value(
                v8::kExternalPointerTypeTagDefault), "host_object", label, "toString",
                AccessOperation::kCall);
        }
    }
    auto runtime_origin = access_runtime_scope(isolate);
    AccessCompletion completion;
    if (sequence != 0) capture_call_inputs(isolate, context, receiver, arguments, completion);
    if (isolate->IsExecutionTerminating()) return;

    if (function_call) {
        // Only our own monitoring entries are transparent to the stringifier.
        // User callable/revoked Proxies retain V8's ordinary native-source result.
        for (const auto index : {kFunctionObserver, kObjectObserver}) {
            v8::Local<v8::Value> observer;
            if (!data->Get(context, index).ToLocal(&observer)) return;
            if (!receiver->StrictEquals(observer)) continue;
            if (!data->Get(context, index == kFunctionObserver ? kFunctionOriginal : kObjectOriginal)
                    .ToLocal(&receiver)) return;
            break;
        }
    }
    // Native tag data reads are builtin housekeeping. Plain JS getters/traps
    // add a user frame naturally; only frame-less native/bound callbacks need
    // explicit user attribution around the original operation.
    const bool user_operation = tracing && !function_call &&
        preserve_native_tag_callback(isolate, context, receiver);
    if (isolate->IsExecutionTerminating()) return;
    std::optional<v8::TryCatch> invocation;
    if (sequence != 0) invocation.emplace(isolate);
    v8::Local<v8::Value> result;
    {
        AccessTrace::OriginScope origin(trace, isolate,
            user_operation ? AccessOriginKind::kUser : AccessOriginKind::kRuntime);
        // Both original algorithms ignore extra arguments; never inspect or
        // coerce them beyond safe metadata capture on the retained event.
        (void)original->Call(context, receiver, 0, nullptr).ToLocal(&result);
    }
    const bool caught = invocation && invocation->HasCaught();
    if (sequence != 0) {
        try {
            if (caught) access_capture_exception(isolate, context, *invocation, completion);
            else if (!result.IsEmpty()) {
                completion.outcome = "returned";
                completion.return_summary = access_value_summary(isolate, result);
            }
            trace->complete(sequence, std::move(completion));
        } catch (...) {
            // Keep the original builtin outcome when diagnostics fail.
        }
    }
    if (caught) { invocation->ReThrow(); return; }
    if (!result.IsEmpty()) info.GetReturnValue().Set(result);
}

bool prototype_and_method(v8::Isolate* isolate, v8::Local<v8::Context> context,
    const char* name, v8::Local<v8::Object>& prototype, v8::Local<v8::Function>& method) {
    v8::Local<v8::Value> constructor_value, prototype_value, method_value;
    v8::Local<v8::String> constructor_name;
    if (!v8::String::NewFromUtf8(isolate, name).ToLocal(&constructor_name) ||
        !context->Global()->Get(context, constructor_name).ToLocal(&constructor_value) ||
        !constructor_value->IsFunction() ||
        !constructor_value.As<v8::Object>()->Get(context,
            v8::String::NewFromUtf8Literal(isolate, "prototype")).ToLocal(&prototype_value) ||
        !prototype_value->IsObject() ||
        !prototype_value.As<v8::Object>()->Get(context,
            v8::String::NewFromUtf8Literal(isolate, "toString")).ToLocal(&method_value) ||
        !method_value->IsFunction()) return false;
    prototype = prototype_value.As<v8::Object>();
    method = method_value.As<v8::Function>();
    return true;
}

}  // namespace

bool install_to_string_monitoring(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    auto runtime_origin = access_runtime_scope(isolate);
    auto global = context->Global();
    auto marker = v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.trace.to-string-observers"));
    v8::Local<v8::Value> installed;
    if (!global->GetPrivate(context, marker).ToLocal(&installed)) return false;
    if (installed->IsArray()) return true;
    v8::Local<v8::Object> prototypes[2];
    v8::Local<v8::Function> originals[2];
    if (!prototype_and_method(isolate, context, "Function", prototypes[0], originals[0]) ||
        !prototype_and_method(isolate, context, "Object", prototypes[1], originals[1])) return false;
    auto data = v8::Array::New(isolate, 6);
    for (int index = 0; index < 2; ++index) {
        const auto* identity = native_api_identity(isolate, prototypes[index]);
        if (identity == nullptr ||
            !data->CreateDataProperty(context, index, originals[index]).FromMaybe(false) ||
            !data->CreateDataProperty(context, kFunctionIdentity + index, v8::External::New(isolate,
                const_cast<void*>(identity), v8::kExternalPointerTypeTagDefault)).FromMaybe(false)) return false;
        auto handler = v8::Object::New(isolate);
        v8::Local<v8::Function> apply;
        v8::Local<v8::Proxy> observer;
        if (!handler->SetPrototype(context, v8::Null(isolate)).FromMaybe(false) ||
            !make_web_method(isolate, context, "apply", 3, &to_string_apply, data).ToLocal(&apply) ||
            !handler->CreateDataProperty(context,
                v8::String::NewFromUtf8Literal(isolate, "apply"), apply).FromMaybe(false) ||
            !v8::Proxy::New(context, originals[index], handler).ToLocal(&observer) ||
            !data->CreateDataProperty(context, kFunctionObserver + index, observer).FromMaybe(false)) return false;
    }
    for (int index = 0; index < 2; ++index) {
        v8::Local<v8::Value> observer;
        if (!data->Get(context, kFunctionObserver + index).ToLocal(&observer) ||
            !prototypes[index]->DefineOwnProperty(context,
                v8::String::NewFromUtf8Literal(isolate, "toString"), observer, v8::DontEnum).FromMaybe(false)) return false;
    }
    return global->SetPrivate(context, marker, data).FromMaybe(false);
}

}  // namespace iv8::browser
