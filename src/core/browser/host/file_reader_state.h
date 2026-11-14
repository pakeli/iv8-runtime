#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../dom/dom_node.h"
#include "event_target.h"

namespace iv8::browser {

class DocumentHost;

class FileReaderEvents : public EventTargetHost {
public:
    std::string global_name() const override { return std::string(); }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override { return {}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate, v8::Local<v8::Context>,
                                      const std::string&) override {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        return v8::Undefined(isolate);
    }
};

struct FileReaderState {
    enum class Mode { None, Text, ArrayBuffer };
    DocumentHost* owner = nullptr;
    FileReaderEvents events;
    v8::Global<v8::Object> self;
    std::vector<std::uint8_t> bytes;
    Mode mode = Mode::None;
    bool pending = false;
    bool alive = true;
};

}  // namespace iv8::browser
