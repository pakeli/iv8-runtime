#include "iv8/context_host.h"
#include "encoding.h"

#include <cctype>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <string>

#include "../internal/common.h"

namespace iv8::browser {

// M8-1 TextEncoder / TextDecoder — a minimal UTF-8-only encoding surface. The actual
// UTF-8 conversion is delegated to V8 itself (String::Utf8Value for encode,
// String::NewFromUtf8 for decode), so multi-byte / lone-surrogate handling is exactly
// whatever V8 does (lenient: bad bytes decode to U+FFFD). No encodeInto / streaming /
// BOM stripping / non-UTF-8 labels / fatal-error behaviour.

// `new TextEncoder()` — no per-instance state (encode() + encoding live on the
// prototype). Requires a construct call.
void text_encoder_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    if (!info.IsConstructCall()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(
                isolate, "TextEncoder constructor requires 'new'")));
    }
}

// encoder.encode(input) -> Uint8Array of String(input) as UTF-8 (encode("") -> []).
void text_encoder_encode(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::String> str;
    if (!info[0]->ToString(context).ToLocal(&str)) {
        return;  // a throwing ToString already scheduled the exception
    }
    v8::String::Utf8Value utf8(isolate, str);
    const std::size_t len =
        (*utf8 != nullptr) ? static_cast<std::size_t>(utf8.length()) : 0;
    v8::Local<v8::ArrayBuffer> buffer = v8::ArrayBuffer::New(isolate, len);
    if (len > 0) {
        std::memcpy(buffer->Data(), *utf8, len);
    }
    info.GetReturnValue().Set(v8::Uint8Array::New(buffer, 0, len));
}

// `new TextDecoder(label?, options?)`. label: undefined/absent, or a string that
// ASCII-trims + lowercases to "" / "utf-8" / "utf8" -> UTF-8; anything else is a
// RangeError. options.fatal / options.ignoreBOM are captured as read-only booleans
// but do NOT change behaviour this phase (decode stays lenient UTF-8). Requires new.
void text_decoder_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!info.IsConstructCall()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(
                isolate, "TextDecoder constructor requires 'new'")));
        return;
    }
    v8::Local<v8::Value> label = info[0];
    if (!label->IsUndefined()) {
        v8::Local<v8::String> str;
        if (!label->ToString(context).ToLocal(&str)) {
            return;  // ToString threw
        }
        v8::String::Utf8Value utf8(isolate, str);
        std::string s = (*utf8 != nullptr)
                            ? std::string(*utf8,
                                          static_cast<std::size_t>(utf8.length()))
                            : std::string();
        std::size_t a = 0, b = s.size();
        while (a < b && std::isspace(static_cast<unsigned char>(s[a]))) a++;
        while (b > a && std::isspace(static_cast<unsigned char>(s[b - 1]))) b--;
        const std::string norm = ascii_lower(s.substr(a, b - a));
        if (norm != "" && norm != "utf-8" && norm != "utf8") {
            isolate->ThrowException(v8::Exception::RangeError(
                v8::String::NewFromUtf8Literal(
                    isolate, "TextDecoder: only the UTF-8 label is supported")));
            return;
        }
    }
    bool fatal = false, ignore_bom = false;
    if (info.Length() > 1 && info[1]->IsObject()) {
        v8::Local<v8::Object> opts = info[1].As<v8::Object>();
        v8::Local<v8::Value> v;
        if (opts->Get(context, v8_string(isolate, "fatal")).ToLocal(&v)) {
            fatal = v->BooleanValue(isolate);
        }
        if (opts->Get(context, v8_string(isolate, "ignoreBOM")).ToLocal(&v)) {
            ignore_bom = v->BooleanValue(isolate);
        }
    }
    v8::Local<v8::Object> self = info.This();
    (void)self->DefineOwnProperty(
        context, v8::String::NewFromUtf8Literal(isolate, "fatal"),
        v8::Boolean::New(isolate, fatal), v8::ReadOnly);
    (void)self->DefineOwnProperty(
        context, v8::String::NewFromUtf8Literal(isolate, "ignoreBOM"),
        v8::Boolean::New(isolate, ignore_bom), v8::ReadOnly);
}

// decoder.decode(input?) -> String. undefined/absent -> ""; an ArrayBuffer or any
// ArrayBufferView (TypedArray / DataView) -> its bytes as lenient UTF-8 (no BOM
// stripping); any other argument -> TypeError.
void text_decoder_decode(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Value> input = info[0];
    if (input->IsUndefined()) {
        info.GetReturnValue().Set(v8::String::Empty(isolate));
        return;
    }
    const std::uint8_t* data = nullptr;
    std::size_t len = 0;
    if (input->IsArrayBuffer()) {
        v8::Local<v8::ArrayBuffer> buf = input.As<v8::ArrayBuffer>();
        data = static_cast<const std::uint8_t*>(buf->Data());
        len = buf->ByteLength();
    } else if (input->IsArrayBufferView()) {
        v8::Local<v8::ArrayBufferView> view = input.As<v8::ArrayBufferView>();
        v8::Local<v8::ArrayBuffer> buf = view->Buffer();
        data = static_cast<const std::uint8_t*>(buf->Data()) + view->ByteOffset();
        len = view->ByteLength();
    } else {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(
                isolate, "TextDecoder.decode: input must be a BufferSource")));
        return;
    }
    if (len == 0) {
        info.GetReturnValue().Set(v8::String::Empty(isolate));
        return;
    }
    v8::Local<v8::String> out;
    if (v8::String::NewFromUtf8(isolate, reinterpret_cast<const char*>(data),
                                v8::NewStringType::kNormal, static_cast<int>(len))
            .ToLocal(&out)) {
        info.GetReturnValue().Set(out);
    } else {
        info.GetReturnValue().Set(v8::String::Empty(isolate));
    }
}

}  // namespace iv8::browser
