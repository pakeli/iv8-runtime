#pragma once

#include "iv8/host_object.h"

namespace iv8 {
class PageState;
namespace browser {

class DOMParserHost : public HostObject {
public:
    explicit DOMParserHost(PageState* page) : page_(page) {}
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "DOMParser"; }
    std::string trace_object_type() const override { return "dom_parser"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override { return {"parseFromString"}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&) override {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;
private:
    PageState* page_;
};

void install_dom_parser_interface(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* page);

}  // namespace browser
}  // namespace iv8
