#include "window_metadata.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "iv8/page_state.h"
#include "iv8/access_summary.h"
#include "iv8/url_parser.h"
#include "iv8/web_method.h"
#include "../internal/common.h"
#include "../internal/trace_hooks.h"

namespace iv8 {

using namespace browser;

constexpr v8::ExternalPointerTypeTag kWindowNameTag =
    v8::kExternalPointerTypeTagDefault;

void window_name_getter(v8::Local<v8::Name>,
                        const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kWindowNameTag));
    const std::uint64_t seq = access_trace_record(
        isolate, page, "window", "window", "name", AccessOperation::kGet);
    AccessPropertyCompletion completion(isolate, seq, info);
    info.GetReturnValue().Set(v8_string(isolate, page->window_name()));
}

void window_name_setter(v8::Local<v8::Name>, v8::Local<v8::Value> value,
                        const v8::PropertyCallbackInfo<void>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kWindowNameTag));
    const std::uint64_t seq = access_trace_record(
        isolate, page, "window", "window", "name", AccessOperation::kSet);
    AccessPropertySetCompletion completion(isolate, seq, info, value);
    v8::Local<v8::String> str;
    if (!value->ToString(context).ToLocal(&str)) {
        return;  // coercion threw; leave the stored name unchanged
    }
    v8::String::Utf8Value utf8(isolate, str);
    page->set_window_name(
        *utf8 != nullptr
            ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
            : std::string());
}

void window_status_getter(v8::Local<v8::Name>,
                          const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kWindowNameTag));
    const std::uint64_t seq = access_trace_record(
        isolate, page, "window", "window", "status", AccessOperation::kGet);
    AccessPropertyCompletion completion(isolate, seq, info);
    info.GetReturnValue().Set(v8_string(isolate, page->window_status()));
}

void window_status_setter(v8::Local<v8::Name>, v8::Local<v8::Value> value,
                          const v8::PropertyCallbackInfo<void>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kWindowNameTag));
    const std::uint64_t seq = access_trace_record(
        isolate, page, "window", "window", "status", AccessOperation::kSet);
    AccessPropertySetCompletion completion(isolate, seq, info, value);
    v8::Local<v8::String> str;
    if (!value->ToString(context).ToLocal(&str)) {
        return;  // coercion threw; leave the stored status unchanged
    }
    v8::String::Utf8Value utf8(isolate, str);
    page->set_window_status(
        *utf8 != nullptr
            ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
            : std::string());
}

constexpr v8::ExternalPointerTypeTag kWindowAliasTag =
    v8::kExternalPointerTypeTagDefault;

void window_alias_getter(v8::Local<v8::Name> property,
                         const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kWindowAliasTag));
    if (page == nullptr) {
        return;
    }
    v8::String::Utf8Value utf8(isolate, property);
    const std::string name(*utf8 != nullptr ? *utf8 : "");
    const std::uint64_t seq = access_trace_record(
        isolate, page, "window", "window", name.c_str(),
        AccessOperation::kGet);
    AccessPropertyCompletion completion(isolate, seq, info);
    info.GetReturnValue().Set(page->window_alias_value(
        isolate, isolate->GetCurrentContext(), name));
}

void window_alias_setter(v8::Local<v8::Name> property,
                         v8::Local<v8::Value> value,
                         const v8::PropertyCallbackInfo<void>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kWindowAliasTag));
    if (page == nullptr) {
        return;
    }
    v8::String::Utf8Value utf8(isolate, property);
    const std::string name(*utf8 != nullptr ? *utf8 : "");
    const std::uint64_t seq = access_trace_record(
        isolate, page, "window", "window", name.c_str(),
        AccessOperation::kSet);
    AccessPropertySetCompletion completion(isolate, seq, info, value);
    page->set_window_alias(isolate, name, value);
}

bool is_secure_context_url(const std::string& base_url) {
    const std::string lower = ascii_lower(base_url);
    if (lower.rfind("https://", 0) == 0 || lower.rfind("wss://", 0) == 0 ||
        lower.rfind("file://", 0) == 0) {
        return true;
    }
    if (lower.rfind("http://", 0) == 0 || lower.rfind("ws://", 0) == 0) {
        const UrlParts parts = decompose_url(base_url);
        const std::string host = ascii_lower(parts.hostname);
        return host == "localhost" || host == "127.0.0.1" ||
               host == "::1" || host == "[::1]";
    }
    return false;
}

v8::Local<v8::Value> PageState::window_metadata_value(
    v8::Isolate* isolate, const std::string& name) const {
    if (name == "devicePixelRatio") {
        return v8::Number::New(isolate, environment_.device_pixel_ratio);
    }
    if (name == "innerWidth") {
        return v8::Integer::New(isolate, environment_.inner_width);
    }
    if (name == "innerHeight") {
        return v8::Integer::New(isolate, environment_.inner_height);
    }
    if (name == "outerWidth") {
        return v8::Integer::New(isolate, environment_.outer_width);
    }
    if (name == "outerHeight") {
        return v8::Integer::New(isolate, environment_.outer_height);
    }
    if (name == "screenX" || name == "screenLeft") {
        return v8::Integer::New(isolate, environment_.screen_x);
    }
    if (name == "screenY" || name == "screenTop") {
        return v8::Integer::New(isolate, environment_.screen_y);
    }
    if (name == "length") {
        return v8::Integer::New(isolate, 0);
    }
    if (name == "isSecureContext") {
        return v8::Boolean::New(isolate,
                               is_secure_context_url(bootstrap_.base_url));
    }
    if (name == "origin") {
        return v8_string(isolate, decompose_url(bootstrap_.base_url).origin);
    }
    if (name == "crossOriginIsolated" || name == "closed") {
        return v8::False(isolate);
    }
    if (name == "frameElement") {
        return v8::Null(isolate);
    }
    // M11-50: there is no layout engine, so the viewport never scrolls; the
    // legacy pageXOffset/pageYOffset names alias scrollX/scrollY.
    if (name == "scrollX" || name == "pageXOffset" ||
        name == "scrollY" || name == "pageYOffset") {
        return v8::Integer::New(isolate, 0);
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> PageState::window_alias_value(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const std::string& name) const {
    if (name == "parent") {
        return window_parent_override_.IsEmpty()
                   ? v8::Local<v8::Value>(context->Global())
                   : window_parent_override_.Get(isolate);
    }
    if (name == "frames") {
        return window_frames_override_.IsEmpty()
                   ? v8::Local<v8::Value>(context->Global())
                   : window_frames_override_.Get(isolate);
    }
    if (name == "top") {
        return context->Global();
    }
    if (name == "opener") {
        return window_opener_override_.IsEmpty()
                   ? v8::Local<v8::Value>(v8::Null(isolate))
                   : window_opener_override_.Get(isolate);
    }
    return v8::Undefined(isolate);
}

void PageState::set_window_alias(v8::Isolate* isolate, const std::string& name,
                                 v8::Local<v8::Value> value) {
    if (name == "parent") {
        window_parent_override_.Reset(isolate, value);
    } else if (name == "frames") {
        window_frames_override_.Reset(isolate, value);
    } else if (name == "opener") {
        window_opener_override_.Reset(isolate, value);
    }
}

void PageState::reset_window_aliases() noexcept {
    window_parent_override_.Reset();
    window_frames_override_.Reset();
    window_opener_override_.Reset();
}

constexpr v8::ExternalPointerTypeTag kWindowTraceTag =
    v8::kExternalPointerTypeTagDefault;

void window_metadata_getter(v8::Local<v8::Name> property,
                            const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kWindowTraceTag));
    v8::String::Utf8Value utf8(isolate, property);
    if (*utf8 == nullptr) {
        return;
    }
    const std::string name(*utf8, static_cast<std::size_t>(utf8.length()));
    const std::uint64_t seq = access_trace_record(
        isolate, page, "window", "window", name.c_str(),
        AccessOperation::kGet);
    AccessPropertyCompletion completion(isolate, seq, info);
    info.GetReturnValue().Set(page->window_metadata_value(isolate, name));
}

}  // namespace iv8
