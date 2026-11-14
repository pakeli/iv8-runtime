#pragma once

#include "navigator_parts.h"
#include "../host/event_target.h"

namespace iv8::browser {

class ScreenHost : public EventTargetHost {
public:
    explicit ScreenHost(const HostEnvironment* environment)
        : environment_(environment), orientation_(environment) {}

    std::string global_name() const override { return "screen"; }
    std::string prototype_key() const override { return "Screen"; }
    std::string trace_object_type() const override { return "screen"; }

    std::vector<std::string> property_names() const override {
        return {"width",      "height",     "availWidth", "availHeight",
                "availLeft",  "availTop",   "colorDepth", "pixelDepth",
                "orientation"};
    }

    std::vector<std::string> method_names() const override { return event_method_names(); }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context,
                                      const std::string& name) override {
        if (name == "width") {
            return v8::Integer::New(isolate, environment_->screen_width);
        }
        if (name == "height") {
            return v8::Integer::New(isolate, environment_->screen_height);
        }
        if (name == "availWidth") {
            return v8::Integer::New(isolate, environment_->avail_width);
        }
        if (name == "availHeight") {
            return v8::Integer::New(isolate, environment_->avail_height);
        }
        // M11-35: reported available origin (independent configured values).
        if (name == "availLeft") {
            return v8::Integer::New(isolate, environment_->avail_left);
        }
        if (name == "availTop") {
            return v8::Integer::New(isolate, environment_->avail_top);
        }
        if (name == "colorDepth") {
            return v8::Integer::New(isolate, environment_->color_depth);
        }
        if (name == "pixelDepth") {
            return v8::Integer::New(isolate, environment_->pixel_depth);
        }
        // M11-35: deterministic orientation derived from the reported screen
        // dimensions. Step 5 exposes it as a nested HostObject.
        if (name == "orientation") {
            return make_host_object(isolate, context, &orientation_);
        }
        return v8::Undefined(isolate);
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& args) override {
        v8::Local<v8::Value> result;
        if (handle_event_method(isolate, context, name, args, result)) return result;
        return v8::Undefined(isolate);
    }

private:
    const HostEnvironment* environment_;
    ScreenOrientationHost orientation_;
};

}  // namespace iv8::browser
