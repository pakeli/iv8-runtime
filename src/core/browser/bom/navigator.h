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
#include "navigator_parts.h"

namespace iv8 {

namespace browser {

// M2-3 minimal navigator (read-only), configurable since M11-1 and extended by
// M11-24/M11-27. Values come from the page's HostEnvironment, which the Python
// facade always supplies fully resolved — this class holds no defaults and never
// derives a value from the host OS, display, or locale, so a page is
// deterministic and identical across Linux/Windows. Still no UA-CH /
// userAgentData surface.
class NavigatorHost : public HostObject {
public:
    explicit NavigatorHost(const HostEnvironment* environment)
        : environment_(environment), connection_(environment) {}

    std::string global_name() const override { return "navigator"; }
    std::string prototype_key() const override { return "Navigator"; }
    std::string trace_object_type() const override { return "navigator"; }

    std::vector<std::string> property_names() const override {
        return {"userAgent",       "platform",          "language",
                "webdriver",       "languages",         "onLine",
                "cookieEnabled",   "hardwareConcurrency", "appName",
                "appCodeName",     "product",           "vendor",
                "vendorSub",       "productSub",        "maxTouchPoints",
                "pdfViewerEnabled", "doNotTrack",        "connection",
                "storage",         "permissions",       "plugins",
                "mimeTypes"};
    }

    std::vector<std::string> method_names() const override { return {"javaEnabled"}; }

    void release_v8_handles() noexcept override {
        storage_object_.Reset();
        permissions_object_.Reset();
        plugins_object_.Reset();
        mime_types_object_.Reset();
    }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context,
                                      const std::string& name) override {
        if (name == "userAgent") {
            return v8_string(isolate, environment_->user_agent);
        }
        if (name == "platform") {
            return v8_string(isolate, environment_->platform);
        }
        if (name == "language") {
            return v8_string(isolate, environment_->language);
        }
        if (name == "webdriver") {
            return v8::Boolean::New(isolate, environment_->webdriver);
        }
        if (name == "languages") {
            const std::vector<std::string>& languages = environment_->languages;
            v8::Local<v8::Array> array =
                v8::Array::New(isolate, static_cast<int>(languages.size()));
            for (std::size_t i = 0; i < languages.size(); i++) {
                (void)array->Set(context, static_cast<std::uint32_t>(i),
                                 v8_string(isolate, languages[i]));
            }
            return array;
        }
        if (name == "onLine") {
            return v8::Boolean::New(isolate, environment_->on_line);
        }
        if (name == "cookieEnabled") {
            return v8::Boolean::New(isolate, environment_->cookie_enabled);
        }
        if (name == "hardwareConcurrency") {
            return v8::Integer::New(isolate,
                                    environment_->hardware_concurrency);
        }
        if (name == "appName") {
            return v8_string(isolate, environment_->app_name);
        }
        if (name == "appCodeName") {
            return v8_string(isolate, environment_->app_code_name);
        }
        if (name == "product") {
            return v8_string(isolate, environment_->product);
        }
        if (name == "vendor") {
            return v8_string(isolate, environment_->vendor);
        }
        if (name == "vendorSub") {
            return v8_string(isolate, environment_->vendor_sub);
        }
        if (name == "productSub") {
            return v8_string(isolate, environment_->product_sub);
        }
        // M11-35: deterministic navigator metadata additions.
        if (name == "maxTouchPoints") {
            return v8::Integer::New(isolate, environment_->max_touch_points);
        }
        if (name == "pdfViewerEnabled") {
            return v8::Boolean::New(isolate,
                                    environment_->pdf_viewer_enabled);
        }
        if (name == "doNotTrack") {
            return v8_string(isolate, environment_->do_not_track);
        }
        if (name == "connection") {
            return make_host_object(isolate, context, &connection_);
        }
        // Step 5e: cache the plain StorageManager object so repeated reads keep
        // the same identity and the read-only method descriptors unchanged.
        // The parent `navigator.storage` read itself is traced by the generic
        // HostObject getter; the method calls are traced by their callbacks.
        if (name == "storage") {
            if (storage_object_.IsEmpty()) {
                storage_object_.Reset(
                    isolate, make_storage_manager_object(isolate, context));
            }
            return storage_object_.Get(isolate);
        }
        // Step 5f: cached plain Permissions object; the parent read is traced
        // by this HostObject getter and query() is a traced native callback.
        if (name == "permissions") {
            if (permissions_object_.IsEmpty()) {
                permissions_object_.Reset(
                    isolate, make_permissions_object(isolate, context));
            }
            return permissions_object_.Get(isolate);
        }
        // Step 5h: legacy empty plugin/mime-type lists and javaEnabled. The
        // arrays and function are cached so identity and read-only descriptors
        // match the previous bootstrap behavior; reads/calls are traced.
        if (name == "plugins" || name == "mimeTypes") {
            v8::Global<v8::Array>& slot =
                name == "plugins" ? plugins_object_ : mime_types_object_;
            if (slot.IsEmpty()) {
                v8::Local<v8::Array> array = v8::Array::New(isolate, 0);
                (void)array->SetIntegrityLevel(context,
                                              v8::IntegrityLevel::kFrozen);
                slot.Reset(isolate, array);
            }
            return slot.Get(isolate);
        }
        return v8::Undefined(isolate);
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        if (name == "javaEnabled") return v8::False(isolate);
        return v8::Undefined(isolate);
    }

private:
    // Owned by PageState (outlives every generation), so this raw pointer stays
    // valid for as long as this host object is installed.
    const HostEnvironment* environment_;
    ConnectionHost connection_;
    // Step 5e/5f: per-generation cached plain navigator.storage and
    // navigator.permissions objects.
    v8::Global<v8::Object> storage_object_;
    v8::Global<v8::Object> permissions_object_;
    // Step 5h: cached legacy navigator metadata.
    v8::Global<v8::Array> plugins_object_;
    v8::Global<v8::Array> mime_types_object_;
};

}  // namespace browser
}  // namespace iv8
