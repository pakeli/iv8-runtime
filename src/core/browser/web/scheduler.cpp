#include "scheduler.h"

#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>
#include <vector>

#include "iv8/access_origin.h"
#include "iv8/access_summary.h"
#include "iv8/page_state.h"
#include "iv8/web_interface.h"
#include "iv8/web_method.h"
#include "../internal/common.h"
#include "../internal/trace_hooks.h"
#include "../window/window_trace_support.h"
#include "dom_exception.h"

namespace iv8::browser {
namespace {

bool is_valid_task_priority(const std::string& value) {
    return value == "user-blocking" || value == "user-visible" ||
           value == "background";
}

int task_priority_rank(const std::string& value, bool is_continuation = false) {
    if (value == "user-blocking") return is_continuation ? 0 : 1;
    if (value == "background") return is_continuation ? 4 : 5;
    return is_continuation ? 2 : 3;
}

bool throw_sched_type_error(v8::Isolate* isolate, const std::string& prefix,
    const std::string& detail) {
    const auto message = prefix + detail;
    v8::Local<v8::String> text;
    if (v8::String::NewFromUtf8(isolate, message.data(), v8::NewStringType::kNormal,
            static_cast<int>(message.size())).ToLocal(&text)) {
        isolate->ThrowException(v8::Exception::TypeError(text));
    }
    return false;
}

v8::Local<v8::Value> sched_rejected_promise(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> reason) {
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver) ||
        !resolver->Reject(context, reason).FromMaybe(false)) {
        return v8::Undefined(isolate);
    }
    return resolver->GetPromise();
}

v8::Local<v8::Value> sched_reject_from_try_catch(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::TryCatch& caught) {
    if (!caught.CanContinue()) {
        caught.ReThrow();
        return v8::Undefined(isolate);
    }
    auto exception = caught.Exception();
    caught.Reset();
    return sched_rejected_promise(isolate, context, exception);
}

bool is_callable(v8::Local<v8::Value> value) {
    return value->IsObject() && value.As<v8::Object>()->IsCallable();
}

v8::MaybeLocal<v8::Value> sched_user_get(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> object,
    v8::Local<v8::Value> key) {
    return access_user_operation(isolate, [&] {
        return object->Get(context, key);
    });
}

v8::MaybeLocal<v8::Value> sched_user_call(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> callable,
    v8::Local<v8::Value> receiver, int count = 0,
    v8::Local<v8::Value>* argv = nullptr) {
    return access_user_operation(isolate, [&] {
        return callable.As<v8::Object>()->CallAsFunction(
            context, receiver, count, argv);
    });
}

bool convert_task_priority(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, const std::string& prefix, std::string* out) {
    v8::Local<v8::String> str;
    if (!access_user_operation(isolate, [&] {
            return input->ToString(context);
        }).ToLocal(&str)) {
        return false;
    }
    v8::String::Utf8Value utf8(isolate, str);
    const std::string text(*utf8 != nullptr ? *utf8 : "",
        *utf8 != nullptr ? static_cast<std::size_t>(utf8.length()) : 0);
    if (!is_valid_task_priority(text)) {
        return throw_sched_type_error(isolate, prefix,
            "The provided value '" + text +
            "' is not a valid enum value of type TaskPriority.");
    }
    *out = text;
    return true;
}

bool convert_sched_enforce_range_ull(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> input,
    const std::string& prefix, std::uint64_t* out) {
    double number = 0.0;
    if (!access_user_operation(isolate, [&] {
            return input->NumberValue(context);
        }).To(&number)) {
        return false;
    }
    if (!std::isfinite(number)) {
        return throw_sched_type_error(isolate, prefix,
            "Value is outside the 'unsigned long long' value range.");
    }
    number = std::trunc(number);
    // WebIDL [EnforceRange] unsigned long long accepts [0, 2^53 - 1] (MAX_SAFE_INTEGER).
    constexpr double kMaxEnforceRangeUll = 9007199254740991.0;
    if (number < 0.0 || number > kMaxEnforceRangeUll) {
        return throw_sched_type_error(isolate, prefix,
            "Value is outside the 'unsigned long long' value range.");
    }
    *out = static_cast<std::uint64_t>(number);
    return true;
}

SchedulerHost* as_scheduler_host(v8::Local<v8::Value> value) noexcept {
    auto* backing = host_object_backing(value);
    if (backing == nullptr || backing->prototype_key() != "Scheduler") {
        return nullptr;
    }
    return static_cast<SchedulerHost*>(backing);
}

TaskSignalHost* as_task_signal(v8::Local<v8::Value> value) noexcept {
    auto* backing = host_object_backing(value);
    if (backing == nullptr || backing->prototype_key() != "TaskSignal") {
        return nullptr;
    }
    return static_cast<TaskSignalHost*>(backing);
}

v8::Local<v8::Private> tpce_brand_slot(v8::Isolate* isolate) {
    return v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.tpce.previousPriority"));
}

v8::MaybeLocal<v8::Object> create_tpce_event_object(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& type,
    bool bubbles, bool cancelable, bool composed,
    const std::string& previous_priority) {
    auto runtime_origin = access_runtime_scope(isolate);
    const auto proto_slot = v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.webidl.prototype.Event"));
    const auto ctor_slot = v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.webidl.constructor"));
    v8::Local<v8::Value> event_proto_val;
    v8::Local<v8::Value> event_ctor_val;
    if (!context->Global()->GetPrivate(context, proto_slot).ToLocal(&event_proto_val) ||
        !event_proto_val->IsObject()) {
        return {};
    }
    if (!event_proto_val.As<v8::Object>()->GetPrivate(context, ctor_slot)
             .ToLocal(&event_ctor_val) ||
        !event_ctor_val->IsFunction()) {
        return {};
    }
    auto tpce_proto = web_interface_prototype(isolate, context, "TaskPriorityChangeEvent");
    if (tpce_proto.IsEmpty()) return {};

    auto init = v8::Object::New(isolate);
    if (!init->SetPrototype(context, v8::Null(isolate)).FromMaybe(false) ||
        !init->CreateDataProperty(context,
            v8::String::NewFromUtf8Literal(isolate, "bubbles"),
            v8::Boolean::New(isolate, bubbles)).FromMaybe(false) ||
        !init->CreateDataProperty(context,
            v8::String::NewFromUtf8Literal(isolate, "cancelable"),
            v8::Boolean::New(isolate, cancelable)).FromMaybe(false) ||
        !init->CreateDataProperty(context,
            v8::String::NewFromUtf8Literal(isolate, "composed"),
            v8::Boolean::New(isolate, composed)).FromMaybe(false)) {
        return {};
    }
    v8::Local<v8::Value> ctor_args[] = {v8_string(isolate, type), init};
    v8::Local<v8::Object> event;
    if (!event_ctor_val.As<v8::Function>()->NewInstance(context, 2, ctor_args)
             .ToLocal(&event) ||
        !event->SetPrototype(context, tpce_proto).FromMaybe(false) ||
        !event->SetPrivate(context, tpce_brand_slot(isolate),
            v8_string(isolate, previous_priority)).FromMaybe(false)) {
        return {};
    }
    return event;
}

void tpce_previous_priority_getter(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    if (!info.This()->IsObject()) {
        const auto seq = access_trace_record(isolate,
            native_api_identity(isolate, context->Global()),
            "host_object", "TaskPriorityChangeEvent", "previousPriority",
            AccessOperation::kGet);
        AccessFunctionPropertyCompletion completion(
            isolate, seq, info, AccessOperation::kGet);
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Illegal invocation")));
        return;
    }
    auto receiver = info.This().As<v8::Object>();
    const auto has = receiver->HasPrivate(context, tpce_brand_slot(isolate));
    if (has.IsNothing() || !has.FromJust()) {
        const auto seq = access_trace_record(isolate,
            native_api_identity(isolate, receiver),
            "host_object", "TaskPriorityChangeEvent", "previousPriority",
            AccessOperation::kGet);
        AccessFunctionPropertyCompletion completion(
            isolate, seq, info, AccessOperation::kGet);
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Illegal invocation")));
        return;
    }
    const auto seq = access_trace_record(isolate,
        native_api_identity(isolate, receiver),
        "host_object", "TaskPriorityChangeEvent", "previousPriority",
        AccessOperation::kGet);
    AccessFunctionPropertyCompletion completion(
        isolate, seq, info, AccessOperation::kGet);
    v8::Local<v8::Value> val;
    if (receiver->GetPrivate(context, tpce_brand_slot(isolate)).ToLocal(&val)) {
        info.GetReturnValue().Set(val);
    }
}

bool move_ctor_to_end(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Object> prototype, const std::string& interface_name) {
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

v8::Local<v8::Private> sched_continuation_slot(v8::Isolate* isolate) {
    return v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.sched.continuation"));
}

v8::Local<v8::Object> make_continuation_state(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& fixed_priority,
    v8::Local<v8::Value> abort_signal_val,
    v8::Local<v8::Value> dynamic_signal_val) {
    auto runtime_origin = access_runtime_scope(isolate);
    auto payload = v8::Array::New(isolate, 3);
    if (!payload->CreateDataProperty(context, 0, v8_string(isolate, fixed_priority))
             .FromMaybe(false) ||
        !payload->CreateDataProperty(context, 1, abort_signal_val)
             .FromMaybe(false) ||
        !payload->CreateDataProperty(context, 2, dynamic_signal_val)
             .FromMaybe(false)) {
        return {};
    }
    auto carrier = v8::Object::New(isolate);
    if (!carrier->SetPrototype(context, v8::Null(isolate)).FromMaybe(false) ||
        !carrier->SetPrivate(context, sched_continuation_slot(isolate), payload)
             .FromMaybe(false)) {
        return {};
    }
    return carrier;
}

v8::Local<v8::Object> current_continuation_state(v8::Isolate* isolate,
    v8::Local<v8::Context> context, std::string* priority_out,
    AbortSignalHost** abort_signal_out,
    TaskSignalHost** dynamic_signal_out) {
    *priority_out = "user-visible";
    *abort_signal_out = nullptr;
    *dynamic_signal_out = nullptr;

    auto runtime_origin = access_runtime_scope(isolate);
    v8::Local<v8::Data> data = isolate->GetContinuationPreservedEmbedderDataV2();
    if (data.IsEmpty() || !data->IsValue() || !data.As<v8::Value>()->IsObject()) {
        return {};
    }
    auto carrier = data.As<v8::Value>().As<v8::Object>();
    const auto slot = sched_continuation_slot(isolate);
    if (!carrier->HasPrivate(context, slot).FromMaybe(false)) {
        return {};
    }
    v8::Local<v8::Value> payload_val;
    if (!carrier->GetPrivate(context, slot).ToLocal(&payload_val) ||
        !payload_val->IsArray()) {
        return {};
    }
    auto payload = payload_val.As<v8::Array>();
    v8::Local<v8::Value> prio_val;
    v8::Local<v8::Value> sig_val;
    v8::Local<v8::Value> dyn_val;
    if (!payload->Get(context, 0).ToLocal(&prio_val) ||
        !payload->Get(context, 1).ToLocal(&sig_val) ||
        !payload->Get(context, 2).ToLocal(&dyn_val)) {
        return {};
    }
    if (!sig_val->IsNullOrUndefined()) {
        *abort_signal_out = as_abort_signal(sig_val);
    }
    if (!dyn_val->IsNullOrUndefined()) {
        if (auto* ts = as_task_signal(dyn_val)) {
            *dynamic_signal_out = ts;
            *priority_out = ts->priority();
            return carrier;
        }
    }
    if (prio_val->IsString()) {
        v8::String::Utf8Value utf8(isolate, prio_val);
        if (*utf8 != nullptr) {
            const std::string p(*utf8, static_cast<std::size_t>(utf8.length()));
            if (is_valid_task_priority(p)) {
                *priority_out = p;
            }
        }
    }
    return carrier;
}

class ContinuationStateScope {
public:
    ContinuationStateScope(v8::Isolate* isolate, v8::Local<v8::Object> state)
        : isolate_(isolate),
          previous_(isolate->GetContinuationPreservedEmbedderDataV2()) {
        if (!state.IsEmpty()) {
            isolate_->SetContinuationPreservedEmbedderDataV2(state);
        } else {
            isolate_->SetContinuationPreservedEmbedderDataV2(v8::Local<v8::Data>());
        }
    }
    ~ContinuationStateScope() {
        if (!previous_.IsEmpty() &&
            !(previous_->IsValue() && previous_.As<v8::Value>()->IsUndefined())) {
            isolate_->SetContinuationPreservedEmbedderDataV2(previous_);
        } else {
            isolate_->SetContinuationPreservedEmbedderDataV2(v8::Local<v8::Data>());
        }
    }

private:
    v8::Isolate* isolate_;
    v8::Local<v8::Data> previous_;
};

void scheduler_timer_fire(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    if (!info.Data()->IsArray()) return;
    auto token = info.Data().As<v8::Array>();
    v8::Local<v8::Value> sched_val;
    v8::Local<v8::Value> id_val;
    if (!token->Get(context, 0).ToLocal(&sched_val) ||
        !token->Get(context, 1).ToLocal(&id_val) ||
        !id_val->IsBigInt()) {
        return;
    }
    auto* scheduler = as_scheduler_host(sched_val);
    bool lossless = false;
    const std::uint64_t task_id = id_val.As<v8::BigInt>()->Uint64Value(&lossless);
    if (scheduler == nullptr || !lossless || task_id == 0) {
        return;
    }

    auto task = scheduler->take_pending_task(task_id);
    if (!task || task->resolver.IsEmpty()) {
        return;
    }
    if (task->dynamic_signal != nullptr) {
        task->dynamic_signal->remove_dynamic_timer(task->timer_id);
        task->dynamic_signal = nullptr;
    }
    if (task->abort_signal != nullptr && task->abort_algorithm_id != 0) {
        task->abort_signal->remove_abort_algorithm(task->abort_algorithm_id);
        task->abort_signal = nullptr;
        task->abort_algorithm_id = 0;
    }

    const bool is_yield = task->is_yield;
    v8::Local<v8::Function> cb;
    if (!is_yield && !task->callback.IsEmpty()) {
        cb = task->callback.Get(isolate);
    }
    auto resolver = task->resolver.Get(isolate);
    v8::Local<v8::Object> cont_state;
    if (!task->continuation_state.IsEmpty()) {
        cont_state = task->continuation_state.Get(isolate);
    }
    task->reset_handles();
    task.reset();

    ContinuationStateScope cont_scope(isolate, cont_state);
    if (is_yield) {
        (void)access_user_operation(isolate, [&] {
            return resolver->Resolve(context, v8::Undefined(isolate));
        });
        return;
    }
    if (cb.IsEmpty()) return;

    v8::TryCatch caught(isolate);
    v8::Local<v8::Value> result;
    if (!sched_user_call(isolate, context, cb, v8::Undefined(isolate)).ToLocal(&result)) {
        if (!caught.CanContinue()) {
            caught.ReThrow();
            return;
        }
        auto exc = caught.Exception();
        caught.Reset();
        (void)access_user_operation(isolate, [&] {
            return resolver->Reject(context, exc);
        });
        return;
    }
    (void)access_user_operation(isolate, [&] {
        return resolver->Resolve(context, result);
    });
}

std::int64_t schedule_pending_scheduler_task(v8::Isolate* isolate,
    v8::Local<v8::Context> context, SchedulerHost* scheduler,
    std::unique_ptr<PendingSchedulerTask> task, std::int32_t delay_ms,
    const std::string& priority, bool is_continuation) {
    auto sched_wrapper = scheduler->wrapper_object(isolate);
    auto* state = state_from_isolate(isolate);
    if (sched_wrapper.IsEmpty() || state == nullptr) {
        task->reset_handles();
        return 0;
    }
    AbortSignalHost* abort_signal = task->abort_signal;
    TaskSignalHost* dynamic_signal = task->dynamic_signal;
    auto* stored = scheduler->store_pending_task(std::move(task));
    const std::uint64_t task_id = stored->id;

    auto runtime_origin = access_runtime_scope(isolate);
    auto token = v8::Array::New(isolate, 2);
    if (!token->CreateDataProperty(context, 0, sched_wrapper).FromMaybe(false) ||
        !token->CreateDataProperty(context, 1,
            v8::BigInt::NewFromUnsigned(isolate, task_id)).FromMaybe(false)) {
        if (auto failed = scheduler->take_pending_task(task_id)) {
            failed->reset_handles();
        }
        return 0;
    }
    v8::Local<v8::Function> fn;
    if (!v8::Function::New(context, &scheduler_timer_fire, token, 0,
            v8::ConstructorBehavior::kThrow).ToLocal(&fn)) {
        if (auto failed = scheduler->take_pending_task(task_id)) {
            failed->reset_handles();
        }
        return 0;
    }

    const std::int64_t timer_id = state->register_timer(
        fn, delay_ms, false, false, task_priority_rank(priority, is_continuation));
    stored->timer_id = timer_id;

    if (dynamic_signal != nullptr) {
        dynamic_signal->add_dynamic_timer(timer_id, is_continuation);
    }
    if (abort_signal != nullptr) {
        stored->abort_algorithm_id = abort_signal->add_abort_algorithm(
            [scheduler, task_id, abort_signal](
                v8::Isolate* iso, v8::Local<v8::Context> ctx) {
                auto cancelled = scheduler->take_pending_task(task_id);
                if (!cancelled || cancelled->resolver.IsEmpty()) return;
                if (cancelled->dynamic_signal != nullptr) {
                    cancelled->dynamic_signal->remove_dynamic_timer(cancelled->timer_id);
                    cancelled->dynamic_signal = nullptr;
                }
                if (auto* st = state_from_isolate(iso)) {
                    st->clear_internal_timer(cancelled->timer_id);
                }
                auto res = cancelled->resolver.Get(iso);
                cancelled->reset_handles();
                cancelled.reset();
                (void)access_user_operation(iso, [&] {
                    return res->Reject(ctx, abort_signal->reason_value(iso));
                });
            });
    }
    return timer_id;
}

void task_signal_any_static(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    watch_record(isolate, "TaskSignal", "any");
    const auto seq = access_trace_record(isolate,
        native_api_identity(isolate, info.This()->IsObject()
            ? info.This().As<v8::Object>() : context->Global()),
        "host_object", "TaskSignal", "any", AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
    const std::string prefix = "Failed to execute 'any' on 'TaskSignal': ";
    if (info.Length() < 1) {
        throw_sched_type_error(isolate, prefix,
            "1 argument required, but only 0 present.");
        return;
    }
    if (!info[0]->IsObject()) {
        throw_sched_type_error(isolate, prefix,
            "The provided value cannot be converted to a sequence.");
        return;
    }
    auto obj = info[0].As<v8::Object>();
    v8::Local<v8::Value> iter_fn;
    if (!sched_user_get(isolate, context, obj, v8::Symbol::GetIterator(isolate))
            .ToLocal(&iter_fn)) {
        return;
    }
    if (!is_callable(iter_fn)) {
        throw_sched_type_error(isolate, prefix,
            "The object must have a callable @@iterator property.");
        return;
    }
    v8::Local<v8::Value> iter_val;
    if (!sched_user_call(isolate, context, iter_fn, info[0]).ToLocal(&iter_val) ||
        !iter_val->IsObject()) {
        if (!iter_val.IsEmpty()) {
            throw_sched_type_error(isolate, prefix,
                "Iterator object must be an object.");
        }
        return;
    }
    auto iter = iter_val.As<v8::Object>();
    v8::Local<v8::Value> next_fn;
    if (!sched_user_get(isolate, context, iter, v8_string(isolate, "next"))
            .ToLocal(&next_fn) ||
        !is_callable(next_fn)) {
        if (!next_fn.IsEmpty()) {
            throw_sched_type_error(isolate, prefix,
                "Expected next() function on iterator.");
        }
        return;
    }
    std::vector<AbortSignalHost*> sources;
    for (;;) {
        v8::Local<v8::Value> step;
        if (!sched_user_call(isolate, context, next_fn, iter).ToLocal(&step) ||
            !step->IsObject()) {
            if (!step.IsEmpty()) {
                throw_sched_type_error(isolate, prefix,
                    "Expected iterator.next() to return an Object.");
            }
            return;
        }
        v8::Local<v8::Value> item;
        if (!sched_user_get(isolate, context, step.As<v8::Object>(),
                v8_string(isolate, "value")).ToLocal(&item)) {
            return;
        }
        v8::Local<v8::Value> done;
        if (!sched_user_get(isolate, context, step.As<v8::Object>(),
                v8_string(isolate, "done")).ToLocal(&done)) {
            return;
        }
        if (done->BooleanValue(isolate)) break;
        auto* sig = as_abort_signal(item);
        if (sig == nullptr) {
            throw_sched_type_error(isolate, prefix,
                "Failed to convert value to 'AbortSignal'.");
            return;
        }
        sources.push_back(sig);
    }

    std::string fixed_priority = "user-visible";
    TaskSignalHost* priority_source = nullptr;
    if (info.Length() >= 2 && !info[1]->IsNullOrUndefined()) {
        if (!info[1]->IsObject()) {
            throw_sched_type_error(isolate, prefix,
                "The provided value is not of type 'TaskSignalAnyInit'.");
            return;
        }
        v8::Local<v8::Value> prio_val;
        if (!sched_user_get(isolate, context, info[1].As<v8::Object>(),
                v8_string(isolate, "priority")).ToLocal(&prio_val)) {
            return;
        }
        if (!prio_val->IsUndefined()) {
            if (auto* ts = as_task_signal(prio_val)) {
                priority_source = ts;
                fixed_priority = ts->priority();
            } else if (!convert_task_priority(isolate, context, prio_val,
                    prefix + "Failed to read the 'priority' property from 'TaskSignalAnyInit': ",
                    &fixed_priority)) {
                return;
            }
        }
    }

    auto* owner = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    auto out_uptr = std::make_unique<TaskSignalHost>(owner, fixed_priority);
    auto* out_sig = out_uptr.get();
    auto out_obj = owner->adopt_host_object(isolate, context, std::move(out_uptr));
    out_sig->bind_wrapper(isolate, out_obj);

    if (priority_source != nullptr) {
        priority_source->add_dependent_task_signal(out_sig);
    }

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

}  // namespace

// --- TaskSignalHost ---

void TaskSignalHost::apply_priority_change(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& next_priority) {
    if (priority_ == next_priority) return;

    priority_changing_ = true;
    struct ChangingResetGuard {
        bool& changing;
        ~ChangingResetGuard() { changing = false; }
    } reset_guard{priority_changing_};

    const std::string prev_priority = priority_;
    priority_ = next_priority;
    if (auto* st = state_from_isolate(isolate)) {
        for (const auto& ref : dynamic_timers_) {
            st->update_internal_timer_priority(
                ref.timer_id, task_priority_rank(next_priority, ref.is_continuation));
        }
    }

    auto wrapper = wrapper_object(isolate);
    if (!wrapper.IsEmpty()) {
        v8::Local<v8::Object> event;
        if (create_tpce_event_object(isolate, context, "prioritychange",
                false, false, false, prev_priority).ToLocal(&event)) {
            dispatch_event(isolate, context, event, wrapper);
        }
    }

    const auto deps = dependent_signals_;
    for (TaskSignalHost* dep : deps) {
        if (dep != nullptr) {
            dep->apply_priority_change(isolate, context, next_priority);
        }
    }
}

v8::Local<v8::Value> TaskSignalHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name) {
    if (name == "priority") return v8_string(isolate, priority_);
    if (name == "onprioritychange") return get_event_handler(isolate, name);
    return AbortSignalHost::get_property(isolate, context, name);
}

void TaskSignalHost::set_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    v8::Local<v8::Value> value) {
    if (name == "onprioritychange") {
        set_event_handler(isolate, name, value);
        return;
    }
    AbortSignalHost::set_property(isolate, context, name, value);
}

// --- TaskControllerHost ---

std::unique_ptr<AbortSignalHost> TaskControllerHost::create_signal_host() {
    return std::make_unique<TaskSignalHost>(owner_, initial_priority_);
}

v8::Local<v8::Value> TaskControllerHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    if (name != "setPriority") {
        return AbortControllerHost::call_method(isolate, context, name, arguments);
    }
    watch_record(isolate, "TaskController", "setPriority");
    const std::string prefix =
        "Failed to execute 'setPriority' on 'TaskController': ";
    if (arguments.Length() < 1) {
        throw_sched_type_error(isolate, prefix,
            "1 argument required, but only 0 present.");
        return v8::Undefined(isolate);
    }
    std::string next_priority;
    if (!convert_task_priority(isolate, context, arguments[0], prefix, &next_priority)) {
        return v8::Undefined(isolate);
    }
    (void)ensure_signal(isolate, context);
    auto* task_sig = static_cast<TaskSignalHost*>(signal_host_);
    if (task_sig->priority_changing()) {
        isolate->ThrowException(make_dom_exception(isolate, context, owner_,
            "NotAllowedError",
            prefix + "Cannot change priority when a prioritychange event is in progress."));
        return v8::Undefined(isolate);
    }
    task_sig->apply_priority_change(isolate, context, next_priority);
    return v8::Undefined(isolate);
}

// --- SchedulerHost ---

v8::Local<v8::Value> SchedulerHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string&) {
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> SchedulerHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, "scheduler", name);
    if (name == "yield") {
        std::string priority = "user-visible";
        AbortSignalHost* inherited_abort = nullptr;
        TaskSignalHost* inherited_dynamic = nullptr;
        auto cont_state = current_continuation_state(
            isolate, context, &priority, &inherited_abort, &inherited_dynamic);
        if (inherited_abort != nullptr && inherited_abort->aborted()) {
            return sched_rejected_promise(isolate, context,
                inherited_abort->reason_value(isolate));
        }
        v8::Local<v8::Promise::Resolver> resolver;
        if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
            return v8::Undefined(isolate);
        }
        auto task = std::make_unique<PendingSchedulerTask>();
        task->is_yield = true;
        task->resolver.Reset(isolate, resolver);
        if (!cont_state.IsEmpty()) {
            task->continuation_state.Reset(isolate, cont_state);
        }
        task->abort_signal = inherited_abort;
        task->dynamic_signal = inherited_dynamic;
        (void)schedule_pending_scheduler_task(
            isolate, context, this, std::move(task), 0, priority,
            /*is_continuation=*/true);
        return resolver->GetPromise();
    }

    if (name != "postTask") {
        return v8::Undefined(isolate);
    }

    const std::string prefix =
        "Failed to execute 'postTask' on 'Scheduler': ";
    v8::TryCatch caught(isolate);
    if (arguments.Length() < 1) {
        throw_sched_type_error(isolate, prefix,
            "1 argument required, but only 0 present.");
        return sched_reject_from_try_catch(isolate, context, caught);
    }
    if (!is_callable(arguments[0])) {
        throw_sched_type_error(isolate, prefix,
            "parameter 1 is not of type 'Function'.");
        return sched_reject_from_try_catch(isolate, context, caught);
    }

    std::uint64_t delay_ms = 0;
    std::string priority = "user-visible";
    AbortSignalHost* signal = nullptr;
    TaskSignalHost* dynamic_task_signal = nullptr;
    v8::Local<v8::Value> signal_wrapper = v8::Undefined(isolate);
    v8::Local<v8::Value> dynamic_wrapper = v8::Undefined(isolate);

    if (arguments.Length() >= 2 && !arguments[1]->IsNullOrUndefined()) {
        if (!arguments[1]->IsObject()) {
            throw_sched_type_error(isolate, prefix,
                "The provided value is not of type 'SchedulerPostTaskOptions'.");
            return sched_reject_from_try_catch(isolate, context, caught);
        }
        auto opts = arguments[1].As<v8::Object>();
        // Lexicographical WebIDL order: delay, priority, signal.
        v8::Local<v8::Value> delay_val;
        if (!sched_user_get(isolate, context, opts, v8_string(isolate, "delay"))
                .ToLocal(&delay_val)) {
            return sched_reject_from_try_catch(isolate, context, caught);
        }
        if (!delay_val->IsUndefined()) {
            if (!convert_sched_enforce_range_ull(isolate, context, delay_val,
                    prefix + "Failed to read the 'delay' property from 'SchedulerPostTaskOptions': ",
                    &delay_ms)) {
                return sched_reject_from_try_catch(isolate, context, caught);
            }
        }

        v8::Local<v8::Value> prio_val;
        if (!sched_user_get(isolate, context, opts, v8_string(isolate, "priority"))
                .ToLocal(&prio_val)) {
            return sched_reject_from_try_catch(isolate, context, caught);
        }
        bool has_explicit_priority = false;
        if (!prio_val->IsUndefined()) {
            if (!convert_task_priority(isolate, context, prio_val,
                    prefix + "Failed to read the 'priority' property from 'SchedulerPostTaskOptions': ",
                    &priority)) {
                return sched_reject_from_try_catch(isolate, context, caught);
            }
            has_explicit_priority = true;
        }

        v8::Local<v8::Value> sig_val;
        if (!sched_user_get(isolate, context, opts, v8_string(isolate, "signal"))
                .ToLocal(&sig_val)) {
            return sched_reject_from_try_catch(isolate, context, caught);
        }
        if (!sig_val->IsNullOrUndefined()) {
            signal = as_abort_signal(sig_val);
            if (signal == nullptr) {
                throw_sched_type_error(isolate, prefix,
                    "Failed to read the 'signal' property from 'SchedulerPostTaskOptions': "
                    "Failed to convert value to 'AbortSignal'.");
                return sched_reject_from_try_catch(isolate, context, caught);
            }
            signal_wrapper = sig_val;
            if (!has_explicit_priority) {
                if (auto* ts = as_task_signal(sig_val)) {
                    priority = ts->priority();
                    dynamic_task_signal = ts;
                    dynamic_wrapper = sig_val;
                }
            }
        }
    }

    if (signal != nullptr && signal->aborted()) {
        return sched_rejected_promise(isolate, context,
            signal->reason_value(isolate));
    }

    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        return v8::Undefined(isolate);
    }
    auto task = std::make_unique<PendingSchedulerTask>();
    task->is_yield = false;
    task->callback.Reset(isolate, arguments[0].As<v8::Function>());
    task->resolver.Reset(isolate, resolver);
    auto cont_state = make_continuation_state(
        isolate, context, priority, signal_wrapper, dynamic_wrapper);
    if (!cont_state.IsEmpty()) {
        task->continuation_state.Reset(isolate, cont_state);
    }
    task->abort_signal = signal;
    task->dynamic_signal = dynamic_task_signal;

    const auto clamped = delay_ms >
            static_cast<std::uint64_t>(std::numeric_limits<std::int32_t>::max())
        ? std::numeric_limits<std::int32_t>::max()
        : static_cast<std::int32_t>(delay_ms);
    (void)schedule_pending_scheduler_task(
        isolate, context, this, std::move(task), clamped, priority,
        /*is_continuation=*/false);

    return resolver->GetPromise();
}

// --- Constructors & Installation ---

v8::Local<v8::Value> construct_task_controller(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    v8::Local<v8::Array> arguments) {
    const std::string prefix = "Failed to construct 'TaskController': ";
    std::string priority = "user-visible";
    if (arguments->Length() >= 1) {
        v8::Local<v8::Value> arg0;
        if (!arguments->Get(context, 0).ToLocal(&arg0)) {
            return v8::Undefined(isolate);
        }
        if (!arg0->IsNullOrUndefined()) {
            if (!arg0->IsObject()) {
                throw_sched_type_error(isolate, prefix,
                    "The provided value is not of type 'TaskControllerInit'.");
                return v8::Undefined(isolate);
            }
            v8::Local<v8::Value> prio_val;
            if (!sched_user_get(isolate, context, arg0.As<v8::Object>(),
                    v8_string(isolate, "priority")).ToLocal(&prio_val)) {
                return v8::Undefined(isolate);
            }
            if (!prio_val->IsUndefined()) {
                if (!convert_task_priority(isolate, context, prio_val,
                        prefix + "Failed to read the 'priority' property from 'TaskControllerInit': ",
                        &priority)) {
                    return v8::Undefined(isolate);
                }
            }
        }
    }
    auto ctrl_uptr = std::make_unique<TaskControllerHost>(owner, std::move(priority));
    auto* ctrl = ctrl_uptr.get();
    auto obj = owner->adopt_host_object(isolate, context, std::move(ctrl_uptr));
    (void)ctrl->ensure_signal(isolate, context);
    return obj;
}

v8::Local<v8::Value> construct_task_priority_change_event(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState*,
    v8::Local<v8::Array> arguments) {
    const std::string prefix = "Failed to construct 'TaskPriorityChangeEvent': ";
    if (arguments->Length() < 2) {
        throw_sched_type_error(isolate, prefix,
            "2 arguments required, but only " +
            std::to_string(arguments->Length()) + " present.");
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> arg0;
    v8::Local<v8::Value> arg1;
    if (!arguments->Get(context, 0).ToLocal(&arg0) ||
        !arguments->Get(context, 1).ToLocal(&arg1)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::String> type_str;
    if (!access_user_operation(isolate, [&] {
            return arg0->ToString(context);
        }).ToLocal(&type_str)) {
        return v8::Undefined(isolate);
    }
    v8::String::Utf8Value type_utf8(isolate, type_str);
    const std::string type_text(*type_utf8 != nullptr ? *type_utf8 : "",
        *type_utf8 != nullptr ? static_cast<std::size_t>(type_utf8.length()) : 0);

    if (!arg1->IsObject()) {
        throw_sched_type_error(isolate, prefix,
            "The provided value is not of type 'TaskPriorityChangeEventInit'.");
        return v8::Undefined(isolate);
    }
    auto dict = arg1.As<v8::Object>();
    // Lexicographical WebIDL EventInit + TaskPriorityChangeEventInit order:
    // bubbles, cancelable, composed, previousPriority.
    v8::Local<v8::Value> bubbles_val;
    v8::Local<v8::Value> cancelable_val;
    v8::Local<v8::Value> composed_val;
    v8::Local<v8::Value> prev_val;
    if (!sched_user_get(isolate, context, dict, v8_string(isolate, "bubbles"))
            .ToLocal(&bubbles_val)) {
        return v8::Undefined(isolate);
    }
    const bool bubbles = bubbles_val->BooleanValue(isolate);
    if (!sched_user_get(isolate, context, dict, v8_string(isolate, "cancelable"))
            .ToLocal(&cancelable_val)) {
        return v8::Undefined(isolate);
    }
    const bool cancelable = cancelable_val->BooleanValue(isolate);
    if (!sched_user_get(isolate, context, dict, v8_string(isolate, "composed"))
            .ToLocal(&composed_val)) {
        return v8::Undefined(isolate);
    }
    const bool composed = composed_val->BooleanValue(isolate);
    if (!sched_user_get(isolate, context, dict, v8_string(isolate, "previousPriority"))
            .ToLocal(&prev_val)) {
        return v8::Undefined(isolate);
    }
    if (prev_val->IsUndefined()) {
        throw_sched_type_error(isolate, prefix,
            "Failed to read the 'previousPriority' property from "
            "'TaskPriorityChangeEventInit': Required member is undefined.");
        return v8::Undefined(isolate);
    }
    std::string prev_priority;
    if (!convert_task_priority(isolate, context, prev_val,
            prefix + "Failed to read the 'previousPriority' property from 'TaskPriorityChangeEventInit': ",
            &prev_priority)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Object> event;
    if (!create_tpce_event_object(isolate, context, type_text,
            bubbles, cancelable, composed, prev_priority).ToLocal(&event)) {
        return v8::Undefined(isolate);
    }
    return event;
}

bool install_scheduler_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner) {
    SchedulerHost sched_decl(owner);
    TaskControllerHost tc_decl(owner);
    TaskSignalHost ts_decl(owner);

    for (HostObject* host : {static_cast<HostObject*>(&sched_decl),
                             static_cast<HostObject*>(&tc_decl),
                             static_cast<HostObject*>(&ts_decl)}) {
        auto proto = web_interface_prototype(isolate, context, host->prototype_key());
        if (proto.IsEmpty()) return false;
        install_host_interface(isolate, context, host);
        if (!move_ctor_to_end(isolate, context, proto, host->prototype_key())) {
            return false;
        }
    }

    // TaskSignal.any static method
    auto owner_ext = v8::External::New(isolate, owner, v8::kExternalPointerTypeTagDefault);
    auto ts_proto = web_interface_prototype(isolate, context, "TaskSignal");
    v8::Local<v8::Value> ts_ctor;
    v8::Local<v8::Function> any_fn;
    if (ts_proto.IsEmpty() ||
        !ts_proto->Get(context, v8_string(isolate, "constructor")).ToLocal(&ts_ctor) ||
        !ts_ctor->IsFunction() ||
        !make_web_method(isolate, context, "any", 1, &task_signal_any_static, owner_ext)
             .ToLocal(&any_fn) ||
        !install_traced_api_data_property(isolate, context, ts_ctor.As<v8::Object>(),
            "any", any_fn, native_api_identity(isolate, ts_ctor.As<v8::Object>()),
            "host_object", "TaskSignal")) {
        return false;
    }

    // TaskPriorityChangeEvent (inherits registered Event)
    auto tpce_proto = web_interface_prototype(isolate, context, "TaskPriorityChangeEvent");
    if (tpce_proto.IsEmpty()) return false;
    v8::Local<v8::Function> prev_getter;
    if (!make_web_method(isolate, context, "get previousPriority", 0,
            &tpce_previous_priority_getter).ToLocal(&prev_getter)) {
        return false;
    }
    v8::PropertyDescriptor prev_desc(prev_getter, v8::Undefined(isolate));
    prev_desc.set_enumerable(true);
    prev_desc.set_configurable(true);
    if (!tpce_proto->DefineProperty(
             context, v8_string(isolate, "previousPriority").As<v8::Name>(), prev_desc)
             .FromMaybe(false) ||
        !move_ctor_to_end(isolate, context, tpce_proto, "TaskPriorityChangeEvent")) {
        return false;
    }

    // Install window.scheduler as a ReadOnly | DontDelete data property on global.
    isolate->SetContinuationPreservedEmbedderDataV2(v8::Local<v8::Data>());
    auto sched_host = std::make_unique<SchedulerHost>(owner);
    auto* sched_raw = sched_host.get();
    auto sched_obj = owner->adopt_host_object(isolate, context, std::move(sched_host));
    sched_raw->bind_wrapper(isolate, sched_obj);
    return install_traced_window_data_property(isolate, context, context->Global(),
        "scheduler", sched_obj,
        static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete));
}

}  // namespace iv8::browser
