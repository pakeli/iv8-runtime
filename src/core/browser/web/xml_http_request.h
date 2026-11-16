#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "../host/event_target.h"

namespace iv8::browser {

// Shared XMLHttpRequest event-handler attributes retain object assignments,
// while the existing EventTarget listener machinery invokes callable values.
class XMLHttpRequestEventTargetHost : public EventTargetHost {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "XMLHttpRequestEventTarget"; }
    std::vector<std::string> property_names() const override;
    std::vector<std::string> writable_property_names() const override;
    std::vector<std::string> method_names() const override;

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name, v8::Local<v8::Value> value) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;
    void release_v8_handles() noexcept override;

protected:
    v8::Local<v8::Value> get_handler_value(v8::Isolate* isolate,
        const std::string& name);
    void set_handler_value(v8::Isolate* isolate, const std::string& name,
        v8::Local<v8::Value> value);

private:
    std::unordered_map<std::string, v8::Global<v8::Value>> handler_values_;
};

class XMLHttpRequestUploadHost final : public XMLHttpRequestEventTargetHost {
public:
    std::string prototype_key() const override { return "XMLHttpRequestUpload"; }
    std::string trace_object_type() const override { return "xml_http_request_upload"; }
    std::string trace_label() const override { return "XMLHttpRequestUpload"; }
};

// A Page-owned shape/monitoring profile. Request methods are native placeholders:
// no transport, ready-state transitions, responses, scheduled work or events.
class XMLHttpRequestHost final : public XMLHttpRequestEventTargetHost {
public:
    XMLHttpRequestHost(v8::Isolate* isolate, v8::Local<v8::Object> upload,
        bool secure_context = true);

    std::string prototype_key() const override { return "XMLHttpRequest"; }
    std::string trace_object_type() const override { return "xml_http_request"; }
    std::string trace_label() const override { return "XMLHttpRequest"; }
    std::vector<std::string> property_names() const override;
    std::vector<std::string> writable_property_names() const override;
    std::vector<std::string> method_names() const override;

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name, v8::Local<v8::Value> value) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;
    void release_v8_handles() noexcept override;

private:
    v8::Global<v8::Object> upload_;
    std::uint32_t timeout_ = 0;
    bool with_credentials_ = false;
    bool secure_context_ = true;
    std::string response_type_;
};

// Install complete defining-prototype surfaces before user scripts construct an
// instance. Reordering retains the original native functions and accessors.
bool install_xml_http_request_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, bool secure_context);

}  // namespace iv8::browser
