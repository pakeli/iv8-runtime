#include "iv8/context_host.h"
#include "base64_globals.h"

#include <cstddef>
#include <cstdint>
#include <string>

#include "iv8/base64.h"
#include "../internal/common.h"

namespace iv8::browser {

// M8-4 atob / btoa — minimal standard Base64. btoa operates on a "binary string"
// (each UTF-16 code unit must be <= 0xFF; its low 8 bits is the byte); atob returns a
// binary string (each output byte becomes a Latin-1 code unit 0..255, via
// NewFromOneByte). ASCII whitespace in atob input is ignored. Both error paths throw a
// JS TypeError (the project has no DOMException/InvalidCharacterError). No base64url /
// ArrayBuffer API / streaming / auto UTF-8.
void btoa_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::String> str;
    if (!info[0]->ToString(context).ToLocal(&str)) {
        return;  // ToString threw
    }
    v8::String::Value units(isolate, str);  // UTF-16 code units
    std::string bytes;
    if (*units != nullptr) {
        bytes.reserve(static_cast<std::size_t>(units.length()));
        for (int i = 0; i < units.length(); i++) {
            const std::uint16_t u = (*units)[i];
            if (u > 0xFF) {
                isolate->ThrowException(v8::Exception::TypeError(
                    v8::String::NewFromUtf8Literal(
                        isolate,
                        "btoa: string contains characters outside the Latin1 range")));
                return;
            }
            bytes += static_cast<char>(u & 0xFF);
        }
    }
    info.GetReturnValue().Set(v8_string(isolate, base64_encode(bytes)));
}

void atob_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::String> str;
    if (!info[0]->ToString(context).ToLocal(&str)) {
        return;  // ToString threw
    }
    v8::String::Utf8Value utf8(isolate, str);  // valid base64 is ASCII
    const std::string input =
        (*utf8 != nullptr)
            ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
            : std::string();
    std::string decoded;
    if (!base64_decode(input, decoded)) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(
                isolate, "atob: the string to be decoded is not correctly encoded")));
        return;
    }
    // Each decoded byte becomes a Latin-1 code unit (0..255) — a "binary string".
    v8::Local<v8::String> result;
    if (v8::String::NewFromOneByte(
            isolate, reinterpret_cast<const std::uint8_t*>(decoded.data()),
            v8::NewStringType::kNormal, static_cast<int>(decoded.size()))
            .ToLocal(&result)) {
        info.GetReturnValue().Set(result);
    } else {
        info.GetReturnValue().Set(v8::String::Empty(isolate));
    }
}

}  // namespace iv8::browser
