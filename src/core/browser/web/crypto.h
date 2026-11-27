#pragma once

#include "iv8/v8_headers.h"

namespace iv8 { class PageState; }

namespace iv8::browser {

bool install_crypto_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner, bool secure);

}  // namespace iv8::browser
