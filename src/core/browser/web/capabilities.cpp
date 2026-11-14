#include "capabilities.h"

#include <cstring>
#include <unordered_set>

#include "iv8/query_parameters.h"
#include "iv8/url_parser.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {

v8::Local<v8::Value> unsupported_capability(v8::Isolate* isolate,
                                                 const std::string& capability) {
    auto error = v8::Exception::Error(v8_string(isolate,
        capability + " execution is unavailable in the offline Page runtime").As<v8::String>());
    (void)error.As<v8::Object>()->Set(isolate->GetCurrentContext(),
        v8_string(isolate, "name"), v8_string(isolate, "NotSupportedError"));
    return error;
}

void web_interface_factory(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto* page = static_cast<PageState*>(info.Data().As<v8::External>()->Value(
        v8::kExternalPointerTypeTagDefault));
    v8::String::Utf8Value name(isolate, info[0]);
    info.GetReturnValue().Set(page->create_web_interface(isolate, isolate->GetCurrentContext(),
        *name != nullptr ? *name : "", info[1].As<v8::Array>()));
}

void offline_fetch(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) return;
    auto error = info.Length() == 0
        ? v8::Exception::TypeError(v8::String::NewFromUtf8Literal(isolate, "A fetch input is required"))
        : unsupported_capability(isolate, "fetch");
    if (!resolver->Reject(context, error).FromMaybe(false)) return;
    info.GetReturnValue().Set(resolver->GetPromise());
}

}  // namespace iv8::browser
