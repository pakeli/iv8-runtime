#include "window_trace_support.h"

#include <limits>

#include "iv8/access_summary.h"
#include "iv8/host_object.h"

namespace iv8::browser {

namespace {

void window_data_getter(v8::Local<v8::Name> property,
                         const v8::PropertyCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    v8::String::Utf8Value member(isolate, property);
    const auto sequence = *member == nullptr ? 0 :
        window_trace_record(isolate, *member, AccessOperation::kGet);
    AccessPropertyCompletion completion(isolate, sequence, info);
    v8::Local<v8::Value> value;
    if (info.Data().As<v8::Array>()->Get(context, 0).ToLocal(&value)) {
        info.GetReturnValue().Set(value);
    }
}

void window_data_setter(v8::Local<v8::Name> property,
                         v8::Local<v8::Value> value,
                         const v8::PropertyCallbackInfo<void>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    v8::String::Utf8Value member(isolate, property);
    const auto sequence = *member == nullptr ? 0 :
        window_trace_record(isolate, *member, AccessOperation::kSet);
    AccessPropertySetCompletion completion(isolate, sequence, info, value);
    if (!info.Data().As<v8::Array>()->CreateDataProperty(context, 0, value).FromMaybe(false)) return;
}

}  // namespace

std::uint64_t window_trace_record(v8::Isolate* isolate, const char* member,
                                  AccessOperation operation) {
    auto* trace = access_trace_from_isolate(isolate);
    if (trace == nullptr || !trace->enabled()) return 0;
    auto context = isolate->GetCurrentContext();
    v8::Local<v8::Value> value;
    if (!context->Global()->GetPrivate(context, v8::Private::ForApi(isolate,
            v8::String::NewFromUtf8Literal(isolate, "iv8.webidl.window.host")))
                .ToLocal(&value) || !value->IsExternal()) return 0;
    auto* host = static_cast<HostObject*>(value.As<v8::External>()->Value(
        v8::kExternalPointerTypeTagDefault));
    return host == nullptr ? 0 : trace->record(host, "window", "window", member, operation);
}

bool install_traced_window_data_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> target,
    std::string_view name, v8::Local<v8::Value> value,
    v8::PropertyAttribute attributes) {
    if (target.IsEmpty() || value.IsEmpty() ||
        name.size() > static_cast<std::size_t>(std::numeric_limits<int>::max())) return false;
    v8::Local<v8::String> property;
    if (!v8::String::NewFromUtf8(isolate, name.data(), v8::NewStringType::kNormal,
            static_cast<int>(name.size())).ToLocal(&property)) return false;
    auto data = v8::Array::New(isolate, 1);
    if (!data->CreateDataProperty(context, 0, value).FromMaybe(false)) return false;
    return target->SetNativeDataProperty(context, property, &window_data_getter,
        &window_data_setter, data, attributes).FromMaybe(false);
}

}  // namespace iv8::browser
