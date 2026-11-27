#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "observable.h"

namespace iv8::browser {

class TaskSignalHost : public AbortSignalHost {
public:
    struct DynamicTimerRef {
        std::int64_t timer_id = 0;
        bool is_continuation = false;
    };

    TaskSignalHost(PageState* owner, std::string priority = "user-visible")
        : AbortSignalHost(owner), priority_(std::move(priority)) {}

    std::string prototype_key() const override { return "TaskSignal"; }
    std::string trace_object_type() const override { return "host_object"; }
    std::string trace_label() const override { return "TaskSignal"; }
    std::vector<std::string> property_names() const override {
        return {"priority", "onprioritychange"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"onprioritychange"};
    }
    std::vector<std::string> method_names() const override { return {}; }
    void release_v8_handles() noexcept override {
        AbortSignalHost::release_v8_handles();
        dependent_signals_.clear();
        dynamic_timers_.clear();
    }

    const std::string& priority() const noexcept { return priority_; }
    bool priority_changing() const noexcept { return priority_changing_; }
    void add_dependent_task_signal(TaskSignalHost* dependent) {
        if (dependent != nullptr) {
            dependent_signals_.push_back(dependent);
        }
    }
    void add_dynamic_timer(std::int64_t timer_id, bool is_continuation) {
        if (timer_id > 0) {
            dynamic_timers_.push_back({timer_id, is_continuation});
        }
    }
    void remove_dynamic_timer(std::int64_t timer_id) {
        dynamic_timers_.erase(
            std::remove_if(dynamic_timers_.begin(), dynamic_timers_.end(),
                [timer_id](const DynamicTimerRef& ref) {
                    return ref.timer_id == timer_id;
                }),
            dynamic_timers_.end());
    }
    void apply_priority_change(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& next_priority);

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name, v8::Local<v8::Value> value) override;

private:
    std::string priority_;
    bool priority_changing_ = false;
    std::vector<TaskSignalHost*> dependent_signals_;
    std::vector<DynamicTimerRef> dynamic_timers_;
};

class TaskControllerHost : public AbortControllerHost {
public:
    TaskControllerHost(PageState* owner, std::string initial_priority = "user-visible")
        : AbortControllerHost(owner),
          initial_priority_(std::move(initial_priority)) {}

    std::string prototype_key() const override { return "TaskController"; }
    std::string trace_object_type() const override { return "host_object"; }
    std::string trace_label() const override { return "TaskController"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override {
        return {"setPriority"};
    }

    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

protected:
    std::unique_ptr<AbortSignalHost> create_signal_host() override;

private:
    std::string initial_priority_;
};

struct PendingSchedulerTask {
    std::uint64_t id = 0;
    bool is_yield = false;
    v8::Global<v8::Function> callback;
    v8::Global<v8::Promise::Resolver> resolver;
    v8::Global<v8::Object> continuation_state;
    AbortSignalHost* abort_signal = nullptr;
    std::uint64_t abort_algorithm_id = 0;
    TaskSignalHost* dynamic_signal = nullptr;
    std::int64_t timer_id = 0;

    void reset_handles() noexcept {
        callback.Reset();
        resolver.Reset();
        continuation_state.Reset();
        abort_signal = nullptr;
        abort_algorithm_id = 0;
        dynamic_signal = nullptr;
        timer_id = 0;
    }
};

class SchedulerHost : public HostObject {
public:
    explicit SchedulerHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "Scheduler"; }
    std::string trace_object_type() const override { return "host_object"; }
    std::string trace_label() const override { return "scheduler"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override {
        return {"postTask", "yield"};
    }
    void release_v8_handles() noexcept override {
        wrapper_.Reset();
        for (auto& entry : pending_tasks_) {
            if (entry.second) {
                entry.second->reset_handles();
            }
        }
        pending_tasks_.clear();
    }

    void bind_wrapper(v8::Isolate* isolate, v8::Local<v8::Object> wrapper) {
        wrapper_.Reset(isolate, wrapper);
    }
    v8::Local<v8::Object> wrapper_object(v8::Isolate* isolate) const {
        if (wrapper_.IsEmpty()) return {};
        return wrapper_.Get(isolate);
    }
    PendingSchedulerTask* store_pending_task(std::unique_ptr<PendingSchedulerTask> task) {
        const std::uint64_t id = next_task_id_++;
        task->id = id;
        auto* raw = task.get();
        pending_tasks_.emplace(id, std::move(task));
        return raw;
    }
    std::unique_ptr<PendingSchedulerTask> take_pending_task(std::uint64_t id) {
        auto it = pending_tasks_.find(id);
        if (it == pending_tasks_.end()) return nullptr;
        auto out = std::move(it->second);
        pending_tasks_.erase(it);
        return out;
    }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    PageState* owner_;
    v8::Global<v8::Object> wrapper_;
    std::uint64_t next_task_id_ = 1;
    std::unordered_map<std::uint64_t, std::unique_ptr<PendingSchedulerTask>> pending_tasks_;
};

v8::Local<v8::Value> construct_task_controller(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    v8::Local<v8::Array> arguments);

v8::Local<v8::Value> construct_task_priority_change_event(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    v8::Local<v8::Array> arguments);

bool install_scheduler_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner);

}  // namespace iv8::browser
