#include "dom_parser.h"
#include "capabilities.h"

#include "iv8/browser_compatibility.h"
#include "iv8/page_state.h"
#include "../internal/common.h"

namespace iv8::browser {

v8::Local<v8::Value> DOMParserHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    if (name != "parseFromString") return v8::Undefined(isolate);
    if (arguments.Length() < 2) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "parseFromString requires source and MIME type")));
        return v8::Undefined(isolate);
    }
    v8::Local<v8::String> source;
    v8::Local<v8::String> mime;
    if (!arguments[0]->ToString(context).ToLocal(&source) ||
        !arguments[1]->ToString(context).ToLocal(&mime)) return v8::Undefined(isolate);
    v8::String::Utf8Value source_text(isolate, source);
    v8::String::Utf8Value mime_text(isolate, mime);
    if (*source_text == nullptr || *mime_text == nullptr) return v8::Undefined(isolate);
    const std::string type(*mime_text, static_cast<std::size_t>(mime_text.length()));
    if (type == "text/html") {
        return page_->parse_html_document(isolate, context,
            std::string(*source_text, static_cast<std::size_t>(source_text.length())));
    }
    if (type == "text/xml" || type == "application/xml" ||
        type == "application/xhtml+xml" || type == "image/svg+xml") {
        isolate->ThrowException(unsupported_capability(isolate, "DOMParser XML parsing"));
    } else {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Unsupported DOMParser MIME type")));
    }
    return v8::Undefined(isolate);
}

void install_dom_parser_interface(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* page) {
    DOMParserHost parser(page);
    install_host_interface(isolate, context, &parser);
}

}  // namespace iv8::browser
