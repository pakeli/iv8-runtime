#include "web_storage.h"

#include <cstring>
#include <unordered_set>

#include "iv8/query_parameters.h"
#include "iv8/url_parser.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {

// V8 15.0 requires a tag on External::New / External::Value. The default tag is
// reused for every PageState-owned pointer type in this file (see also
// kMutationObserverTag / kWindowEventsTag).
constexpr v8::ExternalPointerTypeTag kStorageTag =
    v8::kExternalPointerTypeTagDefault;

v8::Local<v8::Private> storage_brand(v8::Isolate* isolate) {
    return v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.storage.backing"));
}

StorageStore* storage_receiver(v8::Isolate* isolate,
                               v8::Local<v8::Object> receiver) {
    v8::Local<v8::Value> data;
    if (!receiver->GetPrivate(isolate->GetCurrentContext(), storage_brand(isolate))
            .ToLocal(&data)) return nullptr;
    if (!data->IsExternal()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Illegal invocation")));
        return nullptr;
    }
    return static_cast<StorageStore*>(
        data.As<v8::External>()->Value(kStorageTag));
}

std::string storage_string_arg(v8::Isolate* isolate,
                               v8::Local<v8::Context> context,
                               v8::Local<v8::Value> value) {
    v8::Local<v8::String> str;
    if (!value->ToString(context).ToLocal(&str)) {
        return std::string();
    }
    v8::String::Utf8Value utf8(isolate, str);
    return (*utf8 != nullptr)
               ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
               : std::string();
}

StorageStore::iterator storage_find(StorageStore* store,
                                    const std::string& key) {
    return std::find_if(store->begin(), store->end(),
                        [&key](const std::pair<std::string, std::string>& entry) {
                            return entry.first == key;
                        });
}

void storage_get_item(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    StorageStore* store = storage_receiver(isolate, info.This());
    if (store == nullptr) {
        return;
    }
    const std::uint64_t seq = access_trace_record(
        isolate, store, "storage", "storage", "getItem",
        AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
    const std::string key = storage_string_arg(isolate, context, info[0]);
    const auto it = storage_find(store, key);
    if (it == store->end()) {
        info.GetReturnValue().Set(v8::Null(isolate));
        return;
    }
    info.GetReturnValue().Set(v8_string(isolate, it->second));
}

void storage_set_item(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    StorageStore* store = storage_receiver(isolate, info.This());
    if (store == nullptr) {
        return;
    }
    const std::uint64_t seq = access_trace_record(
        isolate, store, "storage", "storage", "setItem",
        AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
    const std::string key = storage_string_arg(isolate, context, info[0]);
    const std::string value = storage_string_arg(isolate, context, info[1]);
    const auto it = storage_find(store, key);
    if (it == store->end()) {
        store->emplace_back(key, value);
    } else {
        it->second = value;
    }
}

void storage_remove_item(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    StorageStore* store = storage_receiver(isolate, info.This());
    if (store == nullptr) {
        return;
    }
    const std::uint64_t seq = access_trace_record(
        isolate, store, "storage", "storage", "removeItem",
        AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
    const std::string key = storage_string_arg(isolate, context, info[0]);
    const auto it = storage_find(store, key);
    if (it != store->end()) {
        store->erase(it);
    }
}

void storage_clear(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    StorageStore* store = storage_receiver(isolate, info.This());
    if (store != nullptr) {
        const std::uint64_t seq = access_trace_record(
            isolate, store, "storage", "storage", "clear",
            AccessOperation::kCall);
        AccessCallCompletion completion(isolate, seq, info);
        store->clear();
    }
}

void storage_key(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    StorageStore* store = storage_receiver(isolate, info.This());
    if (store == nullptr) {
        return;
    }
    const std::uint64_t seq = access_trace_record(
        isolate, store, "storage", "storage", "key",
        AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
    v8::Local<v8::Uint32> index_value;
    if (!info[0]->ToUint32(context).ToLocal(&index_value)) {
        return;
    }
    const std::uint32_t index = index_value->Value();
    if (index >= store->size()) {
        info.GetReturnValue().Set(v8::Null(isolate));
        return;
    }
    info.GetReturnValue().Set(
        v8_string(isolate, (*store)[index].first));
}

void storage_length_getter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto* store = storage_receiver(isolate, info.This());
    if (store == nullptr) return;
    const auto seq = access_trace_record(isolate, store, "storage", "storage", "length", AccessOperation::kGet);
    auto result = v8::Integer::NewFromUnsigned(isolate, static_cast<unsigned>(store->size()));
    if (seq != 0) {
        AccessCompletion completion;
        completion.outcome = "returned";
        completion.value_summary = access_value_summary(isolate, result);
        access_trace_from_isolate(isolate)->complete(seq, std::move(completion));
    }
    info.GetReturnValue().Set(result);
}

}  // namespace iv8::browser
