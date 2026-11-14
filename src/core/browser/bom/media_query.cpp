#include "media_query.h"

#include <cstring>
#include <unordered_set>

#include "iv8/query_parameters.h"
#include "iv8/url_parser.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {

// Step 5g: a matchMedia result. The JS bootstrap still owns the bounded query
// evaluator and the matchMedia function; it calls a temporary native factory
// that maps (query, matches) to a cached HostObject backing and creates a fresh
// JS wrapper per call. `onchange` stays a plain per-wrapper own property.
constexpr v8::ExternalPointerTypeTag kMediaQueryFactoryTag =
    v8::kExternalPointerTypeTagDefault;

void make_media_query_list_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kMediaQueryFactoryTag));
    if (page == nullptr) {
        info.GetReturnValue().Set(v8::Undefined(isolate));
        return;
    }
    std::u16string media;
    if (info.Length() >= 1 && info[0]->IsString()) {
        v8::String::Value utf16(isolate, info[0]);
        if (*utf16 != nullptr) {
            media.assign(reinterpret_cast<const char16_t*>(*utf16),
                         static_cast<std::size_t>(utf16.length()));
        }
    }
    const bool matches = info.Length() >= 2 && info[1]->BooleanValue(isolate);
    HostObject* host = page->media_query_list_host(media, matches);
    const std::uint64_t seq = access_trace_record(
        isolate, host, "media_query_list", "matchMedia", "matchMedia",
        AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
    v8::Local<v8::Object> list = make_host_object(isolate, context, host);
    (void)list->Set(context, v8_string(isolate, "onchange").As<v8::String>(),
                    v8::Null(isolate));
    info.GetReturnValue().Set(list);
}

}  // namespace iv8::browser
