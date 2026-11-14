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

struct PermissionNameInfo {
    const char* key;
};

extern v8::Local<v8::Value> resolved_promise(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> value);

extern void storage_manager_estimate_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void storage_manager_persisted_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void storage_manager_persist_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern v8::Local<v8::Object> make_storage_manager_object(v8::Isolate* isolate, v8::Local<v8::Context> context);

extern const v8::ExternalPointerTypeTag kPermissionStatusTag;

extern const PermissionNameInfo kPermissionNames[];

extern const PermissionNameInfo* find_permission_name(const std::string& name);

extern v8::Local<v8::Value> rejected_type_error_promise(v8::Isolate* isolate, v8::Local<v8::Context> context, const char* message);

extern void permission_status_name_getter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info);

extern void permission_status_state_getter(v8::Local<v8::Name>, const v8::PropertyCallbackInfo<v8::Value>& info);

extern void permission_status_add_listener_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void permission_status_remove_listener_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void permission_status_dispatch_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern v8::Local<v8::Object> make_permission_status_object(v8::Isolate* isolate, v8::Local<v8::Context> context, const PermissionNameInfo* entry);

extern void permissions_query_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

extern v8::Local<v8::Object> make_permissions_object(v8::Isolate* isolate, v8::Local<v8::Context> context);

// Step 5: nested connection metadata. HostObject-backed so reads, missing
// probes, and object identity use the normal access-trace channel.
class ConnectionHost : public HostObject {
public:
    explicit ConnectionHost(const HostEnvironment* environment)
        : environment_(environment) {}

    std::string global_name() const override { return std::string(); }
    std::string trace_object_type() const override { return "connection"; }
    std::string trace_label() const override { return "navigator.connection"; }

    std::vector<std::string> property_names() const override {
        return {"effectiveType", "rtt", "downlink", "saveData"};
    }
    std::vector<std::string> method_names() const override { return {}; }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
                                      v8::Local<v8::Context>,
                                      const std::string& name) override {
        if (name == "effectiveType") {
            return v8_string(isolate,
                             environment_->connection_effective_type);
        }
        if (name == "rtt") {
            return v8::Integer::New(isolate, environment_->connection_rtt);
        }
        if (name == "downlink") {
            return v8::Number::New(isolate, environment_->connection_downlink);
        }
        if (name == "saveData") {
            return v8::Boolean::New(isolate, environment_->connection_save_data);
        }
        return v8::Undefined(isolate);
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        return v8::Undefined(isolate);
    }

private:
    const HostEnvironment* environment_;
};

// Step 5: nested screen orientation metadata.
class ScreenOrientationHost : public HostObject {
public:
    explicit ScreenOrientationHost(const HostEnvironment* environment)
        : environment_(environment) {}

    std::string global_name() const override { return std::string(); }
    std::string trace_object_type() const override { return "orientation"; }
    std::string trace_label() const override { return "screen.orientation"; }

    std::vector<std::string> property_names() const override {
        return {"type", "angle"};
    }
    std::vector<std::string> method_names() const override { return {}; }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
                                      v8::Local<v8::Context>,
                                      const std::string& name) override {
        const bool landscape =
            environment_->screen_width >= environment_->screen_height;
        if (name == "type") {
            return v8_string(isolate, landscape ? "landscape-primary"
                                                : "portrait-primary");
        }
        if (name == "angle") {
            return v8::Integer::New(isolate, landscape ? 0 : 90);
        }
        return v8::Undefined(isolate);
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        return v8::Undefined(isolate);
    }

private:
    const HostEnvironment* environment_;
};

}  // namespace browser
}  // namespace iv8
