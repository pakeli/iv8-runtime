#pragma once

#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "iv8/host_object.h"

namespace iv8 { class PageState; }

namespace iv8::browser {

int dom_exception_legacy_code(std::u16string_view name);

class DOMExceptionHost : public HostObject {
public:
    DOMExceptionHost(std::u16string name, std::u16string message)
        : code_(dom_exception_legacy_code(name)),
          name_(std::move(name)),
          message_(std::move(message)) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "DOMException"; }
    std::string trace_label() const override { return "DOMException"; }
    std::vector<std::string> property_names() const override {
        return {"code", "name", "message"};
    }
    std::vector<std::string> method_names() const override { return {}; }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;

private:
    int code_;
    std::u16string name_;
    std::u16string message_;
};

bool install_dom_exception_interface(v8::Isolate* isolate,
    v8::Local<v8::Context> context);

v8::Local<v8::Value> construct_dom_exception(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    v8::Local<v8::Array> arguments);

v8::Local<v8::Value> make_dom_exception(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    std::string_view name, std::string_view message);

}  // namespace iv8::browser
