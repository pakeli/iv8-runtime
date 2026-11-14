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

namespace iv8 {

// Step 5g: Page-owned backings for matchMedia result objects. Keyed by media
// string + evaluated matches; cleared on every generation.
struct MediaQueryListStore {
    std::unordered_map<std::u16string, std::unique_ptr<HostObject>> by_key;
};

namespace browser {

extern const v8::ExternalPointerTypeTag kMediaQueryFactoryTag;

extern void make_media_query_list_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

class MediaQueryListHost : public HostObject {
public:
    MediaQueryListHost(std::u16string media, bool matches)
        : media_(std::move(media)), matches_(matches) {}

    std::string global_name() const override { return std::string(); }
    std::string trace_object_type() const override { return "media_query_list"; }
    std::string trace_label() const override { return "matchMedia"; }

    std::vector<std::string> property_names() const override {
        return {"media", "matches"};
    }

    std::vector<std::string> method_names() const override {
        return {"addListener",        "removeListener", "addEventListener",
                "removeEventListener", "dispatchEvent"};
    }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
                                      v8::Local<v8::Context>,
                                      const std::string& name) override {
        if (name == "media") {
            // Preserve UTF-16 exactly (including lone surrogates).
            return v8::String::NewFromTwoByte(
                       isolate,
                       reinterpret_cast<const std::uint16_t*>(media_.data()),
                       v8::NewStringType::kNormal,
                       static_cast<int>(media_.size()))
                .ToLocalChecked();
        }
        if (name == "matches") {
            return v8::Boolean::New(isolate, matches_);
        }
        return v8::Undefined(isolate);
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        if (name == "dispatchEvent") {
            return v8::Boolean::New(isolate, false);
        }
        return v8::Undefined(isolate);
    }

private:
    std::u16string media_;
    bool matches_ = false;
};

}  // namespace browser
}  // namespace iv8
