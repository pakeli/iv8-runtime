#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "iv8/v8_headers.h"

namespace iv8::browser {

extern const v8::ExternalPointerTypeTag kBlobTag;

extern const v8::ExternalPointerTypeTag kFileReaderTag;

extern void blob_constructor(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void file_constructor(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void blob_slice(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void blob_text(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void blob_array_buffer(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void file_reader_read_as_text(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void file_reader_read_as_array_buffer(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void file_reader_abort(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void file_reader_add_event_listener(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void file_reader_remove_event_listener(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void file_reader_dispatch_event(const v8::FunctionCallbackInfo<v8::Value>& info);

extern void file_reader_constructor(const v8::FunctionCallbackInfo<v8::Value>& info);

bool is_file_object(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    v8::Local<v8::Value> value);

bool is_blob_object(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    v8::Local<v8::Value> value);

void register_runtime_blob_constructor(v8::Isolate* isolate,
                                       v8::Local<v8::Context> context,
                                       v8::Local<v8::Function> constructor);

v8::MaybeLocal<v8::Object> create_runtime_blob(v8::Isolate* isolate,
                                               v8::Local<v8::Context> context,
                                               v8::Local<v8::String> text,
                                               const std::string& type);

// Capture the original generation-local File prototype during bootstrap. The
// internal factory never consults mutable globals or invokes JS constructors.
bool register_runtime_file_prototype(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> prototype);

// Create an independent immutable byte snapshot using the existing native
// Blob/File brands, readers and monitored fields. Filename code units are kept
// intact; no UTF-8 round trip or user-defined coercion is performed.
v8::MaybeLocal<v8::Object> create_runtime_file(v8::Isolate* isolate,
                                              v8::Local<v8::Context> context,
                                              const std::vector<std::uint8_t>& bytes,
                                              const std::u16string& name,
                                              const std::string& type,
                                              double last_modified);

}  // namespace iv8::browser
