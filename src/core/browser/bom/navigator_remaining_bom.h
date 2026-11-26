#pragma once

#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "iv8/host_object.h"
#include "../host/event_target.h"

namespace iv8 { class PageState; }

namespace iv8::browser {

class DOMRectReadOnlyHost : public HostObject {
public:
    DOMRectReadOnlyHost(double x = 0.0, double y = 0.0,
        double width = 0.0, double height = 0.0)
        : x_(x), y_(y), width_(width), height_(height) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "DOMRectReadOnly"; }
    std::string trace_object_type() const override { return "dom_rect_read_only"; }
    std::string trace_label() const override { return "DOMRectReadOnly"; }
    std::vector<std::string> property_names() const override {
        return {"x", "y", "width", "height", "top", "right", "bottom", "left"};
    }
    std::vector<std::string> method_names() const override { return {"toJSON"}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

protected:
    double x_;
    double y_;
    double width_;
    double height_;
};

class DOMRectHost : public DOMRectReadOnlyHost {
public:
    using DOMRectReadOnlyHost::DOMRectReadOnlyHost;

    std::string prototype_key() const override { return "DOMRect"; }
    std::string trace_object_type() const override { return "dom_rect"; }
    std::string trace_label() const override { return "DOMRect"; }
    std::vector<std::string> writable_property_names() const override {
        return {"x", "y", "width", "height"};
    }
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name, v8::Local<v8::Value> value) override;
};

class MediaDeviceInfoHost : public HostObject {
public:
    MediaDeviceInfoHost(std::string device_id, std::string kind,
        std::string label, std::string group_id)
        : device_id_(std::move(device_id)), kind_(std::move(kind)),
          label_(std::move(label)), group_id_(std::move(group_id)) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "MediaDeviceInfo"; }
    std::string trace_object_type() const override { return "media_device_info"; }
    std::string trace_label() const override { return "MediaDeviceInfo"; }
    std::vector<std::string> property_names() const override {
        return {"deviceId", "kind", "label", "groupId"};
    }
    std::vector<std::string> method_names() const override { return {"toJSON"}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

protected:
    std::string device_id_;
    std::string kind_;
    std::string label_;
    std::string group_id_;
};

class InputDeviceInfoHost : public MediaDeviceInfoHost {
public:
    using MediaDeviceInfoHost::MediaDeviceInfoHost;

    std::string prototype_key() const override { return "InputDeviceInfo"; }
    std::string trace_object_type() const override { return "input_device_info"; }
    std::string trace_label() const override { return "InputDeviceInfo"; }
    std::vector<std::string> method_names() const override {
        return {"toJSON", "getCapabilities"};
    }
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;
};

class MediaDevicesHost : public EventTargetHost {
public:
    explicit MediaDevicesHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "MediaDevices"; }
    std::string trace_object_type() const override { return "media_devices"; }
    std::string trace_label() const override { return "navigator.mediaDevices"; }
    std::vector<std::string> property_names() const override {
        return {"ondevicechange"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"ondevicechange"};
    }
    std::vector<std::string> method_names() const override {
        auto names = event_method_names();
        names.insert(names.end(), {
            "enumerateDevices", "getSupportedConstraints", "getUserMedia",
            "getDisplayMedia", "setCaptureHandleConfig"});
        return names;
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name, v8::Local<v8::Value> value) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    PageState* owner_;
};

class ServiceWorkerContainerHost : public EventTargetHost {
public:
    ServiceWorkerContainerHost(PageState* owner, std::string base_url)
        : owner_(owner), base_url_(std::move(base_url)) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "ServiceWorkerContainer"; }
    std::string trace_object_type() const override { return "service_worker_container"; }
    std::string trace_label() const override { return "navigator.serviceWorker"; }
    std::vector<std::string> property_names() const override {
        return {"controller", "ready", "oncontrollerchange", "onmessage", "onmessageerror"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"oncontrollerchange", "onmessage", "onmessageerror"};
    }
    std::vector<std::string> method_names() const override {
        auto names = event_method_names();
        names.insert(names.end(), {
            "getRegistration", "getRegistrations", "register", "startMessages"});
        return names;
    }
    void release_v8_handles() noexcept override {
        EventTargetHost::release_v8_handles();
        ready_promise_.Reset();
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name, v8::Local<v8::Value> value) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    PageState* owner_;
    std::string base_url_;
    v8::Global<v8::Promise> ready_promise_;
};

struct StorageBucketState {
    std::string name;
    double quota = 0.0;
    std::optional<double> expires;
};

class StorageBucketHost : public HostObject {
public:
    StorageBucketHost(PageState* owner, std::shared_ptr<StorageBucketState> state)
        : owner_(owner), state_(std::move(state)) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "StorageBucket"; }
    std::string trace_object_type() const override { return "storage_bucket"; }
    std::string trace_label() const override { return "StorageBucket"; }
    std::vector<std::string> property_names() const override {
        return {"name", "indexedDB", "caches"};
    }
    std::vector<std::string> method_names() const override {
        return {"estimate", "expires", "getDirectory", "persisted", "setExpires", "persist"};
    }
    void release_v8_handles() noexcept override {
        indexed_db_.Reset();
        caches_.Reset();
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    PageState* owner_;
    std::shared_ptr<StorageBucketState> state_;
    v8::Global<v8::Object> indexed_db_;
    v8::Global<v8::Object> caches_;
};

class StorageBucketManagerHost : public HostObject {
public:
    explicit StorageBucketManagerHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "StorageBucketManager"; }
    std::string trace_object_type() const override { return "storage_buckets"; }
    std::string trace_label() const override { return "navigator.storageBuckets"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override {
        return {"delete", "keys", "open"};
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    void prune_expired_buckets();
    PageState* owner_;
    std::map<std::string, std::shared_ptr<StorageBucketState>> buckets_;
};

class NavigatorLoginHost : public HostObject {
public:
    NavigatorLoginHost() = default;

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "NavigatorLogin"; }
    std::string trace_object_type() const override { return "login"; }
    std::string trace_label() const override { return "navigator.login"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override { return {"setStatus"}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    std::string status_;
};

class NavigatorManagedDataHost : public EventTargetHost {
public:
    explicit NavigatorManagedDataHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "NavigatorManagedData"; }
    std::string trace_object_type() const override { return "managed"; }
    std::string trace_label() const override { return "navigator.managed"; }
    std::vector<std::string> property_names() const override {
        return {"onmanagedconfigurationchange"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"onmanagedconfigurationchange"};
    }
    std::vector<std::string> method_names() const override {
        auto names = event_method_names();
        names.push_back("getManagedConfiguration");
        return names;
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name, v8::Local<v8::Value> value) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    PageState* owner_;
};

class DelegatedInkTrailPresenterHost : public HostObject {
public:
    DelegatedInkTrailPresenterHost(v8::Isolate* isolate,
        v8::Local<v8::Object> presentation_area) {
        if (!presentation_area.IsEmpty()) {
            presentation_area_.Reset(isolate, presentation_area);
        }
    }

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "DelegatedInkTrailPresenter"; }
    std::string trace_object_type() const override {
        return "delegated_ink_trail_presenter";
    }
    std::string trace_label() const override { return "DelegatedInkTrailPresenter"; }
    std::vector<std::string> property_names() const override {
        return {"presentationArea"};
    }
    std::vector<std::string> method_names() const override {
        return {"updateInkTrailStartPoint"};
    }
    void release_v8_handles() noexcept override { presentation_area_.Reset(); }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    v8::Global<v8::Object> presentation_area_;
};

class InkHost : public HostObject {
public:
    explicit InkHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "Ink"; }
    std::string trace_object_type() const override { return "ink"; }
    std::string trace_label() const override { return "navigator.ink"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override {
        return {"requestPresenter"};
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    PageState* owner_;
};

class DevicePostureHost : public EventTargetHost {
public:
    DevicePostureHost() = default;

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "DevicePosture"; }
    std::string trace_object_type() const override { return "device_posture"; }
    std::string trace_label() const override { return "navigator.devicePosture"; }
    std::vector<std::string> property_names() const override {
        return {"type", "onchange"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"onchange"};
    }
    std::vector<std::string> method_names() const override {
        return event_method_names();
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name, v8::Local<v8::Value> value) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;
};

class VirtualKeyboardHost : public EventTargetHost {
public:
    explicit VirtualKeyboardHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "VirtualKeyboard"; }
    std::string trace_object_type() const override { return "virtual_keyboard"; }
    std::string trace_label() const override { return "navigator.virtualKeyboard"; }
    std::vector<std::string> property_names() const override {
        return {"boundingRect", "overlaysContent", "ongeometrychange"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"overlaysContent", "ongeometrychange"};
    }
    std::vector<std::string> method_names() const override {
        auto names = event_method_names();
        names.insert(names.end(), {"hide", "show"});
        return names;
    }
    void release_v8_handles() noexcept override {
        EventTargetHost::release_v8_handles();
        bounding_rect_.Reset();
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name, v8::Local<v8::Value> value) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    PageState* owner_;
    bool overlays_content_ = false;
    v8::Global<v8::Object> bounding_rect_;
};

class WindowControlsOverlayHost : public EventTargetHost {
public:
    explicit WindowControlsOverlayHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "WindowControlsOverlay"; }
    std::string trace_object_type() const override { return "window_controls_overlay"; }
    std::string trace_label() const override { return "navigator.windowControlsOverlay"; }
    std::vector<std::string> property_names() const override {
        return {"visible", "ongeometrychange"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"ongeometrychange"};
    }
    std::vector<std::string> method_names() const override {
        auto names = event_method_names();
        names.push_back("getTitlebarAreaRect");
        return names;
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name, v8::Local<v8::Value> value) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    PageState* owner_;
};

int navigator_cpu_performance_tier(int hardware_concurrency) noexcept;

v8::Local<v8::Value> construct_dom_rect_read_only(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    v8::Local<v8::Array> arguments);

v8::Local<v8::Value> construct_dom_rect(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    v8::Local<v8::Array> arguments);

bool install_navigator_bom07_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner, bool secure_context);

}  // namespace iv8::browser
