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

using StorageStore = std::vector<std::pair<std::string, std::string>>;

extern const v8::ExternalPointerTypeTag kStorageTag;

extern v8::Local<v8::Private> storage_brand(v8::Isolate* isolate);

extern StorageStore* storage_receiver(v8::Isolate* isolate, v8::Local<v8::Object> receiver);

extern std::string storage_string_arg(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Value> value);

extern StorageStore::iterator storage_find(StorageStore* store, const std::string& key);

extern void storage_get_item(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void storage_set_item(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void storage_remove_item(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void storage_clear(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void storage_key(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void storage_length_getter(const v8::FunctionCallbackInfo<v8::Value>& info);

}  // namespace browser
}  // namespace iv8
