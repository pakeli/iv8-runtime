#pragma once

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

}  // namespace iv8::browser
