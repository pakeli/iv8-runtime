#pragma once

#include <string>
#include <utility>
#include <vector>

#include "iv8/host_object.h"
#include "../host/event_target.h"

namespace iv8 { class PageState; }

namespace iv8::browser {

class BluetoothHost : public EventTargetHost {
public:
    explicit BluetoothHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "Bluetooth"; }
    std::string trace_object_type() const override { return "bluetooth"; }
    std::string trace_label() const override { return "navigator.bluetooth"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override {
        auto names = event_method_names();
        names.insert(names.end(), {"getAvailability", "requestDevice"});
        return names;
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    PageState* owner_;
};

class HIDHost : public EventTargetHost {
public:
    explicit HIDHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "HID"; }
    std::string trace_object_type() const override { return "hid"; }
    std::string trace_label() const override { return "navigator.hid"; }
    std::vector<std::string> property_names() const override {
        return {"onconnect", "ondisconnect"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"onconnect", "ondisconnect"};
    }
    std::vector<std::string> method_names() const override {
        auto names = event_method_names();
        names.insert(names.end(), {"getDevices", "requestDevice"});
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

class SerialHost : public EventTargetHost {
public:
    explicit SerialHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "Serial"; }
    std::string trace_object_type() const override { return "serial"; }
    std::string trace_label() const override { return "navigator.serial"; }
    std::vector<std::string> property_names() const override {
        return {"onconnect", "ondisconnect"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"onconnect", "ondisconnect"};
    }
    std::vector<std::string> method_names() const override {
        auto names = event_method_names();
        names.insert(names.end(), {"getPorts", "requestPort"});
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

class USBHost : public EventTargetHost {
public:
    explicit USBHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "USB"; }
    std::string trace_object_type() const override { return "usb"; }
    std::string trace_label() const override { return "navigator.usb"; }
    std::vector<std::string> property_names() const override {
        return {"onconnect", "ondisconnect"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"onconnect", "ondisconnect"};
    }
    std::vector<std::string> method_names() const override {
        auto names = event_method_names();
        names.insert(names.end(), {"getDevices", "requestDevice"});
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

class XRSystemHost : public EventTargetHost {
public:
    explicit XRSystemHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "XRSystem"; }
    std::string trace_object_type() const override { return "xr"; }
    std::string trace_label() const override { return "navigator.xr"; }
    std::vector<std::string> property_names() const override {
        return {"ondevicechange"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"ondevicechange"};
    }
    std::vector<std::string> method_names() const override {
        auto names = event_method_names();
        names.insert(names.end(), {"isSessionSupported", "requestSession"});
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

class WGSLLanguageFeaturesHost : public HostObject {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "WGSLLanguageFeatures"; }
    std::string trace_object_type() const override { return "wgsl_language_features"; }
    std::string trace_label() const override { return "WGSLLanguageFeatures"; }
    std::vector<std::string> property_names() const override { return {"size"}; }
    std::vector<std::string> method_names() const override {
        return {"entries", "forEach", "has", "keys", "values"};
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;
};

class GPUHost : public HostObject {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "GPU"; }
    std::string trace_object_type() const override { return "gpu"; }
    std::string trace_label() const override { return "navigator.gpu"; }
    std::vector<std::string> property_names() const override {
        return {"wgslLanguageFeatures"};
    }
    std::vector<std::string> method_names() const override {
        return {"getPreferredCanvasFormat", "requestAdapter"};
    }
    void release_v8_handles() noexcept override {
        features_object_.Reset();
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    WGSLLanguageFeaturesHost features_;
    v8::Global<v8::Object> features_object_;
};

class PresentationRequestHost : public EventTargetHost {
public:
    PresentationRequestHost(PageState* owner, std::vector<std::string> urls)
        : owner_(owner), urls_(std::move(urls)) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "PresentationRequest"; }
    std::string trace_object_type() const override { return "presentation_request"; }
    std::string trace_label() const override { return "PresentationRequest"; }
    std::vector<std::string> property_names() const override {
        return {"onconnectionavailable"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"onconnectionavailable"};
    }
    std::vector<std::string> method_names() const override {
        auto names = event_method_names();
        names.insert(names.end(), {"getAvailability", "reconnect", "start"});
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
    std::vector<std::string> urls_;
};

class PresentationHost : public HostObject {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "Presentation"; }
    std::string trace_object_type() const override { return "presentation"; }
    std::string trace_label() const override { return "navigator.presentation"; }
    std::vector<std::string> property_names() const override {
        return {"defaultRequest", "receiver"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"defaultRequest"};
    }
    std::vector<std::string> method_names() const override { return {}; }
    void release_v8_handles() noexcept override {
        default_request_.Reset();
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name, v8::Local<v8::Value> value) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    v8::Global<v8::Object> default_request_;
};

bool install_navigator_bom06_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner, bool secure_context);

v8::Local<v8::Value> construct_presentation_request(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    const std::string& base_url, v8::Local<v8::Array> arguments);

}  // namespace iv8::browser
