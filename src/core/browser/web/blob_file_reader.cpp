#include "blob_file_reader.h"
#include "event_constructors.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "iv8/web_method.h"
#include "../host/document_host.h"
#include "../internal/common.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {

constexpr v8::ExternalPointerTypeTag kBlobTag =
    v8::kExternalPointerTypeTagDefault;

constexpr v8::ExternalPointerTypeTag kFileReaderTag =
    v8::kExternalPointerTypeTagDefault;

v8::Local<v8::Private> blob_bytes_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate, v8::String::NewFromUtf8Literal(isolate, "iv8::Blob::bytes"));
}

v8::Local<v8::Private> blob_type_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate, v8::String::NewFromUtf8Literal(isolate, "iv8::Blob::type"));
}

v8::Local<v8::Private> file_reader_state_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8::FileReader::state"));
}

void throw_blob_type_error(v8::Isolate* isolate, const char* message) {
    isolate->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8(isolate, message).ToLocalChecked()));
}

bool blob_read_bytes(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Value> value,
                     std::vector<std::uint8_t>* out) {
    out->clear();
    if (!value->IsObject()) {
        return false;
    }
    v8::Local<v8::Value> stored;
    if (!value.As<v8::Object>()
             ->GetPrivate(context, blob_bytes_key(isolate))
             .ToLocal(&stored) ||
        !stored->IsArrayBuffer()) {
        return false;
    }
    const v8::Local<v8::ArrayBuffer> buffer = stored.As<v8::ArrayBuffer>();
    const std::size_t length = buffer->ByteLength();
    if (length > 0) {
        const auto* data = static_cast<const std::uint8_t*>(buffer->Data());
        out->assign(data, data + length);
    }
    return true;
}

void append_bytes(std::vector<std::uint8_t>* out, const std::uint8_t* data,
                  std::size_t length) {
    if (length > 0) {
        out->insert(out->end(), data, data + length);
    }
}

bool blob_append_part(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Value> value,
                      std::vector<std::uint8_t>* out) {
    std::vector<std::uint8_t> bytes;
    if (blob_read_bytes(isolate, context, value, &bytes)) {
        append_bytes(out, bytes.data(), bytes.size());
        return true;
    }
    if (value->IsArrayBuffer()) {
        const v8::Local<v8::ArrayBuffer> buffer = value.As<v8::ArrayBuffer>();
        append_bytes(out,
                     static_cast<const std::uint8_t*>(buffer->Data()),
                     buffer->ByteLength());
        return true;
    }
    if (value->IsArrayBufferView()) {
        const v8::Local<v8::ArrayBufferView> view =
            value.As<v8::ArrayBufferView>();
        const auto* base =
            static_cast<const std::uint8_t*>(view->Buffer()->Data());
        append_bytes(out, base + view->ByteOffset(), view->ByteLength());
        return true;
    }
    v8::Local<v8::String> str;
    if (!value->ToString(context).ToLocal(&str)) {
        return false;
    }
    v8::String::Utf8Value utf8(isolate, str);
    if (*utf8 != nullptr) {
        append_bytes(out, reinterpret_cast<const std::uint8_t*>(*utf8),
                     static_cast<std::size_t>(utf8.length()));
    }
    return true;
}

bool blob_type_from_options(v8::Isolate* isolate,
                            v8::Local<v8::Context> context,
                            v8::Local<v8::Value> options, std::string* out) {
    out->clear();
    if (!options->IsObject()) {
        return true;
    }
    v8::Local<v8::Value> value;
    if (!options.As<v8::Object>()
             ->Get(context, v8_string(isolate, "type"))
             .ToLocal(&value)) {
        return false;
    }
    if (value->IsUndefined()) {
        return true;
    }
    v8::Local<v8::String> str;
    if (!value->ToString(context).ToLocal(&str)) {
        return false;
    }
    v8::String::Utf8Value utf8(isolate, str);
    if (*utf8 != nullptr) {
        *out = ascii_lower(
            std::string(*utf8, static_cast<std::size_t>(utf8.length())));
    }
    return true;
}

void set_blob_field(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    v8::Local<v8::Object> self, const char* name,
                    v8::Local<v8::Value> value) {
    (void)self->DefineOwnProperty(
        context, v8::String::NewFromUtf8(isolate, name).ToLocalChecked(), value,
        v8::ReadOnly);
}

bool blob_initialize(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Value> parts, v8::Local<v8::Value> options,
                     v8::Local<v8::Object> self) {
    std::vector<std::uint8_t> bytes;
    if (!parts->IsUndefined() && !parts->IsNull()) {
        if (!parts->IsArray()) {
            throw_blob_type_error(isolate, "Blob: parts must be an Array");
            return false;
        }
        const v8::Local<v8::Array> array = parts.As<v8::Array>();
        const std::uint32_t length = array->Length();
        for (std::uint32_t i = 0; i < length; i++) {
            v8::Local<v8::Value> part;
            if (!array->Get(context, i).ToLocal(&part)) {
                return false;
            }
            if (!blob_append_part(isolate, context, part, &bytes)) {
                return false;
            }
        }
    }
    std::string type;
    if (!blob_type_from_options(isolate, context, options, &type)) {
        return false;
    }
    v8::Local<v8::ArrayBuffer> buffer =
        v8::ArrayBuffer::New(isolate, bytes.size());
    if (!bytes.empty()) {
        std::memcpy(buffer->Data(), bytes.data(), bytes.size());
    }
    (void)self->SetPrivate(context, blob_bytes_key(isolate), buffer);
    (void)self->SetPrivate(context, blob_type_key(isolate),
                           v8_string(isolate, type));
    set_blob_field(isolate, context, self, "size",
                   v8::Number::New(isolate, static_cast<double>(bytes.size())));
    set_blob_field(isolate, context, self, "type", v8_string(isolate, type));
    return true;
}

void blob_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!event_construct_check(info, "Blob")) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    (void)blob_initialize(isolate, context, info[0], info[1], info.This());
}

void file_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!event_construct_check(info, "File")) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!blob_initialize(isolate, context, info[0], info[2], info.This())) {
        return;
    }
    std::string name;
    if (info.Length() > 1 && !info[1]->IsUndefined()) {
        v8::Local<v8::String> str;
        if (!info[1]->ToString(context).ToLocal(&str)) {
            return;
        }
        v8::String::Utf8Value utf8(isolate, str);
        if (*utf8 != nullptr) {
            name.assign(*utf8, static_cast<std::size_t>(utf8.length()));
        }
    }
    set_blob_field(isolate, context, info.This(), "name",
                   v8_string(isolate, name));
    double last_modified = 0;
    if (info.Length() > 2 && info[2]->IsObject()) {
        v8::Local<v8::Value> value;
        if (!info[2]
                 .As<v8::Object>()
                 ->Get(context, v8_string(isolate, "lastModified"))
                 .ToLocal(&value)) {
            return;
        }
        if (!value->IsUndefined()) {
            v8::Local<v8::Number> number;
            if (!value->ToNumber(context).ToLocal(&number)) {
                return;
            }
            const double value_number = number->Value();
            if (std::isfinite(value_number)) {
                last_modified = std::trunc(value_number);
            }
        }
    }
    set_blob_field(isolate, context, info.This(), "lastModified",
                   v8::Number::New(isolate, last_modified));
}

DocumentHost* document_from_blob_data(v8::Local<v8::Value> data) {
    if (!data->IsExternal()) {
        return nullptr;
    }
    return static_cast<DocumentHost*>(
        data.As<v8::External>()->Value(kBlobTag));
}

bool blob_bytes_or_throw(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         v8::Local<v8::Object> self,
                         std::vector<std::uint8_t>* out) {
    if (blob_read_bytes(isolate, context, self, out)) {
        return true;
    }
    throw_blob_type_error(isolate, "Illegal invocation");
    return false;
}

bool blob_index_arg(v8::Local<v8::Context> context, v8::Local<v8::Value> value,
                    std::size_t size, std::size_t default_value,
                    std::size_t* out) {
    if (value->IsUndefined()) {
        *out = default_value;
        return true;
    }
    v8::Local<v8::Number> number;
    if (!value->ToNumber(context).ToLocal(&number)) {
        return false;
    }
    double index = number->Value();
    if (std::isnan(index)) {
        index = 0;
    }
    if (index < 0) {
        index = static_cast<double>(size) + index;
    }
    if (index < 0) {
        index = 0;
    }
    if (index > static_cast<double>(size)) {
        index = static_cast<double>(size);
    }
    *out = static_cast<std::size_t>(index);
    return true;
}

void blob_slice(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::vector<std::uint8_t> bytes;
    if (!blob_bytes_or_throw(isolate, context, info.This(), &bytes)) {
        return;
    }
    std::size_t start = 0;
    std::size_t end = bytes.size();
    if (!blob_index_arg(context, info[0], bytes.size(), 0, &start) ||
        !blob_index_arg(context, info[1], bytes.size(), bytes.size(), &end)) {
        return;
    }
    if (end < start) {
        end = start;
    }
    std::string type;
    if (info.Length() > 2 && !info[2]->IsUndefined()) {
        v8::Local<v8::String> str;
        if (!info[2]->ToString(context).ToLocal(&str)) {
            return;
        }
        v8::String::Utf8Value utf8(isolate, str);
        if (*utf8 != nullptr) {
            type = ascii_lower(
                std::string(*utf8, static_cast<std::size_t>(utf8.length())));
        }
    }
    DocumentHost* document = document_from_blob_data(info.Data());
    if (document == nullptr) {
        throw_blob_type_error(isolate, "Blob.slice: invalid receiver");
        return;
    }
    v8::Local<v8::Function> constructor = document->blob_constructor(isolate);
    if (constructor.IsEmpty()) {
        throw_blob_type_error(isolate, "Blob.slice: Blob is unavailable");
        return;
    }
    v8::Local<v8::ArrayBuffer> buffer =
        v8::ArrayBuffer::New(isolate, end - start);
    if (end > start) {
        std::memcpy(buffer->Data(), bytes.data() + start, end - start);
    }
    v8::Local<v8::Array> parts = v8::Array::New(isolate, 1);
    (void)parts->Set(context, 0, buffer);
    v8::Local<v8::Object> options = v8::Object::New(isolate);
    (void)options->Set(context, v8_string(isolate, "type"),
                       v8_string(isolate, type));
    v8::Local<v8::Value> argv[2] = {parts, options};
    v8::Local<v8::Object> result;
    if (constructor->NewInstance(context, 2, argv).ToLocal(&result)) {
        info.GetReturnValue().Set(result);
    }
}

void blob_text(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::vector<std::uint8_t> bytes;
    if (!blob_bytes_or_throw(isolate, context, info.This(), &bytes)) {
        return;
    }
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        return;
    }
    v8::Local<v8::String> text = v8::String::Empty(isolate);
    if (!bytes.empty() &&
        !v8::String::NewFromUtf8(isolate,
                                 reinterpret_cast<const char*>(bytes.data()),
                                 v8::NewStringType::kNormal,
                                 static_cast<int>(bytes.size()))
             .ToLocal(&text)) {
        return;
    }
    (void)resolver->Resolve(context, text);
    info.GetReturnValue().Set(resolver->GetPromise());
}

void blob_array_buffer(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::vector<std::uint8_t> bytes;
    if (!blob_bytes_or_throw(isolate, context, info.This(), &bytes)) {
        return;
    }
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        return;
    }
    v8::Local<v8::ArrayBuffer> buffer =
        v8::ArrayBuffer::New(isolate, bytes.size());
    if (!bytes.empty()) {
        std::memcpy(buffer->Data(), bytes.data(), bytes.size());
    }
    (void)resolver->Resolve(context, buffer);
    info.GetReturnValue().Set(resolver->GetPromise());
}

FileReaderState* file_reader_state_from(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!info.This()->IsObject()) {
        return nullptr;
    }
    v8::Local<v8::Value> value;
    if (!info.This()
             ->GetPrivate(context, file_reader_state_key(isolate))
             .ToLocal(&value) ||
        !value->IsExternal()) {
        return nullptr;
    }
    return static_cast<FileReaderState*>(
        value.As<v8::External>()->Value(kFileReaderTag));
}

FileReaderState* file_reader_state_or_throw(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    FileReaderState* state = file_reader_state_from(info);
    if (state == nullptr) {
        throw_blob_type_error(info.GetIsolate(), "Illegal invocation");
    }
    return state;
}

void file_reader_delivery(const v8::FunctionCallbackInfo<v8::Value>& info);

void file_reader_schedule(v8::Isolate* isolate,
                          v8::Local<v8::Context> context,
                          FileReaderState* state) {
    v8::Local<v8::Function> delivery;
    if (!v8::Function::New(context, &file_reader_delivery,
                           v8::External::New(isolate, state, kFileReaderTag))
             .ToLocal(&delivery)) {
        return;
    }
    isolate->EnqueueMicrotask(delivery);
}

void file_reader_delivery(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!info.Data()->IsExternal()) {
        return;
    }
    auto* state = static_cast<FileReaderState*>(
        info.Data().As<v8::External>()->Value(kFileReaderTag));
    if (state == nullptr || !state->alive || !state->pending ||
        state->self.IsEmpty()) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Object> self = state->self.Get(isolate);
    state->pending = false;
    (void)self->Set(context, v8_string(isolate, "readyState"),
                    v8::Integer::New(isolate, 2));
    if (state->mode == FileReaderState::Mode::Text) {
        v8::Local<v8::Value> result = v8::String::Empty(isolate);
        if (!state->bytes.empty()) {
            v8::Local<v8::String> text;
            if (v8::String::NewFromUtf8(
                    isolate,
                    reinterpret_cast<const char*>(state->bytes.data()),
                    v8::NewStringType::kNormal,
                    static_cast<int>(state->bytes.size()))
                    .ToLocal(&text)) {
                result = text;
            }
        }
        (void)self->Set(context, v8_string(isolate, "result"), result);
    } else {
        v8::Local<v8::ArrayBuffer> buffer =
            v8::ArrayBuffer::New(isolate, state->bytes.size());
        if (!state->bytes.empty()) {
            std::memcpy(buffer->Data(), state->bytes.data(),
                        state->bytes.size());
        }
        (void)self->Set(context, v8_string(isolate, "result"), buffer);
    }
    state->mode = FileReaderState::Mode::None;
    state->bytes.clear();
    state->events.dispatch_native(isolate, context, "load", self);
    state->events.dispatch_native(isolate, context, "loadend", self);
}

void file_reader_begin(const v8::FunctionCallbackInfo<v8::Value>& info,
                       bool as_text) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    FileReaderState* state = file_reader_state_or_throw(info);
    if (state == nullptr) {
        return;
    }
    if (state->pending) {
        throw_blob_type_error(isolate, "FileReader is already reading");
        return;
    }
    std::vector<std::uint8_t> bytes;
    if (info.Length() < 1 ||
        !blob_read_bytes(isolate, context, info[0], &bytes)) {
        throw_blob_type_error(isolate,
                              "FileReader: argument must be a Blob");
        return;
    }
    state->bytes = std::move(bytes);
    state->mode = as_text ? FileReaderState::Mode::Text
                          : FileReaderState::Mode::ArrayBuffer;
    state->pending = true;
    v8::Local<v8::Object> self = info.This();
    (void)self->Set(context, v8_string(isolate, "readyState"),
                    v8::Integer::New(isolate, 1));
    (void)self->Set(context, v8_string(isolate, "result"), v8::Null(isolate));
    state->events.dispatch_native(isolate, context, "loadstart", self);
    file_reader_schedule(isolate, context, state);
}

void file_reader_read_as_text(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    file_reader_begin(info, true);
}

void file_reader_read_as_array_buffer(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    file_reader_begin(info, false);
}

void file_reader_abort(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    FileReaderState* state = file_reader_state_or_throw(info);
    if (state == nullptr || !state->pending) {
        return;
    }
    state->pending = false;
    state->mode = FileReaderState::Mode::None;
    state->bytes.clear();
    v8::Local<v8::Object> self = info.This();
    (void)self->Set(context, v8_string(isolate, "readyState"),
                    v8::Integer::New(isolate, 2));
    (void)self->Set(context, v8_string(isolate, "result"), v8::Null(isolate));
    state->events.dispatch_native(isolate, context, "abort", self);
    state->events.dispatch_native(isolate, context, "loadend", self);
}

void file_reader_event_method(const v8::FunctionCallbackInfo<v8::Value>& info,
                              const char* name) {
    FileReaderState* state = file_reader_state_or_throw(info);
    if (state == nullptr) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> out;
    if (state->events.handle_event_method(isolate, context, name, info, out)) {
        info.GetReturnValue().Set(out);
    }
}

void file_reader_add_event_listener(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    file_reader_event_method(info, "addEventListener");
}

void file_reader_remove_event_listener(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    file_reader_event_method(info, "removeEventListener");
}

void file_reader_dispatch_event(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    file_reader_event_method(info, "dispatchEvent");
}

void file_reader_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!event_construct_check(info, "FileReader")) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    DocumentHost* document = document_from_blob_data(info.Data());
    if (document == nullptr) {
        throw_blob_type_error(isolate, "FileReader: invalid constructor");
        return;
    }
    FileReaderState* state =
        document->create_file_reader(isolate, info.This());
    (void)info.This()->SetPrivate(
        context, file_reader_state_key(isolate),
        v8::External::New(isolate, state, kFileReaderTag));
    (void)info.This()->Set(context, v8_string(isolate, "readyState"),
                           v8::Integer::New(isolate, 0));
    (void)info.This()->Set(context, v8_string(isolate, "result"),
                           v8::Null(isolate));
    (void)info.This()->Set(context, v8_string(isolate, "error"),
                           v8::Null(isolate));
}

}  // namespace iv8::browser
