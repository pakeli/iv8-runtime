#pragma once

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "iv8/host_object.h"
#include "../host/event_target.h"

namespace iv8 { class PageState; }

namespace iv8::browser {

class AbortSignalHost : public EventTargetHost {
public:
    explicit AbortSignalHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "AbortSignal"; }
    std::string trace_object_type() const override { return "abort_signal"; }
    std::string trace_label() const override { return "AbortSignal"; }
    std::vector<std::string> property_names() const override {
        return {"aborted", "reason", "onabort"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"onabort"};
    }
    std::vector<std::string> method_names() const override {
        auto names = event_method_names();
        names.push_back("throwIfAborted");
        return names;
    }
    void release_v8_handles() noexcept override {
        EventTargetHost::release_v8_handles();
        wrapper_.Reset();
        reason_.Reset();
        algorithms_.clear();
    }

    void bind_wrapper(v8::Isolate* isolate, v8::Local<v8::Object> wrapper) {
        wrapper_.Reset(isolate, wrapper);
    }
    v8::Local<v8::Object> wrapper_object(v8::Isolate* isolate) const {
        if (wrapper_.IsEmpty()) return {};
        return wrapper_.Get(isolate);
    }
    bool aborted() const noexcept { return aborted_; }
    v8::Local<v8::Value> reason_value(v8::Isolate* isolate) const {
        if (!aborted_ || reason_.IsEmpty()) return v8::Undefined(isolate);
        return reason_.Get(isolate);
    }
    void initialize_aborted(v8::Isolate* isolate, v8::Local<v8::Context> context,
        v8::Local<v8::Value> reason);
    void abort_with_reason(v8::Isolate* isolate, v8::Local<v8::Context> context,
        v8::Local<v8::Value> reason);
    std::uint64_t add_abort_algorithm(
        std::function<void(v8::Isolate*, v8::Local<v8::Context>)> algorithm);
    void remove_abort_algorithm(std::uint64_t id);

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name, v8::Local<v8::Value> value) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

protected:
    PageState* owner_;

private:
    struct AbortAlgorithmEntry {
        std::uint64_t id = 0;
        std::function<void(v8::Isolate*, v8::Local<v8::Context>)> run;
    };
    bool aborted_ = false;
    v8::Global<v8::Object> wrapper_;
    v8::Global<v8::Value> reason_;
    std::uint64_t next_algorithm_id_ = 1;
    std::vector<AbortAlgorithmEntry> algorithms_;
};

class AbortControllerHost : public HostObject {
public:
    explicit AbortControllerHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "AbortController"; }
    std::string trace_object_type() const override { return "abort_controller"; }
    std::string trace_label() const override { return "AbortController"; }
    std::vector<std::string> property_names() const override { return {"signal"}; }
    std::vector<std::string> method_names() const override { return {"abort"}; }
    void release_v8_handles() noexcept override { signal_object_.Reset(); }

    v8::Local<v8::Object> ensure_signal(v8::Isolate* isolate,
        v8::Local<v8::Context> context);

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

protected:
    virtual std::unique_ptr<AbortSignalHost> create_signal_host();

    PageState* owner_;
    AbortSignalHost* signal_host_ = nullptr;
    v8::Global<v8::Object> signal_object_;
};

class ObservableHost;

class SubscriberHost : public HostObject {
public:
    explicit SubscriberHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "Subscriber"; }
    std::string trace_object_type() const override { return "subscriber"; }
    std::string trace_label() const override { return "Subscriber"; }
    std::vector<std::string> property_names() const override {
        return {"active", "signal"};
    }
    std::vector<std::string> method_names() const override {
        return {"addTeardown", "complete", "error", "next"};
    }
    void release_v8_handles() noexcept override {
        for (auto& obs : observers_) {
            obs.next_cb.Reset();
            obs.error_cb.Reset();
            obs.complete_cb.Reset();
        }
        observers_.clear();
        signal_object_.Reset();
        for (auto& fn : js_teardowns_) fn.Reset();
        js_teardowns_.clear();
        native_teardowns_.clear();
        owner_observable_ = nullptr;
    }

    bool active() const noexcept { return active_; }
    void set_owner_observable(ObservableHost* obs) noexcept { owner_observable_ = obs; }
    v8::Local<v8::Object> ensure_signal(v8::Isolate* isolate,
        v8::Local<v8::Context> context);
    void add_observer(v8::Isolate* isolate, v8::Local<v8::Context> context,
        v8::Local<v8::Function> next_cb,
        v8::Local<v8::Function> error_cb,
        v8::Local<v8::Function> complete_cb,
        AbortSignalHost* external_signal);
    void detach_observer(v8::Isolate* isolate, v8::Local<v8::Context> context,
        std::size_t index, v8::Local<v8::Value> abort_reason);
    void add_native_teardown(
        std::function<void(v8::Isolate*, v8::Local<v8::Context>)> teardown);

    void emit_next(v8::Isolate* isolate, v8::Local<v8::Context> context,
        v8::Local<v8::Value> value);
    void emit_error(v8::Isolate* isolate, v8::Local<v8::Context> context,
        v8::Local<v8::Value> error);
    void emit_complete(v8::Isolate* isolate, v8::Local<v8::Context> context);
    void close_subscription(v8::Isolate* isolate, v8::Local<v8::Context> context,
        v8::Local<v8::Value> abort_reason);

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    struct AttachedObserver {
        v8::Global<v8::Function> next_cb;
        v8::Global<v8::Function> error_cb;
        v8::Global<v8::Function> complete_cb;
        bool detached = false;
    };

    PageState* owner_;
    ObservableHost* owner_observable_ = nullptr;
    bool active_ = true;
    AbortSignalHost* signal_host_ = nullptr;
    v8::Global<v8::Object> signal_object_;
    std::vector<AttachedObserver> observers_;
    std::vector<v8::Global<v8::Function>> js_teardowns_;
    std::vector<std::function<void(v8::Isolate*, v8::Local<v8::Context>)>> native_teardowns_;
};

class ObservableHost : public HostObject {
public:
    using NativeSubscribe = std::function<void(
        v8::Isolate*, v8::Local<v8::Context>, ObservableHost*,
        SubscriberHost*, v8::Local<v8::Object>)>;

    ObservableHost(PageState* owner, v8::Isolate* isolate,
        v8::Local<v8::Function> js_callback)
        : owner_(owner) {
        if (!js_callback.IsEmpty()) {
            js_callback_.Reset(isolate, js_callback);
        }
    }

    ObservableHost(PageState* owner, NativeSubscribe native_subscribe)
        : owner_(owner), native_subscribe_(std::move(native_subscribe)) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "Observable"; }
    std::string trace_object_type() const override { return "observable"; }
    std::string trace_label() const override { return "Observable"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override {
        return {
            "catch", "drop", "every", "filter", "finally", "find", "first",
            "flatMap", "forEach", "inspect", "last", "map", "reduce", "some",
            "subscribe", "switchMap", "take", "takeUntil", "toArray"};
    }
    void release_v8_handles() noexcept override {
        js_callback_.Reset();
        for (auto& item : captured_) item.Reset();
        captured_.clear();
        native_subscribe_ = nullptr;
        active_subscriber_ = nullptr;
    }

    void clear_active_subscriber(SubscriberHost* sub) noexcept {
        if (active_subscriber_ == sub) active_subscriber_ = nullptr;
    }

    std::size_t capture_value(v8::Isolate* isolate, v8::Local<v8::Value> value) {
        captured_.emplace_back(isolate, value);
        return captured_.size() - 1;
    }
    v8::Local<v8::Value> captured_value(v8::Isolate* isolate, std::size_t index) const {
        if (index >= captured_.size() || captured_[index].IsEmpty()) {
            return v8::Undefined(isolate);
        }
        return captured_[index].Get(isolate);
    }

    void subscribe_internal(v8::Isolate* isolate, v8::Local<v8::Context> context,
        v8::Local<v8::Function> next_cb, v8::Local<v8::Function> error_cb,
        v8::Local<v8::Function> complete_cb, AbortSignalHost* external_signal);

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    PageState* owner_;
    SubscriberHost* active_subscriber_ = nullptr;
    v8::Global<v8::Function> js_callback_;
    std::vector<v8::Global<v8::Value>> captured_;
    NativeSubscribe native_subscribe_;
};

AbortSignalHost* as_abort_signal(v8::Local<v8::Value> value) noexcept;

v8::Local<v8::Value> construct_abort_controller(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    v8::Local<v8::Array> arguments);

v8::Local<v8::Value> construct_observable(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    v8::Local<v8::Array> arguments);

v8::Local<v8::Value> event_target_when(EventTargetHost* target,
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const v8::FunctionCallbackInfo<v8::Value>& arguments);

bool install_observable_and_abort_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner);

}  // namespace iv8::browser
