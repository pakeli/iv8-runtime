#include "window_named_properties.h"
#include "window_trace_support.h"

#include <cstddef>
#include <string>
#include <utility>

#include "iv8/access_summary.h"
#include "iv8/web_interface.h"
#include "../host/document_host.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {

namespace {

v8::Intercepted window_named_property(
    v8::Local<v8::Name> property,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (!property->IsString() || !info.Data()->IsExternal()) {
        return v8::Intercepted::kNo;
    }
    auto* document = static_cast<DocumentHost*>(info.Data().As<v8::External>()->Value(
        v8::kExternalPointerTypeTagDefault));
    if (document == nullptr) {
        return v8::Intercepted::kNo;
    }
    v8::String::Utf8Value name(info.GetIsolate(), property);
    if (*name == nullptr) return v8::Intercepted::kNo;
    v8::Local<v8::Value> value;
    v8::TryCatch lookup_error(info.GetIsolate());
    if (!document->window_named_property(info.GetIsolate(),
            info.GetIsolate()->GetCurrentContext(),
            std::string(*name, static_cast<std::size_t>(name.length()))).ToLocal(&value)) {
        if (lookup_error.HasCaught()) {
            const auto sequence = window_trace_record(info.GetIsolate(), *name, AccessOperation::kGet);
            AccessCompletion completion;
            completion.outcome = "threw";
            access_capture_exception(info.GetIsolate(), info.GetIsolate()->GetCurrentContext(), lookup_error, completion);
            if (auto* trace = access_trace_from_isolate(info.GetIsolate())) trace->complete(sequence, std::move(completion));
            lookup_error.ReThrow();
        } else {
            window_trace_record(info.GetIsolate(), *name, AccessOperation::kGetProbe, "missing");
        }
        return v8::Intercepted::kNo;
    }
    const auto sequence = window_trace_record(info.GetIsolate(), *name, AccessOperation::kGet);
    AccessPropertyCompletion completion(info.GetIsolate(), sequence, info);
    info.GetReturnValue().Set(value);
    return v8::Intercepted::kYes;
}

}  // namespace

bool install_window_named_properties(v8::Isolate* isolate,
    v8::Local<v8::Context> context, DocumentHost* document) {
    auto named_template = v8::ObjectTemplate::New(isolate);
    named_template->SetHandler(v8::NamedPropertyHandlerConfiguration(
        &window_named_property, nullptr, nullptr, nullptr, nullptr,
        v8::External::New(isolate, document, v8::kExternalPointerTypeTagDefault),
        static_cast<v8::PropertyHandlerFlags>(
            static_cast<int>(v8::PropertyHandlerFlags::kOnlyInterceptStrings) |
            static_cast<int>(v8::PropertyHandlerFlags::kNonMasking))));
    v8::Local<v8::Object> named_properties;
    return named_template->NewInstance(context).ToLocal(&named_properties) &&
        replace_window_properties_prototype(isolate, context, named_properties);
}

}  // namespace iv8::browser
