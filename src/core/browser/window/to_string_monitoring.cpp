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
    const bool user_call = tracing && trace->user_entry();
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
    bool user_operation = user_call;
    if (tracing && !function_call && !user_operation && receiver->IsObject()) {
        user_operation = runtime_api_field_has_user_accessor(isolate, context,
            receiver.As<v8::Object>(), v8::Symbol::GetToStringTag(isolate), false, false);
    }
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
