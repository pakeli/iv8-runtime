#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "iv8/host_object.h"
#include "../host/event_target.h"

namespace iv8 { class PageState; }

namespace iv8::browser {

class GeolocationPositionErrorHost : public HostObject {
public:
    GeolocationPositionErrorHost(int code, std::string message)
        : code_(code), message_(std::move(message)) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "GeolocationPositionError"; }
    std::string trace_object_type() const override { return "geolocation_position_error"; }
    std::string trace_label() const override { return "GeolocationPositionError"; }
    std::vector<std::string> property_names() const override { return {"code", "message"}; }
    std::vector<std::string> method_names() const override { return {}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    int code_;
    std::string message_;
};

class GeolocationHost : public HostObject {
public:
    GeolocationHost(PageState* owner, bool secure_context)
        : owner_(owner), secure_context_(secure_context) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "Geolocation"; }
    std::string trace_object_type() const override { return "geolocation"; }
    std::string trace_label() const override { return "navigator.geolocation"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override {
        return {"clearWatch", "getCurrentPosition", "watchPosition"};
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

    bool is_watch_active(std::int32_t id) const {
        return active_watches_.contains(id);
    }
    void deactivate_watch(std::int32_t id) {
        active_watches_.erase(id);
    }

private:
    PageState* owner_;
    bool secure_context_;
    std::int32_t next_watch_id_ = 0;
    std::unordered_set<std::int32_t> active_watches_;
};

class ClipboardItemHost : public HostObject {
public:
    ClipboardItemHost(PageState* owner, std::vector<std::string> types,
        std::vector<v8::Global<v8::Value>> values)
        : owner_(owner), types_(std::move(types)), values_(std::move(values)) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "ClipboardItem"; }
    std::string trace_object_type() const override { return "clipboard_item"; }
    std::string trace_label() const override { return "ClipboardItem"; }
    std::vector<std::string> property_names() const override { return {"types"}; }
    std::vector<std::string> method_names() const override { return {"getType"}; }
    void release_v8_handles() noexcept override {
        for (auto& val : values_) val.Reset();
        values_.clear();
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    PageState* owner_;
    std::vector<std::string> types_;
    std::vector<v8::Global<v8::Value>> values_;
};

class ClipboardHost : public EventTargetHost {
public:
    explicit ClipboardHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "Clipboard"; }
    std::string trace_object_type() const override { return "clipboard"; }
    std::string trace_label() const override { return "navigator.clipboard"; }
    std::vector<std::string> property_names() const override {
        return {"onclipboardchange"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"onclipboardchange"};
    }
    std::vector<std::string> method_names() const override {
        auto names = event_method_names();
        names.insert(names.end(), {"read", "readText", "write", "writeText"});
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

class CredentialsContainerHost : public HostObject {
public:
    explicit CredentialsContainerHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "CredentialsContainer"; }
    std::string trace_object_type() const override { return "credentials"; }
    std::string trace_label() const override { return "navigator.credentials"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override {
        return {"create", "get", "preventSilentAccess", "store"};
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    PageState* owner_;
};

class LockHost : public HostObject {
public:
    LockHost(std::string name, std::string mode)
        : name_(std::move(name)), mode_(std::move(mode)) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "Lock"; }
    std::string trace_object_type() const override { return "lock"; }
    std::string trace_label() const override { return "Lock"; }
    std::vector<std::string> property_names() const override {
        return {"name", "mode"};
    }
    std::vector<std::string> method_names() const override { return {}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    std::string name_;
    std::string mode_;
};

class LockManagerHost : public HostObject {
public:
    LockManagerHost(PageState* owner, const std::string& base_url);

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "LockManager"; }
    std::string trace_object_type() const override { return "locks"; }
    std::string trace_label() const override { return "navigator.locks"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override {
        return {"query", "request"};
    }
    void release_v8_handles() noexcept override;
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

    struct LockEntry {
        std::uint64_t id = 0;
        std::string name;
        std::string mode;
        bool if_available = false;
        v8::Global<v8::Object> callback;
        v8::Global<v8::Promise::Resolver> resolver;
    };

    void process_queue(v8::Isolate* isolate, v8::Local<v8::Context> context);
    void release_held_lock(v8::Isolate* isolate, v8::Local<v8::Context> context,
        std::uint64_t id);
    const std::vector<LockEntry>& held_locks() const { return held_; }
    const std::vector<LockEntry>& pending_locks() const { return pending_; }

private:
    PageState* owner_;
    bool opaque_origin_ = false;
    std::uint64_t next_id_ = 0;
    std::vector<LockEntry> held_;
    std::vector<LockEntry> pending_;
};

class WakeLockSentinelHost : public EventTargetHost {
public:
    explicit WakeLockSentinelHost(std::string type) : type_(std::move(type)) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "WakeLockSentinel"; }
    std::string trace_object_type() const override { return "wake_lock_sentinel"; }
    std::string trace_label() const override { return "WakeLockSentinel"; }
    std::vector<std::string> property_names() const override {
        return {"onrelease", "released", "type"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"onrelease"};
    }
    std::vector<std::string> method_names() const override {
        auto names = event_method_names();
        names.push_back("release");
        return names;
    }
    void release_v8_handles() noexcept override {
        self_.Reset();
        EventTargetHost::release_v8_handles();
    }
    void bind_wrapper(v8::Isolate* isolate, v8::Local<v8::Object> wrapper) {
        self_.Reset(isolate, wrapper);
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name, v8::Local<v8::Value> value) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    std::string type_;
    bool released_ = false;
    v8::Global<v8::Object> self_;
};

class WakeLockHost : public HostObject {
public:
    explicit WakeLockHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "WakeLock"; }
    std::string trace_object_type() const override { return "wake_lock"; }
    std::string trace_label() const override { return "navigator.wakeLock"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override { return {"request"}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    PageState* owner_;
};

class KeyboardLayoutMapHost : public HostObject {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "KeyboardLayoutMap"; }
    std::string trace_object_type() const override { return "keyboard_layout_map"; }
    std::string trace_label() const override { return "KeyboardLayoutMap"; }
    std::vector<std::string> property_names() const override { return {"size"}; }
    std::vector<std::string> method_names() const override {
        return {"entries", "forEach", "get", "has", "keys", "values"};
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;
};

class KeyboardHost : public HostObject {
public:
    explicit KeyboardHost(PageState* owner) : owner_(owner) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "Keyboard"; }
    std::string trace_object_type() const override { return "keyboard"; }
    std::string trace_label() const override { return "navigator.keyboard"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override {
        return {"getLayoutMap", "lock", "unlock"};
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    PageState* owner_;
};

struct MediaImageRecord {
    std::string src;
    std::string sizes;
    std::string type;
};

class MediaMetadataHost : public HostObject {
public:
    MediaMetadataHost(std::string base_url, std::string title,
        std::string artist, std::string album,
        std::vector<MediaImageRecord> artwork)
        : base_url_(std::move(base_url)),
          title_(std::move(title)),
          artist_(std::move(artist)),
          album_(std::move(album)),
          artwork_(std::move(artwork)) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "MediaMetadata"; }
    std::string trace_object_type() const override { return "media_metadata"; }
    std::string trace_label() const override { return "MediaMetadata"; }
    std::vector<std::string> property_names() const override {
        return {"title", "artist", "album", "artwork", "chapterInfo"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"title", "artist", "album", "artwork"};
    }
    std::vector<std::string> method_names() const override { return {}; }
    void release_v8_handles() noexcept override {
        chapter_info_.Reset();
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name, v8::Local<v8::Value> value) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    std::string base_url_;
    std::string title_;
    std::string artist_;
    std::string album_;
    std::vector<MediaImageRecord> artwork_;
    v8::Global<v8::Array> chapter_info_;
};

class MediaSessionHost : public HostObject {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "MediaSession"; }
    std::string trace_object_type() const override { return "media_session"; }
    std::string trace_label() const override { return "navigator.mediaSession"; }
    std::vector<std::string> property_names() const override {
        return {"metadata", "playbackState"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"metadata", "playbackState"};
    }
    std::vector<std::string> method_names() const override {
        return {"setActionHandler", "setCameraActive",
                "setMicrophoneActive", "setPositionState"};
    }
    void release_v8_handles() noexcept override {
        metadata_.Reset();
        for (auto& [_, handler] : action_handlers_) handler.Reset();
        action_handlers_.clear();
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name, v8::Local<v8::Value> value) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    std::string playback_state_ = "none";
    v8::Global<v8::Object> metadata_;
    std::unordered_map<std::string, v8::Global<v8::Object>> action_handlers_;
};

class MediaCapabilitiesHost : public HostObject {
public:
    MediaCapabilitiesHost(PageState* owner, bool secure_context)
        : owner_(owner), secure_context_(secure_context) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "MediaCapabilities"; }
    std::string trace_object_type() const override { return "media_capabilities"; }
    std::string trace_label() const override { return "navigator.mediaCapabilities"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override {
        return {"decodingInfo", "encodingInfo"};
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    PageState* owner_;
    bool secure_context_;
};

bool install_navigator_bom05_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner, bool secure_context,
    const std::string& base_url);

v8::Local<v8::Value> construct_clipboard_item(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    v8::Local<v8::Array> arguments);

v8::Local<v8::Value> construct_media_metadata(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    const std::string& base_url, v8::Local<v8::Array> arguments);

}  // namespace iv8::browser
