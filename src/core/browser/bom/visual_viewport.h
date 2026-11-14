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
#include "iv8/page_state.h"
#include "iv8/host_object.h"
#include "iv8/access_summary.h"
#include "iv8/web_interface.h"
#include "iv8/browser_compatibility.h"

namespace iv8 {

namespace browser {

// M11-1/M11-25 minimal screen (read-only). Static dimensions and color/pixel
// depth sourced from the page's HostEnvironment. Deliberately NOT a full Screen
// object: no availLeft / availTop / orientation, no ScreenOrientation, and no
// media-query or layout integration. avail* are independent configured values,
// not computed from width/height.
// Step 5d: the static M11-41 visual viewport. It is a HostObject so its property
// reads and no-op event-target methods enter the normal access-trace channel.
// Values still come from the Page environment and never change within a
// generation; onresize/onscroll store whatever the script assigns, but no events
// are dispatched. The global property remains read-only.
class VisualViewportHost : public HostObject {
public:
    explicit VisualViewportHost(const HostEnvironment* environment)
        : environment_(environment) {}

    std::string global_name() const override { return std::string(); }
    std::string trace_object_type() const override { return "visual_viewport"; }
    std::string trace_label() const override { return "visualViewport"; }

    std::vector<std::string> property_names() const override {
        return {"width",    "height",   "offsetLeft", "offsetTop",
                "pageLeft", "pageTop",  "scale",      "onresize",
                "onscroll"};
    }

    std::vector<std::string> writable_property_names() const override {
        return {"onresize", "onscroll"};
    }

    std::vector<std::string> method_names() const override {
        return {"addEventListener", "removeEventListener", "dispatchEvent"};
    }

    void release_v8_handles() noexcept override {
        onresize_.Reset();
        onscroll_.Reset();
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
        if (name == "onresize") {
            return onresize_.IsEmpty() ? v8::Null(isolate)
                                       : onresize_.Get(isolate);
        }
        if (name == "onscroll") {
            return onscroll_.IsEmpty() ? v8::Null(isolate)
                                       : onscroll_.Get(isolate);
        }
        return v8::Undefined(isolate);
    }

    void set_property(v8::Isolate* isolate, v8::Local<v8::Context>,
                      const std::string& name,
                      v8::Local<v8::Value> value) override {
        if (name == "onresize") {
            onresize_.Reset(isolate, value);
        } else if (name == "onscroll") {
            onscroll_.Reset(isolate, value);
        }
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        if (name == "dispatchEvent") {
            return v8::Boolean::New(isolate, false);
        }
        return v8::Undefined(isolate);
    }

private:
    const HostEnvironment* environment_;
    v8::Global<v8::Value> onresize_;
    v8::Global<v8::Value> onscroll_;
};

}  // namespace browser
}  // namespace iv8
