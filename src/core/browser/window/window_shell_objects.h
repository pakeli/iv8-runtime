#pragma once

#include <memory>
#include <vector>

#include "iv8/host_object.h"

namespace iv8::browser {

// Install ordinary Window namespace/object entries only. Interface prototypes
// use the canonical private registry without publishing new global constructors.
bool install_window_shell_objects(v8::Isolate* isolate,
    v8::Local<v8::Context> context,
    std::vector<std::unique_ptr<HostObject>>& generation_owners);

}  // namespace iv8::browser
