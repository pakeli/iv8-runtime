#include "iv8/context_host.h"
#include "crypto.h"

#include <cstddef>
#include <cstdint>
#include <random>

#if defined(_WIN32)
// advapi32 (linked for V8 builds) exposes the OS RNG as SystemFunction036
// (RtlGenRandom). Declaring it directly avoids pulling windows.h into this TU.
extern "C" __declspec(dllimport) unsigned char __stdcall SystemFunction036(
    void* random_buffer, unsigned long random_buffer_length);
#elif defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <sys/random.h>
#include <unistd.h>
#endif

#include "iv8/access_summary.h"
#include "../internal/common.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {
namespace {

// M11-33: OS-backed entropy for crypto.getRandomValues() / randomUUID(). This is
// deliberately the runtime's ONE non-deterministic surface: supported builds use
// the platform CSPRNG (advapi32 SystemFunction036 on Windows; getrandom with a
// /dev/urandom fallback on Linux; std::random_device elsewhere). It never falls
// back to a deterministic PRNG.
bool fill_os_random_bytes(std::uint8_t* out, std::size_t length) {
#if defined(_WIN32)
    if (length == 0) {
        return true;
    }
    return SystemFunction036(out, static_cast<unsigned long>(length)) != 0;
#elif defined(__linux__)
    std::size_t done = 0;
    while (done < length) {
        const ssize_t n = ::getrandom(out + done, length - done, 0);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }
        done += static_cast<std::size_t>(n);
    }
    if (done == length) {
        return true;
    }
    const int fd = ::open("/dev/urandom", O_RDONLY);
    if (fd < 0) {
        return false;
    }
    while (done < length) {
        const ssize_t n = ::read(fd, out + done, length - done);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            ::close(fd);
            return false;
        }
        if (n == 0) {
            ::close(fd);
            return false;
        }
        done += static_cast<std::size_t>(n);
    }
    ::close(fd);
    return true;
#else
    try {
        std::random_device random_device;
        for (std::size_t i = 0; i < length; i++) {
            out[i] = static_cast<std::uint8_t>(random_device() & 0xFFu);
        }
        return true;
    } catch (...) {
        return false;
    }
#endif
}

// --- M11-33 crypto.getRandomValues / crypto.randomUUID -----------------------
//
// A deliberately small Crypto slice: only OS-backed random bytes and UUID v4.
// getRandomValues follows the browser typed-array contract (integer TypedArrays
// only, 65536-byte quota, returns the SAME view, fills the view's byte range).
// There is no crypto.subtle surface yet. The OS entropy source is the runtime's
// single non-deterministic API; see fill_os_random_bytes().
bool is_integer_typed_array(v8::Local<v8::Value> value) {
    return value->IsInt8Array() || value->IsUint8Array() ||
           value->IsUint8ClampedArray() || value->IsInt16Array() ||
           value->IsUint16Array() || value->IsInt32Array() ||
           value->IsUint32Array() || value->IsBigInt64Array() ||
           value->IsBigUint64Array();
}

}  // namespace

void crypto_get_random_values_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "crypto", "getRandomValues");
    const std::uint64_t seq = access_trace_record(
        info.GetIsolate(), &kCryptoTraceToken, "crypto", "crypto", "getRandomValues",
        AccessOperation::kCall);
    AccessCallCompletion completion(info.GetIsolate(), seq, info);
    v8::Isolate* isolate = info.GetIsolate();
    if (info.Length() < 1 || !is_integer_typed_array(info[0])) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(
                isolate,
                "getRandomValues: argument must be an integer TypedArray")));
        return;
    }
    v8::Local<v8::ArrayBufferView> view =
        info[0].As<v8::ArrayBufferView>();
    v8::Local<v8::ArrayBuffer> buffer = view->Buffer();
    if (buffer->WasDetached()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(
                isolate, "getRandomValues: buffer is detached")));
        return;
    }
    const std::size_t length = view->ByteLength();
    if (length > 65536) {
        // The WHATWG API uses a DOMException (QuotaExceededError); this runtime
        // has no DOMException, so the deterministic substitute is RangeError.
        isolate->ThrowException(v8::Exception::RangeError(
            v8::String::NewFromUtf8Literal(
                isolate,
                "getRandomValues: requested length exceeds 65536 bytes")));
        return;
    }
    if (length == 0) {
        info.GetReturnValue().Set(info[0]);
        return;
    }
    auto* data = static_cast<std::uint8_t*>(buffer->Data());
    if (data == nullptr) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(
                isolate, "getRandomValues: buffer is detached")));
        return;
    }
    if (!fill_os_random_bytes(data + view->ByteOffset(), length)) {
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8Literal(
                isolate, "getRandomValues: OS random source failed")));
        return;
    }
    info.GetReturnValue().Set(info[0]);
}

void crypto_random_uuid_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "crypto", "randomUUID");
    const std::uint64_t seq = access_trace_record(
        info.GetIsolate(), &kCryptoTraceToken, "crypto", "crypto", "randomUUID",
        AccessOperation::kCall);
    AccessCallCompletion completion(info.GetIsolate(), seq, info);
    v8::Isolate* isolate = info.GetIsolate();
    std::uint8_t bytes[16];
    if (!fill_os_random_bytes(bytes, sizeof(bytes))) {
        isolate->ThrowException(v8::Exception::Error(
            v8::String::NewFromUtf8Literal(
                isolate, "randomUUID: OS random source failed")));
        return;
    }
    bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);  // v4
    bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);  // RFC 4122
    static const char kHex[] = "0123456789abcdef";
    char text[36];
    std::size_t out = 0;
    for (std::size_t i = 0; i < sizeof(bytes); i++) {
        if (i == 4 || i == 6 || i == 8 || i == 10) {
            text[out++] = '-';
        }
        text[out++] = kHex[bytes[i] >> 4];
        text[out++] = kHex[bytes[i] & 0x0F];
    }
    info.GetReturnValue().Set(
        v8::String::NewFromUtf8(isolate, text, v8::NewStringType::kNormal, 36)
            .ToLocalChecked());
}

}  // namespace iv8::browser
