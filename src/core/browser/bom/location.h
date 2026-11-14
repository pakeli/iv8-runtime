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
#include "iv8/dom_url.h"
#include "iv8/url_parser.h"

namespace iv8 {

namespace browser {

// M2-3 minimal location (read-only). Values are the static decomposition of the
// page's base URL. Read-only: exposed as getter-only accessors, so a JS-side
// write is a no-op (sloppy) / TypeError (strict) and NEVER triggers navigation
// — there is no assign/replace/reload/href-write navigation in M2-3.
class LocationHost : public HostObject {
public:
    LocationHost(UrlParts parts, PageState* page) : parts_(std::move(parts)), page_(page) {}
    void set_current_url(const std::string& url) { parts_ = decompose_url(url); }

    std::string global_name() const override { return "location"; }
    std::string prototype_key() const override { return "Location"; }
    std::string trace_object_type() const override { return "location"; }

    std::vector<std::string> property_names() const override {
        return {"href",     "origin",   "protocol", "host",
                "hostname", "port", "pathname", "search",   "hash"};
    }

    std::vector<std::string> method_names() const override { return {"toString", "assign", "replace", "reload"}; }

    std::vector<std::string> writable_property_names() const override {
        return {"href", "protocol", "host", "hostname", "port", "pathname", "search", "hash"};
    }
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      const std::string& name, v8::Local<v8::Value> value) override {
        v8::Local<v8::String> text;
        if (!value->ToString(context).ToLocal(&text)) return;
        v8::String::Utf8Value utf8(isolate, text);
        const std::string input(*utf8 != nullptr ? *utf8 : "");
        auto resolved = resolve_dom_url(input, parts_.href);
        if (name != "href") {
            resolved = resolve_dom_url(parts_.href, "");
            // The existing URL implementation owns component normalization.
            resolved = set_dom_url_component(resolved, name, input);
        }
        if (!resolved.valid) {
            isolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8Literal(isolate, "Invalid navigation URL")));
            return;
        }
        page_->record_navigation(isolate, "assign", resolved.href);
    }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate, v8::Local<v8::Context>,
                                      const std::string& name) override {
        if (name == "port") return v8_string(isolate, resolve_dom_url(parts_.href, "").port);
        const std::string* value = component(name);
        if (value != nullptr) {
            return v8_string(isolate, *value);
        }
        return v8::Undefined(isolate);
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& args) override {
        if (name == "toString") return v8_string(isolate, parts_.href);
        std::string requested = parts_.href;
        if (name != "reload") {
            if (args.Length() == 0) {
                isolate->ThrowException(v8::Exception::TypeError(
                    v8::String::NewFromUtf8Literal(isolate, "A navigation URL is required")));
                return v8::Undefined(isolate);
            }
            v8::Local<v8::String> input;
            if (!args[0]->ToString(context).ToLocal(&input)) return v8::Undefined(isolate);
            v8::String::Utf8Value utf8(isolate, input);
            auto url = resolve_dom_url(*utf8 != nullptr ? *utf8 : "", parts_.href);
            if (!url.valid) {
                isolate->ThrowException(v8::Exception::TypeError(
                    v8::String::NewFromUtf8Literal(isolate, "Invalid navigation URL")));
                return v8::Undefined(isolate);
            }
            requested = url.href;
        }
        page_->record_navigation(isolate, name, requested);
        return v8::Undefined(isolate);
    }

private:
    const std::string* component(const std::string& name) const {
        if (name == "href") return &parts_.href;
        if (name == "origin") return &parts_.origin;
        if (name == "protocol") return &parts_.protocol;
        if (name == "host") return &parts_.host;
        if (name == "hostname") return &parts_.hostname;
        if (name == "pathname") return &parts_.pathname;
        if (name == "search") return &parts_.search;
        if (name == "hash") return &parts_.hash;
        return nullptr;
    }

    UrlParts parts_;
    PageState* page_;
};

}  // namespace browser
}  // namespace iv8
