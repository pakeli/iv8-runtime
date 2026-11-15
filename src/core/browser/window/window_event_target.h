#pragma once

#include "../internal/trace_hooks.h"
#include "../host/event_target.h"

namespace iv8::browser {

class WindowEventTarget : public EventTargetHost {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "Window"; }
    std::string trace_object_type() const override { return "window"; }
    std::string trace_label() const override { return "window"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override { return event_method_names(); }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate, v8::Local<v8::Context>,
                                      const std::string&) override {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& args) override {
        watch_record(isolate, "window", name);
        v8::Local<v8::Value> result;
        if (handle_event_method(isolate, context, name, args, result)) return result;
        return v8::Undefined(isolate);
    }

};

}  // namespace iv8::browser
