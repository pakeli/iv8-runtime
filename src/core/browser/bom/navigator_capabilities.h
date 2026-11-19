#pragma once

#include <string>
#include <vector>

#include "iv8/host_object.h"

namespace iv8::browser {

// Populate Scheduling.prototype before user code can inspect it without first
// evaluating navigator.scheduling. This registration owns no instance handles.
bool install_navigator_capability_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context);

// Chrome's captured no-device profile has eight unassigned gamepad slots. The
// array is a fresh mutable snapshot, with no device or trusted-input backend.
v8::Local<v8::Value> empty_gamepad_snapshot(v8::Isolate* isolate,
    v8::Local<v8::Context> context);

// A secure, top-level Page has no installed-app/manifest backend. Each call
// creates its own Promise and asynchronously supplies a new empty snapshot.
v8::Local<v8::Value> empty_related_applications(v8::Isolate* isolate,
    v8::Local<v8::Context> context);

class SchedulingHost : public HostObject {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "Scheduling"; }
    std::string trace_object_type() const override { return "host_object"; }
    std::string trace_label() const override { return "navigator.scheduling"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override { return {"isInputPending"}; }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;
};

}  // namespace iv8::browser
