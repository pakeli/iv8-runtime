#pragma once

#include "iv8/v8_headers.h"

namespace iv8::browser {

class DocumentHost;

// Install the active generation's native resolver between Window and EventTarget.
bool install_window_named_properties(v8::Isolate* isolate,
    v8::Local<v8::Context> context, DocumentHost* document);

}  // namespace iv8::browser
