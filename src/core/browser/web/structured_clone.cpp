#include "structured_clone.h"

#include <cstring>
#include <unordered_set>

#include "iv8/query_parameters.h"
#include "iv8/url_parser.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {

// --- M11-38 structuredClone -------------------------------------------------
//
// A bounded structured clone: primitives, arrays/plain objects, Date, Map, Set,
// ArrayBuffer, TypedArrays, and DataView, with cycle/identity preservation.
// Functions, symbols, promises, host objects, RegExp/Error and other exotic
// objects are not supported and throw a JS TypeError (the project has no
// DOMException DataCloneError). Transfer lists are not supported.
v8::MaybeLocal<v8::Value> structured_clone_value(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> value, v8::Local<v8::Map> seen, int depth) {
    if (depth > 512) {
        isolate->ThrowException(v8::Exception::RangeError(
            v8::String::NewFromUtf8Literal(
                isolate, "structuredClone: nesting is too deep")));
        return {};
    }
    if (value->IsSymbol() || value->IsSymbolObject() ||
        value->IsFunction() || value->IsPromise() || value->IsWeakMap() ||
        value->IsWeakSet() || value->IsRegExp() || value->IsNativeError() ||
        value->IsSharedArrayBuffer() || value->IsProxy()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(
                isolate, "structuredClone: value cannot be cloned")));
        return {};
    }
    if (!value->IsObject()) {
        return value;  // primitives (including BigInt) are returned as-is
    }
    if (value->SameValue(context->Global())) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(
                isolate, "structuredClone: the global object cannot be cloned")));
        return {};
    }
    if (host_object_backing(value) != nullptr) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(
                isolate, "structuredClone: host objects cannot be cloned")));
        return {};
    }
    v8::Local<v8::Value> existing;
    if (seen->Get(context, value).ToLocal(&existing) && !existing->IsUndefined()) {
        return existing;
    }
    const auto remember = [&](v8::Local<v8::Value> clone) {
        if (seen->Set(context, value, clone).IsEmpty()) {
            return false;
        }
        return true;
    };

    if (value->IsDate()) {
        v8::Local<v8::Value> clone;
        if (!v8::Date::New(context, value.As<v8::Date>()->ValueOf())
                 .ToLocal(&clone)) {
            return {};
        }
        if (!remember(clone)) {
            return {};
        }
        return clone;
    }
    if (value->IsBooleanObject()) {
        v8::Local<v8::Value> clone = v8::BooleanObject::New(
            isolate, value.As<v8::BooleanObject>()->ValueOf());
        if (!remember(clone)) {
            return {};
        }
        return clone;
    }
    if (value->IsNumberObject()) {
        v8::Local<v8::Value> clone = v8::NumberObject::New(
            isolate, value.As<v8::NumberObject>()->ValueOf());
        if (!remember(clone)) {
            return {};
        }
        return clone;
    }
    if (value->IsStringObject()) {
        v8::Local<v8::Value> clone = v8::StringObject::New(
            isolate, value.As<v8::StringObject>()->ValueOf());
        if (!remember(clone)) {
            return {};
        }
        return clone;
    }
    if (value->IsBigIntObject()) {
        // v8::BigIntObject::New only accepts int64_t; boxing the original
        // primitive preserves the full BigInt value.
        v8::Local<v8::Value> primitive =
            value.As<v8::BigIntObject>()->ValueOf();
        v8::Local<v8::Object> clone;
        if (!primitive->ToObject(context).ToLocal(&clone)) {
            return {};
        }
        if (!remember(clone)) {
            return {};
        }
        return clone;
    }
    if (value->IsArrayBuffer()) {
        v8::Local<v8::ArrayBuffer> source = value.As<v8::ArrayBuffer>();
        if (source->WasDetached()) {
            isolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8Literal(
                    isolate, "structuredClone: detached buffer")));
            return {};
        }
        const std::size_t length = source->ByteLength();
        v8::Local<v8::ArrayBuffer> clone = v8::ArrayBuffer::New(isolate, length);
        if (length > 0) {
            std::memcpy(clone->Data(), source->Data(), length);
        }
        if (!remember(clone)) {
            return {};
        }
        return clone;
    }
    if (value->IsTypedArray() || value->IsDataView()) {
        v8::Local<v8::ArrayBufferView> view = value.As<v8::ArrayBufferView>();
        // Clone the whole backing buffer through `seen` so multiple views (and
        // a direct reference to the buffer) share one cloned buffer, and keep
        // the original byte offset/length of the view.
        v8::Local<v8::Value> buffer_value;
        if (!structured_clone_value(isolate, context, view->Buffer(), seen,
                                    depth + 1)
                 .ToLocal(&buffer_value)) {
            return {};
        }
        v8::Local<v8::ArrayBuffer> buffer = buffer_value.As<v8::ArrayBuffer>();
        const std::size_t byte_offset = view->ByteOffset();
        const std::size_t byte_length = view->ByteLength();
        v8::Local<v8::Value> clone;
        if (value->IsDataView()) {
            clone = v8::DataView::New(buffer, byte_offset, byte_length);
        } else {
            v8::Local<v8::TypedArray> typed = value.As<v8::TypedArray>();
            const std::size_t count = typed->Length();
            if (value->IsUint8Array()) {
                clone = v8::Uint8Array::New(buffer, byte_offset, count);
            } else if (value->IsUint8ClampedArray()) {
                clone = v8::Uint8ClampedArray::New(buffer, byte_offset, count);
            } else if (value->IsInt8Array()) {
                clone = v8::Int8Array::New(buffer, byte_offset, count);
            } else if (value->IsUint16Array()) {
                clone = v8::Uint16Array::New(buffer, byte_offset, count);
            } else if (value->IsInt16Array()) {
                clone = v8::Int16Array::New(buffer, byte_offset, count);
            } else if (value->IsUint32Array()) {
                clone = v8::Uint32Array::New(buffer, byte_offset, count);
            } else if (value->IsInt32Array()) {
                clone = v8::Int32Array::New(buffer, byte_offset, count);
            } else if (value->IsFloat16Array()) {
                clone = v8::Float16Array::New(buffer, byte_offset, count);
            } else if (value->IsFloat32Array()) {
                clone = v8::Float32Array::New(buffer, byte_offset, count);
            } else if (value->IsFloat64Array()) {
                clone = v8::Float64Array::New(buffer, byte_offset, count);
            } else if (value->IsBigInt64Array()) {
                clone = v8::BigInt64Array::New(buffer, byte_offset, count);
            } else if (value->IsBigUint64Array()) {
                clone = v8::BigUint64Array::New(buffer, byte_offset, count);
            } else {
                isolate->ThrowException(v8::Exception::TypeError(
                    v8::String::NewFromUtf8Literal(
                        isolate, "structuredClone: unsupported typed array")));
                return {};
            }
        }
        if (!remember(clone)) {
            return {};
        }
        return clone;
    }
    if (value->IsMap()) {
        v8::Local<v8::Map> source = value.As<v8::Map>();
        v8::Local<v8::Map> clone = v8::Map::New(isolate);
        if (!remember(clone)) {
            return {};
        }
        v8::Local<v8::Array> pairs = source->AsArray();
        for (std::uint32_t i = 0; i < pairs->Length(); i += 2) {
            v8::Local<v8::Value> key;
            v8::Local<v8::Value> item;
            if (!pairs->Get(context, i).ToLocal(&key) ||
                !pairs->Get(context, i + 1).ToLocal(&item)) {
                return {};
            }
            v8::Local<v8::Value> key_clone;
            v8::Local<v8::Value> item_clone;
            if (!structured_clone_value(isolate, context, key, seen, depth + 1)
                     .ToLocal(&key_clone) ||
                !structured_clone_value(isolate, context, item, seen, depth + 1)
                     .ToLocal(&item_clone)) {
                return {};
            }
            if (clone->Set(context, key_clone, item_clone).IsEmpty()) {
                return {};
            }
        }
        return clone;
    }
    if (value->IsSet()) {
        v8::Local<v8::Set> source = value.As<v8::Set>();
        v8::Local<v8::Set> clone = v8::Set::New(isolate);
        if (!remember(clone)) {
            return {};
        }
        v8::Local<v8::Array> items = source->AsArray();
        for (std::uint32_t i = 0; i < items->Length(); i++) {
            v8::Local<v8::Value> item;
            if (!items->Get(context, i).ToLocal(&item)) {
                return {};
            }
            v8::Local<v8::Value> item_clone;
            if (!structured_clone_value(isolate, context, item, seen, depth + 1)
                     .ToLocal(&item_clone)) {
                return {};
            }
            if (clone->Add(context, item_clone).IsEmpty()) {
                return {};
            }
        }
        return clone;
    }
    v8::Local<v8::Object> source = value.As<v8::Object>();
    v8::Local<v8::Object> clone;
    if (value->IsArray()) {
        clone = v8::Array::New(isolate, value.As<v8::Array>()->Length());
    } else {
        clone = v8::Object::New(isolate);
    }
    if (!remember(clone)) {
        return {};
    }
    v8::Local<v8::Array> keys;
    if (!source
             ->GetOwnPropertyNames(
                 context, v8::PropertyFilter::ONLY_ENUMERABLE,
                 v8::KeyConversionMode::kKeepNumbers)
             .ToLocal(&keys)) {
        return {};
    }
    for (std::uint32_t i = 0; i < keys->Length(); i++) {
        v8::Local<v8::Value> key;
        v8::Local<v8::Value> item;
        if (!keys->Get(context, i).ToLocal(&key) ||
            !source->Get(context, key).ToLocal(&item)) {
            return {};
        }
        v8::Local<v8::Value> item_clone;
        if (!structured_clone_value(isolate, context, item, seen, depth + 1)
                 .ToLocal(&item_clone)) {
            return {};
        }
        if (clone->Set(context, key, item_clone).IsEmpty()) {
            return {};
        }
    }
    return clone;
}

void structured_clone_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (info.Length() < 1) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(
                isolate, "structuredClone: value is required")));
        return;
    }
    v8::Local<v8::Map> seen = v8::Map::New(isolate);
    v8::Local<v8::Value> clone;
    if (!structured_clone_value(isolate, context, info[0], seen, 0)
             .ToLocal(&clone)) {
        return;
    }
    info.GetReturnValue().Set(clone);
}

}  // namespace iv8::browser
