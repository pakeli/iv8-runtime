#include "history.h"
#include "history_state.h"

#include <utility>

#include "iv8/dom_url.h"
#include "iv8/page_state.h"
#include "iv8/web_interface.h"
#include "iv8/web_method.h"
#include "../internal/common.h"

namespace iv8::browser {
namespace {

void history_getter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    if (info.This() != context->Global()) {
        isolate->ThrowException(v8::Exception::TypeError(v8_string(isolate, "Illegal invocation").As<v8::String>()));
        return;
    }
    auto* host = static_cast<HistoryHost*>(info.Data().As<v8::External>()->Value(
        v8::kExternalPointerTypeTagDefault));
    info.GetReturnValue().Set(host->wrapper(isolate));
}

bool to_text(v8::Isolate* isolate, v8::Local<v8::Context> context,
             v8::Local<v8::Value> value, std::string* output) {
    v8::Local<v8::String> string;
    if (!value->ToString(context).ToLocal(&string)) return false;
    v8::String::Utf8Value text(isolate, string);
    if (*text == nullptr) return false;
    output->assign(*text, static_cast<std::size_t>(text.length()));
    return true;
}

}  // namespace

HistoryHost::HistoryHost(PageState* page, std::string url) : page_(page) {
    entries_.push_back({std::move(url), {}});
}

std::vector<std::string> HistoryHost::property_names() const {
    return {"length", "scrollRestoration", "state"};
}

std::vector<std::string> HistoryHost::writable_property_names() const {
    return {"scrollRestoration"};
}

std::vector<std::string> HistoryHost::method_names() const {
    return {"back", "forward", "go", "pushState", "replaceState"};
}

void HistoryHost::release_v8_handles() noexcept {
    state_cache_.Reset();
    wrapper_.Reset();
}

v8::Local<v8::Object> HistoryHost::wrapper(v8::Isolate* isolate) const {
    return wrapper_.Get(isolate);
}

void HistoryHost::install(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    auto object = make_host_object(isolate, context, this);
    wrapper_.Reset(isolate, object);
    auto prototype = web_interface_prototype(isolate, context, "History");
    v8::Local<v8::Value> constructor;
    if (!prototype->Get(context, v8_string(isolate, "constructor")).ToLocal(&constructor)) return;
    // The generic registry creates constructor first; Chrome places it after members.
    if (!prototype->Delete(context, v8_string(isolate, "constructor")).FromMaybe(false)) return;
    if (!prototype->DefineOwnProperty(context, v8_string(isolate, "constructor").As<v8::String>(),
            constructor, v8::DontEnum).FromMaybe(false)) return;
    auto data = v8::External::New(isolate, this, v8::kExternalPointerTypeTagDefault);
    v8::Local<v8::Function> getter;
    if (!make_web_method(isolate, context, "get history", 0, history_getter, data).ToLocal(&getter)) return;
    v8::PropertyDescriptor descriptor(getter, v8::Local<v8::Value>(v8::Undefined(isolate)));
    descriptor.set_enumerable(true);
    descriptor.set_configurable(true);
    (void)context->Global()->DefineProperty(context, v8_string(isolate, "history").As<v8::String>(), descriptor);
}

v8::Local<v8::Value> HistoryHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name) {
    if (name == "length") return v8::Number::New(isolate, static_cast<double>(entries_.size()));
    if (name == "scrollRestoration") return v8_string(isolate, restoration_);
    if (name != "state") return v8::Undefined(isolate);
    if (state_cache_.IsEmpty()) {
        const auto& bytes = entries_[current_].state;
        if (bytes.empty()) return v8::Null(isolate);
        v8::ValueDeserializer deserializer(isolate, bytes.data(), bytes.size());
        if (!deserializer.ReadHeader(context).FromMaybe(false)) return v8::Undefined(isolate);
        v8::Local<v8::Value> value;
        if (!deserializer.ReadValue(context).ToLocal(&value)) return v8::Undefined(isolate);
        state_cache_.Reset(isolate, value);
    }
    return state_cache_.Get(isolate);
}

void HistoryHost::set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
    const std::string& name, v8::Local<v8::Value> value) {
    if (name != "scrollRestoration") return;
    std::string text;
    if (!to_text(isolate, context, value, &text)) return;
    if (text == "auto" || text == "manual") restoration_ = std::move(text);
}

v8::Local<v8::Value> HistoryHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& args) {
    if (name == "pushState" || name == "replaceState") {
        if (args.Length() < 2) {
            isolate->ThrowException(v8::Exception::TypeError(v8_string(isolate,
                "Failed to execute '" + name + "' on 'History': 2 arguments required, but only " +
                std::to_string(args.Length()) + " present.").As<v8::String>()));
            return v8::Undefined(isolate);
        }
        std::string title;
        if (!to_text(isolate, context, args[1], &title)) return v8::Undefined(isolate);
        std::string requested;
        const bool has_url = args.Length() > 2 && !args[2]->IsNullOrUndefined();
        if (has_url && !to_text(isolate, context, args[2], &requested)) return v8::Undefined(isolate);
        std::vector<std::uint8_t> serialized;
        if (!serialize_history_state(isolate, context, args[0], name, &serialized)) return v8::Undefined(isolate);
        std::string url = entries_[current_].url;
        if (has_url && !requested.empty()) {
            auto target = resolve_dom_url(requested, url);
            auto current = resolve_dom_url(url, "");
            if (!target.valid || !current.valid ||
                (current.protocol != "http:" && current.protocol != "https:") ||
                target.origin != current.origin || target.credentials != current.credentials) {
                isolate->ThrowException(dom_exception(isolate, "SecurityError",
                    "Failed to execute '" + name + "' on 'History': The history URL must have the document's origin."));
                return v8::Undefined(isolate);
            }
            url = std::move(target.href);
        }
        Entry entry{url, std::move(serialized)};
        if (name == "pushState") {
            entries_.resize(current_ + 1);
            entries_.push_back(std::move(entry));
            current_ = entries_.size() - 1;
        } else {
            entries_[current_] = std::move(entry);
        }
        state_cache_.Reset();
        page_->apply_history_url(url);
        return v8::Undefined(isolate);
    }
    std::int32_t delta = name == "back" ? -1 : 1;
    if (name == "go") {
        delta = 0;
        if (args.Length() > 0) {
            auto converted = args[0]->Int32Value(context);
            if (converted.IsNothing()) return v8::Undefined(isolate);
            delta = converted.FromJust();
        }
    }
    const auto target = static_cast<std::int64_t>(current_) + delta;
    if (target >= 0 && target < static_cast<std::int64_t>(entries_.size())) {
        page_->record_navigation(isolate, "history." + name, entries_[static_cast<std::size_t>(target)].url);
    }
    return v8::Undefined(isolate);
}

}  // namespace iv8::browser
