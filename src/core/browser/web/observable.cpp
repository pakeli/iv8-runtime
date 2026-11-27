#include "observable.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "iv8/access_origin.h"
#include "iv8/page_state.h"
#include "iv8/web_interface.h"
#include "iv8/web_method.h"
#include "../bom/navigator_parts.h"
#include "../internal/common.h"
#include "../internal/trace_hooks.h"
#include "../internal/web_idl_primitive.h"
#include "../window/window_error_support.h"
#include "../window/window_trace_support.h"
#include "dom_exception.h"

namespace iv8::browser {
namespace {

void report_uncaught_observable_error(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::TryCatch& caught, const char* kind) {
    if (!caught.HasCaught()) return;
    if (!caught.CanContinue()) {
        caught.ReThrow();
        return;
    }
    v8::Local<v8::Value> exc = caught.Exception();
    if (auto* state = state_from_isolate(isolate)) {
        state->record_async_error(isolate, context, caught, kind);
    }
    caught.Reset();
    report_window_exception(isolate, context, exc);
}

bool throw_type_error(v8::Isolate* isolate, const std::string& prefix,
    const std::string& detail) {
    const auto message = prefix + detail;
    v8::Local<v8::String> text;
    if (v8::String::NewFromUtf8(isolate, message.data(), v8::NewStringType::kNormal,
            static_cast<int>(message.size())).ToLocal(&text)) {
        isolate->ThrowException(v8::Exception::TypeError(text));
    }
    return false;
}

v8::Local<v8::Value> rejected_promise(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> reason) {
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver) ||
        !resolver->Reject(context, reason).FromMaybe(false)) {
        return v8::Undefined(isolate);
    }
    return resolver->GetPromise();
}

v8::Local<v8::Value> reject_from_try_catch(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::TryCatch& caught) {
    if (!caught.CanContinue()) {
        caught.ReThrow();
        return v8::Undefined(isolate);
    }
    auto exception = caught.Exception();
    caught.Reset();
    return rejected_promise(isolate, context, exception);
}

bool is_callable_object(v8::Local<v8::Value> value) {
    return value->IsObject() && value.As<v8::Object>()->IsCallable();
}

v8::MaybeLocal<v8::Value> user_get(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> object,
    v8::Local<v8::Value> key) {
    return access_user_operation(isolate, [&] {
        return object->Get(context, key);
    });
}

v8::MaybeLocal<v8::Value> user_call(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> callable,
    v8::Local<v8::Value> receiver, int count = 0,
    v8::Local<v8::Value>* arguments = nullptr) {
    return access_user_operation(isolate, [&] {
        return callable.As<v8::Object>()->CallAsFunction(
            context, receiver, count, arguments);
    });
}

ObservableHost* as_observable(v8::Local<v8::Value> value) noexcept {
    auto* backing = host_object_backing(value);
    if (backing == nullptr || backing->prototype_key() != "Observable") {
        return nullptr;
    }
    return static_cast<ObservableHost*>(backing);
}

bool convert_webidl_ull_wrapping(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> input,
    std::uint64_t* out) {
    double number = 0.0;
    if (!access_user_operation(isolate, [&] {
            return input->NumberValue(context);
        }).To(&number)) {
        return false;
    }
    if (!std::isfinite(number) || number == 0.0) {
        *out = 0;
        return true;
    }
    constexpr double kTwoPow64 = 18446744073709551616.0;
    double int_part = std::trunc(number);
    double mod = std::fmod(int_part, kTwoPow64);
    if (mod < 0.0) mod += kTwoPow64;
    if (mod >= kTwoPow64) mod = 0.0;
    *out = static_cast<std::uint64_t>(mod);
    return true;
}

bool convert_enforce_range_ull(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> input,
    const std::string& prefix, std::uint64_t* out) {
    double number = 0.0;
    if (!access_user_operation(isolate, [&] {
            return input->NumberValue(context);
        }).To(&number)) {
        return false;
    }
    if (!std::isfinite(number)) {
        return throw_type_error(isolate, prefix, "Value is not a finite number.");
    }
    number = std::trunc(number);
    constexpr double kMaxSafeUll = 18446744073709551615.0;
    if (number < 0.0 || number > kMaxSafeUll) {
        return throw_type_error(isolate, prefix,
            "Value is outside the 'unsigned long long' value range.");
    }
    *out = static_cast<std::uint64_t>(number);
    return true;
}

bool parse_subscribe_options(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> input,
    const std::string& prefix, AbortSignalHost** signal_out) {
    *signal_out = nullptr;
    if (input.IsEmpty() || input->IsNullOrUndefined()) return true;
    if (!input->IsObject()) {
        return throw_type_error(isolate, prefix,
            "The provided value is not of type 'SubscribeOptions'.");
    }
    v8::Local<v8::Value> sig_val;
    if (!user_get(isolate, context, input.As<v8::Object>(),
            v8_string(isolate, "signal")).ToLocal(&sig_val)) {
        return false;
    }
    if (!sig_val->IsNullOrUndefined()) {
        auto* sig = as_abort_signal(sig_val);
        if (sig == nullptr) {
            return throw_type_error(isolate, prefix,
                "Failed to read the 'signal' property from 'SubscribeOptions': "
                "Failed to convert value to 'AbortSignal'.");
        }
        *signal_out = sig;
    }
    return true;
}

bool move_constructor_to_end(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> prototype,
    const std::string& interface_name) {
    auto key = v8::String::NewFromUtf8Literal(isolate, "constructor");
    v8::Local<v8::Value> constructor;
    if (!prototype->Get(context, key).ToLocal(&constructor) ||
        !prototype->Delete(context, key).FromMaybe(false)) {
        return false;
    }
    return install_traced_api_data_property(isolate, context, prototype,
        "constructor", constructor, native_api_identity(isolate, prototype),
        "host_object", interface_name + ".prototype", v8::DontEnum);
}

bool reorder_event_target_prototype(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> prototype) {
    const char* ordered[] = {
        "addEventListener", "dispatchEvent", "removeEventListener",
        "when", "constructor"};
    std::vector<std::pair<std::string, v8::Local<v8::Value>>> saved;
    for (const char* raw_name : ordered) {
        auto key = v8_string(isolate, raw_name);
        v8::Local<v8::Value> val;
        if (!prototype->Get(context, key).ToLocal(&val) ||
            !prototype->Delete(context, key).FromMaybe(false)) {
            return false;
        }
        saved.emplace_back(raw_name, val);
    }
    for (const auto& [name, val] : saved) {
        const auto attrs = name == "constructor" ? v8::DontEnum : v8::None;
        if (!install_traced_api_data_property(isolate, context, prototype,
                name, val, native_api_identity(isolate, prototype),
                "host_object", "EventTarget.prototype", attrs)) {
            return false;
        }
    }
    return true;
}

v8::Local<v8::Value> convert_value_to_observable(PageState* owner,
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, const std::string& prefix);

v8::Local<v8::Function> make_internal_fn(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::FunctionCallback cb,
    v8::Local<v8::Value> data) {
    v8::Local<v8::Function> fn;
    if (!v8::Function::New(context, cb, data, 0,
            v8::ConstructorBehavior::kThrow).ToLocal(&fn)) {
        return {};
    }
    return fn;
}

// --- Static AbortSignal callbacks ---

void abort_signal_timeout_fire(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> sig_ext;
    v8::Local<v8::Value> owner_ext;
    if (!data->Get(context, 0).ToLocal(&sig_ext) || !sig_ext->IsExternal() ||
        !data->Get(context, 1).ToLocal(&owner_ext) || !owner_ext->IsExternal()) {
        return;
    }
    auto* sig = static_cast<AbortSignalHost*>(
        sig_ext.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    auto* owner = static_cast<PageState*>(
        owner_ext.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    auto err = make_dom_exception(isolate, context, owner,
        "TimeoutError", "signal timed out");
    sig->abort_with_reason(isolate, context, err);
}

void abort_signal_abort_static(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    watch_record(isolate, "AbortSignal", "abort");
    const auto seq = access_trace_record(isolate,
        native_api_identity(isolate, info.This()->IsObject()
            ? info.This().As<v8::Object>() : context->Global()),
        "host_object", "AbortSignal", "abort", AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
    auto* owner = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    auto sig_uptr = std::make_unique<AbortSignalHost>(owner);
    auto* sig = sig_uptr.get();
    auto obj = owner->adopt_host_object(isolate, context, std::move(sig_uptr));
    sig->bind_wrapper(isolate, obj);
    const v8::Local<v8::Value> reason = info.Length() >= 1
        ? info[0] : v8::Local<v8::Value>(v8::Undefined(isolate));
    sig->initialize_aborted(isolate, context, reason);
    info.GetReturnValue().Set(obj);
}

void abort_signal_any_static(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    watch_record(isolate, "AbortSignal", "any");
    const auto seq = access_trace_record(isolate,
        native_api_identity(isolate, info.This()->IsObject()
            ? info.This().As<v8::Object>() : context->Global()),
        "host_object", "AbortSignal", "any", AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
    const std::string prefix = "Failed to execute 'any' on 'AbortSignal': ";
    if (info.Length() < 1) {
        throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
        return;
    }
    if (!info[0]->IsObject()) {
        throw_type_error(isolate, prefix,
            "The provided value cannot be converted to a sequence.");
        return;
    }
    auto obj = info[0].As<v8::Object>();
    v8::Local<v8::Value> iter_fn;
    if (!user_get(isolate, context, obj, v8::Symbol::GetIterator(isolate))
            .ToLocal(&iter_fn)) {
        return;
    }
    if (!is_callable_object(iter_fn)) {
        throw_type_error(isolate, prefix,
            "The object must have a callable @@iterator property.");
        return;
    }
    v8::Local<v8::Value> iter_val;
    if (!user_call(isolate, context, iter_fn, info[0]).ToLocal(&iter_val) ||
        !iter_val->IsObject()) {
        if (!iter_val.IsEmpty()) {
            throw_type_error(isolate, prefix, "Iterator object must be an object.");
        }
        return;
    }
    auto iter = iter_val.As<v8::Object>();
    v8::Local<v8::Value> next_fn;
    if (!user_get(isolate, context, iter, v8_string(isolate, "next"))
            .ToLocal(&next_fn) ||
        !is_callable_object(next_fn)) {
        if (!next_fn.IsEmpty()) {
            throw_type_error(isolate, prefix, "Expected next() function on iterator.");
        }
        return;
    }
    std::vector<AbortSignalHost*> sources;
    for (;;) {
        v8::Local<v8::Value> step;
        if (!user_call(isolate, context, next_fn, iter).ToLocal(&step) ||
            !step->IsObject()) {
            if (!step.IsEmpty()) {
                throw_type_error(isolate, prefix,
                    "Expected iterator.next() to return an Object.");
            }
            return;
        }
        v8::Local<v8::Value> item;
        if (!user_get(isolate, context, step.As<v8::Object>(), v8_string(isolate, "value"))
                .ToLocal(&item)) {
            return;
        }
        v8::Local<v8::Value> done;
        if (!user_get(isolate, context, step.As<v8::Object>(), v8_string(isolate, "done"))
                .ToLocal(&done)) {
            return;
        }
        if (done->BooleanValue(isolate)) break;
        auto* sig = as_abort_signal(item);
        if (sig == nullptr) {
            throw_type_error(isolate, prefix,
                "Failed to convert value to 'AbortSignal'.");
            return;
        }
        sources.push_back(sig);
    }
    auto* owner = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    auto out_uptr = std::make_unique<AbortSignalHost>(owner);
    auto* out_sig = out_uptr.get();
    auto out_obj = owner->adopt_host_object(isolate, context, std::move(out_uptr));
    out_sig->bind_wrapper(isolate, out_obj);
    for (AbortSignalHost* src : sources) {
        if (src->aborted()) {
            out_sig->initialize_aborted(isolate, context, src->reason_value(isolate));
            info.GetReturnValue().Set(out_obj);
            return;
        }
    }
    for (AbortSignalHost* src : sources) {
        src->add_abort_algorithm([out_sig, src](v8::Isolate* iso, v8::Local<v8::Context> ctx) {
            out_sig->abort_with_reason(iso, ctx, src->reason_value(iso));
        });
    }
    info.GetReturnValue().Set(out_obj);
}

void abort_signal_timeout_static(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    watch_record(isolate, "AbortSignal", "timeout");
    const auto seq = access_trace_record(isolate,
        native_api_identity(isolate, info.This()->IsObject()
            ? info.This().As<v8::Object>() : context->Global()),
        "host_object", "AbortSignal", "timeout", AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
    const std::string prefix = "Failed to execute 'timeout' on 'AbortSignal': ";
    if (info.Length() < 1) {
        throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
        return;
    }
    std::uint64_t ms = 0;
    if (!convert_enforce_range_ull(isolate, context, info[0], prefix, &ms)) {
        return;
    }
    auto* owner = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    auto out_uptr = std::make_unique<AbortSignalHost>(owner);
    auto* out_sig = out_uptr.get();
    auto out_obj = owner->adopt_host_object(isolate, context, std::move(out_uptr));
    out_sig->bind_wrapper(isolate, out_obj);
    if (auto* state = state_from_isolate(isolate)) {
        auto data = v8::Array::New(isolate, 2);
        (void)data->CreateDataProperty(context, 0,
            v8::External::New(isolate, out_sig, v8::kExternalPointerTypeTagDefault));
        (void)data->CreateDataProperty(context, 1,
            v8::External::New(isolate, owner, v8::kExternalPointerTypeTagDefault));
        auto fn = make_internal_fn(isolate, context, &abort_signal_timeout_fire, data);
        const auto clamped = ms > static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())
            ? std::numeric_limits<std::int32_t>::max()
            : static_cast<std::int32_t>(ms);
        if (!fn.IsEmpty()) {
            (void)state->register_timer(fn, clamped, false, false);
        }
    }
    info.GetReturnValue().Set(out_obj);
}

// --- Observable.from & operator internal callbacks ---

void observable_from_static(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    watch_record(isolate, "Observable", "from");
    const auto seq = access_trace_record(isolate,
        native_api_identity(isolate, info.This()->IsObject()
            ? info.This().As<v8::Object>() : context->Global()),
        "host_object", "Observable", "from", AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
    const std::string prefix = "Failed to execute 'from' on 'Observable': ";
    if (info.Length() < 1) {
        throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
        return;
    }
    auto* owner = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    auto converted = convert_value_to_observable(owner, isolate, context, info[0], prefix);
    if (!converted.IsEmpty()) {
        info.GetReturnValue().Set(converted);
    }
}

void promise_from_fulfilled(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto* sub = static_cast<SubscriberHost*>(
        info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    if (!sub->active()) return;
    sub->emit_next(isolate, context,
        info.Length() >= 1 ? info[0] : v8::Local<v8::Value>(v8::Undefined(isolate)));
    sub->emit_complete(isolate, context);
}

void promise_from_rejected(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto* sub = static_cast<SubscriberHost*>(
        info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    if (!sub->active()) return;
    sub->emit_error(isolate, context,
        info.Length() >= 1 ? info[0] : v8::Local<v8::Value>(v8::Undefined(isolate)));
}

struct AsyncIteratorStateHost : public HostObject {
    v8::Global<v8::Object> iter;
    v8::Global<v8::Value> next_fn;
    SubscriberHost* sub = nullptr;
    bool settled = false;

    std::string global_name() const override { return {}; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override { return {}; }
    void release_v8_handles() noexcept override {
        iter.Reset();
        next_fn.Reset();
        sub = nullptr;
        settled = true;
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&) override {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        return v8::Undefined(isolate);
    }
};

void async_iter_step_fulfilled(const v8::FunctionCallbackInfo<v8::Value>& info);
void async_iter_step_rejected(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    if (!info.Data()->IsExternal()) return;
    auto* state = static_cast<AsyncIteratorStateHost*>(
        info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    if (state == nullptr || state->sub == nullptr || !state->sub->active()) return;
    state->settled = true;
    state->sub->emit_error(isolate, context,
        info.Length() >= 1 ? info[0] : v8::Local<v8::Value>(v8::Undefined(isolate)));
}

void pump_async_iterator(v8::Isolate* isolate, v8::Local<v8::Context> context,
    AsyncIteratorStateHost* state) {
    if (state == nullptr || state->settled || state->sub == nullptr ||
        !state->sub->active() || state->iter.IsEmpty() || state->next_fn.IsEmpty()) {
        return;
    }
    auto* sub = state->sub;
    auto iter_val = state->iter.Get(isolate);
    auto next_fn = state->next_fn.Get(isolate);
    v8::TryCatch caught(isolate);
    v8::Local<v8::Value> step_promise_val;
    if (!user_call(isolate, context, next_fn, iter_val).ToLocal(&step_promise_val)) {
        if (!caught.CanContinue()) { caught.ReThrow(); return; }
        auto exc = caught.Exception();
        caught.Reset();
        state->settled = true;
        sub->emit_error(isolate, context, exc);
        return;
    }
    v8::Local<v8::Promise::Resolver> wrap_resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&wrap_resolver) ||
        !wrap_resolver->Resolve(context, step_promise_val).FromMaybe(false)) {
        return;
    }
    auto ext = v8::External::New(isolate, state, v8::kExternalPointerTypeTagDefault);
    auto on_ful = make_internal_fn(isolate, context, &async_iter_step_fulfilled, ext);
    auto on_rej = make_internal_fn(isolate, context, &async_iter_step_rejected, ext);
    if (!on_ful.IsEmpty() && !on_rej.IsEmpty()) {
        (void)wrap_resolver->GetPromise()->Then(context, on_ful, on_rej);
    }
}

void async_iter_step_fulfilled(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    if (!info.Data()->IsExternal()) return;
    auto* state = static_cast<AsyncIteratorStateHost*>(
        info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    if (state == nullptr || state->sub == nullptr || !state->sub->active()) return;
    auto* sub = state->sub;
    v8::TryCatch caught(isolate);
    if (info.Length() < 1 || !info[0]->IsObject()) {
        throw_type_error(isolate, "", "Iterator result is not an object.");
        auto exc = caught.Exception();
        caught.Reset();
        state->settled = true;
        sub->emit_error(isolate, context, exc);
        return;
    }
    auto record = info[0].As<v8::Object>();
    v8::Local<v8::Value> item;
    v8::Local<v8::Value> done;
    if (!user_get(isolate, context, record, v8_string(isolate, "value")).ToLocal(&item) ||
        !user_get(isolate, context, record, v8_string(isolate, "done")).ToLocal(&done)) {
        if (!caught.CanContinue()) { caught.ReThrow(); return; }
        auto exc = caught.Exception();
        caught.Reset();
        state->settled = true;
        sub->emit_error(isolate, context, exc);
        return;
    }
    if (done->BooleanValue(isolate)) {
        state->settled = true;
        sub->emit_complete(isolate, context);
        return;
    }
    sub->emit_next(isolate, context, item);
    if (sub->active()) {
        pump_async_iterator(isolate, context, state);
    }
}

v8::Local<v8::Value> convert_value_to_observable(PageState* owner,
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, const std::string& prefix) {
    if (input.IsEmpty() || !input->IsObject()) {
        throw_type_error(isolate, prefix,
            "Cannot convert value to an Observable. Input value must be an Observable, async iterable, iterable, or Promise.");
        return {};
    }
    if (as_observable(input) != nullptr) {
        return input;
    }
    auto obj = input.As<v8::Object>();
    // 1. Async iterable check.
    v8::Local<v8::Value> async_iter_method;
    if (!user_get(isolate, context, obj, v8::Symbol::GetAsyncIterator(isolate))
            .ToLocal(&async_iter_method)) {
        return {};
    }
    if (!async_iter_method->IsNullOrUndefined()) {
        if (!is_callable_object(async_iter_method)) {
            throw_type_error(isolate, prefix, "@@asyncIterator must be a callable.");
            return {};
        }
        auto obs_uptr = std::make_unique<ObservableHost>(owner,
            [owner](v8::Isolate* iso, v8::Local<v8::Context> ctx,
               ObservableHost* self_obs, SubscriberHost* sub, v8::Local<v8::Object>) {
                if (!sub->active()) return;
                auto target = self_obs->captured_value(iso, 0);
                auto method = self_obs->captured_value(iso, 1);
                v8::TryCatch caught(iso);
                v8::Local<v8::Value> iter_val;
                if (!user_call(iso, ctx, method, target).ToLocal(&iter_val) ||
                    !iter_val->IsObject()) {
                    if (iter_val.IsEmpty()) {
                        if (!caught.CanContinue()) { caught.ReThrow(); return; }
                        auto exc = caught.Exception();
                        caught.Reset();
                        sub->emit_error(iso, ctx, exc);
                    } else {
                        throw_type_error(iso, "", "Iterator object must be an object.");
                        auto exc = caught.Exception();
                        caught.Reset();
                        sub->emit_error(iso, ctx, exc);
                    }
                    return;
                }
                v8::Local<v8::Value> next_fn;
                if (!user_get(iso, ctx, iter_val.As<v8::Object>(),
                        v8_string(iso, "next")).ToLocal(&next_fn) ||
                    !is_callable_object(next_fn)) {
                    if (next_fn.IsEmpty()) {
                        if (!caught.CanContinue()) { caught.ReThrow(); return; }
                    } else {
                        throw_type_error(iso, "", "Expected next() function on iterator.");
                    }
                    auto exc = caught.Exception();
                    caught.Reset();
                    sub->emit_error(iso, ctx, exc);
                    return;
                }
                auto state_uptr = std::make_unique<AsyncIteratorStateHost>();
                state_uptr->iter.Reset(iso, iter_val.As<v8::Object>());
                state_uptr->next_fn.Reset(iso, next_fn);
                state_uptr->sub = sub;
                auto* state_ptr = state_uptr.get();
                (void)owner->adopt_host_object(iso, ctx, std::move(state_uptr));
                sub->add_native_teardown([state_ptr, sub](v8::Isolate* i, v8::Local<v8::Context> c) {
                    if (state_ptr->settled || state_ptr->iter.IsEmpty()) return;
                    state_ptr->settled = true;
                    auto iter_obj = state_ptr->iter.Get(i);
                    auto* sig = as_abort_signal(sub->ensure_signal(i, c));
                    v8::Local<v8::Value> reason = sig != nullptr
                        ? sig->reason_value(i)
                        : v8::Local<v8::Value>(v8::Undefined(i));
                    v8::TryCatch ret_caught(i);
                    v8::Local<v8::Value> ret_fn;
                    if (!user_get(i, c, iter_obj, v8_string(i, "return")).ToLocal(&ret_fn)) {
                        if (!ret_caught.CanContinue()) { ret_caught.ReThrow(); return; }
                        auto exc = ret_caught.Exception();
                        ret_caught.Reset();
                        (void)rejected_promise(i, c, exc);
                        return;
                    }
                    if (ret_fn->IsNullOrUndefined()) return;
                    if (!is_callable_object(ret_fn)) {
                        throw_type_error(i, "", "Iterator return is not callable.");
                        auto exc = ret_caught.Exception();
                        ret_caught.Reset();
                        (void)rejected_promise(i, c, exc);
                        return;
                    }
                    v8::Local<v8::Value> argv[] = {reason};
                    v8::Local<v8::Value> ret_res;
                    if (!user_call(i, c, ret_fn, iter_obj, 1, argv).ToLocal(&ret_res)) {
                        if (!ret_caught.CanContinue()) { ret_caught.ReThrow(); return; }
                        auto exc = ret_caught.Exception();
                        ret_caught.Reset();
                        (void)rejected_promise(i, c, exc);
                        return;
                    }
                    v8::Local<v8::Promise::Resolver> wrap;
                    if (v8::Promise::Resolver::New(c).ToLocal(&wrap)) {
                        (void)wrap->Resolve(c, ret_res);
                    }
                });
                pump_async_iterator(iso, ctx, state_ptr);
            });
        obs_uptr->capture_value(isolate, input);
        obs_uptr->capture_value(isolate, async_iter_method);
        return owner->adopt_host_object(isolate, context, std::move(obs_uptr));
    }

    // 2. Sync iterable check.
    v8::Local<v8::Value> sync_iter_method;
    if (!user_get(isolate, context, obj, v8::Symbol::GetIterator(isolate))
            .ToLocal(&sync_iter_method)) {
        return {};
    }
    if (!sync_iter_method->IsNullOrUndefined()) {
        if (!is_callable_object(sync_iter_method)) {
            throw_type_error(isolate, prefix, "@@iterator must be a callable.");
            return {};
        }
        auto obs_uptr = std::make_unique<ObservableHost>(owner,
            [](v8::Isolate* iso, v8::Local<v8::Context> ctx,
               ObservableHost* self_obs, SubscriberHost* sub, v8::Local<v8::Object>) {
                if (!sub->active()) return;
                auto target = self_obs->captured_value(iso, 0);
                auto method = self_obs->captured_value(iso, 1);
                v8::TryCatch caught(iso);
                v8::Local<v8::Value> iter_val;
                if (!user_call(iso, ctx, method, target).ToLocal(&iter_val) ||
                    !iter_val->IsObject()) {
                    if (!iter_val.IsEmpty()) {
                        throw_type_error(iso, "", "Iterator object must be an object.");
                    }
                    if (!caught.CanContinue()) { caught.ReThrow(); return; }
                    auto exc = caught.Exception();
                    caught.Reset();
                    sub->emit_error(iso, ctx, exc);
                    return;
                }
                auto iter = iter_val.As<v8::Object>();
                v8::Local<v8::Value> next_fn;
                if (!user_get(iso, ctx, iter, v8_string(iso, "next")).ToLocal(&next_fn) ||
                    !is_callable_object(next_fn)) {
                    if (!next_fn.IsEmpty()) {
                        throw_type_error(iso, "", "Expected next() function on iterator.");
                    }
                    if (!caught.CanContinue()) { caught.ReThrow(); return; }
                    auto exc = caught.Exception();
                    caught.Reset();
                    sub->emit_error(iso, ctx, exc);
                    return;
                }
                while (sub->active()) {
                    v8::Local<v8::Value> step;
                    if (!user_call(iso, ctx, next_fn, iter).ToLocal(&step) ||
                        !step->IsObject()) {
                        if (!step.IsEmpty()) {
                            throw_type_error(iso, "",
                                "Expected iterator.next() to return an Object.");
                        }
                        if (!caught.CanContinue()) { caught.ReThrow(); return; }
                        auto exc = caught.Exception();
                        caught.Reset();
                        sub->emit_error(iso, ctx, exc);
                        return;
                    }
                    v8::Local<v8::Value> item;
                    v8::Local<v8::Value> done;
                    if (!user_get(iso, ctx, step.As<v8::Object>(),
                            v8_string(iso, "value")).ToLocal(&item) ||
                        !user_get(iso, ctx, step.As<v8::Object>(),
                            v8_string(iso, "done")).ToLocal(&done)) {
                        if (!caught.CanContinue()) { caught.ReThrow(); return; }
                        auto exc = caught.Exception();
                        caught.Reset();
                        sub->emit_error(iso, ctx, exc);
                        return;
                    }
                    if (done->BooleanValue(iso)) {
                        sub->emit_complete(iso, ctx);
                        return;
                    }
                    sub->emit_next(iso, ctx, item);
                    if (!sub->active()) {
                        v8::Local<v8::Value> ret_fn;
                        if (user_get(iso, ctx, iter, v8_string(iso, "return")).ToLocal(&ret_fn) &&
                            is_callable_object(ret_fn)) {
                            (void)user_call(iso, ctx, ret_fn, iter);
                        }
                        if (caught.HasCaught()) caught.Reset();
                        return;
                    }
                }
            });
        obs_uptr->capture_value(isolate, input);
        obs_uptr->capture_value(isolate, sync_iter_method);
        return owner->adopt_host_object(isolate, context, std::move(obs_uptr));
    }

    // 3. Promise check.
    if (input->IsPromise()) {
        auto obs_uptr = std::make_unique<ObservableHost>(owner,
            [](v8::Isolate* iso, v8::Local<v8::Context> ctx,
               ObservableHost* self_obs, SubscriberHost* sub, v8::Local<v8::Object>) {
                if (!sub->active()) return;
                auto prom = self_obs->captured_value(iso, 0).As<v8::Promise>();
                auto ext = v8::External::New(iso, sub, v8::kExternalPointerTypeTagDefault);
                auto on_ful = make_internal_fn(iso, ctx, &promise_from_fulfilled, ext);
                auto on_rej = make_internal_fn(iso, ctx, &promise_from_rejected, ext);
                if (!on_ful.IsEmpty() && !on_rej.IsEmpty()) {
                    (void)prom->Then(ctx, on_ful, on_rej);
                }
            });
        obs_uptr->capture_value(isolate, input);
        return owner->adopt_host_object(isolate, context, std::move(obs_uptr));
    }

    throw_type_error(isolate, prefix,
        "Cannot convert value to an Observable. Input value must be an Observable, async iterable, iterable, or Promise.");
    return {};
}

// EventTarget.prototype.when event forwarding callback.
void when_listener_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto* sub = static_cast<SubscriberHost*>(
        info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    if (!sub->active()) return;
    sub->emit_next(isolate, context,
        info.Length() >= 1 ? info[0] : v8::Local<v8::Value>(v8::Undefined(isolate)));
}

}  // namespace

AbortSignalHost* as_abort_signal(v8::Local<v8::Value> value) noexcept {
    auto* backing = host_object_backing(value);
    if (backing == nullptr ||
        !web_interface_inherits(backing->prototype_key(), "AbortSignal")) {
        return nullptr;
    }
    return static_cast<AbortSignalHost*>(backing);
}

// --- AbortSignal & AbortController ---

void AbortSignalHost::initialize_aborted(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> reason) {
    aborted_ = true;
    if (reason.IsEmpty() || reason->IsUndefined()) {
        reason = make_dom_exception(isolate, context, owner_,
            "AbortError", "signal is aborted without reason");
    }
    reason_.Reset(isolate, reason);
}

void AbortSignalHost::abort_with_reason(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> reason) {
    if (aborted_) return;
    initialize_aborted(isolate, context, reason);
    auto pending = std::move(algorithms_);
    algorithms_.clear();
    for (auto& entry : pending) {
        if (entry.run) entry.run(isolate, context);
    }
    if (!wrapper_.IsEmpty()) {
        dispatch_native(isolate, context, "abort", wrapper_.Get(isolate));
    }
}

std::uint64_t AbortSignalHost::add_abort_algorithm(
    std::function<void(v8::Isolate*, v8::Local<v8::Context>)> algorithm) {
    if (aborted_) return 0;
    const std::uint64_t id = next_algorithm_id_++;
    algorithms_.push_back({id, std::move(algorithm)});
    return id;
}

void AbortSignalHost::remove_abort_algorithm(std::uint64_t id) {
    if (id == 0) return;
    algorithms_.erase(
        std::remove_if(algorithms_.begin(), algorithms_.end(),
            [id](const AbortAlgorithmEntry& entry) { return entry.id == id; }),
        algorithms_.end());
}

v8::Local<v8::Value> AbortSignalHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name) {
    if (name == "aborted") return v8::Boolean::New(isolate, aborted_);
    if (name == "reason") return reason_value(isolate);
    if (name == "onabort") return get_event_handler(isolate, name);
    return v8::Undefined(isolate);
}

void AbortSignalHost::set_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name, v8::Local<v8::Value> value) {
    if (name == "onabort") {
        set_event_handler(isolate, name, value);
    }
}

v8::Local<v8::Value> AbortSignalHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (!validate_bom_event_dispatch(isolate, context, name, arguments)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> event_result;
    if (handle_event_method(isolate, context, name, arguments, event_result)) {
        return event_result;
    }
    if (name == "throwIfAborted") {
        if (aborted_) {
            isolate->ThrowException(reason_value(isolate));
        }
        return v8::Undefined(isolate);
    }
    return v8::Undefined(isolate);
}

std::unique_ptr<AbortSignalHost> AbortControllerHost::create_signal_host() {
    return std::make_unique<AbortSignalHost>(owner_);
}

v8::Local<v8::Object> AbortControllerHost::ensure_signal(v8::Isolate* isolate,
    v8::Local<v8::Context> context) {
    if (signal_object_.IsEmpty()) {
        auto sig_uptr = create_signal_host();
        signal_host_ = sig_uptr.get();
        auto obj = owner_->adopt_host_object(isolate, context, std::move(sig_uptr));
        signal_host_->bind_wrapper(isolate, obj);
        signal_object_.Reset(isolate, obj);
    }
    return signal_object_.Get(isolate);
}

v8::Local<v8::Value> AbortControllerHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name) {
    if (name == "signal") {
        return ensure_signal(isolate, context);
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> AbortControllerHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (name == "abort") {
        (void)ensure_signal(isolate, context);
        if (signal_host_ != nullptr) {
            const v8::Local<v8::Value> reason = arguments.Length() >= 1
                ? arguments[0] : v8::Local<v8::Value>(v8::Undefined(isolate));
            signal_host_->abort_with_reason(isolate, context, reason);
        }
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> construct_abort_controller(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner, v8::Local<v8::Array>) {
    auto ctrl_uptr = std::make_unique<AbortControllerHost>(owner);
    auto* ctrl = ctrl_uptr.get();
    auto obj = owner->adopt_host_object(isolate, context, std::move(ctrl_uptr));
    (void)ctrl->ensure_signal(isolate, context);
    return obj;
}

// --- Subscriber ---

v8::Local<v8::Object> SubscriberHost::ensure_signal(v8::Isolate* isolate,
    v8::Local<v8::Context> context) {
    if (signal_object_.IsEmpty()) {
        auto sig_uptr = std::make_unique<AbortSignalHost>(owner_);
        signal_host_ = sig_uptr.get();
        auto obj = owner_->adopt_host_object(isolate, context, std::move(sig_uptr));
        signal_host_->bind_wrapper(isolate, obj);
        signal_object_.Reset(isolate, obj);
    }
    return signal_object_.Get(isolate);
}

void SubscriberHost::add_observer(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Function> next_cb,
    v8::Local<v8::Function> error_cb, v8::Local<v8::Function> complete_cb,
    AbortSignalHost* external_signal) {
    (void)ensure_signal(isolate, context);
    if (!active_) return;
    if (external_signal != nullptr && external_signal->aborted()) {
        bool any_active = false;
        for (const auto& obs : observers_) {
            if (!obs.detached) {
                any_active = true;
                break;
            }
        }
        if (!any_active) {
            close_subscription(isolate, context, external_signal->reason_value(isolate));
        }
        return;
    }
    const std::size_t idx = observers_.size();
    AttachedObserver entry;
    if (!next_cb.IsEmpty()) entry.next_cb.Reset(isolate, next_cb);
    if (!error_cb.IsEmpty()) entry.error_cb.Reset(isolate, error_cb);
    if (!complete_cb.IsEmpty()) entry.complete_cb.Reset(isolate, complete_cb);
    observers_.push_back(std::move(entry));
    if (external_signal != nullptr) {
        external_signal->add_abort_algorithm(
            [this, idx, external_signal](v8::Isolate* iso, v8::Local<v8::Context> ctx) {
                detach_observer(iso, ctx, idx, external_signal->reason_value(iso));
            });
    }
}

void SubscriberHost::detach_observer(v8::Isolate* isolate,
    v8::Local<v8::Context> context, std::size_t index,
    v8::Local<v8::Value> abort_reason) {
    if (!active_ || index >= observers_.size() || observers_[index].detached) {
        return;
    }
    observers_[index].detached = true;
    observers_[index].next_cb.Reset();
    observers_[index].error_cb.Reset();
    observers_[index].complete_cb.Reset();
    for (const auto& obs : observers_) {
        if (!obs.detached) return;
    }
    close_subscription(isolate, context, abort_reason);
}

void SubscriberHost::add_native_teardown(
    std::function<void(v8::Isolate*, v8::Local<v8::Context>)> teardown) {
    if (!active_) return;
    native_teardowns_.push_back(std::move(teardown));
}

void SubscriberHost::close_subscription(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> abort_reason) {
    if (!active_) return;
    active_ = false;
    if (owner_observable_ != nullptr) {
        owner_observable_->clear_active_subscriber(this);
        owner_observable_ = nullptr;
    }
    for (auto& obs : observers_) {
        obs.detached = true;
        obs.next_cb.Reset();
        obs.error_cb.Reset();
        obs.complete_cb.Reset();
    }
    observers_.clear();
    (void)ensure_signal(isolate, context);
    if (signal_host_ != nullptr) {
        signal_host_->abort_with_reason(isolate, context, abort_reason);
    }
    auto native_tds = std::move(native_teardowns_);
    native_teardowns_.clear();
    for (auto it = native_tds.rbegin(); it != native_tds.rend(); ++it) {
        if (*it) (*it)(isolate, context);
    }
    std::vector<v8::Local<v8::Function>> js_tds;
    js_tds.reserve(js_teardowns_.size());
    for (auto it = js_teardowns_.rbegin(); it != js_teardowns_.rend(); ++it) {
        if (!it->IsEmpty()) js_tds.push_back(it->Get(isolate));
        it->Reset();
    }
    js_teardowns_.clear();
    for (const auto& fn : js_tds) {
        v8::TryCatch caught(isolate);
        (void)user_call(isolate, context, fn, v8::Undefined(isolate));
        if (caught.HasCaught()) {
            report_uncaught_observable_error(isolate, context, caught, "observable_teardown");
            if (caught.HasCaught()) return;
        }
    }
}

void SubscriberHost::emit_next(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> value) {
    if (!active_) return;
    const std::size_t count = observers_.size();
    v8::Local<v8::Value> argv[] = {value};
    for (std::size_t idx = 0; idx < count; ++idx) {
        if (!active_) break;
        if (idx >= observers_.size() || observers_[idx].detached ||
            observers_[idx].next_cb.IsEmpty()) {
            continue;
        }
        auto cb = observers_[idx].next_cb.Get(isolate);
        v8::TryCatch caught(isolate);
        (void)user_call(isolate, context, cb, v8::Undefined(isolate), 1, argv);
        if (caught.HasCaught()) {
            report_uncaught_observable_error(isolate, context, caught, "observable_next");
            if (caught.HasCaught()) return;
        }
    }
}

void SubscriberHost::emit_error(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> error) {
    if (!active_) {
        report_window_exception(isolate, context, error);
        return;
    }
    std::vector<std::pair<bool, v8::Local<v8::Function>>> handlers;
    for (const auto& obs : observers_) {
        if (obs.detached) continue;
        if (!obs.error_cb.IsEmpty()) {
            handlers.emplace_back(true, obs.error_cb.Get(isolate));
        } else {
            handlers.emplace_back(false, v8::Local<v8::Function>());
        }
    }
    close_subscription(isolate, context, error);
    if (handlers.empty()) {
        report_window_exception(isolate, context, error);
        return;
    }
    v8::Local<v8::Value> argv[] = {error};
    for (const auto& [has_fn, err_fn] : handlers) {
        if (has_fn && !err_fn.IsEmpty()) {
            v8::TryCatch caught(isolate);
            (void)user_call(isolate, context, err_fn, v8::Undefined(isolate), 1, argv);
            if (caught.HasCaught()) {
                report_uncaught_observable_error(isolate, context, caught, "observable_error");
                if (caught.HasCaught()) return;
            }
        } else {
            report_window_exception(isolate, context, error);
        }
    }
}

void SubscriberHost::emit_complete(v8::Isolate* isolate,
    v8::Local<v8::Context> context) {
    if (!active_) return;
    std::vector<v8::Local<v8::Function>> handlers;
    for (const auto& obs : observers_) {
        if (!obs.detached && !obs.complete_cb.IsEmpty()) {
            handlers.push_back(obs.complete_cb.Get(isolate));
        }
    }
    close_subscription(isolate, context, v8::Undefined(isolate));
    for (const auto& comp_fn : handlers) {
        v8::TryCatch caught(isolate);
        (void)user_call(isolate, context, comp_fn, v8::Undefined(isolate));
        if (caught.HasCaught()) {
            report_uncaught_observable_error(isolate, context, caught, "observable_complete");
            if (caught.HasCaught()) return;
        }
    }
}

v8::Local<v8::Value> SubscriberHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name) {
    if (name == "active") return v8::Boolean::New(isolate, active_);
    if (name == "signal") return ensure_signal(isolate, context);
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> SubscriberHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (name == "addTeardown") {
        const std::string prefix =
            "Failed to execute 'addTeardown' on 'Subscriber': ";
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix,
                "1 argument required, but only 0 present.");
            return v8::Undefined(isolate);
        }
        if (!arguments[0]->IsFunction()) {
            throw_type_error(isolate, prefix,
                "parameter 1 is not of type 'Function'.");
            return v8::Undefined(isolate);
        }
        auto fn = arguments[0].As<v8::Function>();
        if (active_) {
            js_teardowns_.emplace_back(isolate, fn);
        } else {
            v8::TryCatch caught(isolate);
            (void)user_call(isolate, context, fn, v8::Undefined(isolate));
            if (caught.HasCaught()) {
                report_uncaught_observable_error(isolate, context, caught, "observable_teardown");
            }
        }
        return v8::Undefined(isolate);
    }
    if (name == "next") {
        if (arguments.Length() < 1) {
            throw_type_error(isolate,
                "Failed to execute 'next' on 'Subscriber': ",
                "1 argument required, but only 0 present.");
            return v8::Undefined(isolate);
        }
        emit_next(isolate, context, arguments[0]);
        return v8::Undefined(isolate);
    }
    if (name == "error") {
        if (arguments.Length() < 1) {
            throw_type_error(isolate,
                "Failed to execute 'error' on 'Subscriber': ",
                "1 argument required, but only 0 present.");
            return v8::Undefined(isolate);
        }
        emit_error(isolate, context, arguments[0]);
        return v8::Undefined(isolate);
    }
    if (name == "complete") {
        emit_complete(isolate, context);
        return v8::Undefined(isolate);
    }
    return v8::Undefined(isolate);
}

// --- Observable construction, subscription, and operators ---

v8::Local<v8::Value> construct_observable(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    v8::Local<v8::Array> arguments) {
    const std::string prefix = "Failed to construct 'Observable': ";
    if (arguments->Length() < 1) {
        throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> arg0;
    if (!arguments->Get(context, 0).ToLocal(&arg0)) return v8::Undefined(isolate);
    if (!arg0->IsFunction()) {
        throw_type_error(isolate, prefix, "parameter 1 is not of type 'Function'.");
        return v8::Undefined(isolate);
    }
    return owner->adopt_host_object(isolate, context,
        std::make_unique<ObservableHost>(owner, isolate, arg0.As<v8::Function>()));
}

void ObservableHost::subscribe_internal(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Function> next_cb,
    v8::Local<v8::Function> error_cb, v8::Local<v8::Function> complete_cb,
    AbortSignalHost* external_signal) {
    if (active_subscriber_ != nullptr && active_subscriber_->active()) {
        if (external_signal != nullptr && external_signal->aborted()) {
            return;
        }
        active_subscriber_->add_observer(isolate, context, next_cb, error_cb,
            complete_cb, external_signal);
        return;
    }
    auto sub_uptr = std::make_unique<SubscriberHost>(owner_);
    auto* sub = sub_uptr.get();
    auto sub_obj = owner_->adopt_host_object(isolate, context, std::move(sub_uptr));
    (void)sub->ensure_signal(isolate, context);
    if (external_signal != nullptr && external_signal->aborted()) {
        sub->close_subscription(isolate, context, external_signal->reason_value(isolate));
    } else {
        active_subscriber_ = sub;
        sub->set_owner_observable(this);
        sub->add_observer(isolate, context, next_cb, error_cb, complete_cb, external_signal);
    }
    if (!js_callback_.IsEmpty()) {
        auto cb = js_callback_.Get(isolate);
        v8::Local<v8::Value> argv[] = {sub_obj};
        v8::TryCatch caught(isolate);
        (void)user_call(isolate, context, cb, v8::Undefined(isolate), 1, argv);
        if (caught.HasCaught()) {
            if (!caught.CanContinue()) { caught.ReThrow(); return; }
            if (!sub->active()) {
                report_uncaught_observable_error(isolate, context, caught, "observable_subscribe");
            } else {
                auto exc = caught.Exception();
                caught.Reset();
                sub->emit_error(isolate, context, exc);
            }
        }
        return;
    }
    if (native_subscribe_) {
        native_subscribe_(isolate, context, this, sub, sub_obj);
    }
}

namespace {

// Helper to create JS observer callbacks from C++ lambdas stored in an owned
// SubscriberBridge HostObject so zero v8::Globals leak outside PageState.
struct ObserverBridgeHost : public HostObject {
    std::function<void(v8::Isolate*, v8::Local<v8::Context>, v8::Local<v8::Value>)> on_next;
    std::function<void(v8::Isolate*, v8::Local<v8::Context>, v8::Local<v8::Value>)> on_error;
    std::function<void(v8::Isolate*, v8::Local<v8::Context>)> on_complete;
    std::function<void(v8::Isolate*, v8::Local<v8::Context>)> pump;
    std::vector<v8::Global<v8::Value>> slots;
    std::deque<v8::Global<v8::Value>> queue;
    std::uint64_t counter = 0;
    bool flag = false;
    AbortSignalHost* active_inner_sig = nullptr;
    bool inner_busy = false;
    bool in_inner_subscribe = false;
    bool outer_completed = false;

    std::string global_name() const override { return {}; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override { return {}; }
    void release_v8_handles() noexcept override {
        for (auto& s : slots) s.Reset();
        slots.clear();
        for (auto& q : queue) q.Reset();
        queue.clear();
        active_inner_sig = nullptr;
        on_next = nullptr;
        on_error = nullptr;
        on_complete = nullptr;
        pump = nullptr;
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&) override {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        return v8::Undefined(isolate);
    }
};

void bridge_next_cb(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto* bridge = static_cast<ObserverBridgeHost*>(
        info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    if (bridge->on_next) {
        bridge->on_next(isolate, context,
            info.Length() >= 1 ? info[0] : v8::Local<v8::Value>(v8::Undefined(isolate)));
    }
}

void bridge_error_cb(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto* bridge = static_cast<ObserverBridgeHost*>(
        info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    if (bridge->on_error) {
        bridge->on_error(isolate, context,
            info.Length() >= 1 ? info[0] : v8::Local<v8::Value>(v8::Undefined(isolate)));
    }
}

void bridge_complete_cb(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto* bridge = static_cast<ObserverBridgeHost*>(
        info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    if (bridge->on_complete) {
        bridge->on_complete(isolate, context);
    }
}

void subscribe_with_bridge(PageState* owner, v8::Isolate* isolate,
    v8::Local<v8::Context> context, ObservableHost* source,
    std::unique_ptr<ObserverBridgeHost> bridge_uptr, AbortSignalHost* sig) {
    auto* bridge = bridge_uptr.get();
    (void)owner->adopt_host_object(isolate, context, std::move(bridge_uptr));
    auto ext = v8::External::New(isolate, bridge, v8::kExternalPointerTypeTagDefault);
    v8::Local<v8::Function> next_fn = bridge->on_next
        ? make_internal_fn(isolate, context, &bridge_next_cb, ext) : v8::Local<v8::Function>{};
    v8::Local<v8::Function> err_fn = bridge->on_error
        ? make_internal_fn(isolate, context, &bridge_error_cb, ext) : v8::Local<v8::Function>{};
    v8::Local<v8::Function> comp_fn = bridge->on_complete
        ? make_internal_fn(isolate, context, &bridge_complete_cb, ext) : v8::Local<v8::Function>{};
    source->subscribe_internal(isolate, context, next_fn, err_fn, comp_fn, sig);
}

AbortSignalHost* subscriber_signal_host(v8::Isolate* isolate,
    v8::Local<v8::Context> context, SubscriberHost* sub) {
    return as_abort_signal(sub->ensure_signal(isolate, context));
}

}  // namespace

v8::Local<v8::Value> ObservableHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string&) {
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> ObservableHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    const std::string prefix =
        "Failed to execute '" + name + "' on 'Observable': ";

    if (name == "subscribe") {
        v8::Local<v8::Function> next_cb;
        v8::Local<v8::Function> error_cb;
        v8::Local<v8::Function> complete_cb;
        if (arguments.Length() >= 1 && !arguments[0]->IsNullOrUndefined()) {
            if (arguments[0]->IsFunction()) {
                next_cb = arguments[0].As<v8::Function>();
            } else if (arguments[0]->IsObject()) {
                auto obs_dict = arguments[0].As<v8::Object>();
                // Lexicographical SubscriptionObserver order: complete, error, next.
                const std::pair<const char*, v8::Local<v8::Function>*> fields[] = {
                    {"complete", &complete_cb}, {"error", &error_cb}, {"next", &next_cb}};
                for (const auto& [field_name, out_fn] : fields) {
                    v8::Local<v8::Value> val;
                    if (!user_get(isolate, context, obs_dict, v8_string(isolate, field_name))
                            .ToLocal(&val)) {
                        return v8::Undefined(isolate);
                    }
                    if (!val->IsNullOrUndefined()) {
                        if (!val->IsFunction()) {
                            throw_type_error(isolate, prefix,
                                std::string("Failed to read the '") + field_name +
                                "' property from 'SubscriptionObserver': The provided value is not of type 'Function'.");
                            return v8::Undefined(isolate);
                        }
                        *out_fn = val.As<v8::Function>();
                    }
                }
            } else {
                throw_type_error(isolate, prefix,
                    "The provided value is not of type '( ObservableSubscriptionCallback or SubscriptionObserver )'.");
                return v8::Undefined(isolate);
            }
        }
        AbortSignalHost* ext_sig = nullptr;
        const v8::Local<v8::Value> opt_arg = arguments.Length() >= 2
            ? arguments[1] : v8::Local<v8::Value>(v8::Undefined(isolate));
        if (!parse_subscribe_options(isolate, context, opt_arg, prefix, &ext_sig)) {
            return v8::Undefined(isolate);
        }
        subscribe_internal(isolate, context, next_cb, error_cb, complete_cb, ext_sig);
        return v8::Undefined(isolate);
    }

    // --- Observable-returning pipe operators ---
    if (name == "map" || name == "filter" || name == "flatMap" ||
        name == "switchMap" || name == "catch" || name == "finally") {
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
            return v8::Undefined(isolate);
        }
        if (!arguments[0]->IsFunction()) {
            throw_type_error(isolate, prefix, "parameter 1 is not of type 'Function'.");
            return v8::Undefined(isolate);
        }
        auto fn_arg = arguments[0].As<v8::Function>();
        ObservableHost* upstream = this;
        PageState* owner = owner_;

        if (name == "map" || name == "filter") {
            const bool is_map = name == "map";
            auto obs_uptr = std::make_unique<ObservableHost>(owner,
                [upstream, owner, is_map](v8::Isolate* iso, v8::Local<v8::Context> ctx,
                    ObservableHost* self_obs, SubscriberHost* sub, v8::Local<v8::Object>) {
                    if (!sub->active()) return;
                    auto bridge = std::make_unique<ObserverBridgeHost>();
                    bridge->slots.emplace_back(iso, self_obs->captured_value(iso, 1));
                    auto* b_ptr = bridge.get();
                    bridge->on_next = [b_ptr, sub, is_map](
                        v8::Isolate* i, v8::Local<v8::Context> c, v8::Local<v8::Value> v) {
                        if (!sub->active()) return;
                        auto cb = b_ptr->slots[0].Get(i).As<v8::Function>();
                        v8::Local<v8::Value> argv[] = {
                            v, v8::Number::New(i, static_cast<double>(b_ptr->counter++))};
                        v8::TryCatch caught(i);
                        v8::Local<v8::Value> out;
                        if (!user_call(i, c, cb, v8::Undefined(i), 2, argv).ToLocal(&out)) {
                            if (!caught.CanContinue()) { caught.ReThrow(); return; }
                            auto exc = caught.Exception();
                            caught.Reset();
                            sub->emit_error(i, c, exc);
                            return;
                        }
                        if (is_map) {
                            sub->emit_next(i, c, out);
                        } else if (out->BooleanValue(i)) {
                            sub->emit_next(i, c, v);
                        }
                    };
                    bridge->on_error = [sub](v8::Isolate* i, v8::Local<v8::Context> c, v8::Local<v8::Value> e) {
                        sub->emit_error(i, c, e);
                    };
                    bridge->on_complete = [sub](v8::Isolate* i, v8::Local<v8::Context> c) {
                        sub->emit_complete(i, c);
                    };
                    subscribe_with_bridge(owner, iso, ctx, upstream, std::move(bridge),
                        subscriber_signal_host(iso, ctx, sub));
                });
            obs_uptr->capture_value(isolate, arguments.This());
            obs_uptr->capture_value(isolate, fn_arg);
            return owner->adopt_host_object(isolate, context, std::move(obs_uptr));
        }

        if (name == "catch") {
            auto obs_uptr = std::make_unique<ObservableHost>(owner,
                [upstream, owner](v8::Isolate* iso, v8::Local<v8::Context> ctx,
                    ObservableHost* self_obs, SubscriberHost* sub, v8::Local<v8::Object>) {
                    if (!sub->active()) return;
                    auto bridge = std::make_unique<ObserverBridgeHost>();
                    bridge->slots.emplace_back(iso, self_obs->captured_value(iso, 1));
                    auto* b_ptr = bridge.get();
                    bridge->on_next = [sub](v8::Isolate* i, v8::Local<v8::Context> c, v8::Local<v8::Value> v) {
                        sub->emit_next(i, c, v);
                    };
                    bridge->on_complete = [sub](v8::Isolate* i, v8::Local<v8::Context> c) {
                        sub->emit_complete(i, c);
                    };
                    bridge->on_error = [b_ptr, owner, sub](
                        v8::Isolate* i, v8::Local<v8::Context> c, v8::Local<v8::Value> e) {
                        if (!sub->active()) return;
                        auto cb = b_ptr->slots[0].Get(i).As<v8::Function>();
                        v8::Local<v8::Value> argv[] = {e};
                        v8::TryCatch caught(i);
                        v8::Local<v8::Value> fallback_raw;
                        v8::Local<v8::Value> fallback_obs_val;
                        if (!user_call(i, c, cb, v8::Undefined(i), 1, argv).ToLocal(&fallback_raw) ||
                            (fallback_obs_val = convert_value_to_observable(owner, i, c, fallback_raw,
                                "Failed to execute 'catch' on 'Observable': ")).IsEmpty()) {
                            if (!caught.CanContinue()) { caught.ReThrow(); return; }
                            auto exc = caught.Exception();
                            caught.Reset();
                            sub->emit_error(i, c, exc);
                            return;
                        }
                        auto* fallback_obs = as_observable(fallback_obs_val);
                        auto inner = std::make_unique<ObserverBridgeHost>();
                        inner->on_next = [sub](v8::Isolate* ii, v8::Local<v8::Context> cc, v8::Local<v8::Value> vv) {
                            sub->emit_next(ii, cc, vv);
                        };
                        inner->on_error = [sub](v8::Isolate* ii, v8::Local<v8::Context> cc, v8::Local<v8::Value> ee) {
                            sub->emit_error(ii, cc, ee);
                        };
                        inner->on_complete = [sub](v8::Isolate* ii, v8::Local<v8::Context> cc) {
                            sub->emit_complete(ii, cc);
                        };
                        subscribe_with_bridge(owner, i, c, fallback_obs, std::move(inner),
                            subscriber_signal_host(i, c, sub));
                    };
                    subscribe_with_bridge(owner, iso, ctx, upstream, std::move(bridge),
                        subscriber_signal_host(iso, ctx, sub));
                });
            obs_uptr->capture_value(isolate, arguments.This());
            obs_uptr->capture_value(isolate, fn_arg);
            return owner->adopt_host_object(isolate, context, std::move(obs_uptr));
        }

        if (name == "finally") {
            auto obs_uptr = std::make_unique<ObservableHost>(owner,
                [upstream, owner](v8::Isolate* iso, v8::Local<v8::Context> ctx,
                    ObservableHost* self_obs, SubscriberHost* sub, v8::Local<v8::Object>) {
                    if (!sub->active()) return;
                    auto bridge = std::make_unique<ObserverBridgeHost>();
                    bridge->slots.emplace_back(iso, self_obs->captured_value(iso, 1));
                    auto* b_ptr = bridge.get();
                    auto run_fin = [b_ptr](v8::Isolate* i, v8::Local<v8::Context> c, v8::Local<v8::Value>* err_out) {
                        if (b_ptr->flag) return true;
                        b_ptr->flag = true;
                        auto cb = b_ptr->slots[0].Get(i).As<v8::Function>();
                        v8::TryCatch caught(i);
                        if (user_call(i, c, cb, v8::Undefined(i)).IsEmpty()) {
                            if (!caught.CanContinue()) { caught.ReThrow(); return false; }
                            *err_out = caught.Exception();
                            caught.Reset();
                            return false;
                        }
                        return true;
                    };
                    sub->add_native_teardown([run_fin](v8::Isolate* i, v8::Local<v8::Context> c) {
                        v8::Local<v8::Value> ignored;
                        (void)run_fin(i, c, &ignored);
                    });
                    bridge->on_next = [sub](v8::Isolate* i, v8::Local<v8::Context> c, v8::Local<v8::Value> v) {
                        sub->emit_next(i, c, v);
                    };
                    bridge->on_error = [sub, run_fin](v8::Isolate* i, v8::Local<v8::Context> c, v8::Local<v8::Value> e) {
                        v8::Local<v8::Value> fin_err;
                        if (!run_fin(i, c, &fin_err) && !fin_err.IsEmpty()) {
                            sub->emit_error(i, c, fin_err);
                            return;
                        }
                        sub->emit_error(i, c, e);
                    };
                    bridge->on_complete = [sub, run_fin](v8::Isolate* i, v8::Local<v8::Context> c) {
                        v8::Local<v8::Value> fin_err;
                        if (!run_fin(i, c, &fin_err) && !fin_err.IsEmpty()) {
                            sub->emit_error(i, c, fin_err);
                            return;
                        }
                        sub->emit_complete(i, c);
                    };
                    subscribe_with_bridge(owner, iso, ctx, upstream, std::move(bridge),
                        subscriber_signal_host(iso, ctx, sub));
                });
            obs_uptr->capture_value(isolate, arguments.This());
            obs_uptr->capture_value(isolate, fn_arg);
            return owner->adopt_host_object(isolate, context, std::move(obs_uptr));
        }

        // flatMap & switchMap
        const bool is_switch = name == "switchMap";
        auto obs_uptr = std::make_unique<ObservableHost>(owner,
            [upstream, owner, is_switch](v8::Isolate* iso, v8::Local<v8::Context> ctx,
                ObservableHost* self_obs, SubscriberHost* sub, v8::Local<v8::Object>) {
                if (!sub->active()) return;
                auto bridge = std::make_unique<ObserverBridgeHost>();
                bridge->slots.emplace_back(iso, self_obs->captured_value(iso, 1));
                auto* b_ptr = bridge.get();
                sub->add_native_teardown([b_ptr, sub](v8::Isolate* i, v8::Local<v8::Context> c) {
                    for (auto& q : b_ptr->queue) q.Reset();
                    b_ptr->queue.clear();
                    b_ptr->inner_busy = false;
                    if (b_ptr->active_inner_sig != nullptr) {
                        auto* sig = b_ptr->active_inner_sig;
                        b_ptr->active_inner_sig = nullptr;
                        auto* sub_sig = subscriber_signal_host(i, c, sub);
                        sig->abort_with_reason(i, c,
                            sub_sig != nullptr ? sub_sig->reason_value(i)
                                               : v8::Local<v8::Value>(v8::Undefined(i)));
                    }
                });
                b_ptr->pump = [b_ptr, owner, sub](
                    v8::Isolate* i, v8::Local<v8::Context> c) {
                    while (!b_ptr->inner_busy && sub->active() && !b_ptr->queue.empty()) {
                        v8::Local<v8::Value> next_val = b_ptr->queue.front().Get(i);
                        b_ptr->queue.front().Reset();
                        b_ptr->queue.pop_front();
                        b_ptr->inner_busy = true;
                        auto mapper = b_ptr->slots[0].Get(i).As<v8::Function>();
                        v8::Local<v8::Value> argv[] = {
                            next_val, v8::Number::New(i, static_cast<double>(++b_ptr->counter))};
                        v8::TryCatch caught(i);
                        v8::Local<v8::Value> inner_raw;
                        v8::Local<v8::Value> inner_obs_val;
                        if (!user_call(i, c, mapper, v8::Undefined(i), 2, argv).ToLocal(&inner_raw) ||
                            (inner_obs_val = convert_value_to_observable(owner, i, c, inner_raw,
                                "Failed to convert mapped value to Observable: ")).IsEmpty()) {
                            if (!caught.CanContinue()) { caught.ReThrow(); return; }
                            auto exc = caught.Exception();
                            caught.Reset();
                            b_ptr->inner_busy = false;
                            sub->emit_error(i, c, exc);
                            return;
                        }
                        auto sig_uptr = std::make_unique<AbortSignalHost>(owner);
                        auto* inner_sig = sig_uptr.get();
                        auto sig_obj = owner->adopt_host_object(i, c, std::move(sig_uptr));
                        inner_sig->bind_wrapper(i, sig_obj);
                        b_ptr->active_inner_sig = inner_sig;
                        auto inner_bridge = std::make_unique<ObserverBridgeHost>();
                        inner_bridge->on_next = [b_ptr, sub, inner_sig](
                            v8::Isolate* ii, v8::Local<v8::Context> cc, v8::Local<v8::Value> vv) {
                            if (b_ptr->active_inner_sig == inner_sig && !inner_sig->aborted()) {
                                sub->emit_next(ii, cc, vv);
                            }
                        };
                        inner_bridge->on_error = [b_ptr, sub, inner_sig](
                            v8::Isolate* ii, v8::Local<v8::Context> cc, v8::Local<v8::Value> ee) {
                            if (b_ptr->active_inner_sig != inner_sig || inner_sig->aborted()) return;
                            b_ptr->active_inner_sig = nullptr;
                            b_ptr->inner_busy = false;
                            sub->emit_error(ii, cc, ee);
                        };
                        inner_bridge->on_complete = [b_ptr, inner_sig](
                            v8::Isolate* ii, v8::Local<v8::Context> cc) {
                            if (b_ptr->active_inner_sig != inner_sig) return;
                            b_ptr->active_inner_sig = nullptr;
                            b_ptr->inner_busy = false;
                            if (b_ptr->in_inner_subscribe) return;
                            if (b_ptr->pump) b_ptr->pump(ii, cc);
                        };
                        b_ptr->in_inner_subscribe = true;
                        subscribe_with_bridge(owner, i, c, as_observable(inner_obs_val),
                            std::move(inner_bridge), inner_sig);
                        b_ptr->in_inner_subscribe = false;
                    }
                    if (!b_ptr->inner_busy && b_ptr->queue.empty() &&
                        b_ptr->outer_completed && sub->active()) {
                        sub->emit_complete(i, c);
                    }
                };
                bridge->on_next = [b_ptr, owner, sub, is_switch](
                    v8::Isolate* i, v8::Local<v8::Context> c, v8::Local<v8::Value> v) {
                    if (!sub->active()) return;
                    if (!is_switch) {
                        b_ptr->queue.emplace_back(i, v);
                        if (!b_ptr->inner_busy && b_ptr->pump) {
                            b_ptr->pump(i, c);
                        }
                        return;
                    }
                    if (b_ptr->active_inner_sig != nullptr) {
                        auto* prev_sig = b_ptr->active_inner_sig;
                        b_ptr->active_inner_sig = nullptr;
                        prev_sig->abort_with_reason(i, c, v8::Undefined(i));
                        if (!sub->active()) return;
                    }
                    auto mapper = b_ptr->slots[0].Get(i).As<v8::Function>();
                    v8::Local<v8::Value> argv[] = {
                        v, v8::Number::New(i, static_cast<double>(++b_ptr->counter))};
                    v8::TryCatch caught(i);
                    v8::Local<v8::Value> inner_raw;
                    v8::Local<v8::Value> inner_obs_val;
                    if (!user_call(i, c, mapper, v8::Undefined(i), 2, argv).ToLocal(&inner_raw) ||
                        (inner_obs_val = convert_value_to_observable(owner, i, c, inner_raw,
                            "Failed to convert mapped value to Observable: ")).IsEmpty()) {
                        if (!caught.CanContinue()) { caught.ReThrow(); return; }
                        auto exc = caught.Exception();
                        caught.Reset();
                        sub->emit_error(i, c, exc);
                        return;
                    }
                    auto sig_uptr = std::make_unique<AbortSignalHost>(owner);
                    auto* inner_sig = sig_uptr.get();
                    auto sig_obj = owner->adopt_host_object(i, c, std::move(sig_uptr));
                    inner_sig->bind_wrapper(i, sig_obj);
                    b_ptr->active_inner_sig = inner_sig;
                    auto inner_bridge = std::make_unique<ObserverBridgeHost>();
                    inner_bridge->on_next = [b_ptr, sub, inner_sig](
                        v8::Isolate* ii, v8::Local<v8::Context> cc, v8::Local<v8::Value> vv) {
                        if (b_ptr->active_inner_sig == inner_sig && !inner_sig->aborted()) {
                            sub->emit_next(ii, cc, vv);
                        }
                    };
                    inner_bridge->on_error = [b_ptr, sub, inner_sig](
                        v8::Isolate* ii, v8::Local<v8::Context> cc, v8::Local<v8::Value> ee) {
                        if (b_ptr->active_inner_sig != inner_sig || inner_sig->aborted()) return;
                        b_ptr->active_inner_sig = nullptr;
                        sub->emit_error(ii, cc, ee);
                    };
                    inner_bridge->on_complete = [b_ptr, sub, inner_sig](
                        v8::Isolate* ii, v8::Local<v8::Context> cc) {
                        if (b_ptr->active_inner_sig != inner_sig) return;
                        b_ptr->active_inner_sig = nullptr;
                        if (b_ptr->outer_completed) {
                            sub->emit_complete(ii, cc);
                        }
                    };
                    subscribe_with_bridge(owner, i, c, as_observable(inner_obs_val),
                        std::move(inner_bridge), inner_sig);
                };
                bridge->on_error = [sub](v8::Isolate* i, v8::Local<v8::Context> c, v8::Local<v8::Value> e) {
                    sub->emit_error(i, c, e);
                };
                bridge->on_complete = [b_ptr, sub, is_switch](v8::Isolate* i, v8::Local<v8::Context> c) {
                    b_ptr->outer_completed = true;
                    if (is_switch) {
                        if (b_ptr->active_inner_sig == nullptr) {
                            sub->emit_complete(i, c);
                        }
                    } else if (!b_ptr->inner_busy && b_ptr->queue.empty()) {
                        sub->emit_complete(i, c);
                    }
                };
                subscribe_with_bridge(owner, iso, ctx, upstream, std::move(bridge),
                    subscriber_signal_host(iso, ctx, sub));
            });
        obs_uptr->capture_value(isolate, arguments.This());
        obs_uptr->capture_value(isolate, fn_arg);
        return owner->adopt_host_object(isolate, context, std::move(obs_uptr));
    }

    if (name == "take" || name == "drop") {
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
            return v8::Undefined(isolate);
        }
        std::uint64_t amount = 0;
        if (!convert_webidl_ull_wrapping(isolate, context, arguments[0], &amount)) {
            return v8::Undefined(isolate);
        }
        const bool is_take = name == "take";
        ObservableHost* upstream = this;
        PageState* owner = owner_;
        auto obs_uptr = std::make_unique<ObservableHost>(owner,
            [upstream, owner, amount, is_take](v8::Isolate* iso, v8::Local<v8::Context> ctx,
                ObservableHost*, SubscriberHost* sub, v8::Local<v8::Object>) {
                if (!sub->active()) return;
                if (is_take && amount == 0) {
                    sub->emit_complete(iso, ctx);
                    return;
                }
                auto bridge = std::make_unique<ObserverBridgeHost>();
                bridge->counter = amount;
                auto* b_ptr = bridge.get();
                bridge->on_next = [b_ptr, sub, is_take](
                    v8::Isolate* i, v8::Local<v8::Context> c, v8::Local<v8::Value> v) {
                    if (!sub->active()) return;
                    if (is_take) {
                        if (b_ptr->counter == 0) return;
                        --b_ptr->counter;
                        sub->emit_next(i, c, v);
                        if (b_ptr->counter == 0) sub->emit_complete(i, c);
                    } else {
                        if (b_ptr->counter > 0) {
                            --b_ptr->counter;
                            return;
                        }
                        sub->emit_next(i, c, v);
                    }
                };
                bridge->on_error = [sub](v8::Isolate* i, v8::Local<v8::Context> c, v8::Local<v8::Value> e) {
                    sub->emit_error(i, c, e);
                };
                bridge->on_complete = [sub](v8::Isolate* i, v8::Local<v8::Context> c) {
                    sub->emit_complete(i, c);
                };
                subscribe_with_bridge(owner, iso, ctx, upstream, std::move(bridge),
                    subscriber_signal_host(iso, ctx, sub));
            });
        obs_uptr->capture_value(isolate, arguments.This());
        return owner->adopt_host_object(isolate, context, std::move(obs_uptr));
    }

    if (name == "takeUntil") {
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
            return v8::Undefined(isolate);
        }
        v8::Local<v8::Value> notifier_arg = arguments[0];
        ObservableHost* upstream = this;
        PageState* owner = owner_;
        auto obs_uptr = std::make_unique<ObservableHost>(owner,
            [upstream, owner](v8::Isolate* iso, v8::Local<v8::Context> ctx,
                ObservableHost* self_obs, SubscriberHost* sub, v8::Local<v8::Object>) {
                if (!sub->active()) return;
                v8::TryCatch caught(iso);
                auto notifier_val = convert_value_to_observable(
                    owner, iso, ctx, self_obs->captured_value(iso, 1),
                    "Failed to execute 'takeUntil' on 'Observable': ");
                if (notifier_val.IsEmpty()) {
                    if (!caught.CanContinue()) { caught.ReThrow(); return; }
                    auto exc = caught.Exception();
                    caught.Reset();
                    sub->emit_error(iso, ctx, exc);
                    return;
                }
                auto notifier_bridge = std::make_unique<ObserverBridgeHost>();
                notifier_bridge->on_next = [sub](v8::Isolate* i, v8::Local<v8::Context> c, v8::Local<v8::Value>) {
                    sub->emit_complete(i, c);
                };
                notifier_bridge->on_error = [sub](v8::Isolate* i, v8::Local<v8::Context> c, v8::Local<v8::Value> e) {
                    sub->emit_error(i, c, e);
                };
                subscribe_with_bridge(owner, iso, ctx, as_observable(notifier_val),
                    std::move(notifier_bridge), subscriber_signal_host(iso, ctx, sub));
                if (!sub->active()) return;
                auto src_bridge = std::make_unique<ObserverBridgeHost>();
                src_bridge->on_next = [sub](v8::Isolate* i, v8::Local<v8::Context> c, v8::Local<v8::Value> v) {
                    sub->emit_next(i, c, v);
                };
                src_bridge->on_error = [sub](v8::Isolate* i, v8::Local<v8::Context> c, v8::Local<v8::Value> e) {
                    sub->emit_error(i, c, e);
                };
                src_bridge->on_complete = [sub](v8::Isolate* i, v8::Local<v8::Context> c) {
                    sub->emit_complete(i, c);
                };
                subscribe_with_bridge(owner, iso, ctx, upstream, std::move(src_bridge),
                    subscriber_signal_host(iso, ctx, sub));
            });
        obs_uptr->capture_value(isolate, arguments.This());
        obs_uptr->capture_value(isolate, notifier_arg);
        return owner->adopt_host_object(isolate, context, std::move(obs_uptr));
    }

    if (name == "inspect") {
        v8::Local<v8::Function> sub_cb, next_cb, err_cb, comp_cb, abort_cb;
        if (arguments.Length() >= 1 && !arguments[0]->IsNullOrUndefined()) {
            if (arguments[0]->IsFunction()) {
                next_cb = arguments[0].As<v8::Function>();
            } else if (arguments[0]->IsObject()) {
                auto dict = arguments[0].As<v8::Object>();
                // Lexicographical ObservableInspector order: abort, complete, error, next, subscribe.
                const std::pair<const char*, v8::Local<v8::Function>*> fields[] = {
                    {"abort", &abort_cb}, {"complete", &comp_cb}, {"error", &err_cb},
                    {"next", &next_cb}, {"subscribe", &sub_cb}};
                for (const auto& [fname, out_fn] : fields) {
                    v8::Local<v8::Value> val;
                    if (!user_get(isolate, context, dict, v8_string(isolate, fname))
                            .ToLocal(&val)) {
                        return v8::Undefined(isolate);
                    }
                    if (!val->IsNullOrUndefined()) {
                        if (!val->IsFunction()) {
                            throw_type_error(isolate, prefix,
                                std::string("Failed to read the '") + fname +
                                "' property from 'ObservableInspector': The provided value is not of type 'Function'.");
                            return v8::Undefined(isolate);
                        }
                        *out_fn = val.As<v8::Function>();
                    }
                }
            } else {
                throw_type_error(isolate, prefix,
                    "The provided value is not of type '( ObservableSubscriptionCallback or ObservableInspector )'.");
                return v8::Undefined(isolate);
            }
        }
        ObservableHost* upstream = this;
        PageState* owner = owner_;
        auto obs_uptr = std::make_unique<ObservableHost>(owner,
            [upstream, owner](v8::Isolate* iso, v8::Local<v8::Context> ctx,
                ObservableHost* self_obs, SubscriberHost* sub, v8::Local<v8::Object>) {
                if (!sub->active()) return;
                auto sub_fn = self_obs->captured_value(iso, 1);
                if (sub_fn->IsFunction()) {
                    v8::TryCatch caught(iso);
                    if (user_call(iso, ctx, sub_fn, v8::Undefined(iso)).IsEmpty()) {
                        if (!caught.CanContinue()) { caught.ReThrow(); return; }
                        auto exc = caught.Exception();
                        caught.Reset();
                        sub->emit_error(iso, ctx, exc);
                        return;
                    }
                }
                auto bridge = std::make_unique<ObserverBridgeHost>();
                for (std::size_t idx = 2; idx <= 5; ++idx) {
                    bridge->slots.emplace_back(iso, self_obs->captured_value(iso, idx));
                }
                auto* b_ptr = bridge.get();
                auto* sub_sig = subscriber_signal_host(iso, ctx, sub);
                if (sub_sig != nullptr) {
                    sub_sig->add_abort_algorithm([b_ptr, sub_sig](v8::Isolate* i, v8::Local<v8::Context> c) {
                        if (!b_ptr->flag && b_ptr->slots[3].Get(i)->IsFunction()) {
                            v8::Local<v8::Value> argv[] = {sub_sig->reason_value(i)};
                            v8::TryCatch caught(i);
                            (void)user_call(i, c, b_ptr->slots[3].Get(i), v8::Undefined(i), 1, argv);
                            if (caught.HasCaught()) {
                                report_uncaught_observable_error(i, c, caught, "observable_inspect_abort");
                            }
                        }
                    });
                }
                bridge->on_next = [b_ptr, sub](v8::Isolate* i, v8::Local<v8::Context> c, v8::Local<v8::Value> v) {
                    if (b_ptr->slots[0].Get(i)->IsFunction()) {
                        v8::Local<v8::Value> argv[] = {v};
                        v8::TryCatch caught(i);
                        if (user_call(i, c, b_ptr->slots[0].Get(i), v8::Undefined(i), 1, argv).IsEmpty()) {
                            if (!caught.CanContinue()) { caught.ReThrow(); return; }
                            auto exc = caught.Exception();
                            caught.Reset();
                            b_ptr->flag = true;
                            sub->emit_error(i, c, exc);
                            return;
                        }
                    }
                    sub->emit_next(i, c, v);
                };
                bridge->on_error = [b_ptr, sub](v8::Isolate* i, v8::Local<v8::Context> c, v8::Local<v8::Value> e) {
                    b_ptr->flag = true;
                    if (b_ptr->slots[1].Get(i)->IsFunction()) {
                        v8::Local<v8::Value> argv[] = {e};
                        v8::TryCatch caught(i);
                        if (user_call(i, c, b_ptr->slots[1].Get(i), v8::Undefined(i), 1, argv).IsEmpty()) {
                            if (!caught.CanContinue()) { caught.ReThrow(); return; }
                            auto exc = caught.Exception();
                            caught.Reset();
                            sub->emit_error(i, c, exc);
                            return;
                        }
                    }
                    sub->emit_error(i, c, e);
                };
                bridge->on_complete = [b_ptr, sub](v8::Isolate* i, v8::Local<v8::Context> c) {
                    b_ptr->flag = true;
                    if (b_ptr->slots[2].Get(i)->IsFunction()) {
                        v8::TryCatch caught(i);
                        if (user_call(i, c, b_ptr->slots[2].Get(i), v8::Undefined(i)).IsEmpty()) {
                            if (!caught.CanContinue()) { caught.ReThrow(); return; }
                            auto exc = caught.Exception();
                            caught.Reset();
                            sub->emit_error(i, c, exc);
                            return;
                        }
                    }
                    sub->emit_complete(i, c);
                };
                subscribe_with_bridge(owner, iso, ctx, upstream, std::move(bridge),
                    subscriber_signal_host(iso, ctx, sub));
            });
        obs_uptr->capture_value(isolate, arguments.This());
        obs_uptr->capture_value(isolate, sub_cb.IsEmpty() ? v8::Local<v8::Value>(v8::Undefined(isolate)) : v8::Local<v8::Value>(sub_cb));
        obs_uptr->capture_value(isolate, next_cb.IsEmpty() ? v8::Local<v8::Value>(v8::Undefined(isolate)) : v8::Local<v8::Value>(next_cb));
        obs_uptr->capture_value(isolate, err_cb.IsEmpty() ? v8::Local<v8::Value>(v8::Undefined(isolate)) : v8::Local<v8::Value>(err_cb));
        obs_uptr->capture_value(isolate, comp_cb.IsEmpty() ? v8::Local<v8::Value>(v8::Undefined(isolate)) : v8::Local<v8::Value>(comp_cb));
        obs_uptr->capture_value(isolate, abort_cb.IsEmpty() ? v8::Local<v8::Value>(v8::Undefined(isolate)) : v8::Local<v8::Value>(abort_cb));
        return owner->adopt_host_object(isolate, context, std::move(obs_uptr));
    }

    // --- Promise-returning terminal operators ---
    if (name == "toArray" || name == "first" || name == "last" ||
        name == "forEach" || name == "every" || name == "some" ||
        name == "find" || name == "reduce") {
        v8::TryCatch caught(isolate);
        const bool takes_fn = name == "forEach" || name == "every" ||
            name == "some" || name == "find" || name == "reduce";
        v8::Local<v8::Function> fn_arg;
        if (takes_fn) {
            if (arguments.Length() < 1) {
                throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
                return reject_from_try_catch(isolate, context, caught);
            }
            if (!arguments[0]->IsFunction()) {
                throw_type_error(isolate, prefix, "parameter 1 is not of type 'Function'.");
                return reject_from_try_catch(isolate, context, caught);
            }
            fn_arg = arguments[0].As<v8::Function>();
        }
        const int opt_index = name == "reduce" ? 2 : (takes_fn ? 1 : 0);
        AbortSignalHost* ext_sig = nullptr;
        const v8::Local<v8::Value> opt_val = arguments.Length() > opt_index
            ? arguments[opt_index] : v8::Local<v8::Value>(v8::Undefined(isolate));
        if (!parse_subscribe_options(isolate, context, opt_val, prefix, &ext_sig)) {
            return reject_from_try_catch(isolate, context, caught);
        }
        if (ext_sig != nullptr && ext_sig->aborted()) {
            return rejected_promise(isolate, context, ext_sig->reason_value(isolate));
        }

        v8::Local<v8::Promise::Resolver> resolver;
        if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
            return v8::Undefined(isolate);
        }
        auto ac_uptr = std::make_unique<AbortControllerHost>(owner_);
        auto* ac = ac_uptr.get();
        (void)owner_->adopt_host_object(isolate, context, std::move(ac_uptr));
        auto* inner_sig = as_abort_signal(ac->ensure_signal(isolate, context));

        auto bridge = std::make_unique<ObserverBridgeHost>();
        auto* b_ptr = bridge.get();
        bridge->slots.emplace_back(isolate, resolver);
        bridge->slots.emplace_back(isolate, fn_arg.IsEmpty()
            ? v8::Local<v8::Value>(v8::Undefined(isolate)) : v8::Local<v8::Value>(fn_arg));
        if (name == "toArray") {
            bridge->slots.emplace_back(isolate, v8::Array::New(isolate, 0));
        } else if (name == "reduce") {
            const bool has_init = arguments.Length() >= 2 && !arguments[1]->IsUndefined();
            bridge->flag = has_init;
            bridge->slots.emplace_back(isolate, has_init
                ? arguments[1] : v8::Local<v8::Value>(v8::Undefined(isolate)));
        } else {
            bridge->slots.emplace_back(isolate, v8::Undefined(isolate));
        }

        if (ext_sig != nullptr) {
            ext_sig->add_abort_algorithm([b_ptr, inner_sig, ext_sig](
                v8::Isolate* i, v8::Local<v8::Context> c) {
                auto res = b_ptr->slots[0].Get(i).As<v8::Promise::Resolver>();
                inner_sig->abort_with_reason(i, c, ext_sig->reason_value(i));
                (void)access_user_operation(i, [&] {
                    return res->Reject(c, ext_sig->reason_value(i));
                });
            });
        }

        bridge->on_error = [b_ptr](v8::Isolate* i, v8::Local<v8::Context> c, v8::Local<v8::Value> e) {
            auto res = b_ptr->slots[0].Get(i).As<v8::Promise::Resolver>();
            (void)access_user_operation(i, [&] { return res->Reject(c, e); });
        };

        bridge->on_next = [b_ptr, inner_sig, name](
            v8::Isolate* i, v8::Local<v8::Context> c, v8::Local<v8::Value> v) {
            auto res = b_ptr->slots[0].Get(i).As<v8::Promise::Resolver>();
            if (name == "toArray") {
                auto arr = b_ptr->slots[2].Get(i).As<v8::Array>();
                (void)arr->CreateDataProperty(c, static_cast<std::uint32_t>(b_ptr->counter++), v);
                return;
            }
            if (name == "first") {
                b_ptr->flag = true;
                inner_sig->abort_with_reason(i, c, v8::Undefined(i));
                (void)access_user_operation(i, [&] { return res->Resolve(c, v); });
                return;
            }
            if (name == "last") {
                b_ptr->flag = true;
                b_ptr->slots[2].Reset(i, v);
                return;
            }
            if (name == "reduce") {
                const auto idx = b_ptr->counter++;
                if (!b_ptr->flag) {
                    b_ptr->flag = true;
                    b_ptr->slots[2].Reset(i, v);
                    return;
                }
                auto cb = b_ptr->slots[1].Get(i).As<v8::Function>();
                v8::Local<v8::Value> argv[] = {
                    b_ptr->slots[2].Get(i), v, v8::Number::New(i, static_cast<double>(idx))};
                v8::TryCatch step_catch(i);
                v8::Local<v8::Value> acc;
                if (!user_call(i, c, cb, v8::Undefined(i), 3, argv).ToLocal(&acc)) {
                    if (!step_catch.CanContinue()) { step_catch.ReThrow(); return; }
                    auto exc = step_catch.Exception();
                    step_catch.Reset();
                    inner_sig->abort_with_reason(i, c, exc);
                    (void)access_user_operation(i, [&] { return res->Reject(c, exc); });
                    return;
                }
                b_ptr->slots[2].Reset(i, acc);
                return;
            }
            // forEach, every, some, find
            auto cb = b_ptr->slots[1].Get(i).As<v8::Function>();
            v8::Local<v8::Value> argv[] = {
                v, v8::Number::New(i, static_cast<double>(b_ptr->counter++))};
            v8::TryCatch step_catch(i);
            v8::Local<v8::Value> out;
            if (!user_call(i, c, cb, v8::Undefined(i), 2, argv).ToLocal(&out)) {
                if (!step_catch.CanContinue()) { step_catch.ReThrow(); return; }
                auto exc = step_catch.Exception();
                step_catch.Reset();
                inner_sig->abort_with_reason(i, c, exc);
                (void)access_user_operation(i, [&] { return res->Reject(c, exc); });
                return;
            }
            if (name == "every" && !out->BooleanValue(i)) {
                inner_sig->abort_with_reason(i, c, v8::Undefined(i));
                (void)access_user_operation(i, [&] { return res->Resolve(c, v8::False(i)); });
            } else if (name == "some" && out->BooleanValue(i)) {
                inner_sig->abort_with_reason(i, c, v8::Undefined(i));
                (void)access_user_operation(i, [&] { return res->Resolve(c, v8::True(i)); });
            } else if (name == "find" && out->BooleanValue(i)) {
                b_ptr->flag = true;
                inner_sig->abort_with_reason(i, c, v8::Undefined(i));
                (void)access_user_operation(i, [&] { return res->Resolve(c, v); });
            }
        };

        bridge->on_complete = [b_ptr, name](v8::Isolate* i, v8::Local<v8::Context> c) {
            auto res = b_ptr->slots[0].Get(i).As<v8::Promise::Resolver>();
            if (name == "toArray") {
                (void)access_user_operation(i, [&] {
                    return res->Resolve(c, b_ptr->slots[2].Get(i));
                });
            } else if (name == "first" || name == "last") {
                if (!b_ptr->flag) {
                    auto err = v8::Exception::RangeError(
                        v8::String::NewFromUtf8Literal(i, "No values in Observable"));
                    (void)access_user_operation(i, [&] { return res->Reject(c, err); });
                } else if (name == "last") {
                    (void)access_user_operation(i, [&] {
                        return res->Resolve(c, b_ptr->slots[2].Get(i));
                    });
                }
            } else if (name == "reduce") {
                if (!b_ptr->flag) {
                    auto err = v8::Exception::TypeError(v8::String::NewFromUtf8Literal(
                        i, "Reduce of empty array with no initial value"));
                    (void)access_user_operation(i, [&] { return res->Reject(c, err); });
                } else {
                    (void)access_user_operation(i, [&] {
                        return res->Resolve(c, b_ptr->slots[2].Get(i));
                    });
                }
            } else if (name == "forEach" || name == "find") {
                if (name == "find" && b_ptr->flag) return;
                (void)access_user_operation(i, [&] {
                    return res->Resolve(c, v8::Undefined(i));
                });
            } else if (name == "every") {
                (void)access_user_operation(i, [&] { return res->Resolve(c, v8::True(i)); });
            } else if (name == "some") {
                (void)access_user_operation(i, [&] { return res->Resolve(c, v8::False(i)); });
            }
        };

        subscribe_with_bridge(owner_, isolate, context, this, std::move(bridge), inner_sig);
        return resolver->GetPromise();
    }

    return v8::Undefined(isolate);
}

// --- EventTarget.prototype.when ---

v8::Local<v8::Value> event_target_when(EventTargetHost* target,
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    const std::string prefix = "Failed to execute 'when' on 'EventTarget': ";
    if (arguments.Length() < 1) {
        throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
        return v8::Undefined(isolate);
    }
    std::string type;
    if (!web_idl_dom_string(isolate, context, arguments[0], prefix, &type)) {
        return v8::Undefined(isolate);
    }
    bool passive = false;
    if (arguments.Length() >= 2 && !arguments[1]->IsNullOrUndefined()) {
        if (!arguments[1]->IsObject()) {
            throw_type_error(isolate, prefix,
                "The provided value is not of type 'ObservableEventListenerOptions'.");
            return v8::Undefined(isolate);
        }
        v8::Local<v8::Value> passive_val;
        if (!user_get(isolate, context, arguments[1].As<v8::Object>(),
                v8_string(isolate, "passive")).ToLocal(&passive_val)) {
            return v8::Undefined(isolate);
        }
        passive = passive_val->BooleanValue(isolate);
    }
    v8::Local<v8::Value> owner_ext;
    if (!context->Global()->GetPrivate(context, v8::Private::ForApi(
            isolate, v8::String::NewFromUtf8Literal(isolate, "iv8.page.owner")))
            .ToLocal(&owner_ext) ||
        !owner_ext->IsExternal()) {
        return v8::Undefined(isolate);
    }
    auto* owner = static_cast<PageState*>(
        owner_ext.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    auto obs_uptr = std::make_unique<ObservableHost>(owner,
        [target, type, passive](v8::Isolate* iso, v8::Local<v8::Context> ctx,
            ObservableHost*, SubscriberHost* sub, v8::Local<v8::Object>) {
            if (!sub->active()) return;
            auto ext = v8::External::New(iso, sub, v8::kExternalPointerTypeTagDefault);
            auto listener = make_internal_fn(iso, ctx, &when_listener_callback, ext);
            if (listener.IsEmpty()) return;
            target->add_observable_listener(iso, type, listener, passive);
            auto slot = sub->ensure_signal(iso, ctx);
            (void)slot->SetPrivate(ctx,
                v8::Private::ForApi(iso, v8::String::NewFromUtf8Literal(iso, "iv8.when.fn")),
                listener);
            sub->add_native_teardown([target, type, sub](v8::Isolate* i, v8::Local<v8::Context> c) {
                auto sig_obj = sub->ensure_signal(i, c);
                v8::Local<v8::Value> fn_val;
                if (sig_obj->GetPrivate(c, v8::Private::ForApi(
                        i, v8::String::NewFromUtf8Literal(i, "iv8.when.fn"))).ToLocal(&fn_val) &&
                    fn_val->IsFunction()) {
                    target->remove_observable_listener(i, type, fn_val.As<v8::Function>());
                }
            });
        });
    obs_uptr->capture_value(isolate, arguments.This());
    return owner->adopt_host_object(isolate, context, std::move(obs_uptr));
}

bool install_observable_and_abort_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner) {
    ObservableHost obs_decl(owner, nullptr);
    SubscriberHost sub_decl(owner);
    AbortControllerHost ac_decl(owner);
    AbortSignalHost as_decl(owner);

    for (HostObject* host : {static_cast<HostObject*>(&obs_decl),
                             static_cast<HostObject*>(&sub_decl),
                             static_cast<HostObject*>(&ac_decl),
                             static_cast<HostObject*>(&as_decl)}) {
        auto proto = web_interface_prototype(isolate, context, host->prototype_key());
        if (proto.IsEmpty()) return false;
        install_host_interface(isolate, context, host);
        if (!move_constructor_to_end(isolate, context, proto, host->prototype_key())) {
            return false;
        }
    }

    auto owner_ext = v8::External::New(isolate, owner, v8::kExternalPointerTypeTagDefault);
    if (!context->Global()->SetPrivate(context, v8::Private::ForApi(
            isolate, v8::String::NewFromUtf8Literal(isolate, "iv8.page.owner")),
            owner_ext).FromMaybe(false)) {
        return false;
    }

    // Observable.from
    auto obs_proto = web_interface_prototype(isolate, context, "Observable");
    v8::Local<v8::Value> obs_ctor;
    v8::Local<v8::Function> from_fn;
    if (obs_proto.IsEmpty() ||
        !obs_proto->Get(context, v8_string(isolate, "constructor")).ToLocal(&obs_ctor) ||
        !obs_ctor->IsFunction() ||
        !make_web_method(isolate, context, "from", 1, &observable_from_static, owner_ext)
             .ToLocal(&from_fn) ||
        !install_traced_api_data_property(isolate, context, obs_ctor.As<v8::Object>(),
            "from", from_fn, native_api_identity(isolate, obs_ctor.As<v8::Object>()),
            "host_object", "Observable")) {
        return false;
    }

    // AbortSignal.abort, AbortSignal.any, AbortSignal.timeout
    auto as_proto = web_interface_prototype(isolate, context, "AbortSignal");
    v8::Local<v8::Value> as_ctor;
    if (as_proto.IsEmpty() ||
        !as_proto->Get(context, v8_string(isolate, "constructor")).ToLocal(&as_ctor) ||
        !as_ctor->IsFunction()) {
        return false;
    }
    const std::tuple<const char*, int, v8::FunctionCallback> as_statics[] = {
        {"abort", 0, &abort_signal_abort_static},
        {"any", 1, &abort_signal_any_static},
        {"timeout", 1, &abort_signal_timeout_static},
    };
    for (const auto& [sname, slen, scb] : as_statics) {
        v8::Local<v8::Function> sfn;
        if (!make_web_method(isolate, context, sname, slen, scb, owner_ext).ToLocal(&sfn) ||
            !install_traced_api_data_property(isolate, context, as_ctor.As<v8::Object>(),
                sname, sfn, native_api_identity(isolate, as_ctor.As<v8::Object>()),
                "host_object", "AbortSignal")) {
            return false;
        }
    }

    // Ensure EventTarget.prototype has Chrome's exact key order:
    // addEventListener, dispatchEvent, removeEventListener, when, constructor.
    auto et_proto = web_interface_prototype(isolate, context, "EventTarget");
    if (et_proto.IsEmpty()) return false;
    StandaloneEventTarget et_decl;
    install_host_interface(isolate, context, &et_decl);
    return reorder_event_target_prototype(isolate, context, et_proto);
}

}  // namespace iv8::browser
