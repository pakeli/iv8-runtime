#include "navigator_permissions_bom.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

#include "iv8/access_origin.h"
#include "iv8/access_summary.h"
#include "iv8/dom_url.h"
#include "iv8/page_state.h"
#include "iv8/web_interface.h"
#include "iv8/web_method.h"
#include "../internal/common.h"
#include "../internal/trace_hooks.h"
#include "../internal/web_idl_primitive.h"
#include "../web/blob_file_reader.h"
#include "../web/dom_exception.h"
#include "../web/event_constructors.h"
#include "../window/window_trace_support.h"
#include "navigator_parts.h"

namespace iv8::browser {
namespace {

bool throw_type_error(v8::Isolate* isolate, const std::string& prefix,
    const std::string& detail) {
    const auto message = prefix + detail;
    v8::Local<v8::String> text;
    if (v8::String::NewFromUtf8(isolate, message.data(), v8::NewStringType::kNormal,
            static_cast<int>(message.size())).ToLocal(&text)) {
        isolate->ThrowException(v8::Exception::TypeError(text));
    }
    return false;
}

v8::Local<v8::Value> rejected_promise(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> reason) {
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver) ||
        !resolver->Reject(context, reason).FromMaybe(false)) {
        return v8::Undefined(isolate);
    }
    return resolver->GetPromise();
}

v8::Local<v8::Value> reject_from_try_catch(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::TryCatch& caught) {
    if (!caught.CanContinue()) {
        caught.ReThrow();
        return v8::Undefined(isolate);
    }
    auto exception = caught.Exception();
    caught.Reset();
    return rejected_promise(isolate, context, exception);
}

bool is_callable_object(v8::Local<v8::Value> value) {
    return value->IsObject() && value.As<v8::Object>()->IsCallable();
}

v8::MaybeLocal<v8::Value> user_get(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> object,
    v8::Local<v8::Value> key) {
    return access_user_operation(isolate, [&] {
        return object->Get(context, key);
    });
}

v8::MaybeLocal<v8::Value> user_call(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> callable,
    v8::Local<v8::Value> receiver, int count = 0,
    v8::Local<v8::Value>* arguments = nullptr) {
    return access_user_operation(isolate, [&] {
        return callable.As<v8::Object>()->CallAsFunction(
            context, receiver, count, arguments);
    });
}

template <typename ItemConverter>
bool convert_sequence(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, const std::string& prefix,
    ItemConverter&& convert_item) {
    if (!input->IsObject()) {
        return throw_type_error(isolate, prefix,
            "The provided value cannot be converted to a sequence.");
    }
    auto object = input.As<v8::Object>();
    v8::Local<v8::Value> method;
    if (!user_get(isolate, context, object, v8::Symbol::GetIterator(isolate))
            .ToLocal(&method)) {
        return false;
    }
    if (method->IsNullOrUndefined()) {
        return throw_type_error(isolate, prefix,
            "The object must have a callable @@iterator property.");
    }
    if (!is_callable_object(method)) {
        return throw_type_error(isolate, prefix, "@@iterator must be a callable.");
    }
    v8::Local<v8::Value> iterator_value;
    if (!user_call(isolate, context, method, input).ToLocal(&iterator_value)) {
        return false;
    }
    if (!iterator_value->IsObject()) {
        return throw_type_error(isolate, prefix,
            "Iterator object must be an object.");
    }
    auto iterator = iterator_value.As<v8::Object>();
    v8::Local<v8::Value> next;
    if (!user_get(isolate, context, iterator, v8_string(isolate, "next"))
            .ToLocal(&next)) {
        return false;
    }
    if (!is_callable_object(next)) {
        return throw_type_error(isolate, prefix,
            "Expected next() function on iterator.");
    }
    for (;;) {
        v8::Local<v8::Value> step;
        if (!user_call(isolate, context, next, iterator).ToLocal(&step)) {
            return false;
        }
        if (!step->IsObject()) {
            return throw_type_error(isolate, prefix,
                "Expected iterator.next() to return an Object.");
        }
        auto record = step.As<v8::Object>();
        v8::Local<v8::Value> item;
        v8::Local<v8::Value> done;
        // Blink's UnpackIteratorResult reads `value` before `done`, even on the
        // terminal step (empirically verified against headless Chrome).
        if (!user_get(isolate, context, record, v8_string(isolate, "value"))
                .ToLocal(&item) ||
            !user_get(isolate, context, record, v8_string(isolate, "done"))
                .ToLocal(&done)) {
            return false;
        }
        if (done->BooleanValue(isolate)) {
            return true;
        }
        if (!convert_item(item)) {
            return false;
        }
    }
}

bool move_constructor_to_end(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> prototype,
    const std::string& interface_name) {
    auto key = v8::String::NewFromUtf8Literal(isolate, "constructor");
    v8::Local<v8::Value> constructor;
    if (!prototype->Get(context, key).ToLocal(&constructor) ||
        !prototype->Delete(context, key).FromMaybe(false)) {
        return false;
    }
    return install_traced_api_data_property(isolate, context, prototype,
        "constructor", constructor, native_api_identity(isolate, prototype),
        "host_object", interface_name + ".prototype", v8::DontEnum);
}

void deliver_task_promise_settlement(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> resolver_val;
    v8::Local<v8::Value> is_reject_val;
    v8::Local<v8::Value> payload;
    if (!data->Get(context, 0).ToLocal(&resolver_val) ||
        !data->Get(context, 1).ToLocal(&is_reject_val) ||
        !data->Get(context, 2).ToLocal(&payload)) {
        return;
    }
    auto resolver = resolver_val.As<v8::Promise::Resolver>();
    const bool reject = is_reject_val->BooleanValue(isolate);
    (void)access_user_operation(isolate, [&] {
        return reject ? resolver->Reject(context, payload)
                      : resolver->Resolve(context, payload);
    });
}

v8::Local<v8::Value> schedule_task_promise(v8::Isolate* isolate,
    v8::Local<v8::Context> context, bool reject, v8::Local<v8::Value> payload) {
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        return v8::Undefined(isolate);
    }
    auto* state = state_from_isolate(isolate);
    if (state == nullptr) return v8::Undefined(isolate);
    auto data = v8::Array::New(isolate, 3);
    if (!data->CreateDataProperty(context, 0, resolver).FromMaybe(false) ||
        !data->CreateDataProperty(context, 1, v8::Boolean::New(isolate, reject)).FromMaybe(false) ||
        !data->CreateDataProperty(context, 2, payload).FromMaybe(false)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Function> delivery;
    if (!v8::Function::New(context, &deliver_task_promise_settlement, data, 0,
            v8::ConstructorBehavior::kThrow).ToLocal(&delivery)) {
        return v8::Undefined(isolate);
    }
    (void)state->register_timer(delivery, 0, false, false);
    return resolver->GetPromise();
}

// Declaration-only hosts for uninstantiated GeolocationPosition/Coordinates/Credential.
class GeolocationPositionDeclHost : public HostObject {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "GeolocationPosition"; }
    std::vector<std::string> property_names() const override {
        return {"coords", "timestamp"};
    }
    std::vector<std::string> method_names() const override { return {"toJSON"}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&) override {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        return v8::Undefined(isolate);
    }
};

class GeolocationCoordinatesDeclHost : public HostObject {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "GeolocationCoordinates"; }
    std::vector<std::string> property_names() const override {
        return {"latitude", "longitude", "altitude", "accuracy",
                "altitudeAccuracy", "heading", "speed"};
    }
    std::vector<std::string> method_names() const override { return {"toJSON"}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&) override {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        return v8::Undefined(isolate);
    }
};

class CredentialDeclHost : public HostObject {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "Credential"; }
    std::vector<std::string> property_names() const override {
        return {"id", "type"};
    }
    std::vector<std::string> method_names() const override { return {}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&) override {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        return v8::Undefined(isolate);
    }
};

void deliver_geolocation_error(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> geo_ext;
    v8::Local<v8::Value> owner_ext;
    v8::Local<v8::Value> callback;
    v8::Local<v8::Value> code_val;
    v8::Local<v8::Value> msg_val;
    v8::Local<v8::Value> watch_id_val;
    if (!data->Get(context, 0).ToLocal(&geo_ext) ||
        !data->Get(context, 1).ToLocal(&owner_ext) ||
        !data->Get(context, 2).ToLocal(&callback) ||
        !data->Get(context, 3).ToLocal(&code_val) ||
        !data->Get(context, 4).ToLocal(&msg_val) ||
        !data->Get(context, 5).ToLocal(&watch_id_val) ||
        !geo_ext->IsExternal() || !owner_ext->IsExternal()) {
        return;
    }
    auto* geo = static_cast<GeolocationHost*>(
        geo_ext.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    auto* owner = static_cast<PageState*>(
        owner_ext.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    const std::int32_t watch_id = watch_id_val.As<v8::Int32>()->Value();
    if (watch_id > 0 && !geo->is_watch_active(watch_id)) {
        return;
    }
    const int code = code_val.As<v8::Int32>()->Value();
    if (watch_id > 0 && code == 1) {
        geo->deactivate_watch(watch_id);
    }
    if (callback->IsNullOrUndefined()) return;
    v8::String::Utf8Value msg_utf8(isolate, msg_val);
    auto error_obj = owner->adopt_host_object(isolate, context,
        std::make_unique<GeolocationPositionErrorHost>(
            code, *msg_utf8 ? *msg_utf8 : ""));
    v8::TryCatch caught(isolate);
    v8::Local<v8::Value> args[] = {error_obj};
    (void)access_user_operation(isolate, [&] {
        return callback.As<v8::Object>()->CallAsFunction(
            context, v8::Undefined(isolate), 1, args);
    });
    if (caught.HasCaught()) {
        if (!caught.CanContinue()) { caught.ReThrow(); return; }
        if (auto* state = state_from_isolate(isolate)) {
            state->record_async_error(isolate, context, caught, "geolocation");
        }
        caught.Reset();
    }
}

void schedule_geolocation_error(v8::Isolate* isolate, v8::Local<v8::Context> context,
    GeolocationHost* geo, PageState* owner, v8::Local<v8::Value> callback,
    int code, const char* message, std::int32_t watch_id) {
    auto* state = state_from_isolate(isolate);
    if (state == nullptr) return;
    auto data = v8::Array::New(isolate, 6);
    if (!data->CreateDataProperty(context, 0, v8::External::New(
            isolate, geo, v8::kExternalPointerTypeTagDefault)).FromMaybe(false) ||
        !data->CreateDataProperty(context, 1, v8::External::New(
            isolate, owner, v8::kExternalPointerTypeTagDefault)).FromMaybe(false) ||
        !data->CreateDataProperty(context, 2, callback).FromMaybe(false) ||
        !data->CreateDataProperty(context, 3, v8::Integer::New(isolate, code)).FromMaybe(false) ||
        !data->CreateDataProperty(context, 4, v8_string(isolate, message)).FromMaybe(false) ||
        !data->CreateDataProperty(context, 5, v8::Integer::New(isolate, watch_id)).FromMaybe(false)) {
        return;
    }
    v8::Local<v8::Function> delivery;
    if (!v8::Function::New(context, &deliver_geolocation_error, data, 0,
            v8::ConstructorBehavior::kThrow).ToLocal(&delivery)) {
        return;
    }
    (void)state->register_timer(delivery, 0, false, false);
}

constexpr std::array<std::pair<const char*, const char*>, 48> kKeyboardLayoutEntries = {{
    {"KeyK", "k"}, {"KeyG", "g"}, {"Digit2", "2"}, {"Digit0", "0"},
    {"KeyV", "v"}, {"KeyA", "a"}, {"Backquote", "`"}, {"KeyL", "l"},
    {"IntlBackslash", "\\"}, {"Quote", "'"}, {"KeyW", "w"}, {"Digit8", "8"},
    {"KeyM", "m"}, {"KeyH", "h"}, {"Period", "."}, {"Digit7", "7"},
    {"Digit1", "1"}, {"KeyP", "p"}, {"KeyD", "d"}, {"KeyF", "f"},
    {"KeyO", "o"}, {"KeyQ", "q"}, {"KeyC", "c"}, {"KeyN", "n"},
    {"BracketLeft", "["}, {"KeyZ", "z"}, {"KeyY", "y"}, {"Digit3", "3"},
    {"Digit6", "6"}, {"Digit5", "5"}, {"KeyX", "x"}, {"Slash", "/"},
    {"Backslash", "\\"}, {"Comma", ","}, {"Minus", "-"}, {"Digit4", "4"},
    {"KeyB", "b"}, {"KeyT", "t"}, {"Digit9", "9"}, {"KeyS", "s"},
    {"KeyI", "i"}, {"KeyU", "u"}, {"Equal", "="}, {"KeyJ", "j"},
    {"Semicolon", ";"}, {"KeyR", "r"}, {"BracketRight", "]"}, {"KeyE", "e"},
}};

void deliver_lock_manager_query(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> mgr_ext;
    v8::Local<v8::Value> resolver_val;
    if (!data->Get(context, 0).ToLocal(&mgr_ext) ||
        !data->Get(context, 1).ToLocal(&resolver_val) ||
        !mgr_ext->IsExternal()) {
        return;
    }
    auto* mgr = static_cast<LockManagerHost*>(
        mgr_ext.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    auto make_list = [&](const std::vector<LockManagerHost::LockEntry>& entries) {
        auto arr = v8::Array::New(isolate, static_cast<int>(entries.size()));
        for (std::uint32_t i = 0; i < entries.size(); ++i) {
            auto item = v8::Object::New(isolate);
            (void)item->CreateDataProperty(context,
                v8_string(isolate, "clientId").As<v8::String>(),
                v8_string(isolate, "iv8-page-client"));
            (void)item->CreateDataProperty(context,
                v8_string(isolate, "mode").As<v8::String>(),
                v8_string(isolate, entries[i].mode));
            (void)item->CreateDataProperty(context,
                v8_string(isolate, "name").As<v8::String>(),
                v8_string(isolate, entries[i].name));
            (void)arr->CreateDataProperty(context, i, item);
        }
        return arr;
    };
    auto snapshot = v8::Object::New(isolate);
    (void)snapshot->CreateDataProperty(context,
        v8_string(isolate, "held").As<v8::String>(), make_list(mgr->held_locks()));
    (void)snapshot->CreateDataProperty(context,
        v8_string(isolate, "pending").As<v8::String>(), make_list(mgr->pending_locks()));
    (void)access_user_operation(isolate, [&] {
        return resolver_val.As<v8::Promise::Resolver>()->Resolve(context, snapshot);
    });
}

void deliver_lock_manager_pump(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    if (!info.Data()->IsExternal()) return;
    auto* mgr = static_cast<LockManagerHost*>(
        info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    mgr->process_queue(isolate, context);
}

void settle_held_lock_fulfill(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> mgr_ext;
    v8::Local<v8::Value> id_val;
    v8::Local<v8::Value> resolver_val;
    if (!data->Get(context, 0).ToLocal(&mgr_ext) ||
        !data->Get(context, 1).ToLocal(&id_val) ||
        !data->Get(context, 2).ToLocal(&resolver_val) ||
        !mgr_ext->IsExternal()) {
        return;
    }
    auto* mgr = static_cast<LockManagerHost*>(
        mgr_ext.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    const auto id = static_cast<std::uint64_t>(id_val.As<v8::Number>()->Value());
    mgr->release_held_lock(isolate, context, id);
    const v8::Local<v8::Value> value = info.Length() > 0
        ? info[0] : v8::Local<v8::Value>(v8::Undefined(isolate));
    (void)access_user_operation(isolate, [&] {
        return resolver_val.As<v8::Promise::Resolver>()->Resolve(context, value);
    });
}

void settle_held_lock_reject(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> mgr_ext;
    v8::Local<v8::Value> id_val;
    v8::Local<v8::Value> resolver_val;
    if (!data->Get(context, 0).ToLocal(&mgr_ext) ||
        !data->Get(context, 1).ToLocal(&id_val) ||
        !data->Get(context, 2).ToLocal(&resolver_val) ||
        !mgr_ext->IsExternal()) {
        return;
    }
    auto* mgr = static_cast<LockManagerHost*>(
        mgr_ext.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    const auto id = static_cast<std::uint64_t>(id_val.As<v8::Number>()->Value());
    mgr->release_held_lock(isolate, context, id);
    const v8::Local<v8::Value> reason = info.Length() > 0
        ? info[0] : v8::Local<v8::Value>(v8::Undefined(isolate));
    (void)access_user_operation(isolate, [&] {
        return resolver_val.As<v8::Promise::Resolver>()->Reject(context, reason);
    });
}

bool parse_media_artwork_list(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, const std::string& base_url,
    const std::string& prefix, std::vector<MediaImageRecord>* output) {
    output->clear();
    return convert_sequence(isolate, context, input, prefix,
        [&](v8::Local<v8::Value> item) {
            if (!item->IsObject()) {
                return throw_type_error(isolate, prefix,
                    "The provided value is not of type 'MediaImage'.");
            }
            auto dict = item.As<v8::Object>();
            MediaImageRecord image;
            v8::Local<v8::Value> sizes_val;
            if (!user_get(isolate, context, dict, v8_string(isolate, "sizes"))
                    .ToLocal(&sizes_val)) {
                return false;
            }
            if (!sizes_val->IsUndefined() &&
                !web_idl_dom_string(isolate, context, sizes_val, prefix, &image.sizes)) {
                return false;
            }
            v8::Local<v8::Value> src_val;
            if (!user_get(isolate, context, dict, v8_string(isolate, "src"))
                    .ToLocal(&src_val)) {
                return false;
            }
            if (src_val->IsUndefined()) {
                return throw_type_error(isolate, prefix,
                    "Failed to read the 'src' property from 'MediaImage': "
                    "Required member is undefined.");
            }
            std::string raw_src;
            if (!web_idl_usv_string(isolate, context, src_val, prefix, &raw_src)) {
                return false;
            }
            const DomUrl resolved = resolve_dom_url(raw_src, base_url);
            if (!resolved.valid) {
                return throw_type_error(isolate, prefix,
                    "Failed to construct 'MediaMetadata': '" + raw_src +
                    "' could not be resolved as a valid URL.");
            }
            image.src = resolved.href;
            v8::Local<v8::Value> type_val;
            if (!user_get(isolate, context, dict, v8_string(isolate, "type"))
                    .ToLocal(&type_val)) {
                return false;
            }
            if (!type_val->IsUndefined() &&
                !web_idl_dom_string(isolate, context, type_val, prefix, &image.type)) {
                return false;
            }
            output->push_back(std::move(image));
            return true;
        });
}

bool supported_media_capability_content_type(const std::string& content_type,
    bool is_video, bool is_encode) {
    const auto semi = content_type.find(';');
    std::string mime = semi == std::string::npos
        ? content_type : content_type.substr(0, semi);
    while (!mime.empty() && std::isspace(static_cast<unsigned char>(mime.front()))) {
        mime.erase(0, 1);
    }
    while (!mime.empty() && std::isspace(static_cast<unsigned char>(mime.back()))) {
        mime.pop_back();
    }
    mime = ascii_lower(mime);
    if (is_video && !mime.starts_with("video/")) return false;
    if (!is_video && !mime.starts_with("audio/")) return false;
    if (semi == std::string::npos) {
        return !is_video && (mime == "audio/wav" || mime == "audio/mpeg" || mime == "audio/aac");
    }
    std::string rest = content_type.substr(semi + 1);
    while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.front()))) {
        rest.erase(0, 1);
    }
    while (!rest.empty() && std::isspace(static_cast<unsigned char>(rest.back()))) {
        rest.pop_back();
    }
    if (!ascii_lower(rest).starts_with("codecs=")) return false;
    rest = rest.substr(7);
    if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"') {
        rest = rest.substr(1, rest.size() - 2);
    }
    if (rest.empty()) return false;
    if (is_encode) {
        // Headless software WebRTC/MediaRecorder profile: VP8/VP9/Opus only when
        // explicitly supported; match Chrome's conservative webrtc encode check.
        return false;
    }
    if (is_video) {
        if (mime != "video/mp4" && mime != "video/webm") return false;
        return rest.starts_with("avc1.42") || rest.starts_with("avc1.4d") ||
               rest.starts_with("avc1.4D") || rest.starts_with("avc1.64") ||
               rest.starts_with("avc3.") || rest == "vp8" || rest == "vp9" ||
               rest.starts_with("vp09.") || rest.starts_with("av01.");
    }
    if (mime != "audio/mp4" && mime != "audio/webm" && mime != "audio/ogg") {
        return false;
    }
    return rest.starts_with("mp4a.40.") || rest == "opus" ||
           rest == "vorbis" || rest == "flac";
}

bool valid_mime_prefix(const std::string& content_type, std::string_view expected_prefix) {
    const auto slash = content_type.find('/');
    if (slash == std::string::npos) return false;
    const auto semi = content_type.find(';');
    if (semi != std::string::npos && semi < slash) return false;
    std::string prefix = ascii_lower(content_type.substr(0, slash + 1));
    while (!prefix.empty() && std::isspace(static_cast<unsigned char>(prefix.front()))) {
        prefix.erase(0, 1);
    }
    const std::string subtype = content_type.substr(slash + 1,
        semi == std::string::npos ? std::string::npos : semi - slash - 1);
    return prefix == expected_prefix && !subtype.empty();
}

void forward_microtask_resolve(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> resolver_val;
    v8::Local<v8::Value> payload;
    if (!data->Get(context, 0).ToLocal(&resolver_val) ||
        !data->Get(context, 1).ToLocal(&payload)) {
        return;
    }
    (void)access_user_operation(isolate, [&] {
        return resolver_val.As<v8::Promise::Resolver>()->Resolve(context, payload);
    });
}

}  // namespace

// --- Geolocation ---

v8::Local<v8::Value> GeolocationPositionErrorHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name) {
    if (name == "code") return v8::Integer::New(isolate, code_);
    if (name == "message") return v8_string(isolate, message_);
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> GeolocationPositionErrorHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&,
    const v8::FunctionCallbackInfo<v8::Value>&) {
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> GeolocationHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&) {
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> GeolocationHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    const std::string prefix = "Failed to execute '" + name + "' on 'Geolocation': ";
    if (arguments.Length() < 1) {
        throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
        return v8::Undefined(isolate);
    }
    if (name == "clearWatch") {
        v8::Local<v8::Value> prim;
        if (!web_idl_to_primitive(isolate, context, arguments[0], false, prefix, &prim)) {
            return v8::Undefined(isolate);
        }
        if (prim->IsSymbol()) {
            throw_type_error(isolate, prefix, "Cannot convert a Symbol value to a number");
            return v8::Undefined(isolate);
        }
        if (prim->IsBigInt()) {
            throw_type_error(isolate, prefix, "Cannot convert a BigInt value to a number");
            return v8::Undefined(isolate);
        }
        std::int32_t id = 0;
        if (!prim->Int32Value(context).To(&id)) return v8::Undefined(isolate);
        active_watches_.erase(id);
        return v8::Undefined(isolate);
    }
    if (!is_callable_object(arguments[0])) {
        throw_type_error(isolate, prefix, "parameter 1 is not of type 'Function'.");
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> error_cb = v8::Undefined(isolate);
    if (arguments.Length() > 1 && !arguments[1]->IsNullOrUndefined()) {
        if (!is_callable_object(arguments[1])) {
            throw_type_error(isolate, prefix, "parameter 2 is not of type 'Function'.");
            return v8::Undefined(isolate);
        }
        error_cb = arguments[1];
    }
    bool zero_timeout = false;
    if (arguments.Length() > 2 && !arguments[2]->IsNullOrUndefined()) {
        if (!arguments[2]->IsObject()) {
            throw_type_error(isolate, prefix,
                "The provided value is not of type 'PositionOptions'.");
            return v8::Undefined(isolate);
        }
        auto opts = arguments[2].As<v8::Object>();
        v8::Local<v8::Value> eha;
        v8::Local<v8::Value> max_age;
        v8::Local<v8::Value> timeout;
        if (!user_get(isolate, context, opts, v8_string(isolate, "enableHighAccuracy"))
                .ToLocal(&eha)) {
            return v8::Undefined(isolate);
        }
        (void)eha->BooleanValue(isolate);
        if (!user_get(isolate, context, opts, v8_string(isolate, "maximumAge"))
                .ToLocal(&max_age)) {
            return v8::Undefined(isolate);
        }
        if (!max_age->IsUndefined()) {
            v8::Local<v8::Number> n;
            if (!access_user_operation(isolate, [&] {
                    return max_age->ToNumber(context);
                }).ToLocal(&n)) {
                return v8::Undefined(isolate);
            }
        }
        if (!user_get(isolate, context, opts, v8_string(isolate, "timeout"))
                .ToLocal(&timeout)) {
            return v8::Undefined(isolate);
        }
        if (!timeout->IsUndefined()) {
            v8::Local<v8::Number> n;
            if (!access_user_operation(isolate, [&] {
                    return timeout->ToNumber(context);
                }).ToLocal(&n)) {
                return v8::Undefined(isolate);
            }
            const double val = n->Value();
            if (std::isnan(val) || std::trunc(val) <= 0.0) {
                zero_timeout = true;
            }
        }
    }
    const bool is_watch = name == "watchPosition";
    std::int32_t watch_id = 0;
    if (is_watch) {
        watch_id = ++next_watch_id_;
        active_watches_.insert(watch_id);
    }
    if (!secure_context_) {
        schedule_geolocation_error(isolate, context, this, owner_, error_cb, 1,
            "Only secure origins are allowed (see: https://goo.gl/Y0ZkNV).", watch_id);
    } else if (zero_timeout) {
        schedule_geolocation_error(isolate, context, this, owner_, error_cb, 3,
            "Timeout expired", watch_id);
        if (is_watch) {
            schedule_geolocation_error(isolate, context, this, owner_, error_cb, 1,
                "User denied Geolocation", watch_id);
        }
    } else {
        schedule_geolocation_error(isolate, context, this, owner_, error_cb, 1,
            "User denied Geolocation", watch_id);
    }
    return is_watch
        ? v8::Local<v8::Value>(v8::Integer::New(isolate, watch_id))
        : v8::Local<v8::Value>(v8::Undefined(isolate));
}

// --- Clipboard & ClipboardItem ---

v8::Local<v8::Value> ClipboardItemHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name) {
    if (name == "types") {
        auto arr = v8::Array::New(isolate, static_cast<int>(types_.size()));
        for (std::uint32_t i = 0; i < types_.size(); ++i) {
            (void)arr->CreateDataProperty(context, i, v8_string(isolate, types_[i]));
        }
        (void)arr->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen);
        return arr;
    }
    return v8::Undefined(isolate);
}

void clipboard_item_get_type_fulfill(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> resolver_val;
    v8::Local<v8::Value> type_val;
    if (!data->Get(context, 0).ToLocal(&resolver_val) ||
        !data->Get(context, 1).ToLocal(&type_val)) {
        return;
    }
    auto resolver = resolver_val.As<v8::Promise::Resolver>();
    const v8::Local<v8::Value> val = info.Length() > 0
        ? info[0] : v8::Local<v8::Value>(v8::Undefined(isolate));
    if (is_blob_object(isolate, context, val)) {
        (void)access_user_operation(isolate, [&] {
            return resolver->Resolve(context, val);
        });
        return;
    }
    if (val->IsString()) {
        v8::String::Utf8Value mime_utf8(isolate, type_val);
        v8::Local<v8::Object> blob;
        if (create_runtime_blob(isolate, context, val.As<v8::String>(),
                *mime_utf8 ? *mime_utf8 : "").ToLocal(&blob)) {
            (void)access_user_operation(isolate, [&] {
                return resolver->Resolve(context, blob);
            });
            return;
        }
    }
    auto err = v8::Exception::TypeError(v8_string(isolate,
        "Failed to execute 'getType' on 'ClipboardItem': "
        "The item value is not of type 'Blob or DOMString'.").As<v8::String>());
    (void)access_user_operation(isolate, [&] {
        return resolver->Reject(context, err);
    });
}

void clipboard_item_get_type_reject(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto resolver = info.Data().As<v8::Promise::Resolver>();
    const v8::Local<v8::Value> reason = info.Length() > 0
        ? info[0] : v8::Local<v8::Value>(v8::Undefined(isolate));
    (void)access_user_operation(isolate, [&] {
        return resolver->Reject(context, reason);
    });
}

v8::Local<v8::Value> ClipboardItemHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (name != "getType") return v8::Undefined(isolate);
    const std::string prefix = "Failed to execute 'getType' on 'ClipboardItem': ";
    v8::TryCatch caught(isolate);
    if (arguments.Length() < 1) {
        throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
        return reject_from_try_catch(isolate, context, caught);
    }
    std::string type;
    if (!web_idl_dom_string(isolate, context, arguments[0], prefix, &type)) {
        return reject_from_try_catch(isolate, context, caught);
    }
    for (std::size_t i = 0; i < types_.size(); ++i) {
        if (types_[i] == type) {
            auto stored = values_[i].Get(isolate);
            if (is_blob_object(isolate, context, stored)) {
                return resolved_promise(isolate, context, stored);
            }
            if (stored->IsString()) {
                v8::Local<v8::Object> blob;
                if (create_runtime_blob(isolate, context,
                        stored.As<v8::String>(), type).ToLocal(&blob)) {
                    return resolved_promise(isolate, context, blob);
                }
            }
            v8::Local<v8::Promise::Resolver> wrap;
            v8::Local<v8::Promise::Resolver> outer;
            if (!v8::Promise::Resolver::New(context).ToLocal(&wrap) ||
                !v8::Promise::Resolver::New(context).ToLocal(&outer) ||
                !wrap->Resolve(context, stored).FromMaybe(false)) {
                return v8::Undefined(isolate);
            }
            auto hook_data = v8::Array::New(isolate, 2);
            (void)hook_data->CreateDataProperty(context, 0, outer);
            (void)hook_data->CreateDataProperty(context, 1, v8_string(isolate, type));
            v8::Local<v8::Function> on_fulfill;
            v8::Local<v8::Function> on_reject;
            if (v8::Function::New(context, &clipboard_item_get_type_fulfill,
                    hook_data, 1, v8::ConstructorBehavior::kThrow).ToLocal(&on_fulfill) &&
                v8::Function::New(context, &clipboard_item_get_type_reject,
                    outer, 1, v8::ConstructorBehavior::kThrow).ToLocal(&on_reject)) {
                (void)wrap->GetPromise()->Then(context, on_fulfill, on_reject);
            }
            return outer->GetPromise();
        }
    }
    auto err = make_dom_exception(isolate, context, owner_, "NotFoundError",
        prefix + "The type '" + type + "' was not found");
    return rejected_promise(isolate, context, err);
}

v8::Local<v8::Value> ClipboardHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name) {
    if (name == "onclipboardchange") {
        return get_event_handler(isolate, name);
    }
    return v8::Undefined(isolate);
}

void ClipboardHost::set_property(v8::Isolate* isolate, v8::Local<v8::Context>,
    const std::string& name, v8::Local<v8::Value> value) {
    if (name == "onclipboardchange") {
        set_event_handler(isolate, name, value);
    }
}

v8::Local<v8::Value> ClipboardHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (!validate_bom_event_dispatch(isolate, context, name, arguments)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> event_result;
    if (handle_event_method(isolate, context, name, arguments, event_result)) {
        return event_result;
    }
    const std::string prefix = "Failed to execute '" + name + "' on 'Clipboard': ";
    v8::TryCatch caught(isolate);
    if (name == "read" || name == "readText") {
        auto err = make_dom_exception(isolate, context, owner_,
            "NotAllowedError", prefix + "Read permission denied.");
        return schedule_task_promise(isolate, context, true, err);
    }
    if (name == "write") {
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
            return reject_from_try_catch(isolate, context, caught);
        }
        std::size_t count = 0;
        if (!convert_sequence(isolate, context, arguments[0], prefix,
                [&](v8::Local<v8::Value> item) {
                    auto* host = host_object_backing(item);
                    if (host == nullptr || host->prototype_key() != "ClipboardItem") {
                        return throw_type_error(isolate, prefix,
                            "Failed to convert value to 'ClipboardItem'.");
                    }
                    ++count;
                    return true;
                })) {
            return reject_from_try_catch(isolate, context, caught);
        }
        if (count == 0) {
            return resolved_promise(isolate, context, v8::Undefined(isolate));
        }
        auto err = make_dom_exception(isolate, context, owner_,
            "NotAllowedError", prefix + "Write permission denied.");
        return schedule_task_promise(isolate, context, true, err);
    }
    if (name == "writeText") {
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
            return reject_from_try_catch(isolate, context, caught);
        }
        std::string text;
        if (!web_idl_dom_string(isolate, context, arguments[0], prefix, &text)) {
            return reject_from_try_catch(isolate, context, caught);
        }
        auto err = make_dom_exception(isolate, context, owner_,
            "NotAllowedError", prefix + "Write permission denied.");
        return schedule_task_promise(isolate, context, true, err);
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> construct_clipboard_item(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    v8::Local<v8::Array> arguments) {
    const std::string prefix = "Failed to construct 'ClipboardItem': ";
    if (arguments->Length() < 1) {
        throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> items_val;
    if (!arguments->Get(context, 0).ToLocal(&items_val)) return v8::Undefined(isolate);
    if (!items_val->IsObject()) {
        throw_type_error(isolate, prefix,
            "parameter 1 ('items') is not an object.");
        return v8::Undefined(isolate);
    }
    auto record = items_val.As<v8::Object>();
    v8::Local<v8::Array> keys;
    if (!access_user_operation(isolate, [&] {
            return record->GetOwnPropertyNames(context);
        }).ToLocal(&keys)) {
        return v8::Undefined(isolate);
    }
    if (keys->Length() == 0) {
        throw_type_error(isolate, prefix, "Empty dictionary argument");
        return v8::Undefined(isolate);
    }
    std::vector<std::string> types;
    std::vector<v8::Global<v8::Value>> values;
    for (std::uint32_t i = 0; i < keys->Length(); ++i) {
        v8::Local<v8::Value> key;
        v8::Local<v8::Value> val;
        if (!keys->Get(context, i).ToLocal(&key) ||
            !user_get(isolate, context, record, key).ToLocal(&val)) {
            return v8::Undefined(isolate);
        }
        std::string mime;
        if (!web_idl_dom_string(isolate, context, key, prefix, &mime)) {
            return v8::Undefined(isolate);
        }
        types.push_back(std::move(mime));
        values.emplace_back(isolate, val);
    }
    return owner->adopt_host_object(isolate, context,
        std::make_unique<ClipboardItemHost>(
            owner, std::move(types), std::move(values)));
}

void clipboard_item_supports(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    const auto seq = native_api_trace_record(
        isolate, info.This(), "ClipboardItem", "supports", AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
    watch_record(isolate, "ClipboardItem", "supports");
    const std::string prefix = "Failed to execute 'supports' on 'ClipboardItem': ";
    if (info.Length() < 1) {
        throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
        return;
    }
    std::string type;
    if (!web_idl_dom_string(isolate, context, info[0], prefix, &type)) {
        return;
    }
    if (type.starts_with("web ")) {
        type = type.substr(4);
    }
    const bool supported = type == "text/plain" || type == "text/html" ||
        type == "image/png" || type == "image/svg+xml" || type == "text/uri-list";
    info.GetReturnValue().Set(v8::Boolean::New(isolate, supported));
}

void credential_is_conditional_mediation_available(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    const auto seq = native_api_trace_record(
        isolate, info.This(), "Credential",
        "isConditionalMediationAvailable", AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
    watch_record(isolate, "Credential", "isConditionalMediationAvailable");
    info.GetReturnValue().Set(
        resolved_promise(isolate, context, v8::False(isolate)));
}

// --- CredentialsContainer ---

v8::Local<v8::Value> CredentialsContainerHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&) {
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> CredentialsContainerHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    const std::string prefix =
        "Failed to execute '" + name + "' on 'CredentialsContainer': ";
    v8::TryCatch caught(isolate);
    if (name == "preventSilentAccess") {
        return schedule_task_promise(isolate, context, false, v8::Undefined(isolate));
    }
    if (name == "store") {
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
            return reject_from_try_catch(isolate, context, caught);
        }
        auto* host = host_object_backing(arguments[0]);
        if (host == nullptr || host->prototype_key() != "Credential") {
            throw_type_error(isolate, prefix,
                "parameter 1 is not of type 'Credential'.");
            return reject_from_try_catch(isolate, context, caught);
        }
        return schedule_task_promise(isolate, context, false, arguments[0]);
    }
    if (name == "get") {
        bool any_type = false;
        if (arguments.Length() > 0 && !arguments[0]->IsNullOrUndefined()) {
            if (!arguments[0]->IsObject()) {
                throw_type_error(isolate, prefix,
                    "The provided value is not of type 'CredentialRequestOptions'.");
                return reject_from_try_catch(isolate, context, caught);
            }
            auto opts = arguments[0].As<v8::Object>();
            for (const char* key : {"federated", "identity", "mediation",
                                    "otp", "password", "publicKey", "signal"}) {
                v8::Local<v8::Value> val;
                if (!user_get(isolate, context, opts, v8_string(isolate, key))
                        .ToLocal(&val)) {
                    return reject_from_try_catch(isolate, context, caught);
                }
                if (std::string_view(key) == "mediation") {
                    if (!val->IsUndefined()) {
                        const std::string med_prefix =
                            prefix + "Failed to read the 'mediation' property from 'CredentialRequestOptions': ";
                        std::string med;
                        if (!web_idl_dom_string(isolate, context, val, med_prefix, &med)) {
                            return reject_from_try_catch(isolate, context, caught);
                        }
                        if (med != "silent" && med != "optional" &&
                            med != "required" && med != "conditional") {
                            throw_type_error(isolate, med_prefix,
                                "The provided value '" + med +
                                "' is not a valid enum value of type CredentialMediationRequirement.");
                            return reject_from_try_catch(isolate, context, caught);
                        }
                    }
                } else if (std::string_view(key) == "signal") {
                    if (!val->IsNullOrUndefined()) {
                        throw_type_error(isolate, prefix,
                            "Failed to read the 'signal' property from 'CredentialRequestOptions': "
                            "Failed to convert value to 'AbortSignal'.");
                        return reject_from_try_catch(isolate, context, caught);
                    }
                } else if (std::string_view(key) == "password") {
                    if (val->BooleanValue(isolate)) any_type = true;
                } else if (!val->IsUndefined()) {
                    if (!val->IsObject()) {
                        throw_type_error(isolate, prefix,
                            std::string("Failed to read the '") + key +
                            "' property from 'CredentialRequestOptions': "
                            "The provided value is not of type 'Object'.");
                        return reject_from_try_catch(isolate, context, caught);
                    }
                    any_type = true;
                }
            }
        }
        if (!any_type) {
            auto err = make_dom_exception(isolate, context, owner_,
                "NotSupportedError",
                "No credential type was specified in the request.");
            return rejected_promise(isolate, context, err);
        }
        return schedule_task_promise(isolate, context, false, v8::Null(isolate));
    }
    if (name == "create") {
        int count = 0;
        if (arguments.Length() > 0 && !arguments[0]->IsNullOrUndefined()) {
            if (!arguments[0]->IsObject()) {
                throw_type_error(isolate, prefix,
                    "The provided value is not of type 'CredentialCreationOptions'.");
                return reject_from_try_catch(isolate, context, caught);
            }
            auto opts = arguments[0].As<v8::Object>();
            for (const char* key : {"federated", "password", "publicKey", "signal"}) {
                v8::Local<v8::Value> val;
                if (!user_get(isolate, context, opts, v8_string(isolate, key))
                        .ToLocal(&val)) {
                    return reject_from_try_catch(isolate, context, caught);
                }
                if (std::string_view(key) == "signal") {
                    if (!val->IsNullOrUndefined()) {
                        throw_type_error(isolate, prefix,
                            "Failed to read the 'signal' property from 'CredentialCreationOptions': "
                            "Failed to convert value to 'AbortSignal'.");
                        return reject_from_try_catch(isolate, context, caught);
                    }
                } else if (!val->IsUndefined()) {
                    if (!val->IsObject()) {
                        throw_type_error(isolate, prefix,
                            std::string("Failed to read the '") + key +
                            "' property from 'CredentialCreationOptions': "
                            "The provided value is not of type 'Object'.");
                        return reject_from_try_catch(isolate, context, caught);
                    }
                    ++count;
                }
            }
        }
        if (count != 1) {
            auto err = make_dom_exception(isolate, context, owner_,
                "NotSupportedError",
                "Only exactly one of 'password', 'federated', and 'publicKey' "
                "credential types are currently supported.");
            return rejected_promise(isolate, context, err);
        }
        return schedule_task_promise(isolate, context, false, v8::Null(isolate));
    }
    return v8::Undefined(isolate);
}

// --- LockManager & Lock ---

v8::Local<v8::Value> LockHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name) {
    if (name == "name") return v8_string(isolate, name_);
    if (name == "mode") return v8_string(isolate, mode_);
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> LockHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&,
    const v8::FunctionCallbackInfo<v8::Value>&) {
    return v8::Undefined(isolate);
}

LockManagerHost::LockManagerHost(PageState* owner, const std::string& base_url)
    : owner_(owner) {
    const DomUrl parsed = resolve_dom_url(base_url, "");
    opaque_origin_ = !parsed.valid || parsed.origin == "null";
}

void LockManagerHost::release_v8_handles() noexcept {
    for (auto& entry : held_) {
        entry.callback.Reset();
        entry.resolver.Reset();
    }
    for (auto& entry : pending_) {
        entry.callback.Reset();
        entry.resolver.Reset();
    }
    held_.clear();
    pending_.clear();
}

v8::Local<v8::Value> LockManagerHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&) {
    return v8::Undefined(isolate);
}

void LockManagerHost::release_held_lock(
    v8::Isolate* isolate, v8::Local<v8::Context> context, std::uint64_t id) {
    held_.erase(std::remove_if(held_.begin(), held_.end(),
        [id](const LockEntry& entry) { return entry.id == id; }), held_.end());
    process_queue(isolate, context);
}

void LockManagerHost::process_queue(
    v8::Isolate* isolate, v8::Local<v8::Context> context) {
    auto can_grant = [&](const LockEntry& req) {
        for (const auto& held : held_) {
            if (held.name == req.name) {
                if (held.mode == "exclusive" || req.mode == "exclusive") {
                    return false;
                }
            }
        }
        return true;
    };
    std::unordered_set<std::string> blocked_names;
    std::size_t idx = 0;
    while (idx < pending_.size()) {
        const bool grantable = !blocked_names.contains(pending_[idx].name) &&
            can_grant(pending_[idx]);
        if (!grantable && !pending_[idx].if_available) {
            blocked_names.insert(pending_[idx].name);
            ++idx;
            continue;
        }
        LockEntry current = std::move(pending_[idx]);
        pending_.erase(pending_.begin() + static_cast<std::ptrdiff_t>(idx));
        auto cb = current.callback.Get(isolate);
        auto resolver = current.resolver.Get(isolate);
        if (!grantable && current.if_available) {
            v8::TryCatch caught(isolate);
            v8::Local<v8::Value> null_arg[] = {v8::Null(isolate)};
            v8::Local<v8::Value> ret;
            if (!user_call(isolate, context, cb, v8::Undefined(isolate), 1, null_arg)
                    .ToLocal(&ret)) {
                if (!caught.CanContinue()) { caught.ReThrow(); return; }
                auto err = caught.Exception();
                caught.Reset();
                (void)resolver->Reject(context, err);
                continue;
            }
            v8::Local<v8::Promise::Resolver> adopt;
            if (v8::Promise::Resolver::New(context).ToLocal(&adopt) &&
                adopt->Resolve(context, ret).FromMaybe(false)) {
                (void)resolver->Resolve(context, adopt->GetPromise());
            }
            continue;
        }
        const std::uint64_t id = current.id;
        const std::string lock_name = current.name;
        const std::string lock_mode = current.mode;
        held_.push_back(std::move(current));
        auto lock_obj = owner_->adopt_host_object(isolate, context,
            std::make_unique<LockHost>(lock_name, lock_mode));
        v8::TryCatch caught(isolate);
        v8::Local<v8::Value> lock_args[] = {lock_obj};
        v8::Local<v8::Value> ret;
        if (!user_call(isolate, context, cb, v8::Undefined(isolate), 1, lock_args)
                .ToLocal(&ret)) {
            if (!caught.CanContinue()) { caught.ReThrow(); return; }
            auto err = caught.Exception();
            caught.Reset();
            release_held_lock(isolate, context, id);
            (void)resolver->Reject(context, err);
            continue;
        }
        v8::Local<v8::Promise::Resolver> wrap;
        if (!v8::Promise::Resolver::New(context).ToLocal(&wrap) ||
            !wrap->Resolve(context, ret).FromMaybe(false)) {
            release_held_lock(isolate, context, id);
            continue;
        }
        auto hook_data = v8::Array::New(isolate, 3);
        (void)hook_data->CreateDataProperty(context, 0, v8::External::New(
            isolate, this, v8::kExternalPointerTypeTagDefault));
        (void)hook_data->CreateDataProperty(context, 1,
            v8::Number::New(isolate, static_cast<double>(id)));
        (void)hook_data->CreateDataProperty(context, 2, resolver);
        v8::Local<v8::Function> on_fulfill;
        v8::Local<v8::Function> on_reject;
        if (v8::Function::New(context, &settle_held_lock_fulfill, hook_data, 1,
                v8::ConstructorBehavior::kThrow).ToLocal(&on_fulfill) &&
            v8::Function::New(context, &settle_held_lock_reject, hook_data, 1,
                v8::ConstructorBehavior::kThrow).ToLocal(&on_reject)) {
            (void)wrap->GetPromise()->Then(context, on_fulfill, on_reject);
        }
    }
}

v8::Local<v8::Value> LockManagerHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    const std::string prefix = "Failed to execute '" + name + "' on 'LockManager': ";
    v8::TryCatch caught(isolate);
    if (name == "query") {
        if (opaque_origin_) {
            auto err = make_dom_exception(isolate, context, owner_,
                "SecurityError", prefix + "Access to the Web Locks API is denied in this context.");
            return rejected_promise(isolate, context, err);
        }
        v8::Local<v8::Promise::Resolver> resolver;
        if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
            return v8::Undefined(isolate);
        }
        auto* state = state_from_isolate(isolate);
        if (state == nullptr) return v8::Undefined(isolate);
        auto data = v8::Array::New(isolate, 2);
        (void)data->CreateDataProperty(context, 0, v8::External::New(
            isolate, this, v8::kExternalPointerTypeTagDefault));
        (void)data->CreateDataProperty(context, 1, resolver);
        v8::Local<v8::Function> delivery;
        if (v8::Function::New(context, &deliver_lock_manager_query, data, 0,
                v8::ConstructorBehavior::kThrow).ToLocal(&delivery)) {
            (void)state->register_timer(delivery, 0, false, false);
        }
        return resolver->GetPromise();
    }
    if (name == "request") {
        if (arguments.Length() < 2) {
            throw_type_error(isolate, prefix, "2 arguments required, but only " +
                std::to_string(arguments.Length()) + " present.");
            return reject_from_try_catch(isolate, context, caught);
        }
        std::string lock_name;
        if (!web_idl_dom_string(isolate, context, arguments[0], prefix, &lock_name)) {
            return reject_from_try_catch(isolate, context, caught);
        }
        std::string mode = "exclusive";
        bool if_available = false;
        bool steal = false;
        v8::Local<v8::Value> cb_arg;
        if (arguments.Length() == 2) {
            if (!is_callable_object(arguments[1])) {
                throw_type_error(isolate, prefix,
                    "parameter 2 is not of type 'Function'.");
                return reject_from_try_catch(isolate, context, caught);
            }
            cb_arg = arguments[1];
        } else {
            if (!arguments[1]->IsNullOrUndefined()) {
                if (!arguments[1]->IsObject()) {
                    throw_type_error(isolate, prefix,
                        "The provided value is not of type 'LockOptions'.");
                    return reject_from_try_catch(isolate, context, caught);
                }
                auto opts = arguments[1].As<v8::Object>();
                v8::Local<v8::Value> if_avail_val;
                v8::Local<v8::Value> mode_val;
                v8::Local<v8::Value> signal_val;
                v8::Local<v8::Value> steal_val;
                if (!user_get(isolate, context, opts, v8_string(isolate, "ifAvailable"))
                        .ToLocal(&if_avail_val)) {
                    return reject_from_try_catch(isolate, context, caught);
                }
                if_available = if_avail_val->BooleanValue(isolate);
                if (!user_get(isolate, context, opts, v8_string(isolate, "mode"))
                        .ToLocal(&mode_val)) {
                    return reject_from_try_catch(isolate, context, caught);
                }
                if (!mode_val->IsUndefined()) {
                    const std::string mode_prefix =
                        prefix + "Failed to read the 'mode' property from 'LockOptions': ";
                    if (!web_idl_dom_string(isolate, context, mode_val, mode_prefix, &mode)) {
                        return reject_from_try_catch(isolate, context, caught);
                    }
                    if (mode != "exclusive" && mode != "shared") {
                        throw_type_error(isolate, mode_prefix,
                            "The provided value '" + mode +
                            "' is not a valid enum value of type LockMode.");
                        return reject_from_try_catch(isolate, context, caught);
                    }
                }
                if (!user_get(isolate, context, opts, v8_string(isolate, "signal"))
                        .ToLocal(&signal_val)) {
                    return reject_from_try_catch(isolate, context, caught);
                }
                if (!signal_val->IsNullOrUndefined()) {
                    throw_type_error(isolate, prefix,
                        "Failed to read the 'signal' property from 'LockOptions': "
                        "Failed to convert value to 'AbortSignal'.");
                    return reject_from_try_catch(isolate, context, caught);
                }
                if (!user_get(isolate, context, opts, v8_string(isolate, "steal"))
                        .ToLocal(&steal_val)) {
                    return reject_from_try_catch(isolate, context, caught);
                }
                steal = steal_val->BooleanValue(isolate);
            }
            if (!is_callable_object(arguments[2])) {
                throw_type_error(isolate, prefix,
                    "parameter 3 is not of type 'Function'.");
                return reject_from_try_catch(isolate, context, caught);
            }
            cb_arg = arguments[2];
        }
        if (opaque_origin_) {
            auto err = make_dom_exception(isolate, context, owner_,
                "SecurityError", prefix + "Access to the Web Locks API is denied in this context.");
            return rejected_promise(isolate, context, err);
        }
        if (lock_name.starts_with('-')) {
            auto err = make_dom_exception(isolate, context, owner_,
                "NotSupportedError", prefix + "Names cannot start with '-'.");
            return rejected_promise(isolate, context, err);
        }
        if (steal && if_available) {
            auto err = make_dom_exception(isolate, context, owner_,
                "NotSupportedError",
                prefix + "The 'steal' and 'ifAvailable' options cannot be used together.");
            return rejected_promise(isolate, context, err);
        }
        if (steal && mode != "exclusive") {
            auto err = make_dom_exception(isolate, context, owner_,
                "NotSupportedError",
                prefix + "The 'steal' option may only be used with 'exclusive' locks.");
            return rejected_promise(isolate, context, err);
        }
        if (steal) {
            for (auto& existing : held_) {
                if (existing.name == lock_name && !existing.resolver.IsEmpty()) {
                    auto abort_err = make_dom_exception(isolate, context, owner_,
                        "AbortError",
                        "Lock broken by another request with the 'steal' option.");
                    (void)existing.resolver.Get(isolate)->Reject(context, abort_err);
                }
            }
            held_.erase(std::remove_if(held_.begin(), held_.end(),
                [&](const LockEntry& entry) { return entry.name == lock_name; }),
                held_.end());
        }
        v8::Local<v8::Promise::Resolver> resolver;
        if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
            return v8::Undefined(isolate);
        }
        LockEntry entry;
        entry.id = ++next_id_;
        entry.name = std::move(lock_name);
        entry.mode = std::move(mode);
        entry.if_available = if_available;
        entry.callback.Reset(isolate, cb_arg.As<v8::Object>());
        entry.resolver.Reset(isolate, resolver);
        if (steal) {
            pending_.insert(pending_.begin(), std::move(entry));
        } else {
            pending_.push_back(std::move(entry));
        }
        if (auto* state = state_from_isolate(isolate)) {
            v8::Local<v8::Function> pump;
            if (v8::Function::New(context, &deliver_lock_manager_pump,
                    v8::External::New(isolate, this, v8::kExternalPointerTypeTagDefault),
                    0, v8::ConstructorBehavior::kThrow).ToLocal(&pump)) {
                (void)state->register_timer(pump, 0, false, false);
            }
        }
        return resolver->GetPromise();
    }
    return v8::Undefined(isolate);
}

// --- WakeLock & WakeLockSentinel ---

v8::Local<v8::Value> WakeLockSentinelHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name) {
    if (name == "onrelease") return get_event_handler(isolate, name);
    if (name == "released") return v8::Boolean::New(isolate, released_);
    if (name == "type") return v8_string(isolate, type_);
    return v8::Undefined(isolate);
}

void WakeLockSentinelHost::set_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name, v8::Local<v8::Value> value) {
    if (name == "onrelease") set_event_handler(isolate, name, value);
}

v8::Local<v8::Value> WakeLockSentinelHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (!validate_bom_event_dispatch(isolate, context, name, arguments)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> event_result;
    if (handle_event_method(isolate, context, name, arguments, event_result)) {
        return event_result;
    }
    if (name == "release") {
        if (!released_) {
            released_ = true;
            if (!self_.IsEmpty()) {
                v8::Local<v8::Object> event_obj;
                if (create_runtime_event(isolate, context, "release").ToLocal(&event_obj)) {
                    dispatch_event(isolate, context, event_obj, self_.Get(isolate));
                }
            }
        }
        return resolved_promise(isolate, context, v8::Undefined(isolate));
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> WakeLockHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&) {
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> WakeLockHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (name != "request") return v8::Undefined(isolate);
    const std::string prefix = "Failed to execute 'request' on 'WakeLock': ";
    v8::TryCatch caught(isolate);
    std::string type = "screen";
    if (arguments.Length() > 0 && !arguments[0]->IsUndefined()) {
        if (!web_idl_dom_string(isolate, context, arguments[0], prefix, &type)) {
            return reject_from_try_catch(isolate, context, caught);
        }
        if (type != "screen") {
            throw_type_error(isolate, prefix,
                "The provided value '" + type +
                "' is not a valid enum value of type WakeLockType.");
            return reject_from_try_catch(isolate, context, caught);
        }
    }
    auto sentinel = std::make_unique<WakeLockSentinelHost>(type);
    auto* raw = sentinel.get();
    auto wrapper = owner_->adopt_host_object(isolate, context, std::move(sentinel));
    raw->bind_wrapper(isolate, wrapper);
    return schedule_task_promise(isolate, context, false, wrapper);
}

// --- Keyboard & KeyboardLayoutMap ---

v8::Local<v8::Value> KeyboardLayoutMapHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name) {
    if (name == "size") {
        return v8::Integer::New(isolate, static_cast<int>(kKeyboardLayoutEntries.size()));
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> KeyboardLayoutMapHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    const std::string prefix = "Failed to execute '" + name + "' on 'KeyboardLayoutMap': ";
    if (name == "get" || name == "has") {
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
            return v8::Undefined(isolate);
        }
        std::string key;
        if (!web_idl_dom_string(isolate, context, arguments[0], prefix, &key)) {
            return v8::Undefined(isolate);
        }
        for (const auto& [code, val] : kKeyboardLayoutEntries) {
            if (key == code) {
                return name == "has"
                    ? v8::Local<v8::Value>(v8::True(isolate))
                    : v8_string(isolate, val);
            }
        }
        return name == "has"
            ? v8::Local<v8::Value>(v8::False(isolate))
            : v8::Local<v8::Value>(v8::Undefined(isolate));
    }
    if (name == "forEach") {
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
            return v8::Undefined(isolate);
        }
        if (!is_callable_object(arguments[0])) {
            throw_type_error(isolate, prefix, "parameter 1 is not of type 'Function'.");
            return v8::Undefined(isolate);
        }
        const v8::Local<v8::Value> this_arg = arguments.Length() > 1
            ? arguments[1] : v8::Local<v8::Value>(v8::Undefined(isolate));
        for (const auto& [code, val] : kKeyboardLayoutEntries) {
            v8::Local<v8::Value> cb_args[] = {
                v8_string(isolate, val), v8_string(isolate, code), arguments.This()};
            if (user_call(isolate, context, arguments[0], this_arg, 3, cb_args).IsEmpty()) {
                return v8::Undefined(isolate);
            }
        }
        return v8::Undefined(isolate);
    }
    if (name == "entries" || name == "keys" || name == "values") {
        auto arr = v8::Array::New(isolate, static_cast<int>(kKeyboardLayoutEntries.size()));
        for (std::uint32_t i = 0; i < kKeyboardLayoutEntries.size(); ++i) {
            const auto& [code, val] = kKeyboardLayoutEntries[i];
            if (name == "keys") {
                (void)arr->CreateDataProperty(context, i, v8_string(isolate, code));
            } else if (name == "values") {
                (void)arr->CreateDataProperty(context, i, v8_string(isolate, val));
            } else {
                auto pair = v8::Array::New(isolate, 2);
                (void)pair->CreateDataProperty(context, 0, v8_string(isolate, code));
                (void)pair->CreateDataProperty(context, 1, v8_string(isolate, val));
                (void)arr->CreateDataProperty(context, i, pair);
            }
        }
        v8::Local<v8::Value> values_fn;
        v8::Local<v8::Value> iterator;
        if (!arr->Get(context, v8::Symbol::GetIterator(isolate)).ToLocal(&values_fn) ||
            !values_fn->IsFunction() ||
            !values_fn.As<v8::Function>()->Call(context, arr, 0, nullptr).ToLocal(&iterator)) {
            return v8::Undefined(isolate);
        }
        return iterator;
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> KeyboardHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&) {
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> KeyboardHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (name == "unlock") {
        return v8::Undefined(isolate);
    }
    const std::string prefix = "Failed to execute '" + name + "' on 'Keyboard': ";
    v8::TryCatch caught(isolate);
    if (name == "getLayoutMap") {
        auto map_obj = owner_->adopt_host_object(isolate, context,
            std::make_unique<KeyboardLayoutMapHost>());
        return schedule_task_promise(isolate, context, false, map_obj);
    }
    if (name == "lock") {
        if (arguments.Length() > 0 && !arguments[0]->IsUndefined()) {
            if (!convert_sequence(isolate, context, arguments[0], prefix,
                    [&](v8::Local<v8::Value> item) {
                        std::string code;
                        return web_idl_dom_string(isolate, context, item, prefix, &code);
                    })) {
                return reject_from_try_catch(isolate, context, caught);
            }
        }
        return schedule_task_promise(isolate, context, false, v8::Undefined(isolate));
    }
    return v8::Undefined(isolate);
}

// --- MediaMetadata & MediaSession ---

v8::Local<v8::Value> MediaMetadataHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name) {
    if (name == "title") return v8_string(isolate, title_);
    if (name == "artist") return v8_string(isolate, artist_);
    if (name == "album") return v8_string(isolate, album_);
    if (name == "artwork") {
        auto arr = v8::Array::New(isolate, static_cast<int>(artwork_.size()));
        for (std::uint32_t i = 0; i < artwork_.size(); ++i) {
            auto img = v8::Object::New(isolate);
            (void)img->CreateDataProperty(context,
                v8_string(isolate, "sizes").As<v8::String>(),
                v8_string(isolate, artwork_[i].sizes));
            (void)img->CreateDataProperty(context,
                v8_string(isolate, "src").As<v8::String>(),
                v8_string(isolate, artwork_[i].src));
            (void)img->CreateDataProperty(context,
                v8_string(isolate, "type").As<v8::String>(),
                v8_string(isolate, artwork_[i].type));
            (void)img->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen);
            (void)arr->CreateDataProperty(context, i, img);
        }
        (void)arr->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen);
        return arr;
    }
    if (name == "chapterInfo") {
        if (chapter_info_.IsEmpty()) {
            auto arr = v8::Array::New(isolate, 0);
            (void)arr->SetIntegrityLevel(context, v8::IntegrityLevel::kFrozen);
            chapter_info_.Reset(isolate, arr);
        }
        return chapter_info_.Get(isolate);
    }
    return v8::Undefined(isolate);
}

void MediaMetadataHost::set_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    v8::Local<v8::Value> value) {
    const std::string prefix =
        "Failed to set the '" + name + "' property on 'MediaMetadata': ";
    if (name == "title") {
        (void)web_idl_dom_string(isolate, context, value, prefix, &title_);
    } else if (name == "artist") {
        (void)web_idl_dom_string(isolate, context, value, prefix, &artist_);
    } else if (name == "album") {
        (void)web_idl_dom_string(isolate, context, value, prefix, &album_);
    } else if (name == "artwork") {
        std::vector<MediaImageRecord> updated;
        if (parse_media_artwork_list(isolate, context, value, base_url_, prefix, &updated)) {
            artwork_ = std::move(updated);
        }
    }
}

v8::Local<v8::Value> MediaMetadataHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&,
    const v8::FunctionCallbackInfo<v8::Value>&) {
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> construct_media_metadata(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    const std::string& base_url, v8::Local<v8::Array> arguments) {
    const std::string prefix = "Failed to construct 'MediaMetadata': ";
    std::string title;
    std::string artist;
    std::string album;
    std::vector<MediaImageRecord> artwork;
    if (arguments->Length() > 0) {
        v8::Local<v8::Value> init;
        if (!arguments->Get(context, 0).ToLocal(&init)) return v8::Undefined(isolate);
        if (!init->IsNullOrUndefined()) {
            if (!init->IsObject()) {
                throw_type_error(isolate, prefix,
                    "The provided value is not of type 'MediaMetadataInit'.");
                return v8::Undefined(isolate);
            }
            auto dict = init.As<v8::Object>();
            v8::Local<v8::Value> v_album;
            v8::Local<v8::Value> v_artist;
            v8::Local<v8::Value> v_artwork;
            v8::Local<v8::Value> v_title;
            if (!user_get(isolate, context, dict, v8_string(isolate, "album")).ToLocal(&v_album))
                return v8::Undefined(isolate);
            if (!v_album->IsUndefined() && !web_idl_dom_string(isolate, context, v_album, prefix, &album))
                return v8::Undefined(isolate);
            if (!user_get(isolate, context, dict, v8_string(isolate, "artist")).ToLocal(&v_artist))
                return v8::Undefined(isolate);
            if (!v_artist->IsUndefined() && !web_idl_dom_string(isolate, context, v_artist, prefix, &artist))
                return v8::Undefined(isolate);
            if (!user_get(isolate, context, dict, v8_string(isolate, "artwork")).ToLocal(&v_artwork))
                return v8::Undefined(isolate);
            if (!v_artwork->IsUndefined() &&
                !parse_media_artwork_list(isolate, context, v_artwork, base_url, prefix, &artwork)) {
                return v8::Undefined(isolate);
            }
            if (!user_get(isolate, context, dict, v8_string(isolate, "title")).ToLocal(&v_title))
                return v8::Undefined(isolate);
            if (!v_title->IsUndefined() && !web_idl_dom_string(isolate, context, v_title, prefix, &title))
                return v8::Undefined(isolate);
        }
    }
    return owner->adopt_host_object(isolate, context,
        std::make_unique<MediaMetadataHost>(base_url, std::move(title),
            std::move(artist), std::move(album), std::move(artwork)));
}

v8::Local<v8::Value> MediaSessionHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name) {
    if (name == "metadata") {
        return metadata_.IsEmpty()
            ? v8::Local<v8::Value>(v8::Null(isolate))
            : v8::Local<v8::Value>(metadata_.Get(isolate));
    }
    if (name == "playbackState") {
        return v8_string(isolate, playback_state_);
    }
    return v8::Undefined(isolate);
}

void MediaSessionHost::set_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    v8::Local<v8::Value> value) {
    const std::string prefix =
        "Failed to set the '" + name + "' property on 'MediaSession': ";
    if (name == "metadata") {
        if (value->IsNullOrUndefined()) {
            metadata_.Reset();
            return;
        }
        auto* host = host_object_backing(value);
        if (host == nullptr || host->prototype_key() != "MediaMetadata") {
            throw_type_error(isolate, prefix,
                "Failed to convert value to 'MediaMetadata'.");
            return;
        }
        metadata_.Reset(isolate, value.As<v8::Object>());
    } else if (name == "playbackState") {
        std::string next_state;
        if (!web_idl_dom_string(isolate, context, value, prefix, &next_state)) {
            return;
        }
        if (next_state == "none" || next_state == "paused" || next_state == "playing") {
            playback_state_ = std::move(next_state);
        }
    }
}

v8::Local<v8::Value> MediaSessionHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    const std::string prefix = "Failed to execute '" + name + "' on 'MediaSession': ";
    if (name == "setActionHandler") {
        if (arguments.Length() < 2) {
            throw_type_error(isolate, prefix, "2 arguments required, but only " +
                std::to_string(arguments.Length()) + " present.");
            return v8::Undefined(isolate);
        }
        std::string action;
        if (!web_idl_dom_string(isolate, context, arguments[0], prefix, &action)) {
            return v8::Undefined(isolate);
        }
        static const std::unordered_set<std::string_view> valid_actions = {
            "play", "pause", "seekbackward", "seekforward", "previoustrack",
            "nexttrack", "skipad", "stop", "seekto", "togglemicrophone",
            "togglecamera", "hangup", "previousslide", "nextslide",
            "enterpictureinpicture",
        };
        if (!valid_actions.contains(action)) {
            throw_type_error(isolate, prefix,
                "The provided value '" + action +
                "' is not a valid enum value of type MediaSessionAction.");
            return v8::Undefined(isolate);
        }
        if (arguments[1]->IsNullOrUndefined()) {
            action_handlers_.erase(action);
            return v8::Undefined(isolate);
        }
        if (!is_callable_object(arguments[1])) {
            throw_type_error(isolate, prefix, "parameter 2 is not of type 'Function'.");
            return v8::Undefined(isolate);
        }
        action_handlers_[action].Reset(isolate, arguments[1].As<v8::Object>());
        return v8::Undefined(isolate);
    }
    if (name == "setCameraActive" || name == "setMicrophoneActive") {
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
            return v8::Undefined(isolate);
        }
        (void)arguments[0]->BooleanValue(isolate);
        return v8::Undefined(isolate);
    }
    if (name == "setPositionState") {
        if (arguments.Length() == 0 || arguments[0]->IsNullOrUndefined()) {
            return v8::Undefined(isolate);
        }
        if (!arguments[0]->IsObject()) {
            throw_type_error(isolate, prefix,
                "The provided value is not of type 'MediaPositionState'.");
            return v8::Undefined(isolate);
        }
        auto dict = arguments[0].As<v8::Object>();
        double duration = std::numeric_limits<double>::quiet_NaN();
        bool has_duration = false;
        double playback_rate = 1.0;
        double position = 0.0;
        v8::Local<v8::Value> v_dur;
        v8::Local<v8::Value> v_rate;
        v8::Local<v8::Value> v_pos;
        if (!user_get(isolate, context, dict, v8_string(isolate, "duration")).ToLocal(&v_dur))
            return v8::Undefined(isolate);
        if (!v_dur->IsUndefined()) {
            has_duration = true;
            v8::Local<v8::Number> n;
            if (!access_user_operation(isolate, [&] { return v_dur->ToNumber(context); }).ToLocal(&n))
                return v8::Undefined(isolate);
            duration = n->Value();
            if (std::isnan(duration)) {
                throw_type_error(isolate, prefix,
                    "Failed to read the 'duration' property from 'MediaPositionState': "
                    "The provided double value is non-finite.");
                return v8::Undefined(isolate);
            }
        }
        if (!user_get(isolate, context, dict, v8_string(isolate, "playbackRate")).ToLocal(&v_rate))
            return v8::Undefined(isolate);
        if (!v_rate->IsUndefined()) {
            v8::Local<v8::Number> n;
            if (!access_user_operation(isolate, [&] { return v_rate->ToNumber(context); }).ToLocal(&n))
                return v8::Undefined(isolate);
            playback_rate = n->Value();
            if (!std::isfinite(playback_rate)) {
                throw_type_error(isolate, prefix,
                    "Failed to read the 'playbackRate' property from 'MediaPositionState': "
                    "The provided double value is non-finite.");
                return v8::Undefined(isolate);
            }
        }
        if (!user_get(isolate, context, dict, v8_string(isolate, "position")).ToLocal(&v_pos))
            return v8::Undefined(isolate);
        if (!v_pos->IsUndefined()) {
            v8::Local<v8::Number> n;
            if (!access_user_operation(isolate, [&] { return v_pos->ToNumber(context); }).ToLocal(&n))
                return v8::Undefined(isolate);
            position = n->Value();
            if (!std::isfinite(position)) {
                throw_type_error(isolate, prefix,
                    "Failed to read the 'position' property from 'MediaPositionState': "
                    "The provided double value is non-finite.");
                return v8::Undefined(isolate);
            }
        }
        if (!has_duration) {
            throw_type_error(isolate, prefix, "The duration must be provided.");
            return v8::Undefined(isolate);
        }
        if (duration < 0.0) {
            throw_type_error(isolate, prefix,
                "The provided duration cannot be less than zero.");
            return v8::Undefined(isolate);
        }
        if (position < 0.0) {
            throw_type_error(isolate, prefix,
                "The provided position cannot be less than zero.");
            return v8::Undefined(isolate);
        }
        if (position > duration) {
            throw_type_error(isolate, prefix,
                "The provided position cannot be greater than the duration.");
            return v8::Undefined(isolate);
        }
        if (playback_rate == 0.0) {
            throw_type_error(isolate, prefix,
                "The provided playbackRate cannot be equal to zero.");
            return v8::Undefined(isolate);
        }
        return v8::Undefined(isolate);
    }
    return v8::Undefined(isolate);
}

// --- MediaCapabilities ---

v8::Local<v8::Value> MediaCapabilitiesHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&) {
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> MediaCapabilitiesHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    const bool is_decode = name == "decodingInfo";
    const std::string dict_name = is_decode
        ? "MediaDecodingConfiguration" : "MediaEncodingConfiguration";
    const std::string prefix =
        "Failed to execute '" + name + "' on 'MediaCapabilities': ";
    v8::TryCatch caught(isolate);
    if (arguments.Length() < 1) {
        throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
        return reject_from_try_catch(isolate, context, caught);
    }
    if (!arguments[0]->IsObject()) {
        throw_type_error(isolate, prefix,
            "The provided value is not of type '" + dict_name + "'.");
        return reject_from_try_catch(isolate, context, caught);
    }
    auto cfg = arguments[0].As<v8::Object>();
    v8::Local<v8::Value> v_audio;
    v8::Local<v8::Value> v_type;
    v8::Local<v8::Value> v_video;
    if (!user_get(isolate, context, cfg, v8_string(isolate, "audio")).ToLocal(&v_audio)) {
        return reject_from_try_catch(isolate, context, caught);
    }
    bool has_audio = false;
    std::string audio_type;
    if (!v_audio->IsUndefined()) {
        if (!v_audio->IsObject()) {
            throw_type_error(isolate, prefix,
                "The provided value is not of type 'AudioConfiguration'.");
            return reject_from_try_catch(isolate, context, caught);
        }
        has_audio = true;
        v8::Local<v8::Value> ct;
        if (!user_get(isolate, context, v_audio.As<v8::Object>(),
                v8_string(isolate, "contentType")).ToLocal(&ct)) {
            return reject_from_try_catch(isolate, context, caught);
        }
        if (ct->IsUndefined()) {
            throw_type_error(isolate, prefix,
                "Failed to read the 'contentType' property from 'AudioConfiguration': "
                "Required member is undefined.");
            return reject_from_try_catch(isolate, context, caught);
        }
        if (!web_idl_dom_string(isolate, context, ct, prefix, &audio_type)) {
            return reject_from_try_catch(isolate, context, caught);
        }
    }
    if (!user_get(isolate, context, cfg, v8_string(isolate, "type")).ToLocal(&v_type)) {
        return reject_from_try_catch(isolate, context, caught);
    }
    if (v_type->IsUndefined()) {
        throw_type_error(isolate, prefix,
            "Failed to read the 'type' property from '" + dict_name +
            "': Required member is undefined.");
        return reject_from_try_catch(isolate, context, caught);
    }
    std::string type;
    if (!web_idl_dom_string(isolate, context, v_type, prefix, &type)) {
        return reject_from_try_catch(isolate, context, caught);
    }
    if (is_decode) {
        if (type != "file" && type != "media-source" && type != "webrtc") {
            throw_type_error(isolate, prefix,
                "Failed to read the 'type' property from 'MediaDecodingConfiguration': "
                "The provided value '" + type +
                "' is not a valid enum value of type MediaDecodingType.");
            return reject_from_try_catch(isolate, context, caught);
        }
    } else if (type != "record" && type != "webrtc") {
        throw_type_error(isolate, prefix,
            "Failed to read the 'type' property from 'MediaEncodingConfiguration': "
            "The provided value '" + type +
            "' is not a valid enum value of type MediaEncodingType.");
        return reject_from_try_catch(isolate, context, caught);
    }
    if (!user_get(isolate, context, cfg, v8_string(isolate, "video")).ToLocal(&v_video)) {
        return reject_from_try_catch(isolate, context, caught);
    }
    bool has_video = false;
    std::string video_type;
    if (!v_video->IsUndefined()) {
        if (!v_video->IsObject()) {
            throw_type_error(isolate, prefix,
                "The provided value is not of type 'VideoConfiguration'.");
            return reject_from_try_catch(isolate, context, caught);
        }
        has_video = true;
        auto v_obj = v_video.As<v8::Object>();
        for (const char* req : {"bitrate", "contentType", "framerate", "height", "width"}) {
            v8::Local<v8::Value> fv;
            if (!user_get(isolate, context, v_obj, v8_string(isolate, req)).ToLocal(&fv)) {
                return reject_from_try_catch(isolate, context, caught);
            }
            if (fv->IsUndefined()) {
                throw_type_error(isolate, prefix,
                    std::string("Failed to read the '") + req +
                    "' property from 'VideoConfiguration': Required member is undefined.");
                return reject_from_try_catch(isolate, context, caught);
            }
            if (std::string_view(req) == "contentType") {
                if (!web_idl_dom_string(isolate, context, fv, prefix, &video_type)) {
                    return reject_from_try_catch(isolate, context, caught);
                }
            } else {
                v8::Local<v8::Number> n;
                if (!access_user_operation(isolate, [&] { return fv->ToNumber(context); }).ToLocal(&n)) {
                    return reject_from_try_catch(isolate, context, caught);
                }
                if (!std::isfinite(n->Value()) || n->Value() <= 0.0) {
                    throw_type_error(isolate, prefix,
                        "The video configuration dictionary is not valid.");
                    return reject_from_try_catch(isolate, context, caught);
                }
            }
        }
    }
    if (!has_audio && !has_video) {
        throw_type_error(isolate, prefix,
            "The configuration must specify either an audio or video configuration.");
        return reject_from_try_catch(isolate, context, caught);
    }
    if (has_audio && !valid_mime_prefix(audio_type, "audio/")) {
        throw_type_error(isolate, prefix,
            "The audio configuration dictionary is not valid.");
        return reject_from_try_catch(isolate, context, caught);
    }
    if (has_video && !valid_mime_prefix(video_type, "video/")) {
        throw_type_error(isolate, prefix,
            "The video configuration dictionary is not valid.");
        return reject_from_try_catch(isolate, context, caught);
    }
    bool supported = true;
    if (has_audio && !supported_media_capability_content_type(audio_type, false, !is_decode)) {
        supported = false;
    }
    if (has_video && !supported_media_capability_content_type(video_type, true, !is_decode)) {
        supported = false;
    }
    const bool smooth = supported;
    const bool power_efficient = supported && !has_video;
    auto info = v8::Object::New(isolate);
    if (is_decode) {
        (void)info->CreateDataProperty(context,
            v8_string(isolate, "keySystemAccess").As<v8::String>(), v8::Null(isolate));
    }
    (void)info->CreateDataProperty(context,
        v8_string(isolate, "powerEfficient").As<v8::String>(),
        v8::Boolean::New(isolate, power_efficient));
    (void)info->CreateDataProperty(context,
        v8_string(isolate, "smooth").As<v8::String>(),
        v8::Boolean::New(isolate, smooth));
    (void)info->CreateDataProperty(context,
        v8_string(isolate, "supported").As<v8::String>(),
        v8::Boolean::New(isolate, supported));
    if (is_decode && has_video && supported) {
        v8::Local<v8::Promise::Resolver> outer;
        v8::Local<v8::Promise::Resolver> hop;
        if (!v8::Promise::Resolver::New(context).ToLocal(&outer) ||
            !v8::Promise::Resolver::New(context).ToLocal(&hop) ||
            !hop->Resolve(context, v8::Undefined(isolate)).FromMaybe(false)) {
            return v8::Undefined(isolate);
        }
        auto data = v8::Array::New(isolate, 2);
        (void)data->CreateDataProperty(context, 0, outer);
        (void)data->CreateDataProperty(context, 1, info);
        v8::Local<v8::Function> next_fn;
        if (v8::Function::New(context, &forward_microtask_resolve, data, 0,
                v8::ConstructorBehavior::kThrow).ToLocal(&next_fn)) {
            (void)hop->GetPromise()->Then(context, next_fn);
        }
        return outer->GetPromise();
    }
    return resolved_promise(isolate, context, info);
}

// --- Interface installation ---

bool install_navigator_bom05_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner, bool secure_context,
    const std::string& base_url) {
    GeolocationHost geo(owner, secure_context);
    GeolocationPositionDeclHost geo_pos;
    GeolocationCoordinatesDeclHost geo_coords;
    MediaSessionHost media_session;
    MediaMetadataHost media_metadata(base_url, "", "", "", {});
    MediaCapabilitiesHost media_caps(owner, secure_context);

    for (HostObject* host : {static_cast<HostObject*>(&geo),
                             static_cast<HostObject*>(&geo_pos),
                             static_cast<HostObject*>(&geo_coords),
                             static_cast<HostObject*>(&media_session),
                             static_cast<HostObject*>(&media_metadata),
                             static_cast<HostObject*>(&media_caps)}) {
        auto proto = web_interface_prototype(isolate, context, host->prototype_key());
        if (proto.IsEmpty()) return false;
        install_host_interface(isolate, context, host);
        if (!move_constructor_to_end(isolate, context, proto, host->prototype_key())) {
            return false;
        }
    }

    // GeolocationPositionError constants on constructor and prototype.
    auto geo_err_proto = web_interface_prototype(
        isolate, context, "GeolocationPositionError");
    if (geo_err_proto.IsEmpty()) return false;
    auto ctor_key = v8::String::NewFromUtf8Literal(isolate, "constructor");
    v8::Local<v8::Value> geo_err_ctor_val;
    if (!geo_err_proto->Get(context, ctor_key).ToLocal(&geo_err_ctor_val) ||
        !geo_err_ctor_val->IsObject() ||
        !geo_err_proto->Delete(context, ctor_key).FromMaybe(false)) {
        return false;
    }
    auto geo_err_ctor = geo_err_ctor_val.As<v8::Object>();
    GeolocationPositionErrorHost geo_err_decl(1, "");
    install_host_interface(isolate, context, &geo_err_decl);
    const auto const_attrs = static_cast<v8::PropertyAttribute>(
        v8::ReadOnly | v8::DontDelete);
    for (const auto& [member, val] : std::initializer_list<std::pair<const char*, int>>{
            {"PERMISSION_DENIED", 1}, {"POSITION_UNAVAILABLE", 2}, {"TIMEOUT", 3}}) {
        if (!install_traced_api_data_property(isolate, context, geo_err_ctor, member,
                v8::Integer::New(isolate, val), native_api_identity(isolate, geo_err_ctor),
                "host_object", "GeolocationPositionError", const_attrs) ||
            !install_traced_api_data_property(isolate, context, geo_err_proto, member,
                v8::Integer::New(isolate, val), native_api_identity(isolate, geo_err_proto),
                "host_object", "GeolocationPositionError.prototype", const_attrs)) {
            return false;
        }
    }
    if (!install_traced_api_data_property(isolate, context, geo_err_proto,
            "constructor", geo_err_ctor, native_api_identity(isolate, geo_err_proto),
            "host_object", "GeolocationPositionError.prototype", v8::DontEnum)) {
        return false;
    }

    if (!secure_context) {
        return true;
    }

    ClipboardHost clipboard(owner);
    ClipboardItemHost clipboard_item(owner, {}, {});
    CredentialsContainerHost credentials(owner);
    CredentialDeclHost credential;
    LockManagerHost locks(owner, base_url);
    LockHost lock("", "exclusive");
    WakeLockHost wake_lock(owner);
    WakeLockSentinelHost wake_sentinel("screen");
    KeyboardHost keyboard(owner);
    KeyboardLayoutMapHost kb_map;

    for (HostObject* host : {static_cast<HostObject*>(&clipboard),
                             static_cast<HostObject*>(&clipboard_item),
                             static_cast<HostObject*>(&credentials),
                             static_cast<HostObject*>(&credential),
                             static_cast<HostObject*>(&locks),
                             static_cast<HostObject*>(&lock),
                             static_cast<HostObject*>(&wake_lock),
                             static_cast<HostObject*>(&wake_sentinel),
                             static_cast<HostObject*>(&keyboard),
                             static_cast<HostObject*>(&kb_map)}) {
        auto proto = web_interface_prototype(isolate, context, host->prototype_key());
        if (proto.IsEmpty()) return false;
        install_host_interface(isolate, context, host);
        if (!move_constructor_to_end(isolate, context, proto, host->prototype_key())) {
            return false;
        }
    }

    auto kb_proto = web_interface_prototype(isolate, context, "KeyboardLayoutMap");
    v8::Local<v8::Value> entries_fn;
    if (kb_proto.IsEmpty() ||
        !kb_proto->Get(context, v8_string(isolate, "entries")).ToLocal(&entries_fn) ||
        !install_traced_api_named_data_property(isolate, context, kb_proto,
            v8::Symbol::GetIterator(isolate), entries_fn,
            native_api_identity(isolate, kb_proto), "host_object",
            "KeyboardLayoutMap.prototype", v8::DontEnum)) {
        return false;
    }

    auto ci_proto = web_interface_prototype(isolate, context, "ClipboardItem");
    v8::Local<v8::Value> ci_ctor_val;
    v8::Local<v8::Function> supports_fn;
    if (ci_proto.IsEmpty() ||
        !ci_proto->Get(context, v8::String::NewFromUtf8Literal(isolate, "constructor"))
            .ToLocal(&ci_ctor_val) || !ci_ctor_val->IsObject() ||
        !make_web_method(isolate, context, "supports", 1, &clipboard_item_supports)
            .ToLocal(&supports_fn) ||
        !install_traced_api_data_property(isolate, context, ci_ctor_val.As<v8::Object>(),
            "supports", supports_fn, native_api_identity(isolate, ci_ctor_val.As<v8::Object>()),
            "host_object", "ClipboardItem")) {
        return false;
    }

    auto cred_proto = web_interface_prototype(isolate, context, "Credential");
    v8::Local<v8::Value> cred_ctor_val;
    v8::Local<v8::Function> cond_fn;
    if (cred_proto.IsEmpty() ||
        !cred_proto->Get(context, v8::String::NewFromUtf8Literal(isolate, "constructor"))
            .ToLocal(&cred_ctor_val) || !cred_ctor_val->IsObject() ||
        !make_web_method(isolate, context, "isConditionalMediationAvailable", 0,
            &credential_is_conditional_mediation_available).ToLocal(&cond_fn) ||
        !install_traced_api_data_property(isolate, context, cred_ctor_val.As<v8::Object>(),
            "isConditionalMediationAvailable", cond_fn,
            native_api_identity(isolate, cred_ctor_val.As<v8::Object>()),
            "host_object", "Credential")) {
        return false;
    }
    return true;
}

}  // namespace iv8::browser
