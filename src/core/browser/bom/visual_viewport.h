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
#include "navigator_parts.h"
#include "iv8/page_state.h"
#include "iv8/host_object.h"
#include "iv8/access_summary.h"
#include "iv8/web_interface.h"
#include "iv8/browser_compatibility.h"

namespace iv8 {

namespace browser {

// Static viewport metadata with native EventTarget methods. Only explicit
// synthetic dispatch runs listeners/handlers; no layout/scroll backend exists.
class VisualViewportHost : public EventTargetHost {
public:
    explicit VisualViewportHost(const HostEnvironment* environment)
        : environment_(environment) {}

    std::string global_name() const override { return std::string(); }
    std::string prototype_key() const override { return "VisualViewport"; }
    std::string trace_object_type() const override { return "visual_viewport"; }
    std::string trace_label() const override { return "visualViewport"; }

    std::vector<std::string> property_names() const override {
        return {"offsetLeft", "offsetTop", "pageLeft", "pageTop", "width",
                "height", "scale", "onresize", "onscroll", "onscrollend"};
    }

    std::vector<std::string> writable_property_names() const override {
        return {"onresize", "onscroll", "onscrollend"};
    }

    std::vector<std::string> method_names() const override {
        return event_method_names();
    }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
                                      v8::Local<v8::Context>,
                                      const std::string& name) override {
        if (name == "width") {
            return v8::Integer::New(isolate, environment_->inner_width);
        }
        if (name == "height") {
            return v8::Integer::New(isolate, environment_->inner_height);
        }
        if (name == "offsetLeft" || name == "offsetTop" || name == "pageLeft" ||
            name == "pageTop") {
            return v8::Integer::New(isolate, 0);
        }
        if (name == "scale") {
            return v8::Number::New(isolate, 1.0);
        }
        if (name == "onresize" || name == "onscroll" || name == "onscrollend") {
            return get_event_handler(isolate, name);
        }
        return v8::Undefined(isolate);
    }

    void set_property(v8::Isolate* isolate, v8::Local<v8::Context>,
                      const std::string& name,
                      v8::Local<v8::Value> value) override {
        if (name == "onresize" || name == "onscroll" || name == "onscrollend") {
            set_event_handler(isolate, name, value);
        }
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override {
        watch_record(isolate, trace_label(), name);
        if (!validate_bom_event_dispatch(isolate, context, name, arguments))
            return v8::Undefined(isolate);
        v8::Local<v8::Value> result;
        if (handle_event_method(isolate, context, name, arguments, result)) return result;
        return v8::Undefined(isolate);
    }

private:
    const HostEnvironment* environment_;
};

}  // namespace browser
}  // namespace iv8
