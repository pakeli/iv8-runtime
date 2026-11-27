#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../internal/common.h"
#include "../internal/trace_hooks.h"
#include "../web/performance.h"
#include "../window/window_trace_support.h"
#include "iv8/page_state.h"
#include "iv8/host_object.h"
#include "iv8/access_summary.h"
#include "iv8/access_origin.h"
#include "iv8/web_interface.h"
#include "iv8/web_method.h"
#include "iv8/browser_compatibility.h"

namespace py = pybind11;

namespace iv8 {

namespace browser {

class ConsoleHost;

inline v8::Local<v8::Private> console_task_brand_slot(v8::Isolate* isolate) {
    return v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.console.task.brand"));
}

inline void console_task_run_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (info.IsConstructCall()) {
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8Literal(
                isolate, "'run' called with illegal receiver.")));
        return;
    }
    if (!info.This()->IsObject() ||
        !info.This().As<v8::Object>()->HasPrivate(
            context, console_task_brand_slot(isolate)).FromMaybe(false)) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Illegal invocation")));
        return;
    }
    if (info.Length() < 1 ||
        !(info[0]->IsObject() && info[0].As<v8::Object>()->IsCallable())) {
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8Literal(
                isolate, "First argument must be a function.")));
        return;
    }
    v8::Local<v8::Value> result;
    if (access_user_operation(isolate, [&] {
            return info[0].As<v8::Object>()->CallAsFunction(
                context, v8::Undefined(isolate), 0, nullptr);
        }).ToLocal(&result)) {
        info.GetReturnValue().Set(result);
    }
}

class ConsoleHost : public HostObject {
public:
    std::string global_name() const override { return "console"; }
    std::string trace_object_type() const override { return "console"; }

    std::vector<std::string> property_names() const override { return {}; }

    std::vector<std::string> method_names() const override {
        return {
            "debug", "error", "info", "log", "warn",
            "dir", "dirxml", "table", "trace",
            "group", "groupCollapsed", "groupEnd",
            "clear", "count", "countReset", "assert",
            "profile", "profileEnd",
            "time", "timeLog", "timeEnd", "timeStamp",
            "context", "createTask"};
    }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate, v8::Local<v8::Context>,
                                      const std::string&) override {
        return v8::Undefined(isolate);
    }

    v8::Local<v8::Value> dispatch_logging_method(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& args,
        const std::string& context_prefix = std::string()) {
        const auto prefix = context_prefix.empty()
            ? std::string() : "[" + context_prefix + "] ";
        if (name == "groupEnd" || name == "clear" || name == "profile" ||
            name == "profileEnd" || name == "timeStamp") {
            return v8::Undefined(isolate);
        }
        if (name == "countReset") {
            const std::string label = prefix + console_label(isolate, context, args);
            counts_.erase(label);
            return v8::Undefined(isolate);
        }
        if (name == "timeLog") {
            const std::string raw_label = console_label(isolate, context, args);
            const std::string key = prefix + raw_label;
            const auto started = timers_.find(key);
            if (started != timers_.end()) {
                const auto* state = state_from_isolate(isolate);
                const auto current = state == nullptr ? 0 : state->event_loop_time_ms();
                std::string message = key + ": " + std::to_string(current - started->second) + " ms";
                if (args.Length() > 1) {
                    message += " " + join_args(isolate, context, args, 1);
                }
                log_message("info", message);
            }
            return v8::Undefined(isolate);
        }
        if (name == "assert") {
            const bool condition =
                args.Length() > 0 && args[0]->BooleanValue(isolate);
            if (condition) {
                return v8::Undefined(isolate);
            }
            const std::string detail = join_args(isolate, context, args, 1);
            log_message("error", prefix + (detail.empty()
                                               ? std::string("Assertion failed")
                                               : "Assertion failed: " + detail));
            return v8::Undefined(isolate);
        }
        if (name == "count") {
            const std::string key = prefix + console_label(isolate, context, args);
            const int next = ++counts_[key];
            log_message("info", key + ": " + std::to_string(next));
            return v8::Undefined(isolate);
        }
        if (name == "time" || name == "timeEnd") {
            const std::string raw_label = console_label(isolate, context, args);
            const std::string key = prefix + raw_label;
            log_message("info", prefix +
                        (name == "time" ? "Timer started: "
                                        : "Timer ended: ") + raw_label);
            const auto* state = state_from_isolate(isolate);
            if (name == "time") timers_.emplace(key, state == nullptr ? 0 : state->event_loop_time_ms());
            else timers_.erase(key);
            return v8::Undefined(isolate);
        }
        const std::string message = prefix + join_args(isolate, context, args);
        const char* level = "info";
        if (name == "debug" || name == "trace") {
            level = "debug";
        } else if (name == "warn") {
            level = "warning";
        } else if (name == "error") {
            level = "error";
        }
        log_message(level, message);
        return v8::Undefined(isolate);
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& args) override {
        watch_record(isolate, "console", name);
        if (name == "createTask") {
            if (args.Length() < 1 || !args[0]->IsString() ||
                args[0].As<v8::String>()->Length() == 0) {
                isolate->ThrowException(v8::Exception::Error(
                    v8::String::NewFromUtf8Literal(
                        isolate, "First argument must be a non-empty string.")));
                return v8::Undefined(isolate);
            }
            auto runtime_origin = access_runtime_scope(isolate);
            auto tpl = v8::FunctionTemplate::New(isolate);
            v8::Local<v8::Object> task;
            if (!tpl->InstanceTemplate()->NewInstance(context).ToLocal(&task)) {
                return v8::Undefined(isolate);
            }
            (void)task->SetPrivate(context, console_task_brand_slot(isolate),
                v8::True(isolate));
            v8::Local<v8::Function> run_fn;
            if (!v8::Function::New(context, &console_task_run_callback,
                    v8::Local<v8::Value>(), 0,
                    v8::ConstructorBehavior::kAllow).ToLocal(&run_fn)) {
                return v8::Undefined(isolate);
            }
            run_fn->SetName(v8::String::NewFromUtf8Literal(isolate, "run"));
            (void)task->CreateDataProperty(context,
                v8::String::NewFromUtf8Literal(isolate, "run"), run_fn);
            return task;
        }
        if (name == "context") {
            const std::string ctx_name =
                (args.Length() == 0 || args[0]->IsUndefined())
                    ? std::string("anonymous")
                    : stringify_argument(isolate, context, args[0]);
            auto runtime_origin = access_runtime_scope(isolate);
            auto tpl = v8::FunctionTemplate::New(isolate);
            v8::Local<v8::Object> ctx_obj;
            if (!tpl->InstanceTemplate()->NewInstance(context).ToLocal(&ctx_obj)) {
                return v8::Undefined(isolate);
            }
            static const char* kCtxMethods[] = {
                "dir", "dirxml", "table", "groupEnd", "clear",
                "count", "countReset", "profile", "profileEnd",
                "debug", "error", "info", "log", "warn", "trace",
                "group", "groupCollapsed",
                "time", "timeLog", "timeEnd", "timeStamp", "assert"};
            auto host_ext = v8::External::New(
                isolate, this, v8::kExternalPointerTypeTagDefault);
            auto prefix_val = v8_string(isolate, ctx_name);
            for (const char* m : kCtxMethods) {
                auto data = v8::Array::New(isolate, 3);
                (void)data->CreateDataProperty(context, 0, host_ext);
                (void)data->CreateDataProperty(context, 1, v8_string(isolate, m));
                (void)data->CreateDataProperty(context, 2, prefix_val);
                v8::Local<v8::Function> fn;
                if (!make_web_method(isolate, context, m, 1,
                        &context_method_callback, data).ToLocal(&fn)) {
                    return v8::Undefined(isolate);
                }
                (void)ctx_obj->CreateDataProperty(
                    context, v8_string(isolate, m).As<v8::Name>(), fn);
            }
            return ctx_obj;
        }
        return dispatch_logging_method(isolate, context, name, args);
    }

private:
    static void context_method_callback(
        const v8::FunctionCallbackInfo<v8::Value>& info) {
        v8::Isolate* isolate = info.GetIsolate();
        v8::Local<v8::Context> context = isolate->GetCurrentContext();
        auto data = info.Data().As<v8::Array>();
        v8::Local<v8::Value> host_ext;
        v8::Local<v8::Value> m_val;
        v8::Local<v8::Value> p_val;
        if (!data->Get(context, 0).ToLocal(&host_ext) || !host_ext->IsExternal() ||
            !data->Get(context, 1).ToLocal(&m_val) ||
            !data->Get(context, 2).ToLocal(&p_val)) {
            return;
        }
        auto* self = static_cast<ConsoleHost*>(
            host_ext.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
        v8::String::Utf8Value m_utf8(isolate, m_val);
        v8::String::Utf8Value p_utf8(isolate, p_val);
        const std::string m_str(*m_utf8 != nullptr ? *m_utf8 : "");
        const std::string p_str(*p_utf8 != nullptr ? *p_utf8 : "");
        info.GetReturnValue().Set(
            self->dispatch_logging_method(isolate, context, m_str, info, p_str));
    }

    static std::string stringify_argument(v8::Isolate* isolate,
                                           v8::Local<v8::Context> context,
                                           v8::Local<v8::Value> value) {
        v8::TryCatch try_catch(isolate);
        v8::Local<v8::String> str;
        if (access_user_operation(isolate, [&] {
                return value->ToString(context);
            }).ToLocal(&str)) {
            v8::String::Utf8Value utf8(isolate, str);
            if (*utf8 != nullptr) {
                return std::string(*utf8, static_cast<std::size_t>(utf8.length()));
            }
            return {};
        }
        try_catch.Reset();
        return "<unprintable>";
    }

    static std::string console_label(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context,
                                      const v8::FunctionCallbackInfo<v8::Value>& args) {
        return args.Length() == 0 || args[0]->IsUndefined()
                   ? std::string("default")
                   : stringify_argument(isolate, context, args[0]);
    }

    static void log_message(const char* level, const std::string& message) {
        py::gil_scoped_acquire gil;
        try {
            py::module_::import("logging")
                .attr("getLogger")("iv8.console")
                .attr(level)(py::str(message));
        } catch (const py::error_already_set&) {
        }
    }

    static std::string join_args(v8::Isolate* isolate,
                                 v8::Local<v8::Context> context,
                                 const v8::FunctionCallbackInfo<v8::Value>& args,
                                 int start = 0) {
        std::string message;
        for (int i = start; i < args.Length(); ++i) {
            if (i > start) {
                message += ' ';
            }
            message += stringify_argument(isolate, context, args[i]);
        }
        return message;
    }

    std::unordered_map<std::string, int> counts_;
    std::unordered_map<std::string, std::int64_t> timers_;
};

inline void console_memory_getter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    auto* host = static_cast<ConsoleHost*>(
        info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    const std::uint64_t seq = access_trace_record(
        isolate, host, "console", "console", "memory", AccessOperation::kGet);
    AccessFunctionPropertyCompletion completion(
        isolate, seq, info, AccessOperation::kGet);
    info.GetReturnValue().Set(create_memory_info_object(isolate, context));
}

inline void console_memory_setter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* host = static_cast<ConsoleHost*>(
        info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    const std::uint64_t seq = access_trace_record(
        isolate, host, "console", "console", "memory", AccessOperation::kSet);
    AccessFunctionPropertyCompletion completion(
        isolate, seq, info, AccessOperation::kSet);
}

inline bool install_console_extras(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> console_object,
    ConsoleHost* host) {
    auto host_ext = v8::External::New(
        isolate, host, v8::kExternalPointerTypeTagDefault);
    v8::Local<v8::Function> mem_get;
    v8::Local<v8::Function> mem_set;
    if (!make_web_method(isolate, context, "", 0, &console_memory_getter, host_ext)
             .ToLocal(&mem_get) ||
        !make_web_method(isolate, context, "", 1, &console_memory_setter, host_ext)
             .ToLocal(&mem_set)) {
        return false;
    }
    v8::PropertyDescriptor mem_desc(mem_get, mem_set);
    mem_desc.set_enumerable(true);
    mem_desc.set_configurable(true);
    if (!console_object->DefineProperty(
             context, v8::String::NewFromUtf8Literal(isolate, "memory"), mem_desc)
             .FromMaybe(false)) {
        return false;
    }
    return install_traced_api_named_data_property(isolate, context, console_object,
        v8::Symbol::GetToStringTag(isolate),
        v8::String::NewFromUtf8Literal(isolate, "console"),
        host, "console", "console",
        static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum));
}

}  // namespace browser
}  // namespace iv8
