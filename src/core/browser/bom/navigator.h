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
#include "iv8/access_origin.h"
#include "iv8/web_interface.h"
#include "iv8/browser_compatibility.h"
#include "navigator_parts.h"
#include "navigator_identity.h"
#include "navigator_capabilities.h"
#include "navigator_vibration.h"
#include "navigator_methods.h"
#include "navigator_permissions_bom.h"
#include "navigator_devices_bom.h"
#include "navigator_remaining_bom.h"
#include "legacy_quota.h"
#include "../web/file_system.h"

namespace iv8 {

namespace browser {

// M2-3 minimal navigator (read-only), configurable since M11-1 and extended by
// M11-24/M11-27. Values come from the page's HostEnvironment, which the Python
// facade always supplies fully resolved — this class holds no defaults and never
// derives a value from the host OS, display, or locale, so a page is
// deterministic and identical across Linux/Windows. UA-CH values use the same
// configured environment; capability inspection uses an offline device profile.
class NavigatorHost : public HostObject {
public:
    NavigatorHost(const HostEnvironment* environment, bool secure_context,
        PageState* owner, const std::string& base_url)
        : environment_(environment), secure_context_(secure_context),
          owner_(owner), base_url_(base_url),
          connection_(environment), user_agent_data_(environment), quota_(owner, base_url),
          geolocation_(owner, secure_context), clipboard_(owner),
          credentials_(owner), locks_(owner, base_url),
          wake_lock_(owner), keyboard_(owner),
          media_capabilities_(owner, secure_context),
          bluetooth_(owner), hid_(owner), serial_(owner),
          usb_(owner), xr_(owner),
          media_devices_(owner), service_worker_(owner, base_url),
          storage_buckets_(owner), managed_(owner), ink_(owner),
          virtual_keyboard_(owner), window_controls_overlay_(owner) {}

    std::string global_name() const override { return "navigator"; }
    std::string prototype_key() const override { return "Navigator"; }
    std::string trace_object_type() const override { return "navigator"; }

    std::vector<std::string> property_names() const override {
        std::vector<std::string> names = {
            "userAgent", "appVersion", "deviceMemory", "userAgentData", "userActivation",
            "geolocation", "ink", "mediaCapabilities", "windowControlsOverlay",
            "platform", "language", "webdriver", "languages", "onLine",
            "cookieEnabled", "hardwareConcurrency", "appName", "appCodeName",
            "product", "vendor", "vendorSub", "productSub", "maxTouchPoints",
            "pdfViewerEnabled", "doNotTrack", "connection", "scheduling",
            "permissions", "plugins", "mimeTypes",
            "webkitTemporaryStorage", "webkitPersistentStorage", "mediaSession"};
        if (secure_context_) {
            names.insert(std::find(names.begin(), names.end(), "permissions"), "storage");
            names.insert(names.end(), {
                "cpuPerformance", "bluetooth", "clipboard", "credentials", "keyboard",
                "managed", "mediaDevices", "serviceWorker", "virtualKeyboard",
                "locks", "wakeLock", "gpu", "login", "devicePosture",
                "hid", "presentation", "serial", "usb", "xr", "storageBuckets"});
        }
        return names;
    }

    std::vector<std::string> method_names() const override {
        std::vector<std::string> names = {"getGamepads", "javaEnabled", "vibrate"};
        if (secure_context_) {
            names.insert(names.end(), {
                "canShare", "share", "clearAppBadge", "getUserMedia",
                "requestMIDIAccess", "requestMediaKeySystemAccess",
                "setAppBadge", "webkitGetUserMedia", "getInstalledRelatedApps",
                "registerProtocolHandler", "unregisterProtocolHandler"});
        }
        return names;
    }

    void release_v8_handles() noexcept override {
        connection_.release_v8_handles();
        clipboard_.release_v8_handles();
        locks_.release_v8_handles();
        media_session_.release_v8_handles();
        bluetooth_.release_v8_handles();
        hid_.release_v8_handles();
        serial_.release_v8_handles();
        usb_.release_v8_handles();
        xr_.release_v8_handles();
        gpu_.release_v8_handles();
        presentation_.release_v8_handles();
        media_devices_.release_v8_handles();
        service_worker_.release_v8_handles();
        managed_.release_v8_handles();
        device_posture_.release_v8_handles();
        virtual_keyboard_.release_v8_handles();
        window_controls_overlay_.release_v8_handles();
        connection_object_.Reset();
        scheduling_object_.Reset();
        storage_object_.Reset();
        permissions_object_.Reset();
        plugins_object_.Reset();
        mime_types_object_.Reset();
        user_agent_data_object_.Reset();
        user_activation_object_.Reset();
        quota_object_.Reset();
        geolocation_object_.Reset();
        clipboard_object_.Reset();
        credentials_object_.Reset();
        locks_object_.Reset();
        wake_lock_object_.Reset();
        keyboard_object_.Reset();
        media_session_object_.Reset();
        media_capabilities_object_.Reset();
        bluetooth_object_.Reset();
        hid_object_.Reset();
        serial_object_.Reset();
        usb_object_.Reset();
        xr_object_.Reset();
        gpu_object_.Reset();
        presentation_object_.Reset();
        media_devices_object_.Reset();
        service_worker_object_.Reset();
        storage_buckets_object_.Reset();
        login_object_.Reset();
        managed_object_.Reset();
        ink_object_.Reset();
        device_posture_object_.Reset();
        virtual_keyboard_object_.Reset();
        window_controls_overlay_object_.Reset();
    }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context,
                                      const std::string& name) override {
        if (name == "userAgent") {
            return v8_string(isolate, environment_->user_agent);
        }
        if (name == "webkitTemporaryStorage" || name == "webkitPersistentStorage") {
            if (quota_object_.IsEmpty()) quota_object_.Reset(isolate, make_host_object(isolate, context, &quota_));
            return quota_object_.Get(isolate);
        }
        if (name == "appVersion") return v8_string(isolate, environment_->app_version);
        if (name == "deviceMemory") return v8::Number::New(isolate, environment_->device_memory);
        if (name == "userAgentData" || name == "userActivation") {
            auto& cached = name == "userAgentData" ? user_agent_data_object_ : user_activation_object_;
            if (cached.IsEmpty()) {
                HostObject* host = name == "userAgentData"
                    ? static_cast<HostObject*>(&user_agent_data_)
                    : static_cast<HostObject*>(&user_activation_);
                cached.Reset(isolate, make_host_object(isolate, context, host));
            }
            return cached.Get(isolate);
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
                (void)access_user_operation(isolate, [&] {
                    return array->Set(context, static_cast<std::uint32_t>(i),
                        v8_string(isolate, languages[i]));
                });
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
            if (connection_object_.IsEmpty()) {
                connection_object_.Reset(isolate, make_host_object(isolate, context, &connection_));
            }
            return connection_object_.Get(isolate);
        }
        if (name == "scheduling") {
            if (scheduling_object_.IsEmpty()) {
                scheduling_object_.Reset(isolate, make_host_object(isolate, context, &scheduling_));
            }
            return scheduling_object_.Get(isolate);
        }
        if (name == "geolocation") {
            if (geolocation_object_.IsEmpty()) {
                geolocation_object_.Reset(isolate, make_host_object(isolate, context, &geolocation_));
            }
            return geolocation_object_.Get(isolate);
        }
        if (name == "clipboard") {
            if (clipboard_object_.IsEmpty()) {
                clipboard_object_.Reset(isolate, make_host_object(isolate, context, &clipboard_));
            }
            return clipboard_object_.Get(isolate);
        }
        if (name == "credentials") {
            if (credentials_object_.IsEmpty()) {
                credentials_object_.Reset(isolate, make_host_object(isolate, context, &credentials_));
            }
            return credentials_object_.Get(isolate);
        }
        if (name == "locks") {
            if (locks_object_.IsEmpty()) {
                locks_object_.Reset(isolate, make_host_object(isolate, context, &locks_));
            }
            return locks_object_.Get(isolate);
        }
        if (name == "wakeLock") {
            if (wake_lock_object_.IsEmpty()) {
                wake_lock_object_.Reset(isolate, make_host_object(isolate, context, &wake_lock_));
            }
            return wake_lock_object_.Get(isolate);
        }
        if (name == "keyboard") {
            if (keyboard_object_.IsEmpty()) {
                keyboard_object_.Reset(isolate, make_host_object(isolate, context, &keyboard_));
            }
            return keyboard_object_.Get(isolate);
        }
        if (name == "mediaSession") {
            if (media_session_object_.IsEmpty()) {
                media_session_object_.Reset(isolate, make_host_object(isolate, context, &media_session_));
            }
            return media_session_object_.Get(isolate);
        }
        if (name == "mediaCapabilities") {
            if (media_capabilities_object_.IsEmpty()) {
                media_capabilities_object_.Reset(isolate, make_host_object(isolate, context, &media_capabilities_));
            }
            return media_capabilities_object_.Get(isolate);
        }
        if (name == "bluetooth") {
            if (bluetooth_object_.IsEmpty()) {
                bluetooth_object_.Reset(isolate, make_host_object(isolate, context, &bluetooth_));
            }
            return bluetooth_object_.Get(isolate);
        }
        if (name == "hid") {
            if (hid_object_.IsEmpty()) {
                hid_object_.Reset(isolate, make_host_object(isolate, context, &hid_));
            }
            return hid_object_.Get(isolate);
        }
        if (name == "serial") {
            if (serial_object_.IsEmpty()) {
                serial_object_.Reset(isolate, make_host_object(isolate, context, &serial_));
            }
            return serial_object_.Get(isolate);
        }
        if (name == "usb") {
            if (usb_object_.IsEmpty()) {
                usb_object_.Reset(isolate, make_host_object(isolate, context, &usb_));
            }
            return usb_object_.Get(isolate);
        }
        if (name == "xr") {
            if (xr_object_.IsEmpty()) {
                xr_object_.Reset(isolate, make_host_object(isolate, context, &xr_));
            }
            return xr_object_.Get(isolate);
        }
        if (name == "gpu") {
            if (gpu_object_.IsEmpty()) {
                gpu_object_.Reset(isolate, make_host_object(isolate, context, &gpu_));
            }
            return gpu_object_.Get(isolate);
        }
        if (name == "presentation") {
            if (presentation_object_.IsEmpty()) {
                presentation_object_.Reset(isolate, make_host_object(isolate, context, &presentation_));
            }
            return presentation_object_.Get(isolate);
        }
        if (name == "cpuPerformance") {
            return v8::Integer::New(isolate,
                navigator_cpu_performance_tier(environment_->hardware_concurrency));
        }
        if (name == "mediaDevices") {
            if (media_devices_object_.IsEmpty()) {
                media_devices_object_.Reset(isolate, make_host_object(isolate, context, &media_devices_));
            }
            return media_devices_object_.Get(isolate);
        }
        if (name == "serviceWorker") {
            if (service_worker_object_.IsEmpty()) {
                service_worker_object_.Reset(isolate, make_host_object(isolate, context, &service_worker_));
            }
            return service_worker_object_.Get(isolate);
        }
        if (name == "storageBuckets") {
            if (storage_buckets_object_.IsEmpty()) {
                storage_buckets_object_.Reset(isolate, make_host_object(isolate, context, &storage_buckets_));
            }
            return storage_buckets_object_.Get(isolate);
        }
        if (name == "login") {
            if (login_object_.IsEmpty()) {
                login_object_.Reset(isolate, make_host_object(isolate, context, &login_));
            }
            return login_object_.Get(isolate);
        }
        if (name == "managed") {
            if (managed_object_.IsEmpty()) {
                managed_object_.Reset(isolate, make_host_object(isolate, context, &managed_));
            }
            return managed_object_.Get(isolate);
        }
        if (name == "ink") {
            if (ink_object_.IsEmpty()) {
                ink_object_.Reset(isolate, make_host_object(isolate, context, &ink_));
            }
            return ink_object_.Get(isolate);
        }
        if (name == "devicePosture") {
            if (device_posture_object_.IsEmpty()) {
                device_posture_object_.Reset(isolate, make_host_object(isolate, context, &device_posture_));
            }
            return device_posture_object_.Get(isolate);
        }
        if (name == "virtualKeyboard") {
            if (virtual_keyboard_object_.IsEmpty()) {
                virtual_keyboard_object_.Reset(isolate, make_host_object(isolate, context, &virtual_keyboard_));
            }
            return virtual_keyboard_object_.Get(isolate);
        }
        if (name == "windowControlsOverlay") {
            if (window_controls_overlay_object_.IsEmpty()) {
                window_controls_overlay_object_.Reset(isolate, make_host_object(isolate, context, &window_controls_overlay_));
            }
            return window_controls_overlay_object_.Get(isolate);
        }
        // BOM-10: native secure-context StorageManager is SameObject per
        // generation; its backing virtual storage survives same-origin reloads.
        if (name == "storage") {
            if (storage_object_.IsEmpty()) {
                storage_object_.Reset(
                    isolate, make_native_storage_manager(isolate, context, owner_,
                        owner_->file_system_origin(), owner_->file_system_store()));
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
        v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override {
        if (name == "getGamepads") return empty_gamepad_snapshot(isolate, context);
        if (name == "getInstalledRelatedApps") return empty_related_applications(isolate, context);
        if (name == "vibrate") return navigator_vibrate(isolate, context, arguments);
        if (name == "javaEnabled") return v8::False(isolate);
        return navigator_call_bom04_method(
            isolate, context, owner_, base_url_, name, arguments);
    }

private:
    // Owned by PageState (outlives every generation), so this raw pointer stays
    // valid for as long as this host object is installed.
    const HostEnvironment* environment_;
    const bool secure_context_;
    PageState* owner_;
    std::string base_url_;
    ConnectionHost connection_;
    SchedulingHost scheduling_;
    NavigatorUADataHost user_agent_data_;
    UserActivationHost user_activation_;
    LegacyQuotaHost quota_;
    GeolocationHost geolocation_;
    ClipboardHost clipboard_;
    CredentialsContainerHost credentials_;
    LockManagerHost locks_;
    WakeLockHost wake_lock_;
    KeyboardHost keyboard_;
    MediaSessionHost media_session_;
    MediaCapabilitiesHost media_capabilities_;
    BluetoothHost bluetooth_;
    HIDHost hid_;
    SerialHost serial_;
    USBHost usb_;
    XRSystemHost xr_;
    GPUHost gpu_;
    PresentationHost presentation_;
    MediaDevicesHost media_devices_;
    ServiceWorkerContainerHost service_worker_;
    StorageBucketManagerHost storage_buckets_;
    NavigatorLoginHost login_;
    NavigatorManagedDataHost managed_;
    InkHost ink_;
    DevicePostureHost device_posture_;
    VirtualKeyboardHost virtual_keyboard_;
    WindowControlsOverlayHost window_controls_overlay_;
    v8::Global<v8::Object> quota_object_;
    v8::Global<v8::Object> geolocation_object_;
    v8::Global<v8::Object> clipboard_object_;
    v8::Global<v8::Object> credentials_object_;
    v8::Global<v8::Object> locks_object_;
    v8::Global<v8::Object> wake_lock_object_;
    v8::Global<v8::Object> keyboard_object_;
    v8::Global<v8::Object> media_session_object_;
    v8::Global<v8::Object> media_capabilities_object_;
    v8::Global<v8::Object> bluetooth_object_;
    v8::Global<v8::Object> hid_object_;
    v8::Global<v8::Object> serial_object_;
    v8::Global<v8::Object> usb_object_;
    v8::Global<v8::Object> xr_object_;
    v8::Global<v8::Object> gpu_object_;
    v8::Global<v8::Object> presentation_object_;
    v8::Global<v8::Object> media_devices_object_;
    v8::Global<v8::Object> service_worker_object_;
    v8::Global<v8::Object> storage_buckets_object_;
    v8::Global<v8::Object> login_object_;
    v8::Global<v8::Object> managed_object_;
    v8::Global<v8::Object> ink_object_;
    v8::Global<v8::Object> device_posture_object_;
    v8::Global<v8::Object> virtual_keyboard_object_;
    v8::Global<v8::Object> window_controls_overlay_object_;
    v8::Global<v8::Object> user_agent_data_object_;
    v8::Global<v8::Object> user_activation_object_;
    v8::Global<v8::Object> connection_object_;
    v8::Global<v8::Object> scheduling_object_;
    // Per-generation cached native navigator.storage and plain
    // navigator.permissions objects.
    v8::Global<v8::Object> storage_object_;
    v8::Global<v8::Object> permissions_object_;
    // Step 5h: cached legacy navigator metadata.
    v8::Global<v8::Array> plugins_object_;
    v8::Global<v8::Array> mime_types_object_;
};

}  // namespace browser
}  // namespace iv8
