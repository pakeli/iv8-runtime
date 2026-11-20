#pragma once

#include <string>
#include <utility>
#include <vector>

#include "iv8/host_object.h"

namespace iv8 { class PageState; }

namespace iv8::browser {

inline constexpr double kOfflineStorageQuota = 10737418240.0;

class DOMErrorHost : public HostObject {
public:
    DOMErrorHost(std::u16string name, std::u16string message)
        : name_(std::move(name)), message_(std::move(message)) {}
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "DOMError"; }
    std::string trace_label() const override { return "DOMError"; }
    std::vector<std::string> property_names() const override { return {"name", "message"}; }
    std::vector<std::string> method_names() const override { return {}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;
private:
    std::u16string name_;
    std::u16string message_;
};

class LegacyQuotaHost : public HostObject {
public:
    LegacyQuotaHost(PageState* owner, const std::string& base_url);
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "DeprecatedStorageQuota"; }
    std::string trace_label() const override { return "DeprecatedStorageQuota"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override {
        return {"queryUsageAndQuota", "requestQuota"};
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;
private:
    PageState* owner_;
    bool opaque_;
};

bool install_legacy_quota_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner);
v8::Local<v8::Value> construct_dom_error(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner, v8::Local<v8::Array> arguments);

}  // namespace iv8::browser
