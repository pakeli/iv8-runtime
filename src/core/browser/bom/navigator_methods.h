#pragma once

#include <string>
#include <utility>
#include <vector>

#include "iv8/host_object.h"
#include "../host/event_target.h"

namespace iv8 { class PageState; }

namespace iv8::browser {

struct ClearKeyMediaCapability {
    std::string content_type;
    bool has_encryption_scheme = false;
    std::string encryption_scheme;
    std::string robustness;
};

struct ClearKeyConfiguration {
    std::vector<ClearKeyMediaCapability> audio_capabilities;
    std::string distinctive_identifier = "not-allowed";
    std::vector<std::string> init_data_types;
    std::string label;
    std::string persistent_state = "not-allowed";
    std::vector<std::string> session_types = {"temporary"};
    std::vector<ClearKeyMediaCapability> video_capabilities;
};

class MediaKeyStatusMapHost : public HostObject {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "MediaKeyStatusMap"; }
    std::string trace_object_type() const override { return "media_key_status_map"; }
    std::string trace_label() const override { return "MediaKeyStatusMap"; }
    std::vector<std::string> property_names() const override { return {"size"}; }
    std::vector<std::string> method_names() const override {
        return {"get", "has", "entries", "forEach", "keys", "values"};
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;
};

class MediaKeySessionHost : public EventTargetHost {
public:
    explicit MediaKeySessionHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "MediaKeySession"; }
    std::string trace_object_type() const override { return "media_key_session"; }
    std::string trace_label() const override { return "MediaKeySession"; }
    std::vector<std::string> property_names() const override {
        return {"sessionId", "expiration", "closed", "keyStatuses",
                "onkeystatuseschange", "onmessage"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"onkeystatuseschange", "onmessage"};
    }
    std::vector<std::string> method_names() const override {
        auto names = event_method_names();
        names.insert(names.end(),
            {"close", "generateRequest", "load", "remove", "update"});
        return names;
    }
    void release_v8_handles() noexcept override {
        closed_resolver_.Reset();
        key_statuses_object_.Reset();
        EventTargetHost::release_v8_handles();
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
    MediaKeyStatusMapHost key_statuses_;
    v8::Global<v8::Promise::Resolver> closed_resolver_;
    v8::Global<v8::Object> key_statuses_object_;
};

class MediaKeysHost : public HostObject {
public:
    explicit MediaKeysHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "MediaKeys"; }
    std::string trace_object_type() const override { return "media_keys"; }
    std::string trace_label() const override { return "MediaKeys"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override {
        return {"createSession", "setServerCertificate", "getStatusForPolicy"};
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    PageState* owner_;
};

class MediaKeySystemAccessHost : public HostObject {
public:
    MediaKeySystemAccessHost(PageState* owner, std::string key_system,
        ClearKeyConfiguration configuration)
        : owner_(owner),
          key_system_(std::move(key_system)),
          configuration_(std::move(configuration)) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "MediaKeySystemAccess"; }
    std::string trace_object_type() const override { return "media_key_system_access"; }
    std::string trace_label() const override { return "MediaKeySystemAccess"; }
    std::vector<std::string> property_names() const override { return {"keySystem"}; }
    std::vector<std::string> method_names() const override {
        return {"createMediaKeys", "getConfiguration"};
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    PageState* owner_;
    std::string key_system_;
    ClearKeyConfiguration configuration_;
};

bool install_navigator_method_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner, bool secure_context);

v8::Local<v8::Value> navigator_call_bom04_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    const std::string& base_url, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments);

}  // namespace iv8::browser
