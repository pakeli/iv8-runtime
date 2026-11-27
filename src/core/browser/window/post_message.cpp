#include "post_message.h"
#include "window_trace_support.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "iv8/web_method.h"
#include "iv8/access_summary.h"
#include "iv8/access_origin.h"
#include "iv8/host_object.h"
#include "../internal/common.h"
#include "../host/event_target.h"
#include "../internal/trace_hooks.h"
#include "../window/window_event_handlers.h"

namespace iv8::browser {

namespace {

std::string safe_text(v8::Isolate* isolate, v8::Local<v8::Value> value,
                       std::size_t maximum = 4096) {
    if (value.IsEmpty() || !value->IsString()) return {};
    v8::String::Utf8Value text(isolate, value);
    if (*text == nullptr) return {};
    auto length = std::min(static_cast<std::size_t>(text.length()), maximum);
    while (length < static_cast<std::size_t>(text.length()) && length > 0 &&
           (static_cast<unsigned char>((*text)[length]) & 0xc0) == 0x80) --length;
    return std::string(*text, length);
}

bool safe_error_message(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         v8::Local<v8::Value> error) {
    if (!error->IsObject()) return true;
    if (!error->IsNativeError() || error->IsProxy()) return false;
    bool found_name = false;
    bool found_message = false;
    auto current = error.As<v8::Object>();
    for (int depth = 0; depth < 20; ++depth) {
        if (current->IsProxy()) return false;
        for (const auto& [name, found] : {
                std::pair<const char*, bool*>{"name", &found_name},
                std::pair<const char*, bool*>{"message", &found_message}}) {
            if (*found) continue;
            v8::Local<v8::String> key;
            v8::Local<v8::Value> descriptor;
            if (!v8::String::NewFromUtf8(isolate, name).ToLocal(&key) ||
                !current->GetOwnPropertyDescriptor(context, key).ToLocal(&descriptor)) return false;
            if (!descriptor->IsObject()) continue;
            auto object = descriptor.As<v8::Object>();
            const auto value_key = v8::String::NewFromUtf8Literal(isolate, "value");
            if (!object->HasOwnProperty(context, value_key).FromMaybe(false)) return false;
            v8::Local<v8::Value> value;
            if (!object->Get(context, value_key).ToLocal(&value) || value->IsObject()) return false;
            *found = true;
        }
        if (found_name && found_message) return true;
        auto parent = current->GetPrototype();
        if (!parent->IsObject()) return true;
        current = parent.As<v8::Object>();
    }
    return false;
}

void capture_thrown_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Value> error, AccessCompletion& completion) {
    completion.outcome = "threw";
    completion.value_summary = access_value_summary(isolate, error);
    if (!safe_error_message(isolate, context, error)) return;
    const auto message = v8::Exception::CreateMessage(isolate, error);
    if (!message.IsEmpty()) {
        completion.exception_message = safe_text(isolate, message->Get());
        completion.exception_location = safe_text(isolate, message->GetScriptResourceName());
        int line = 0;
        int column = 0;
        if (message->GetLineNumber(context).To(&line)) {
            if (!completion.exception_location.empty()) completion.exception_location += ':';
            completion.exception_location += std::to_string(line);
        }
        if (message->GetStartColumn(context).To(&column)) {
            completion.exception_location += ':' + std::to_string(column);
        }
    }
    if (error->IsNativeError()) {
        completion.exception_name = safe_text(isolate, error.As<v8::Object>()->GetConstructorName(), 128);
        const auto stack = v8::Exception::GetStackTrace(error);
        if (!stack.IsEmpty()) {
            const int count = std::min(stack->GetFrameCount(), 20);
            for (int index = 0; index < count; ++index) {
                auto frame = stack->GetFrame(isolate, index);
                if (index) completion.exception_stack += '\n';
                const auto name = safe_text(isolate, frame->GetFunctionName());
                completion.exception_stack += "at " + (name.empty() ? std::string("<anonymous>") : name) +
                    " (" + safe_text(isolate, frame->GetScriptNameOrSourceURL()) + ':' +
                    std::to_string(frame->GetLineNumber()) + ':' + std::to_string(frame->GetColumn()) + ')';
                if (completion.exception_stack.size() > 4096) {
                    completion.exception_stack.resize(4096);
                    break;
                }
            }
        }
    }
}

void script_call_begin(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    v8::TryCatch diagnostic(isolate);
    try {
        if (info.Length() < 2 || !info[0]->IsString()) return;
        const auto path = safe_text(isolate, info[0], 128);
        std::uint64_t sequence = 0;
        std::string operation_name = "call";
        {
            auto bridge_origin = access_runtime_bridge_scope(isolate);
            if (path == "window.postMessage" || path == "window.matchMedia" ||
                path == "window.CSSStyleDeclaration") {
                const auto member = path.substr(7);
                watch_record(isolate, "window", member);
                sequence = window_trace_record(isolate, member.c_str(), AccessOperation::kCall);
            } else if (path.empty() && info.Length() >= 7 && info[2]->IsString() &&
                       info[3]->IsObject() && !info[3]->IsProxy() && info[4]->IsString() &&
                       info[5]->IsString() && (info[6]->IsString() || info[6]->IsSymbol())) {
                operation_name = safe_text(isolate, info[2], 64);
                AccessOperation operation;
                if (operation_name == "call") operation = AccessOperation::kCall;
                else if (operation_name == "get") operation = AccessOperation::kGet;
                else if (operation_name == "set") operation = AccessOperation::kSet;
                else if (operation_name == "has") operation = AccessOperation::kHas;
                else if (operation_name == "own_keys") operation = AccessOperation::kOwnKeys;
                else if (operation_name == "get_own_property_descriptor") operation = AccessOperation::kGetOwnPropertyDescriptor;
                else return;
                auto type = safe_text(isolate, info[4], 128);
                auto label = safe_text(isolate, info[5], 128);
                std::string member;
                if (info[6]->IsString()) member = safe_text(isolate, info[6], 512);
                else member = "Symbol(" + safe_text(isolate, info[6].As<v8::Symbol>()->Description(isolate), 128) + ')';
                if (operation == AccessOperation::kCall) watch_record(isolate, label, member);
                auto* trace = access_trace_from_isolate(isolate);
                if (trace != nullptr && trace->enabled()) {
                    const void* identity = nullptr;
                    if (auto* host = host_object_backing(info[3])) {
                        identity = host;
                        type = host->trace_object_type();
                        label = host->trace_label();
                    } else identity = native_api_identity(isolate, info[3].As<v8::Object>());
                    if (identity != nullptr) sequence = trace->record(identity, type, label, member, operation);
                }
            }
        }
        auto* trace = access_trace_from_isolate(isolate);
        if (trace == nullptr || !trace->enabled()) return;
        std::uint32_t count = 0;
        auto summaries = v8::Array::New(isolate);
        if (sequence != 0 && (info[1]->IsArgumentsObject() || info[1]->IsArray())) {
            auto arguments = info[1].As<v8::Object>();
            v8::Local<v8::Value> length;
            if (!arguments->Get(context, v8::String::NewFromUtf8Literal(isolate, "length")).ToLocal(&length)) return;
            if (length->IsUint32()) count = length.As<v8::Uint32>()->Value();
            for (std::uint32_t index = 0; index < count; ++index) {
                v8::Local<v8::Value> argument;
                v8::Local<v8::String> summary;
                if (!arguments->Get(context, index).ToLocal(&argument)) return;
                const auto text = access_value_summary(isolate, argument);
                if (text.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return;
                if (!v8::String::NewFromUtf8(isolate, text.data(), v8::NewStringType::kNormal,
                        static_cast<int>(text.size())).ToLocal(&summary) ||
                    !summaries->CreateDataProperty(context, index, summary).FromMaybe(false)) return;
            }
        }
        auto token = v8::Array::New(isolate, 8);
        const auto previous_origin = trace->origin_mark();
        v8::Local<v8::String> operation_text;
        if (!v8::String::NewFromUtf8(isolate, operation_name.c_str()).ToLocal(&operation_text)) return;
        if (!token->CreateDataProperty(context, 0, v8::BigInt::NewFromUnsigned(isolate, sequence)).FromMaybe(false) ||
            !token->CreateDataProperty(context, 1, v8::Integer::NewFromUnsigned(isolate, count)).FromMaybe(false) ||
            !token->CreateDataProperty(context, 2, summaries).FromMaybe(false) ||
            !token->CreateDataProperty(context, 3, operation_text).FromMaybe(false) ||
            !token->CreateDataProperty(context, 4, v8::Integer::New(isolate, previous_origin.runtime_depth)).FromMaybe(false) ||
            !token->CreateDataProperty(context, 5, v8::Boolean::New(isolate, previous_origin.user_entry)).FromMaybe(false) ||
            !token->CreateDataProperty(context, 6, v8::Boolean::New(isolate, previous_origin.runtime_bridge)).FromMaybe(false) ||
            !token->CreateDataProperty(context, 7, v8::True(isolate)).FromMaybe(false)) return;
        trace->enter_origin(isolate, AccessOriginKind::kRuntime);
        info.GetReturnValue().Set(token);
    } catch (...) {
        // Diagnostic allocation/formatting must not change the public JS call.
    }
}

void script_call_end(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    v8::TryCatch diagnostic(isolate);
    try {
        if (info.Length() < 3 || !info[0]->IsArray()) return;
        auto token = info[0].As<v8::Array>();
        v8::Local<v8::Value> runtime_depth, user_entry, runtime_bridge, active;
        if (token->Get(context, 7).ToLocal(&active) && active->IsTrue() &&
            token->Get(context, 4).ToLocal(&runtime_depth) && runtime_depth->IsInt32() &&
            token->Get(context, 5).ToLocal(&user_entry) &&
            token->Get(context, 6).ToLocal(&runtime_bridge)) {
            if (auto* trace = access_trace_from_isolate(isolate)) {
                trace->restore_origin({runtime_depth.As<v8::Int32>()->Value(),
                    user_entry->IsTrue(), runtime_bridge->IsTrue()});
            }
            if (!token->CreateDataProperty(context, 7, v8::False(isolate)).FromMaybe(false)) return;
        }
        v8::Local<v8::Value> sequence_value;
        v8::Local<v8::Value> count;
        v8::Local<v8::Value> summaries;
        v8::Local<v8::Value> operation;
        if (!token->Get(context, 0).ToLocal(&sequence_value) || !sequence_value->IsBigInt() ||
            !token->Get(context, 1).ToLocal(&count) || !count->IsUint32() ||
            !token->Get(context, 2).ToLocal(&summaries) || !summaries->IsArray() ||
            !token->Get(context, 3).ToLocal(&operation) || !operation->IsString()) return;
        bool lossless = false;
        const auto sequence = sequence_value.As<v8::BigInt>()->Uint64Value(&lossless);
        if (!lossless || sequence == 0) return;
        AccessCompletion completion;
        completion.argument_count = count.As<v8::Uint32>()->Value();
        for (std::uint32_t index = 0; index < summaries.As<v8::Array>()->Length(); ++index) {
            v8::Local<v8::Value> summary;
            if (!summaries.As<v8::Array>()->Get(context, index).ToLocal(&summary)) return;
            completion.argument_summaries.push_back(safe_text(isolate, summary,
                std::numeric_limits<std::size_t>::max()));
        }
        if (info[2]->IsTrue()) capture_thrown_value(isolate, context, info[1], completion);
        else {
            completion.outcome = "returned";
            const auto operation_name = safe_text(isolate, operation, 64);
            if (operation_name == "call") completion.return_summary = access_value_summary(isolate, info[1]);
            else if (operation_name == "set" && !completion.argument_summaries.empty())
                completion.value_summary = completion.argument_summaries.front();
            else completion.value_summary = access_value_summary(isolate, info[1]);
        }
        if (auto* trace = access_trace_from_isolate(isolate)) trace->complete(sequence, std::move(completion));
        if (!token->CreateDataProperty(context, 0, v8::BigInt::NewFromUnsigned(isolate, 0)).FromMaybe(false)) return;
    } catch (...) {
        // The original JS catch/finally retains the returned/thrown value.
    }
}

void script_user_operation(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    if (info.Length() >= 2 && info[0]->IsObject() && info[1]->IsString()) {
        v8::String::Utf8Value name(isolate, info[1]);
        v8::Local<v8::Value> result;
        if (*name != nullptr && get_runtime_api_field(isolate, isolate->GetCurrentContext(),
                info[0].As<v8::Object>(), *name).ToLocal(&result)) {
            info.GetReturnValue().Set(result);
        }
        return;
    }
    if (info.Length() < 1 || !info[0]->IsFunction()) return;
    auto* trace = access_trace_from_isolate(isolate);
    if (trace != nullptr && trace->enabled() &&
        info.Length() >= 3 && info[1]->IsObject() && info[2]->IsName() &&
        !runtime_api_field_has_user_accessor(isolate, isolate->GetCurrentContext(),
            info[1].As<v8::Object>(), info[2].As<v8::Name>(),
            info.Length() >= 4 && info[3]->IsTrue(),
            info.Length() >= 5 && info[4]->IsTrue())) {
        auto runtime_origin = access_runtime_scope(isolate);
        v8::Local<v8::Value> result;
        if (info[0].As<v8::Function>()->Call(isolate->GetCurrentContext(),
                v8::Undefined(isolate), 0, nullptr).ToLocal(&result)) info.GetReturnValue().Set(result);
        return;
    }
    if (info.Length() >= 2 && info[1]->IsTrue()) {
        bool user_entry = false;
        if (auto* trace = access_trace_from_isolate(isolate); trace != nullptr && trace->enabled()) {
            auto bridge_origin = access_runtime_bridge_scope(isolate);
            user_entry = trace->user_entry();
        }
        if (!user_entry) {
            auto runtime_origin = access_runtime_scope(isolate);
            v8::Local<v8::Value> result;
            if (info[0].As<v8::Function>()->Call(isolate->GetCurrentContext(),
                    v8::Undefined(isolate), 0, nullptr).ToLocal(&result)) info.GetReturnValue().Set(result);
            return;
        }
    }
    auto user_origin = access_user_scope(isolate);
    v8::Local<v8::Value> result;
    if (info[0].As<v8::Function>()->Call(isolate->GetCurrentContext(),
            v8::Undefined(isolate), 0, nullptr).ToLocal(&result)) {
        info.GetReturnValue().Set(result);
    }
}

void script_data_property(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    if (info.Length() < 8 || !info[0]->IsObject() || info[0]->IsProxy() ||
        (!info[1]->IsString() && !info[1]->IsSymbol()) || !info[6]->IsString() ||
        !info[7]->IsObject() || info[7]->IsProxy()) return;
    auto object = info[0].As<v8::Object>();
    const auto name = safe_text(isolate, info[1], 512);
    auto label = safe_text(isolate, info[6], 128);
    std::string type = "host_object";
    const void* identity = nullptr;
    if (auto* host = host_object_backing(info[7])) {
        identity = host;
        type = host->trace_object_type();
        label = host->trace_label();
    } else identity = native_api_identity(isolate, info[7].As<v8::Object>());
    auto attributes = v8::None;
    if (!info[3]->IsTrue()) attributes = static_cast<v8::PropertyAttribute>(attributes | v8::ReadOnly);
    if (!info[4]->IsTrue()) attributes = static_cast<v8::PropertyAttribute>(attributes | v8::DontEnum);
    if (!info[5]->IsTrue()) attributes = static_cast<v8::PropertyAttribute>(attributes | v8::DontDelete);
    if (info[1]->IsSymbol()) {
        info.GetReturnValue().Set(install_traced_api_named_data_property(isolate, context,
            object, info[1].As<v8::Name>(), info[2], identity, type, label, attributes));
        return;
    }
    info.GetReturnValue().Set(install_traced_api_data_property(isolate, context, object,
        name, info[2], identity, type, label, attributes, info.Length() >= 9 && info[8]->IsTrue()));
}

}  // namespace

void window_post_message_dispatch(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (info.Length() < 1 || !info[0]->IsObject()) {
        return;
    }
    auto* target = static_cast<EventTargetHost*>(
        info.Data().As<v8::External>()->Value(kWindowEventsTag));
    target->dispatch_event(isolate, context, info[0].As<v8::Object>(),
                           context->Global());
}

bool install_script_call_trace_hooks(v8::Isolate* isolate,
    v8::Local<v8::Context> context) {
    for (const auto& [name, callback] : {
            std::pair<const char*, v8::FunctionCallback>{"__iv8HostCallBegin", &script_call_begin},
            std::pair<const char*, v8::FunctionCallback>{"__iv8HostCallEnd", &script_call_end},
            std::pair<const char*, v8::FunctionCallback>{"__iv8HostUserOperation", &script_user_operation}}) {
        v8::Local<v8::String> property;
        v8::Local<v8::Function> function;
        if (!v8::String::NewFromUtf8(isolate, name).ToLocal(&property) ||
            !make_web_method(isolate, context, name,
                callback == &script_call_begin ? 2 : callback == &script_call_end ? 3 : 1,
                callback).ToLocal(&function) ||
            !context->Global()->DefineOwnProperty(context, property, function,
                static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum)).FromMaybe(false)) return false;
    }
    return true;
}

bool make_script_call_trace_hooks(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Function>& begin,
    v8::Local<v8::Function>& end, v8::Local<v8::Function>& data_property,
    v8::Local<v8::Function>& user_operation) {
    return make_web_method(isolate, context, "__iv8HostCallBegin", 2, &script_call_begin).ToLocal(&begin) &&
        make_web_method(isolate, context, "__iv8HostCallEnd", 3, &script_call_end).ToLocal(&end) &&
        make_web_method(isolate, context, "__iv8HostDataProperty", 8, &script_data_property).ToLocal(&data_property) &&
        make_web_method(isolate, context, "__iv8HostUserOperation", 1, &script_user_operation).ToLocal(&user_operation);
}

bool clear_script_call_trace_hooks(v8::Isolate* isolate,
    v8::Local<v8::Context> context) {
    return context->Global()->Delete(context,
        v8::String::NewFromUtf8Literal(isolate, "__iv8HostCallBegin")).FromMaybe(false) &&
        context->Global()->Delete(context,
            v8::String::NewFromUtf8Literal(isolate, "__iv8HostCallEnd")).FromMaybe(false) &&
        context->Global()->Delete(context,
            v8::String::NewFromUtf8Literal(isolate, "__iv8HostUserOperation")).FromMaybe(false);
}

}  // namespace iv8::browser
