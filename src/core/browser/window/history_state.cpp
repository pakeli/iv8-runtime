#include "history_state.h"

#include <cstdlib>
#include <memory>
#include <utility>

namespace iv8::browser {
namespace {

// The pinned V8 SDK is built without RTTI. Keep this Delegate subclass in a
// matching, PCH-free translation unit, without importing pybind11 headers.
class HistorySerializer : public v8::ValueSerializer::Delegate {
public:
    HistorySerializer(v8::Isolate* isolate, std::string operation)
        : isolate_(isolate), operation_(std::move(operation)) {}
    v8::Maybe<bool> WriteHostObject(v8::Isolate*, v8::Local<v8::Object>) override {
        ThrowDataCloneError(v8::String::NewFromUtf8Literal(isolate_, "The host object could not be cloned."));
        return v8::Nothing<bool>();
    }
    v8::Maybe<std::uint32_t> GetSharedArrayBufferId(v8::Isolate*,
        v8::Local<v8::SharedArrayBuffer>) override {
        ThrowDataCloneError(v8::String::NewFromUtf8Literal(isolate_, "Shared memory is unavailable in History state."));
        return v8::Nothing<std::uint32_t>();
    }
    void ThrowDataCloneError(v8::Local<v8::String> message) override {
        v8::String::Utf8Value text(isolate_, message);
        const std::string description = "Failed to execute '" + operation_ +
            "' on 'History': " + (*text != nullptr ? *text : "The value could not be cloned.");
        v8::Local<v8::String> string;
        if (!v8::String::NewFromUtf8(isolate_, description.data(), v8::NewStringType::kNormal,
                static_cast<int>(description.size())).ToLocal(&string)) return;
        auto error = v8::Exception::Error(string);
        (void)error.As<v8::Object>()->Set(isolate_->GetCurrentContext(),
            v8::String::NewFromUtf8Literal(isolate_, "name"),
            v8::String::NewFromUtf8Literal(isolate_, "DataCloneError"));
        isolate_->ThrowException(error);
    }
private:
    v8::Isolate* isolate_;
    std::string operation_;
};

}  // namespace

bool serialize_history_state(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> value, const std::string& operation,
    std::vector<std::uint8_t>* output) {
    HistorySerializer delegate(isolate, operation);
    v8::ValueSerializer serializer(isolate, &delegate);
    serializer.WriteHeader();
    if (!serializer.WriteValue(context, value).FromMaybe(false)) return false;
    auto [data, size] = serializer.Release();
    std::unique_ptr<std::uint8_t, decltype(&std::free)> owned(data, &std::free);
    output->assign(data, data + size);
    return true;
}

}  // namespace iv8::browser
