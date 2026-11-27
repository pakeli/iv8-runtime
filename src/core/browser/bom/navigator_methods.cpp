#include "navigator_methods.h"

#include <algorithm>
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
#include "iv8/dom_url.h"
#include "iv8/page_state.h"
#include "iv8/web_interface.h"
#include "../internal/common.h"
#include "../internal/trace_hooks.h"
#include "../internal/web_idl_primitive.h"
#include "../window/window_trace_support.h"
#include "../web/blob_file_reader.h"
#include "../web/dom_exception.h"
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

bool throw_dom_exception(v8::Isolate* isolate, v8::Local<v8::Context> context,
    PageState* owner, std::string_view name, const std::string& message) {
    auto exception = make_dom_exception(isolate, context, owner, name, message);
    if (!exception.IsEmpty()) {
        isolate->ThrowException(exception);
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

bool is_buffer_source(v8::Local<v8::Value> value) {
    return value->IsArrayBuffer() || value->IsArrayBufferView();
}

std::size_t buffer_source_byte_length(v8::Local<v8::Value> value) {
    if (value->IsArrayBuffer()) {
        return value.As<v8::ArrayBuffer>()->ByteLength();
    }
    if (value->IsArrayBufferView()) {
        return value.As<v8::ArrayBufferView>()->ByteLength();
    }
    return 0;
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
        return throw_type_error(isolate, prefix,
            "@@iterator must be a callable.");
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
        // terminal step (verified against Chrome in canShare and EME probes).
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

struct ShareData {
    bool has_files = false;
    std::size_t file_count = 0;
    bool has_text = false;
    std::string text;
    bool has_title = false;
    std::string title;
    bool has_url = false;
    std::string url;
};

bool convert_share_data(v8::Isolate* isolate, v8::Local<v8::Context> context,
    const v8::FunctionCallbackInfo<v8::Value>& arguments,
    const std::string& prefix, ShareData* output) {
    *output = ShareData{};
    if (arguments.Length() == 0 || arguments[0]->IsNullOrUndefined()) {
        return true;
    }
    if (!arguments[0]->IsObject()) {
        return throw_type_error(isolate, prefix,
            "The provided value is not of type 'ShareData'.");
    }
    auto dict = arguments[0].As<v8::Object>();
    v8::Local<v8::Value> files;
    if (!user_get(isolate, context, dict, v8_string(isolate, "files"))
            .ToLocal(&files)) {
        return false;
    }
    if (!files->IsUndefined()) {
        output->has_files = true;
        const std::string files_prefix =
            prefix + "Failed to read the 'files' property from 'ShareData': ";
        if (!convert_sequence(isolate, context, files, files_prefix,
                [&](v8::Local<v8::Value> item) {
                    if (!is_file_object(isolate, context, item)) {
                        return throw_type_error(isolate, files_prefix,
                            "Failed to convert value to 'File'.");
                    }
                    ++output->file_count;
                    return true;
                })) {
            return false;
        }
    }
    for (const auto& [field, has_flag, target] :
        std::initializer_list<std::tuple<const char*, bool*, std::string*>>{
            {"text", &output->has_text, &output->text},
            {"title", &output->has_title, &output->title},
            {"url", &output->has_url, &output->url}}) {
        v8::Local<v8::Value> value;
        if (!user_get(isolate, context, dict, v8_string(isolate, field))
                .ToLocal(&value)) {
            return false;
        }
        if (!value->IsUndefined()) {
            *has_flag = true;
            const std::string field_prefix =
                prefix + "Failed to read the '" + field + "' property from 'ShareData': ";
            if (!web_idl_usv_string(isolate, context, value, field_prefix, target)) {
                return false;
            }
        }
    }
    return true;
}

v8::Local<v8::Value> navigator_can_share(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& base_url,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    const std::string prefix = "Failed to execute 'canShare' on 'Navigator': ";
    ShareData data;
    if (!convert_share_data(isolate, context, arguments, prefix, &data)) {
        return v8::Undefined(isolate);
    }
    const bool any_shareable = data.has_title || data.has_text || data.has_url ||
        (data.has_files && data.file_count > 0);
    if (!any_shareable) {
        return v8::False(isolate);
    }
    if (data.has_url) {
        const DomUrl resolved = resolve_dom_url(data.url, base_url);
        if (!resolved.valid ||
            (resolved.protocol != "http:" && resolved.protocol != "https:")) {
            return v8::False(isolate);
        }
    }
    return v8::True(isolate);
}

v8::Local<v8::Value> navigator_share(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    const std::string prefix = "Failed to execute 'share' on 'Navigator': ";
    v8::TryCatch caught(isolate);
    ShareData data;
    if (!convert_share_data(isolate, context, arguments, prefix, &data)) {
        return reject_from_try_catch(isolate, context, caught);
    }
    auto error = make_dom_exception(isolate, context, owner, "NotAllowedError",
        prefix + "Must be handling a user gesture to perform a share request.");
    return rejected_promise(isolate, context, error);
}

v8::Local<v8::Value> navigator_set_app_badge(v8::Isolate* isolate,
    v8::Local<v8::Context> context,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    if (arguments.Length() == 0 || arguments[0]->IsUndefined()) {
        return resolved_promise(isolate, context, v8::Undefined(isolate));
    }
    const std::string prefix = "Failed to execute 'setAppBadge' on 'Navigator': ";
    v8::TryCatch caught(isolate);
    std::uint64_t contents = 0;
    if (!web_idl_enforce_range_ull(isolate, context, arguments[0], prefix, &contents)) {
        return reject_from_try_catch(isolate, context, caught);
    }
    (void)contents;
    return resolved_promise(isolate, context, v8::Undefined(isolate));
}

bool is_rfc3986_scheme(const std::string& scheme) {
    if (scheme.empty()) return false;
    const unsigned char first = static_cast<unsigned char>(scheme[0]);
    if (!((first >= 'a' && first <= 'z') || (first >= 'A' && first <= 'Z'))) {
        return false;
    }
    for (std::size_t index = 1; index < scheme.size(); ++index) {
        const unsigned char ch = static_cast<unsigned char>(scheme[index]);
        const bool ok = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
            (ch >= '0' && ch <= '9') || ch == '+' || ch == '-' || ch == '.';
        if (!ok) return false;
    }
    return true;
}

bool is_safelisted_protocol_scheme(const std::string& lower_scheme) {
    static const std::unordered_set<std::string_view> safelist = {
        "bitcoin", "cabal", "dat", "did", "doi", "dweb", "ethereum",
        "ftp", "ftps", "geo", "hyper", "im", "ipfs", "ipns", "irc",
        "ircs", "magnet", "mailto", "matrix", "mms", "news", "nntp",
        "openpgp4fpr", "sftp", "sip", "sms", "smsto", "ssb", "ssh",
        "tel", "urn", "webcal", "wtai", "xmpp",
    };
    return safelist.contains(lower_scheme);
}

bool is_loopback_hostname(const std::string& hostname) {
    if (hostname == "localhost" || hostname == "[::1]" ||
        hostname.ends_with(".localhost")) {
        return true;
    }
    if (!hostname.starts_with("127.")) return false;
    int dots = 0;
    int value = 0;
    int digits = 0;
    for (std::size_t index = 4; index < hostname.size(); ++index) {
        const char ch = hostname[index];
        if (ch == '.') {
            if (digits == 0 || value > 255) return false;
            ++dots;
            value = 0;
            digits = 0;
        } else if (ch >= '0' && ch <= '9') {
            value = value * 10 + (ch - '0');
            ++digits;
            if (digits > 3 || value > 255) return false;
        } else {
            return false;
        }
    }
    return dots == 2 && digits > 0 && value <= 255;
}

v8::Local<v8::Value> navigator_protocol_handler(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    const std::string& base_url, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    const std::string prefix = "Failed to execute '" + name + "' on 'Navigator': ";
    if (arguments.Length() < 2) {
        throw_type_error(isolate, prefix, "2 arguments required, but only " +
            std::to_string(arguments.Length()) + " present.");
        return v8::Undefined(isolate);
    }
    std::string raw_scheme;
    std::string raw_url;
    if (!web_idl_dom_string(isolate, context, arguments[0], prefix, &raw_scheme) ||
        !web_idl_usv_string(isolate, context, arguments[1], prefix, &raw_url)) {
        return v8::Undefined(isolate);
    }
    if (!is_rfc3986_scheme(raw_scheme)) {
        throw_dom_exception(isolate, context, owner, "SecurityError",
            prefix + "The scheme name '" + raw_scheme +
            "' is not allowed by URI syntax (RFC3986).");
        return v8::Undefined(isolate);
    }
    const std::string lower_scheme = ascii_lower(raw_scheme);
    if (lower_scheme.starts_with("web+")) {
        bool valid_suffix = lower_scheme.size() > 4;
        for (std::size_t index = 4; valid_suffix && index < lower_scheme.size(); ++index) {
            const char ch = lower_scheme[index];
            if (ch < 'a' || ch > 'z') valid_suffix = false;
        }
        if (!valid_suffix) {
            throw_dom_exception(isolate, context, owner, "SecurityError",
                prefix + "The scheme name '" + lower_scheme +
                "' is not allowed. Schemes starting with '" + lower_scheme +
                "' must be followed by one or more ASCII letters.");
            return v8::Undefined(isolate);
        }
    } else if (!is_safelisted_protocol_scheme(lower_scheme)) {
        throw_dom_exception(isolate, context, owner, "SecurityError",
            prefix + "The scheme '" + lower_scheme +
            "' doesn't belong to the scheme allowlist. Please prefix "
            "non-allowlisted schemes with the string 'web+'.");
        return v8::Undefined(isolate);
    }
    const auto token_pos = raw_url.find("%s");
    if (token_pos == std::string::npos) {
        throw_dom_exception(isolate, context, owner, "SyntaxError",
            prefix + "The url provided ('" + raw_url + "') does not contain '%s'.");
        return v8::Undefined(isolate);
    }
    const std::string stripped =
        raw_url.substr(0, token_pos) + raw_url.substr(token_pos + 2);
    const DomUrl resolved = resolve_dom_url(stripped, base_url);
    if (!resolved.valid) {
        throw_dom_exception(isolate, context, owner, "SyntaxError",
            prefix + "The custom handler URL created by removing '%s' and prepending '" +
            base_url + "' is invalid.");
        return v8::Undefined(isolate);
    }
    const bool trustworthy_http = resolved.protocol == "https:" ||
        (resolved.protocol == "http:" && is_loopback_hostname(resolved.hostname));
    if (!trustworthy_http) {
        throw_dom_exception(isolate, context, owner, "SecurityError",
            prefix + "The scheme of the url provided must be HTTP(S).");
        return v8::Undefined(isolate);
    }
    const DomUrl doc_url = resolve_dom_url(base_url, "");
    if (!doc_url.valid || doc_url.origin == "null" ||
        resolved.origin == "null" || resolved.origin != doc_url.origin) {
        throw_dom_exception(isolate, context, owner, "SecurityError",
            prefix + "Can only register custom handler in the document's origin.");
        return v8::Undefined(isolate);
    }
    return v8::Undefined(isolate);
}

void deliver_user_media_error(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> callback;
    v8::Local<v8::Value> owner_ext;
    if (!data->Get(context, 0).ToLocal(&callback) ||
        !data->Get(context, 1).ToLocal(&owner_ext) ||
        !owner_ext->IsExternal()) {
        return;
    }
    auto* owner = static_cast<PageState*>(
        owner_ext.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    auto error = make_dom_exception(isolate, context, owner,
        "NotAllowedError", "Permission denied");
    if (error.IsEmpty()) return;
    v8::TryCatch caught(isolate);
    v8::Local<v8::Value> values[] = {error};
    (void)access_user_operation(isolate, [&] {
        return callback.As<v8::Object>()->CallAsFunction(
            context, v8::Undefined(isolate), 1, values);
    });
    if (caught.HasCaught()) {
        if (!caught.CanContinue()) { caught.ReThrow(); return; }
        if (auto* state = state_from_isolate(isolate)) {
            state->record_async_error(isolate, context, caught, "user_media");
        }
        caught.Reset();
    }
}

bool convert_media_track_constraints(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> track,
    const std::string& prefix) {
    static constexpr const char* kTrackKeys[] = {
        "aspectRatio", "autoGainControl", "brightness", "channelCount",
        "colorTemperature", "contrast", "deviceId", "displaySurface",
        "echoCancellation", "exposureCompensation", "exposureMode",
        "exposureTime", "facingMode", "focusDistance", "focusMode",
        "frameRate", "groupId", "height", "iso", "latency", "mandatory",
        "noiseSuppression", "optional", "pan", "pointsOfInterest",
        "resizeMode", "restrictOwnAudio", "sampleRate", "sampleSize",
        "saturation", "sharpness", "suppressLocalAudioPlayback", "tilt",
        "torch", "voiceIsolation", "whiteBalanceMode", "width", "zoom",
        "advanced",
    };
    for (const char* key : kTrackKeys) {
        v8::Local<v8::Value> field;
        if (!user_get(isolate, context, track, v8_string(isolate, key))
                .ToLocal(&field)) {
            return false;
        }
        if (std::string_view(key) == "advanced" && !field->IsUndefined()) {
            if (!convert_sequence(isolate, context, field, prefix,
                    [&](v8::Local<v8::Value> item) {
                        if (!item->IsNullOrUndefined() && !item->IsObject()) {
                            return throw_type_error(isolate, prefix,
                                "The provided value is not of type 'MediaTrackConstraintSet'.");
                        }
                        return true;
                    })) {
                return false;
            }
        }
    }
    return true;
}

v8::Local<v8::Value> navigator_get_user_media(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    const std::string prefix = "Failed to execute '" + name + "' on 'Navigator': ";
    if (arguments.Length() < 3) {
        throw_type_error(isolate, prefix, "3 arguments required, but only " +
            std::to_string(arguments.Length()) + " present.");
        return v8::Undefined(isolate);
    }
    bool audio_requested = false;
    bool video_requested = false;
    if (!arguments[0]->IsNullOrUndefined()) {
        if (!arguments[0]->IsObject()) {
            throw_type_error(isolate, prefix,
                "The provided value is not of type 'MediaStreamConstraints'.");
            return v8::Undefined(isolate);
        }
        auto constraints = arguments[0].As<v8::Object>();
        v8::Local<v8::Value> audio;
        v8::Local<v8::Value> video;
        if (!user_get(isolate, context, constraints, v8_string(isolate, "audio"))
                .ToLocal(&audio)) {
            return v8::Undefined(isolate);
        }
        if (!audio->IsUndefined()) {
            if (audio->IsObject()) {
                if (!convert_media_track_constraints(
                        isolate, context, audio.As<v8::Object>(), prefix)) {
                    return v8::Undefined(isolate);
                }
                audio_requested = true;
            } else {
                audio_requested = audio->BooleanValue(isolate);
            }
        }
        if (!user_get(isolate, context, constraints, v8_string(isolate, "video"))
                .ToLocal(&video)) {
            return v8::Undefined(isolate);
        }
        if (!video->IsUndefined()) {
            if (video->IsObject()) {
                if (!convert_media_track_constraints(
                        isolate, context, video.As<v8::Object>(), prefix)) {
                    return v8::Undefined(isolate);
                }
                video_requested = true;
            } else {
                video_requested = video->BooleanValue(isolate);
            }
        }
    }
    if (!is_callable_object(arguments[1])) {
        throw_type_error(isolate, prefix, "parameter 2 is not of type 'Function'.");
        return v8::Undefined(isolate);
    }
    if (!is_callable_object(arguments[2])) {
        throw_type_error(isolate, prefix, "parameter 3 is not of type 'Function'.");
        return v8::Undefined(isolate);
    }
    if (!audio_requested && !video_requested) {
        throw_type_error(isolate, prefix,
            "At least one of audio and video must be requested");
        return v8::Undefined(isolate);
    }
    auto* state = state_from_isolate(isolate);
    if (state == nullptr) return v8::Undefined(isolate);
    auto data = v8::Array::New(isolate, 2);
    if (!data->CreateDataProperty(context, 0, arguments[2]).FromMaybe(false) ||
        !data->CreateDataProperty(context, 1, v8::External::New(
            isolate, owner, v8::kExternalPointerTypeTagDefault)).FromMaybe(false)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Function> delivery;
    if (!v8::Function::New(context, &deliver_user_media_error, data, 0,
            v8::ConstructorBehavior::kThrow).ToLocal(&delivery)) {
        return v8::Undefined(isolate);
    }
    (void)state->register_timer(delivery, 0, false, false);
    return v8::Undefined(isolate);
}

void deliver_deferred_dom_rejection(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> resolver_val;
    v8::Local<v8::Value> owner_ext;
    v8::Local<v8::Value> name_val;
    v8::Local<v8::Value> message_val;
    if (!data->Get(context, 0).ToLocal(&resolver_val) ||
        !data->Get(context, 1).ToLocal(&owner_ext) ||
        !data->Get(context, 2).ToLocal(&name_val) ||
        !data->Get(context, 3).ToLocal(&message_val) ||
        !owner_ext->IsExternal()) {
        return;
    }
    auto* owner = static_cast<PageState*>(
        owner_ext.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    v8::String::Utf8Value name_utf8(isolate, name_val);
    v8::String::Utf8Value message_utf8(isolate, message_val);
    auto error = make_dom_exception(isolate, context, owner,
        *name_utf8 ? *name_utf8 : "Error",
        *message_utf8 ? *message_utf8 : "");
    if (error.IsEmpty()) return;
    (void)access_user_operation(isolate, [&] {
        return resolver_val.As<v8::Promise::Resolver>()->Reject(context, error);
    });
}

v8::Local<v8::Value> schedule_deferred_dom_rejection(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    std::string_view error_name, std::string_view error_message) {
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        return v8::Undefined(isolate);
    }
    auto* state = state_from_isolate(isolate);
    if (state == nullptr) return v8::Undefined(isolate);
    auto data = v8::Array::New(isolate, 4);
    if (!data->CreateDataProperty(context, 0, resolver).FromMaybe(false) ||
        !data->CreateDataProperty(context, 1, v8::External::New(
            isolate, owner, v8::kExternalPointerTypeTagDefault)).FromMaybe(false) ||
        !data->CreateDataProperty(context, 2,
            v8_string(isolate, std::string(error_name))).FromMaybe(false) ||
        !data->CreateDataProperty(context, 3,
            v8_string(isolate, std::string(error_message))).FromMaybe(false)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Function> delivery;
    if (!v8::Function::New(context, &deliver_deferred_dom_rejection, data, 0,
            v8::ConstructorBehavior::kThrow).ToLocal(&delivery)) {
        return v8::Undefined(isolate);
    }
    (void)state->register_timer(delivery, 0, false, false);
    return resolver->GetPromise();
}

v8::Local<v8::Value> navigator_request_midi_access(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    const std::string prefix = "Failed to execute 'requestMIDIAccess' on 'Navigator': ";
    v8::TryCatch caught(isolate);
    if (arguments.Length() > 0 && !arguments[0]->IsNullOrUndefined()) {
        if (!arguments[0]->IsObject()) {
            throw_type_error(isolate, prefix,
                "The provided value is not of type 'MIDIOptions'.");
            return reject_from_try_catch(isolate, context, caught);
        }
        v8::Local<v8::Value> sysex;
        if (!user_get(isolate, context, arguments[0].As<v8::Object>(),
                v8_string(isolate, "sysex")).ToLocal(&sysex)) {
            return reject_from_try_catch(isolate, context, caught);
        }
        (void)sysex->BooleanValue(isolate);
    }
    return schedule_deferred_dom_rejection(isolate, context, owner,
        "NotAllowedError", "Permission to use Web MIDI API was not granted.");
}

struct ParsedMediaConfiguration {
    bool has_audio = false;
    std::vector<ClearKeyMediaCapability> audio_capabilities;
    std::string distinctive_identifier = "optional";
    bool has_init_data_types = false;
    std::vector<std::string> init_data_types;
    std::string label;
    std::string persistent_state = "optional";
    bool has_session_types = false;
    std::vector<std::string> session_types;
    bool has_video = false;
    std::vector<ClearKeyMediaCapability> video_capabilities;
};

bool convert_media_capability(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, const std::string& prefix,
    ClearKeyMediaCapability* output) {
    *output = ClearKeyMediaCapability{};
    if (input->IsNullOrUndefined()) {
        return true;
    }
    if (!input->IsObject()) {
        return throw_type_error(isolate, prefix,
            "The provided value is not of type 'MediaKeySystemMediaCapability'.");
    }
    auto dict = input.As<v8::Object>();
    v8::Local<v8::Value> content_type;
    if (!user_get(isolate, context, dict, v8_string(isolate, "contentType"))
            .ToLocal(&content_type)) {
        return false;
    }
    if (!content_type->IsUndefined() &&
        !web_idl_dom_string(isolate, context, content_type, prefix, &output->content_type)) {
        return false;
    }
    v8::Local<v8::Value> scheme;
    if (!user_get(isolate, context, dict, v8_string(isolate, "encryptionScheme"))
            .ToLocal(&scheme)) {
        return false;
    }
    if (!scheme->IsNullOrUndefined()) {
        output->has_encryption_scheme = true;
        if (!web_idl_dom_string(isolate, context, scheme, prefix, &output->encryption_scheme)) {
            return false;
        }
    }
    v8::Local<v8::Value> robustness;
    if (!user_get(isolate, context, dict, v8_string(isolate, "robustness"))
            .ToLocal(&robustness)) {
        return false;
    }
    if (!robustness->IsUndefined() &&
        !web_idl_dom_string(isolate, context, robustness, prefix, &output->robustness)) {
        return false;
    }
    return true;
}

bool convert_requirement_enum(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> value, const std::string& prefix, std::string* output) {
    if (value->IsUndefined()) {
        *output = "optional";
        return true;
    }
    if (!web_idl_dom_string(isolate, context, value, prefix, output)) {
        return false;
    }
    if (*output != "required" && *output != "optional" && *output != "not-allowed") {
        return throw_type_error(isolate, prefix,
            "The provided value '" + *output +
            "' is not a valid enum value of type MediaKeysRequirement.");
    }
    return true;
}

bool convert_media_configuration(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, const std::string& prefix,
    ParsedMediaConfiguration* output) {
    *output = ParsedMediaConfiguration{};
    if (input->IsNullOrUndefined()) {
        return true;
    }
    if (!input->IsObject()) {
        return throw_type_error(isolate, prefix,
            "The provided value is not of type 'MediaKeySystemConfiguration'.");
    }
    auto dict = input.As<v8::Object>();
    v8::Local<v8::Value> audio;
    if (!user_get(isolate, context, dict, v8_string(isolate, "audioCapabilities"))
            .ToLocal(&audio)) {
        return false;
    }
    if (!audio->IsUndefined()) {
        output->has_audio = true;
        if (!convert_sequence(isolate, context, audio, prefix,
                [&](v8::Local<v8::Value> item) {
                    ClearKeyMediaCapability cap;
                    if (!convert_media_capability(isolate, context, item, prefix, &cap)) {
                        return false;
                    }
                    output->audio_capabilities.push_back(std::move(cap));
                    return true;
                })) {
            return false;
        }
    }
    v8::Local<v8::Value> distinctive;
    if (!user_get(isolate, context, dict, v8_string(isolate, "distinctiveIdentifier"))
            .ToLocal(&distinctive) ||
        !convert_requirement_enum(isolate, context, distinctive, prefix,
            &output->distinctive_identifier)) {
        return false;
    }
    v8::Local<v8::Value> init_types;
    if (!user_get(isolate, context, dict, v8_string(isolate, "initDataTypes"))
            .ToLocal(&init_types)) {
        return false;
    }
    if (!init_types->IsUndefined()) {
        output->has_init_data_types = true;
        if (!convert_sequence(isolate, context, init_types, prefix,
                [&](v8::Local<v8::Value> item) {
                    std::string text;
                    if (!web_idl_dom_string(isolate, context, item, prefix, &text)) {
                        return false;
                    }
                    output->init_data_types.push_back(std::move(text));
                    return true;
                })) {
            return false;
        }
    }
    v8::Local<v8::Value> label;
    if (!user_get(isolate, context, dict, v8_string(isolate, "label"))
            .ToLocal(&label)) {
        return false;
    }
    if (!label->IsUndefined() &&
        !web_idl_dom_string(isolate, context, label, prefix, &output->label)) {
        return false;
    }
    v8::Local<v8::Value> persistent;
    if (!user_get(isolate, context, dict, v8_string(isolate, "persistentState"))
            .ToLocal(&persistent) ||
        !convert_requirement_enum(isolate, context, persistent, prefix,
            &output->persistent_state)) {
        return false;
    }
    v8::Local<v8::Value> sessions;
    if (!user_get(isolate, context, dict, v8_string(isolate, "sessionTypes"))
            .ToLocal(&sessions)) {
        return false;
    }
    if (!sessions->IsUndefined()) {
        output->has_session_types = true;
        if (!convert_sequence(isolate, context, sessions, prefix,
                [&](v8::Local<v8::Value> item) {
                    std::string text;
                    if (!web_idl_dom_string(isolate, context, item, prefix, &text)) {
                        return false;
                    }
                    output->session_types.push_back(std::move(text));
                    return true;
                })) {
            return false;
        }
    }
    v8::Local<v8::Value> video;
    if (!user_get(isolate, context, dict, v8_string(isolate, "videoCapabilities"))
            .ToLocal(&video)) {
        return false;
    }
    if (!video->IsUndefined()) {
        output->has_video = true;
        if (!convert_sequence(isolate, context, video, prefix,
                [&](v8::Local<v8::Value> item) {
                    ClearKeyMediaCapability cap;
                    if (!convert_media_capability(isolate, context, item, prefix, &cap)) {
                        return false;
                    }
                    output->video_capabilities.push_back(std::move(cap));
                    return true;
                })) {
            return false;
        }
    }
    return true;
}

std::string trim_ascii(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.remove_suffix(1);
    }
    return std::string(text);
}

bool parse_mime_and_codecs(const std::string& content_type,
    std::string* mime, std::vector<std::string>* codecs) {
    const auto semi = content_type.find(';');
    if (semi == std::string::npos) return false;
    *mime = ascii_lower(trim_ascii(std::string_view(content_type).substr(0, semi)));
    std::string rest = trim_ascii(std::string_view(content_type).substr(semi + 1));
    if (!ascii_lower(rest).starts_with("codecs=")) return false;
    rest = trim_ascii(std::string_view(rest).substr(7));
    if (rest.size() >= 2 && rest.front() == '"' && rest.back() == '"') {
        rest = rest.substr(1, rest.size() - 2);
    }
    codecs->clear();
    std::size_t start = 0;
    while (start <= rest.size()) {
        auto comma = rest.find(',', start);
        if (comma == std::string::npos) comma = rest.size();
        auto codec = trim_ascii(std::string_view(rest).substr(start, comma - start));
        if (!codec.empty()) codecs->push_back(std::move(codec));
        if (comma == rest.size()) break;
        start = comma + 1;
    }
    return !codecs->empty();
}

bool is_hex_string(std::string_view text) {
    if (text.empty()) return false;
    for (unsigned char ch : text) {
        if (!std::isxdigit(ch)) return false;
    }
    return true;
}

bool is_digits_string(std::string_view text) {
    if (text.empty()) return false;
    for (unsigned char ch : text) {
        if (!std::isdigit(ch)) return false;
    }
    return true;
}

bool valid_avc_codec(std::string_view codec) {
    if ((!codec.starts_with("avc1.") && !codec.starts_with("avc3.")) ||
        codec.size() != 11) {
        return false;
    }
    auto suffix = codec.substr(5);
    return is_hex_string(suffix) && suffix.substr(0, 2) != "00" &&
        suffix.substr(4, 2) != "00";
}

bool valid_vp09_codec(std::string_view codec) {
    if (!codec.starts_with("vp09.")) return false;
    std::vector<std::string_view> parts;
    std::size_t start = 5;
    while (start <= codec.size()) {
        auto dot = codec.find('.', start);
        if (dot == std::string_view::npos) dot = codec.size();
        parts.push_back(codec.substr(start, dot - start));
        if (dot == codec.size()) break;
        start = dot + 1;
    }
    if (parts.size() < 3 || parts.size() > 8) return false;
    for (auto part : parts) {
        if (part.size() != 2 || !is_digits_string(part)) return false;
    }
    return (parts[0] == "00" || parts[0] == "01" ||
            parts[0] == "02" || parts[0] == "03") &&
           (parts[2] == "08" || parts[2] == "10" || parts[2] == "12");
}

bool valid_av01_codec(std::string_view codec) {
    if (!codec.starts_with("av01.")) return false;
    std::vector<std::string_view> parts;
    std::size_t start = 5;
    while (start <= codec.size()) {
        auto dot = codec.find('.', start);
        if (dot == std::string_view::npos) dot = codec.size();
        parts.push_back(codec.substr(start, dot - start));
        if (dot == codec.size()) break;
        start = dot + 1;
    }
    if (parts.size() < 3 || parts.size() > 10) return false;
    if (parts[0].size() != 1 || parts[0][0] < '0' || parts[0][0] > '2') return false;
    if (parts[1].size() != 3 || !is_digits_string(parts[1].substr(0, 2)) ||
        (parts[1][2] != 'M' && parts[1][2] != 'H')) {
        return false;
    }
    if (parts[2] != "08" && parts[2] != "10" && parts[2] != "12") return false;
    for (std::size_t index = 3; index < parts.size(); ++index) {
        if (!is_digits_string(parts[index])) return false;
    }
    return true;
}

bool valid_mp4a_codec(std::string_view codec) {
    const std::string lower = ascii_lower(std::string(codec));
    static const std::unordered_set<std::string_view> allowed = {
        "mp4a.40", "mp4a.40.2", "mp4a.40.02", "mp4a.40.5", "mp4a.40.05",
        "mp4a.40.29", "mp4a.40.42", "mp4a.66", "mp4a.67", "mp4a.68",
        "mp4a.69", "mp4a.6b",
    };
    return allowed.contains(lower);
}

bool supported_clearkey_capability(const ClearKeyMediaCapability& capability,
    bool is_video) {
    if (!capability.robustness.empty()) return false;
    if (capability.has_encryption_scheme &&
        capability.encryption_scheme != "cenc" &&
        capability.encryption_scheme != "cbcs") {
        return false;
    }
    std::string mime;
    std::vector<std::string> codecs;
    if (!parse_mime_and_codecs(capability.content_type, &mime, &codecs)) {
        return false;
    }
    if (is_video) {
        if (mime != "video/mp4" && mime != "video/webm") return false;
        for (const auto& codec : codecs) {
            const bool mp4_ok = mime == "video/mp4" &&
                (valid_avc_codec(codec) || valid_vp09_codec(codec) ||
                 valid_av01_codec(codec));
            const bool webm_ok = mime == "video/webm" &&
                (codec == "vp8" || codec == "vp9" ||
                 valid_vp09_codec(codec) || valid_av01_codec(codec));
            if (!mp4_ok && !webm_ok) return false;
        }
        return true;
    }
    if (mime != "audio/mp4" && mime != "audio/webm") return false;
    for (const auto& codec : codecs) {
        const bool mp4_ok = mime == "audio/mp4" &&
            (valid_mp4a_codec(codec) || codec == "flac" || codec == "opus");
        const bool webm_ok = mime == "audio/webm" &&
            (codec == "opus" || codec == "vorbis");
        if (!mp4_ok && !webm_ok) return false;
    }
    return true;
}

bool match_clearkey_configuration(const ParsedMediaConfiguration& candidate,
    ClearKeyConfiguration* accumulated) {
    if (candidate.distinctive_identifier == "required" ||
        candidate.persistent_state == "required") {
        return false;
    }
    ClearKeyConfiguration out;
    out.label = candidate.label;
    if (candidate.has_session_types) {
        out.session_types.clear();
        for (const auto& session_type : candidate.session_types) {
            if (session_type != "temporary") return false;
            out.session_types.push_back(session_type);
        }
        if (out.session_types.empty()) out.session_types.push_back("temporary");
    }
    if (candidate.has_init_data_types && !candidate.init_data_types.empty()) {
        for (const auto& init_type : candidate.init_data_types) {
            if (init_type == "cenc" || init_type == "keyids" || init_type == "webm") {
                out.init_data_types.push_back(init_type);
            }
        }
        if (out.init_data_types.empty()) return false;
    }
    const bool any_media = (!candidate.audio_capabilities.empty()) ||
        (!candidate.video_capabilities.empty());
    if (!any_media) return false;
    if (!candidate.audio_capabilities.empty()) {
        for (const auto& cap : candidate.audio_capabilities) {
            if (supported_clearkey_capability(cap, false)) {
                out.audio_capabilities.push_back(cap);
            }
        }
        if (out.audio_capabilities.empty()) return false;
    }
    if (!candidate.video_capabilities.empty()) {
        for (const auto& cap : candidate.video_capabilities) {
            if (supported_clearkey_capability(cap, true)) {
                out.video_capabilities.push_back(cap);
            }
        }
        if (out.video_capabilities.empty()) return false;
    }
    *accumulated = std::move(out);
    return true;
}

v8::Local<v8::Value> navigator_request_media_key_system_access(
    v8::Isolate* isolate, v8::Local<v8::Context> context, PageState* owner,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    const std::string prefix =
        "Failed to execute 'requestMediaKeySystemAccess' on 'Navigator': ";
    v8::TryCatch caught(isolate);
    if (arguments.Length() < 2) {
        throw_type_error(isolate, prefix, "2 arguments required, but only " +
            std::to_string(arguments.Length()) + " present.");
        return reject_from_try_catch(isolate, context, caught);
    }
    std::string key_system;
    if (!web_idl_dom_string(isolate, context, arguments[0], prefix, &key_system)) {
        return reject_from_try_catch(isolate, context, caught);
    }
    std::vector<ParsedMediaConfiguration> configs;
    if (!convert_sequence(isolate, context, arguments[1], prefix,
            [&](v8::Local<v8::Value> item) {
                ParsedMediaConfiguration parsed;
                if (!convert_media_configuration(isolate, context, item, prefix, &parsed)) {
                    return false;
                }
                configs.push_back(std::move(parsed));
                return true;
            })) {
        return reject_from_try_catch(isolate, context, caught);
    }
    if (key_system.empty()) {
        throw_type_error(isolate, prefix, "The keySystem parameter is empty.");
        return reject_from_try_catch(isolate, context, caught);
    }
    if (configs.empty()) {
        throw_type_error(isolate, prefix,
            "The supportedConfigurations parameter is empty.");
        return reject_from_try_catch(isolate, context, caught);
    }
    if (key_system == "org.w3.clearkey") {
        for (const auto& candidate : configs) {
            ClearKeyConfiguration matched;
            if (match_clearkey_configuration(candidate, &matched)) {
                auto access = owner->adopt_host_object(isolate, context,
                    std::make_unique<MediaKeySystemAccessHost>(
                        owner, key_system, std::move(matched)));
                return resolved_promise(isolate, context, access);
            }
        }
    }
    if (key_system == "com.widevine.alpha") {
        return schedule_deferred_dom_rejection(isolate, context, owner,
            "NotSupportedError",
            "Unsupported keySystem or supportedConfigurations.");
    }
    auto unsupported = make_dom_exception(isolate, context, owner,
        "NotSupportedError", "Unsupported keySystem or supportedConfigurations.");
    return rejected_promise(isolate, context, unsupported);
}

void deliver_media_keys(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> resolver_val;
    v8::Local<v8::Value> owner_ext;
    if (!data->Get(context, 0).ToLocal(&resolver_val) ||
        !data->Get(context, 1).ToLocal(&owner_ext) ||
        !owner_ext->IsExternal()) {
        return;
    }
    auto* owner = static_cast<PageState*>(
        owner_ext.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    auto keys = owner->adopt_host_object(isolate, context,
        std::make_unique<MediaKeysHost>(owner));
    if (keys.IsEmpty()) return;
    (void)access_user_operation(isolate, [&] {
        return resolver_val.As<v8::Promise::Resolver>()->Resolve(context, keys);
    });
}

v8::Local<v8::Array> make_capability_array(v8::Isolate* isolate,
    v8::Local<v8::Context> context,
    const std::vector<ClearKeyMediaCapability>& items) {
    auto array = v8::Array::New(isolate, static_cast<int>(items.size()));
    for (std::uint32_t index = 0; index < items.size(); ++index) {
        auto obj = v8::Object::New(isolate);
        const auto& cap = items[index];
        v8::Local<v8::Value> scheme = cap.has_encryption_scheme
            ? v8_string(isolate, cap.encryption_scheme)
            : v8::Local<v8::Value>(v8::Null(isolate));
        (void)obj->CreateDataProperty(context,
            v8_string(isolate, "contentType").As<v8::String>(),
            v8_string(isolate, cap.content_type));
        (void)obj->CreateDataProperty(context,
            v8_string(isolate, "encryptionScheme").As<v8::String>(), scheme);
        (void)obj->CreateDataProperty(context,
            v8_string(isolate, "robustness").As<v8::String>(),
            v8_string(isolate, cap.robustness));
        (void)array->CreateDataProperty(context, index, obj);
    }
    return array;
}

v8::Local<v8::Array> make_string_array(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::vector<std::string>& items) {
    auto array = v8::Array::New(isolate, static_cast<int>(items.size()));
    for (std::uint32_t index = 0; index < items.size(); ++index) {
        (void)array->CreateDataProperty(context, index,
            v8_string(isolate, items[index]));
    }
    return array;
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

}  // namespace

v8::Local<v8::Value> MediaKeyStatusMapHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name) {
    if (name == "size") {
        return v8::Integer::New(isolate, 0);
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> MediaKeyStatusMapHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    const std::string prefix =
        "Failed to execute '" + name + "' on 'MediaKeyStatusMap': ";
    if (name == "get" || name == "has") {
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
            return v8::Undefined(isolate);
        }
        if (!is_buffer_source(arguments[0])) {
            throw_type_error(isolate, prefix,
                "parameter 1 is not of type 'ArrayBuffer or ArrayBufferView'.");
            return v8::Undefined(isolate);
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
        return v8::Undefined(isolate);
    }
    if (name == "entries" || name == "keys" || name == "values") {
        auto empty = v8::Array::New(isolate, 0);
        v8::Local<v8::Value> iterator_fn;
        v8::Local<v8::Value> iterator;
        if (!empty->Get(context, v8::Symbol::GetIterator(isolate)).ToLocal(&iterator_fn) ||
            !iterator_fn->IsFunction() ||
            !iterator_fn.As<v8::Function>()->Call(context, empty, 0, nullptr)
                .ToLocal(&iterator)) {
            return v8::Undefined(isolate);
        }
        return iterator;
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> MediaKeySessionHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name) {
    if (name == "sessionId") {
        return v8::String::Empty(isolate);
    }
    if (name == "expiration") {
        return v8::Number::New(isolate, std::numeric_limits<double>::quiet_NaN());
    }
    if (name == "closed") {
        if (closed_resolver_.IsEmpty()) {
            v8::Local<v8::Promise::Resolver> resolver;
            if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
                return v8::Undefined(isolate);
            }
            closed_resolver_.Reset(isolate, resolver);
        }
        return closed_resolver_.Get(isolate)->GetPromise();
    }
    if (name == "keyStatuses") {
        if (key_statuses_object_.IsEmpty()) {
            key_statuses_object_.Reset(
                isolate, make_host_object(isolate, context, &key_statuses_));
        }
        return key_statuses_object_.Get(isolate);
    }
    if (name == "onkeystatuseschange" || name == "onmessage") {
        return get_event_handler(isolate, name);
    }
    return v8::Undefined(isolate);
}

void MediaKeySessionHost::set_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name, v8::Local<v8::Value> value) {
    if (name == "onkeystatuseschange" || name == "onmessage") {
        set_event_handler(isolate, name, value);
    }
}

v8::Local<v8::Value> MediaKeySessionHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (!validate_bom_event_dispatch(isolate, context, name, arguments)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> event_result;
    if (handle_event_method(isolate, context, name, arguments, event_result)) {
        return event_result;
    }
    const std::string prefix =
        "Failed to execute '" + name + "' on 'MediaKeySession': ";
    v8::TryCatch caught(isolate);
    if (name == "load") {
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
            return reject_from_try_catch(isolate, context, caught);
        }
        std::string session_id;
        if (!web_idl_dom_string(isolate, context, arguments[0], prefix, &session_id)) {
            return reject_from_try_catch(isolate, context, caught);
        }
        throw_type_error(isolate, prefix, "The session type is not persistent.");
        return reject_from_try_catch(isolate, context, caught);
    }
    if (name == "update") {
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix, "1 argument required, but only 0 present.");
            return reject_from_try_catch(isolate, context, caught);
        }
        if (!is_buffer_source(arguments[0])) {
            throw_type_error(isolate, prefix,
                "parameter 1 is not of type 'ArrayBuffer or ArrayBufferView'.");
            return reject_from_try_catch(isolate, context, caught);
        }
        auto error = make_dom_exception(isolate, context, owner_,
            "InvalidStateError", prefix + "The session is not callable.");
        return rejected_promise(isolate, context, error);
    }
    if (name == "generateRequest") {
        if (arguments.Length() < 2) {
            throw_type_error(isolate, prefix, "2 arguments required, but only " +
                std::to_string(arguments.Length()) + " present.");
            return reject_from_try_catch(isolate, context, caught);
        }
        std::string init_data_type;
        if (!web_idl_dom_string(isolate, context, arguments[0], prefix, &init_data_type)) {
            return reject_from_try_catch(isolate, context, caught);
        }
        if (!is_buffer_source(arguments[1])) {
            throw_type_error(isolate, prefix,
                "parameter 2 is not of type 'ArrayBuffer or ArrayBufferView'.");
            return reject_from_try_catch(isolate, context, caught);
        }
        if (buffer_source_byte_length(arguments[1]) == 0) {
            throw_type_error(isolate, prefix, "The initData parameter is empty.");
            return reject_from_try_catch(isolate, context, caught);
        }
        auto error = make_dom_exception(isolate, context, owner_,
            "NotSupportedError",
            prefix + "The initialization data type '" + init_data_type +
            "' is not supported.");
        return rejected_promise(isolate, context, error);
    }
    if (name == "close" || name == "remove") {
        auto error = make_dom_exception(isolate, context, owner_,
            "InvalidStateError", prefix + "The session is not callable.");
        return rejected_promise(isolate, context, error);
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> MediaKeysHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string&) {
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> MediaKeysHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    const std::string prefix = "Failed to execute '" + name + "' on 'MediaKeys': ";
    if (name == "createSession") {
        std::string session_type = "temporary";
        if (arguments.Length() > 0 && !arguments[0]->IsUndefined()) {
            if (!web_idl_dom_string(isolate, context, arguments[0], prefix, &session_type)) {
                return v8::Undefined(isolate);
            }
            if (session_type != "temporary" && session_type != "persistent-license") {
                throw_type_error(isolate, prefix,
                    "The provided value '" + session_type +
                    "' is not a valid enum value of type MediaKeySessionType.");
                return v8::Undefined(isolate);
            }
        }
        if (session_type != "temporary") {
            throw_dom_exception(isolate, context, owner_, "NotSupportedError",
                prefix + "Unsupported session type.");
            return v8::Undefined(isolate);
        }
        return owner_->adopt_host_object(isolate, context,
            std::make_unique<MediaKeySessionHost>(owner_));
    }
    if (name == "setServerCertificate") {
        v8::TryCatch caught(isolate);
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix,
                "1 argument required, but only 0 present.");
            return reject_from_try_catch(isolate, context, caught);
        }
        if (!is_buffer_source(arguments[0])) {
            throw_type_error(isolate, prefix,
                "parameter 1 is not of type 'ArrayBuffer or ArrayBufferView'.");
            return reject_from_try_catch(isolate, context, caught);
        }
        if (buffer_source_byte_length(arguments[0]) == 0) {
            throw_type_error(isolate, prefix,
                "The serverCertificate parameter is empty.");
            return reject_from_try_catch(isolate, context, caught);
        }
        return resolved_promise(isolate, context, v8::False(isolate));
    }
    if (name == "getStatusForPolicy") {
        v8::TryCatch caught(isolate);
        if (arguments.Length() > 0 && !arguments[0]->IsNullOrUndefined()) {
            if (!arguments[0]->IsObject()) {
                throw_type_error(isolate, prefix,
                    "The provided value is not of type 'MediaKeysPolicy'.");
                return reject_from_try_catch(isolate, context, caught);
            }
            v8::Local<v8::Value> hdcp;
            if (!user_get(isolate, context, arguments[0].As<v8::Object>(),
                    v8_string(isolate, "minHdcpVersion")).ToLocal(&hdcp)) {
                return reject_from_try_catch(isolate, context, caught);
            }
            if (!hdcp->IsUndefined()) {
                std::string version;
                if (!web_idl_dom_string(isolate, context, hdcp, prefix, &version)) {
                    return reject_from_try_catch(isolate, context, caught);
                }
            }
        }
        return resolved_promise(isolate, context, v8_string(isolate, "usable"));
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> MediaKeySystemAccessHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name) {
    if (name == "keySystem") {
        return v8_string(isolate, key_system_);
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> MediaKeySystemAccessHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>&) {
    watch_record(isolate, trace_label(), name);
    if (name == "getConfiguration") {
        auto result = v8::Object::New(isolate);
        (void)result->CreateDataProperty(context,
            v8_string(isolate, "audioCapabilities").As<v8::String>(),
            make_capability_array(isolate, context, configuration_.audio_capabilities));
        (void)result->CreateDataProperty(context,
            v8_string(isolate, "distinctiveIdentifier").As<v8::String>(),
            v8_string(isolate, configuration_.distinctive_identifier));
        (void)result->CreateDataProperty(context,
            v8_string(isolate, "initDataTypes").As<v8::String>(),
            make_string_array(isolate, context, configuration_.init_data_types));
        (void)result->CreateDataProperty(context,
            v8_string(isolate, "label").As<v8::String>(),
            v8_string(isolate, configuration_.label));
        (void)result->CreateDataProperty(context,
            v8_string(isolate, "persistentState").As<v8::String>(),
            v8_string(isolate, configuration_.persistent_state));
        (void)result->CreateDataProperty(context,
            v8_string(isolate, "sessionTypes").As<v8::String>(),
            make_string_array(isolate, context, configuration_.session_types));
        (void)result->CreateDataProperty(context,
            v8_string(isolate, "videoCapabilities").As<v8::String>(),
            make_capability_array(isolate, context, configuration_.video_capabilities));
        return result;
    }
    if (name == "createMediaKeys") {
        v8::Local<v8::Promise::Resolver> resolver;
        if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
            return v8::Undefined(isolate);
        }
        auto* state = state_from_isolate(isolate);
        if (state == nullptr) return v8::Undefined(isolate);
        auto data = v8::Array::New(isolate, 2);
        if (!data->CreateDataProperty(context, 0, resolver).FromMaybe(false) ||
            !data->CreateDataProperty(context, 1, v8::External::New(
                isolate, owner_, v8::kExternalPointerTypeTagDefault)).FromMaybe(false)) {
            return v8::Undefined(isolate);
        }
        v8::Local<v8::Function> delivery;
        if (!v8::Function::New(context, &deliver_media_keys, data, 0,
                v8::ConstructorBehavior::kThrow).ToLocal(&delivery)) {
            return v8::Undefined(isolate);
        }
        (void)state->register_timer(delivery, 0, false, false);
        return resolver->GetPromise();
    }
    return v8::Undefined(isolate);
}

bool install_navigator_method_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner, bool secure_context) {
    if (!install_dom_exception_interface(isolate, context)) {
        return false;
    }
    if (!secure_context) {
        return true;
    }
    MediaKeySystemAccessHost access(owner, "org.w3.clearkey", ClearKeyConfiguration{});
    MediaKeysHost keys(owner);
    MediaKeySessionHost session(owner);
    MediaKeyStatusMapHost status_map;
    for (HostObject* host : {static_cast<HostObject*>(&access),
                             static_cast<HostObject*>(&session),
                             static_cast<HostObject*>(&status_map)}) {
        auto proto = web_interface_prototype(isolate, context, host->prototype_key());
        if (proto.IsEmpty()) return false;
        install_host_interface(isolate, context, host);
        if (!move_constructor_to_end(isolate, context, proto, host->prototype_key())) {
            return false;
        }
    }
    // MediaKeys places getStatusForPolicy after constructor in Chrome.
    auto keys_proto = web_interface_prototype(isolate, context, "MediaKeys");
    if (keys_proto.IsEmpty()) return false;
    install_host_interface(isolate, context, &keys);
    auto policy_key = v8_string(isolate, "getStatusForPolicy").As<v8::String>();
    v8::Local<v8::Value> policy_fn;
    if (!keys_proto->Get(context, policy_key).ToLocal(&policy_fn) ||
        !keys_proto->Delete(context, policy_key).FromMaybe(false) ||
        !move_constructor_to_end(isolate, context, keys_proto, "MediaKeys") ||
        !install_traced_api_data_property(isolate, context, keys_proto,
            "getStatusForPolicy", policy_fn, native_api_identity(isolate, keys_proto),
            "host_object", "MediaKeys.prototype")) {
        return false;
    }
    // MediaKeyStatusMap[Symbol.iterator] aliases entries.
    auto map_proto = web_interface_prototype(isolate, context, "MediaKeyStatusMap");
    v8::Local<v8::Value> entries_fn;
    if (map_proto.IsEmpty() ||
        !map_proto->Get(context, v8_string(isolate, "entries")).ToLocal(&entries_fn) ||
        !install_traced_api_named_data_property(isolate, context, map_proto,
            v8::Symbol::GetIterator(isolate), entries_fn,
            native_api_identity(isolate, map_proto), "host_object",
            "MediaKeyStatusMap.prototype", v8::DontEnum)) {
        return false;
    }
    return true;
}

v8::Local<v8::Value> navigator_call_bom04_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    const std::string& base_url, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, "navigator", name);
    if (name == "canShare") {
        return navigator_can_share(isolate, context, base_url, arguments);
    }
    if (name == "share") {
        return navigator_share(isolate, context, owner, arguments);
    }
    if (name == "setAppBadge") {
        return navigator_set_app_badge(isolate, context, arguments);
    }
    if (name == "clearAppBadge") {
        return resolved_promise(isolate, context, v8::Undefined(isolate));
    }
    if (name == "registerProtocolHandler" || name == "unregisterProtocolHandler") {
        return navigator_protocol_handler(
            isolate, context, owner, base_url, name, arguments);
    }
    if (name == "getUserMedia" || name == "webkitGetUserMedia") {
        return navigator_get_user_media(isolate, context, owner, name, arguments);
    }
    if (name == "requestMIDIAccess") {
        return navigator_request_midi_access(isolate, context, owner, arguments);
    }
    if (name == "requestMediaKeySystemAccess") {
        return navigator_request_media_key_system_access(
            isolate, context, owner, arguments);
    }
    return v8::Undefined(isolate);
}

}  // namespace iv8::browser
