#include "iv8/access_summary.h"

#include <algorithm>
#include <cstdio>
#include <utility>

#include "iv8/host_object.h"

namespace iv8 {

namespace {

// Only primitive/type inspection is performed here: no toString/valueOf,
// property Get, or other user code can run while producing metadata.
constexpr int kMaxSummaryBytes = 64;
constexpr int kMaxConvertedStringBytes = 4096;
constexpr int kMaxArgumentSummaries = 8;
constexpr int kMaxExceptionBytes = 4096;

std::string summarize_string(v8::Isolate* isolate,
                             v8::Local<v8::String> value) {
    const int utf8_length = value->Utf8Length(isolate);
    std::string summary = "string:" + std::to_string(utf8_length) + ":";
    if (utf8_length > kMaxConvertedStringBytes) {
        summary += "...";
        return summary;
    }
    v8::String::Utf8Value utf8(isolate, value);
    if (*utf8 == nullptr) {
        summary += "...";
        return summary;
    }
    const std::size_t length = static_cast<std::size_t>(utf8.length());
    std::size_t cut =
        std::min(length, static_cast<std::size_t>(kMaxSummaryBytes));
    if (cut < length) {
        // Do not split a UTF-8 continuation byte; Python must receive valid
        // UTF-8 from the report.
        while (cut > 0 &&
               (static_cast<unsigned char>((*utf8)[cut]) & 0xC0) == 0x80) {
            --cut;
        }
    }
    const auto* bytes = reinterpret_cast<const unsigned char*>(*utf8);
    for (std::size_t i = 0; i < cut; ++i) {
        const unsigned char c = bytes[i];
        switch (c) {
            case '\\':
                summary += "\\\\";
                break;
            case '\n':
                summary += "\\n";
                break;
            case '\r':
                summary += "\\r";
                break;
            case '\t':
                summary += "\\t";
                break;
            default:
                if (c < 0x20 || c == 0x7f) {
                    char escaped[8];
                    std::snprintf(escaped, sizeof(escaped), "\\x%02x", c);
                    summary += escaped;
                } else {
                    summary += static_cast<char>(c);
                }
                break;
        }
    }
    if (cut < length) {
        summary += "...";
    }
    return summary;
}

std::string copy_string(v8::Isolate* isolate, v8::Local<v8::String> value,
                        std::size_t max_bytes = kMaxExceptionBytes) {
    if (value.IsEmpty()) {
        return std::string();
    }
    v8::String::Utf8Value utf8(isolate, value);
    if (*utf8 == nullptr) {
        return std::string();
    }
    const std::size_t length = static_cast<std::size_t>(utf8.length());
    std::size_t cut = std::min(length, max_bytes);
    if (cut < length) {
        while (cut > 0 &&
               (static_cast<unsigned char>((*utf8)[cut]) & 0xC0) == 0x80) {
            --cut;
        }
    }
    std::string out(*utf8, cut);
    if (cut < length) {
        out += "...";
    }
    return out;
}

std::string property_text(v8::Isolate* isolate, v8::Local<v8::Value> value) {
    if (!value.IsEmpty() && value->IsString()) {
        return copy_string(isolate, value.As<v8::String>());
    }
    return std::string();
}

std::string render_stack(v8::Isolate* isolate,
                         v8::Local<v8::StackTrace> stack) {
    if (stack.IsEmpty()) {
        return std::string();
    }
    std::string out;
    const int count = stack->GetFrameCount();
    for (int i = 0; i < count; ++i) {
        v8::Local<v8::StackFrame> frame = stack->GetFrame(isolate, i);
        if (frame.IsEmpty()) {
            continue;
        }
        std::string function = property_text(isolate, frame->GetFunctionName());
        if (function.empty()) {
            function = "<anonymous>";
        }
        std::string script =
            property_text(isolate, frame->GetScriptNameOrSourceURL());
        if (script.empty()) {
            script = "<unknown>";
        }
        out += "    at " + function + " (" + script + ":" +
               std::to_string(frame->GetLineNumber()) + ":" +
               std::to_string(frame->GetColumn()) + ")\n";
        if (out.size() >= static_cast<std::size_t>(kMaxExceptionBytes)) {
            out += "...";
            break;
        }
    }
    return out;
}

}  // namespace

std::string access_value_summary(v8::Isolate* isolate,
                                 v8::Local<v8::Value> value) {
    if (value.IsEmpty() || value->IsUndefined()) {
        return "undefined";
    }
    if (value->IsNull()) {
        return "null";
    }
    if (value->IsBoolean()) {
        return value->BooleanValue(isolate) ? "boolean:true" : "boolean:false";
    }
    if (value->IsNumber()) {
        char buffer[40];
        std::snprintf(buffer, sizeof(buffer), "%.17g",
                      value.As<v8::Number>()->Value());
        return std::string("number:") + buffer;
    }
    if (value->IsString()) {
        return summarize_string(isolate, value.As<v8::String>());
    }
    if (value->IsBigInt()) {
        return "bigint";
    }
    if (value->IsSymbol()) {
        return "symbol";
    }
    if (value->IsFunction()) {
        return "function";
    }
    if (value->IsArray()) {
        return "array";
    }
    if (value->IsObject()) {
        return host_object_backing(value) != nullptr ? "host_object" : "object";
    }
    return "unknown";
}

std::vector<std::string> access_argument_summaries(
    v8::Isolate* isolate, const v8::FunctionCallbackInfo<v8::Value>& info) {
    const int count = std::min(info.Length(), kMaxArgumentSummaries);
    std::vector<std::string> summaries;
    summaries.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        summaries.push_back(access_value_summary(isolate, info[i]));
    }
    return summaries;
}

void access_capture_exception(v8::Isolate* isolate,
                              v8::Local<v8::Context> context,
                              v8::TryCatch& try_catch,
                              AccessCompletion& completion) {
    completion.outcome = "threw";
    v8::Local<v8::Message> message = try_catch.Message();
    if (!message.IsEmpty()) {
        completion.exception_message = property_text(isolate, message->Get());
        v8::Local<v8::Value> resource = message->GetScriptResourceName();
        const std::string resource_name = property_text(isolate, resource);
        int line = 0;
        int column = 0;
        bool has_line = false;
        bool has_column = false;
        if (!context.IsEmpty()) {
            has_line = message->GetLineNumber(context).To(&line);
            has_column = message->GetStartColumn(context).To(&column);
        }
        if (!resource_name.empty()) {
            completion.exception_location = resource_name;
        }
        if (has_line) {
            if (!completion.exception_location.empty()) {
                completion.exception_location += ":";
            }
            completion.exception_location += std::to_string(line);
        }
        if (has_column) {
            completion.exception_location += ":";
            completion.exception_location += std::to_string(column);
        }
        v8::Local<v8::StackTrace> stack = message->GetStackTrace();
        completion.exception_stack = render_stack(isolate, stack);
    }
    v8::Local<v8::Value> exception = try_catch.Exception();
    if (!exception.IsEmpty() && exception->IsObject()) {
        v8::Local<v8::String> constructor =
            exception.As<v8::Object>()->GetConstructorName();
        if (!constructor.IsEmpty()) {
            completion.exception_name = copy_string(isolate, constructor, 128);
        }
    }
    if (completion.exception_stack.empty()) {
        completion.exception_stack = render_stack(
            isolate, v8::StackTrace::CurrentStackTrace(
                         isolate, 20, v8::StackTrace::kDetailed));
    }
}

AccessCallCompletion::AccessCallCompletion(
    v8::Isolate* isolate, std::uint64_t seq,
    const v8::FunctionCallbackInfo<v8::Value>& info)
    : isolate_(isolate), seq_(seq), info_(&info) {
    if (seq_ == 0) {
        return;
    }
    try {
        try_catch_.emplace(isolate);
        completion_.argument_count = static_cast<std::uint64_t>(info.Length());
        completion_.argument_summaries =
            access_argument_summaries(isolate, info);
    } catch (...) {
        try_catch_.reset();
    }
}

AccessCallCompletion::~AccessCallCompletion() noexcept {
    if (seq_ == 0) {
        return;
    }
    AccessTrace* trace = access_trace_from_isolate(isolate_);
    bool caught = false;
    try {
        caught = try_catch_ && try_catch_->HasCaught();
        if (caught) {
            completion_.outcome = "threw";
            access_capture_exception(isolate_, isolate_->GetCurrentContext(),
                                     *try_catch_, completion_);
        } else {
            completion_.outcome = "returned";
            completion_.return_summary = access_value_summary(
                isolate_, info_->GetReturnValue().Get());
        }
        if (trace != nullptr) {
            trace->complete(seq_, std::move(completion_));
        }
    } catch (...) {
        // Metadata is best-effort; never alter the callback's behavior.
    }
    if (caught && try_catch_) {
        try {
            (void)try_catch_->ReThrow();
        } catch (...) {
            // ReThrow itself does not throw; keep the destructor noexcept.
        }
    }
}

AccessPropertyCompletion::AccessPropertyCompletion(
    v8::Isolate* isolate, std::uint64_t seq,
    const v8::PropertyCallbackInfo<v8::Value>& info)
    : isolate_(isolate), seq_(seq), info_(&info) {
    if (seq_ == 0) {
        return;
    }
    try {
        try_catch_.emplace(isolate);
    } catch (...) {
        try_catch_.reset();
    }
}

AccessPropertyCompletion::~AccessPropertyCompletion() noexcept {
    if (seq_ == 0) {
        return;
    }
    AccessTrace* trace = access_trace_from_isolate(isolate_);
    bool caught = false;
    try {
        caught = try_catch_ && try_catch_->HasCaught();
        if (caught) {
            completion_.outcome = "threw";
            access_capture_exception(isolate_, isolate_->GetCurrentContext(),
                                     *try_catch_, completion_);
        } else {
            completion_.outcome = "returned";
            completion_.value_summary = access_value_summary(
                isolate_, info_->GetReturnValue().Get());
        }
        if (trace != nullptr) {
            trace->complete(seq_, std::move(completion_));
        }
    } catch (...) {
        // Metadata is best-effort; never alter the callback's behavior.
    }
    if (caught && try_catch_) {
        try {
            (void)try_catch_->ReThrow();
        } catch (...) {
            // ReThrow itself does not throw; keep the destructor noexcept.
        }
    }
}

AccessPropertySetCompletion::AccessPropertySetCompletion(
    v8::Isolate* isolate, std::uint64_t seq,
    const v8::PropertyCallbackInfo<void>& /*info*/,
    v8::Local<v8::Value> value)
    : isolate_(isolate), seq_(seq) {
    if (seq_ == 0) {
        return;
    }
    try {
        try_catch_.emplace(isolate);
        completion_.value_summary = access_value_summary(isolate, value);
    } catch (...) {
        try_catch_.reset();
    }
}

AccessPropertySetCompletion::~AccessPropertySetCompletion() noexcept {
    if (seq_ == 0) {
        return;
    }
    AccessTrace* trace = access_trace_from_isolate(isolate_);
    bool caught = false;
    try {
        caught = try_catch_ && try_catch_->HasCaught();
        if (caught) {
            completion_.outcome = "threw";
            access_capture_exception(isolate_, isolate_->GetCurrentContext(),
                                     *try_catch_, completion_);
        } else {
            completion_.outcome = "returned";
        }
        if (trace != nullptr) {
            trace->complete(seq_, std::move(completion_));
        }
    } catch (...) {
        // Metadata is best-effort; never alter the callback's behavior.
    }
    if (caught && try_catch_) {
        try {
            (void)try_catch_->ReThrow();
        } catch (...) {
            // ReThrow itself does not throw; keep the destructor noexcept.
        }
    }
}

}  // namespace iv8
