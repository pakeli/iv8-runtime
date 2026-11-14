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
#include "iv8/url_parser.h"
#include "iv8/query_parameters.h"

namespace iv8 {

namespace browser {

extern void url_constructor(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void url_to_href(const v8::FunctionCallbackInfo<v8::Value>& info);

extern v8::Local<v8::Private> usp_private_key(v8::Isolate* isolate);

extern std::string usp_read_state(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> self);

extern void usp_write_state(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> self, const std::string& s);

extern std::string usp_arg_string(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> v);

extern void url_search_params_constructor(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void usp_get(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void usp_get_all(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void usp_has(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void usp_append(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void usp_set(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void usp_delete(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void usp_to_string(const v8::FunctionCallbackInfo<v8::Value>& info);

}  // namespace browser
}  // namespace iv8
