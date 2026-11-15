#pragma once

#include <string>
#include <unordered_map>

#include "iv8/v8_headers.h"

namespace iv8::browser {

// Generation-owned IDL handler state shared by Window, Document and Elements.
// The owning EventTargetHost releases these handles before isolate disposal.
class EventHandlerStore {
public:
    v8::Local<v8::Value> get(v8::Isolate* isolate, const std::string& name);

    void set(v8::Isolate* isolate, const std::string& name,
             v8::Local<v8::Value> value);

    void invoke(v8::Isolate* isolate, v8::Local<v8::Context> context,
                v8::Local<v8::Object> event, v8::Local<v8::Value> receiver,
                int phase, const std::string& type);

    void release_v8_handles() noexcept;

private:
    std::unordered_map<std::string, v8::Global<v8::Function>> handlers_;
};

}  // namespace iv8::browser
