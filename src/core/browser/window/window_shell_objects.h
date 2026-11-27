#pragma once

#include <memory>
#include <string_view>
#include <vector>

#include "iv8/host_object.h"

namespace iv8 { class PageState; }

namespace iv8::browser {

// Install ordinary Window namespace/object entries only. Interface prototypes
// use the canonical private registry without publishing new global constructors.
bool install_window_shell_objects(v8::Isolate* isolate,
    v8::Local<v8::Context> context,
    std::vector<std::unique_ptr<HostObject>>& generation_owners);

v8::Local<v8::Object> create_window_shell_instance(
    PageState* owner, v8::Isolate* isolate, v8::Local<v8::Context> context,
    std::string_view kind);

}  // namespace iv8::browser
