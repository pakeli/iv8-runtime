#pragma once

#include <string>
#include <vector>

#include "iv8/page_state.h"
#include "iv8/host_object.h"

namespace iv8::browser {

// Register defining-prototype members before any nested getter is evaluated.
void install_navigator_identity_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const HostEnvironment* environment);

class NavigatorUADataHost : public HostObject {
public:
    explicit NavigatorUADataHost(const HostEnvironment* environment)
        : environment_(environment) {}
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "NavigatorUAData"; }
    std::string trace_object_type() const override { return "navigator_ua_data"; }
    std::string trace_label() const override { return "navigator.userAgentData"; }
    std::vector<std::string> property_names() const override {
        return {"brands", "mobile", "platform"};
    }
    std::vector<std::string> method_names() const override {
        return {"getHighEntropyValues", "toJSON"};
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    const HostEnvironment* environment_;
};

class UserActivationHost : public HostObject {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "UserActivation"; }
    std::string trace_object_type() const override { return "user_activation"; }
    std::string trace_label() const override { return "navigator.userActivation"; }
    std::vector<std::string> property_names() const override {
        return {"hasBeenActive", "isActive"};
    }
    std::vector<std::string> method_names() const override { return {}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;
};

class DOMStringListHost : public HostObject {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "DOMStringList"; }
    std::string trace_object_type() const override { return "dom_string_list"; }
    std::string trace_label() const override { return "location.ancestorOrigins"; }
    std::vector<std::string> property_names() const override { return {"length"}; }
    std::vector<std::string> method_names() const override { return {"item", "contains"}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;
};

}  // namespace iv8::browser
