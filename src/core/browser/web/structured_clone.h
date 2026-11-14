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

namespace browser {

extern v8::MaybeLocal<v8::Value> structured_clone_value(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> value, v8::Local<v8::Map> seen, int depth);

extern void structured_clone_callback(const v8::FunctionCallbackInfo<v8::Value>& info);

}  // namespace browser
}  // namespace iv8
