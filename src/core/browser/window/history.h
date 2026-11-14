#pragma once

#include <cstdint>
#include <cstddef>
#include <string>
#include <vector>

#include "iv8/host_object.h"

namespace iv8 {
class PageState;
namespace browser {

class HistoryHost : public HostObject {
public:
    HistoryHost(PageState* page, std::string url);
    std::string global_name() const override { return "history"; }
    std::string prototype_key() const override { return "History"; }
    std::string trace_object_type() const override { return "history"; }
    std::vector<std::string> property_names() const override;
    std::vector<std::string> writable_property_names() const override;
    std::vector<std::string> method_names() const override;
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name, v8::Local<v8::Value> value) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& args) override;
    void release_v8_handles() noexcept override;
    void install(v8::Isolate* isolate, v8::Local<v8::Context> context);
    v8::Local<v8::Object> wrapper(v8::Isolate* isolate) const;

private:
    struct Entry {
        std::string url;
        std::vector<std::uint8_t> state;
    };
    PageState* page_;
    std::vector<Entry> entries_;
    std::size_t current_ = 0;
    std::string restoration_ = "auto";
    v8::Global<v8::Value> state_cache_;
    v8::Global<v8::Object> wrapper_;
};

}  // namespace browser
}  // namespace iv8
