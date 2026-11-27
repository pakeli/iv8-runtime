#include "crypto.h"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <vector>

#if defined(_WIN32)
extern "C" __declspec(dllimport) unsigned char __stdcall SystemFunction036(
    void* random_buffer, unsigned long random_buffer_length);
#elif defined(__linux__)
#include <cerrno>
#include <fcntl.h>
#include <sys/random.h>
#include <unistd.h>
#endif

#include "iv8/access_origin.h"
#include "iv8/access_summary.h"
#include "iv8/host_object.h"
#include "iv8/page_state.h"
#include "iv8/web_interface.h"
#include "iv8/web_method.h"
#include "../internal/common.h"
#include "../internal/trace_hooks.h"
#include "../window/window_trace_support.h"
#include "digest_backend.h"
#include "dom_exception.h"

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

bool crypto_type_error(v8::Isolate* isolate, const std::string& message) {
    isolate->ThrowException(v8::Exception::TypeError(v8_string(isolate, message).As<v8::String>()));
    return false;
}

v8::Local<v8::Value> reject_crypto_exception(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::TryCatch& caught) {
    if (!caught.CanContinue()) {
        caught.ReThrow();
        return v8::Undefined(isolate);
    }
    auto exception = caught.Exception();
    caught.Reset();
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver) ||
        !resolver->Reject(context, exception).FromMaybe(false)) {
        return v8::Undefined(isolate);
    }
    return resolver->GetPromise();
}

bool algorithm_to_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, const std::string& prefix,
    v8::Local<v8::String>* output) {
    if (input->IsSymbol()) {
        return crypto_type_error(isolate, prefix + "Cannot convert a Symbol value to a string");
    }
    return access_user_operation(isolate, [&] {
        return input->ToString(context);
    }).ToLocal(output);
}

bool normalize_digest_algorithm(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    v8::Local<v8::Value> algorithm, const std::string& prefix,
    std::string* output) {
    v8::Local<v8::Value> name = algorithm;
    if (algorithm->IsObject()) {
        if (!access_user_operation(isolate, [&] {
                return algorithm.As<v8::Object>()->Get(context,
                    v8::String::NewFromUtf8Literal(isolate, "name"));
            }).ToLocal(&name)) return false;
        if (name->IsUndefined()) {
            return crypto_type_error(isolate,
                prefix + "Algorithm: name: Missing or not a string");
        }
    }
    v8::Local<v8::String> text;
    if (!algorithm_to_string(isolate, context, name, prefix, &text)) return false;
    v8::String::Utf8Value utf8(isolate, text);
    std::string normalized(*utf8 != nullptr ? *utf8 : "",
        *utf8 != nullptr ? static_cast<std::size_t>(utf8.length()) : 0);
    for (char& character : normalized) {
        if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
    }
    if (normalized == "SHA-1" || normalized == "SHA-256" ||
        normalized == "SHA-384" || normalized == "SHA-512") {
        *output = std::move(normalized);
        return true;
    }
    // Names recognized by Chrome's WebCrypto registry, but not for digest.
    static const std::pair<const char*, const char*> other_algorithms[] = {
        {"AES-CBC", "AES-CBC"}, {"AES-CTR", "AES-CTR"}, {"AES-GCM", "AES-GCM"},
        {"AES-KW", "AES-KW"}, {"HMAC", "HMAC"},
        {"RSASSA-PKCS1-V1_5", "RSASSA-PKCS1-v1_5"}, {"RSA-PSS", "RSA-PSS"},
        {"RSA-OAEP", "RSA-OAEP"}, {"ECDSA", "ECDSA"}, {"ECDH", "ECDH"},
        {"HKDF", "HKDF"}, {"PBKDF2", "PBKDF2"},
        {"ED25519", "Ed25519"}, {"X25519", "X25519"},
        {"ML-KEM-768", "ML-KEM-768"}, {"ML-KEM-1024", "ML-KEM-1024"},
        {"ML-DSA-44", "ML-DSA-44"}, {"ML-DSA-65", "ML-DSA-65"},
        {"ML-DSA-87", "ML-DSA-87"}, {"CHACHA20-POLY1305", "ChaCha20-Poly1305"},
    };
    std::string detail = "Algorithm: Unrecognized name";
    for (const auto& [candidate, canonical] : other_algorithms) {
        if (normalized == candidate) {
            detail = std::string(canonical) + ": Unsupported operation: digest";
            break;
        }
    }
    isolate->ThrowException(make_dom_exception(isolate, context, owner,
        "NotSupportedError", prefix + detail));
    return false;
}

class SubtleCryptoHost final : public HostObject {
public:
    explicit SubtleCryptoHost(PageState* owner) : owner_(owner) {}
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "SubtleCrypto"; }
    std::string trace_object_type() const override { return "crypto"; }
    std::string trace_label() const override { return "crypto.subtle"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override {
        return {"decrypt", "deriveBits", "deriveKey", "digest", "encrypt",
            "exportKey", "generateKey", "importKey", "sign", "unwrapKey",
            "verify", "wrapKey", "decapsulateBits", "decapsulateKey",
            "encapsulateBits", "encapsulateKey", "getPublicKey"};
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&) override {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override {
        watch_record(isolate, "crypto.subtle", name);
        const std::string prefix = "Failed to execute '" + name + "' on 'SubtleCrypto': ";
        v8::TryCatch caught(isolate);
        const int required = web_method_length("SubtleCrypto", name);
        if (arguments.Length() < required) {
            crypto_type_error(isolate, prefix + std::to_string(required) +
                " arguments required, but only " + std::to_string(arguments.Length()) + " present.");
            return reject_crypto_exception(isolate, context, caught);
        }
        if (name != "digest") {
            isolate->ThrowException(make_dom_exception(isolate, context, owner_,
                "NotSupportedError", prefix + "This operation is not supported by the offline digest profile."));
            return reject_crypto_exception(isolate, context, caught);
        }

        // AlgorithmIdentifier's primitive conversion precedes BufferSource
        // validation; dictionary normalization runs only after that validation.
        v8::Local<v8::Value> algorithm = arguments[0];
        if (!algorithm->IsObject()) {
            v8::Local<v8::String> text;
            if (!algorithm_to_string(isolate, context, algorithm, prefix, &text))
                return reject_crypto_exception(isolate, context, caught);
            algorithm = text;
        }
        auto data = arguments[1];
        if (!data->IsArrayBuffer() && !data->IsArrayBufferView()) {
            crypto_type_error(isolate, prefix +
                "The provided value is not of type '(ArrayBuffer or ArrayBufferView)'.");
            return reject_crypto_exception(isolate, context, caught);
        }
        v8::Local<v8::ArrayBuffer> buffer = data->IsArrayBuffer()
            ? data.As<v8::ArrayBuffer>() : data.As<v8::ArrayBufferView>()->Buffer();
        if (buffer->IsSharedArrayBuffer()) {
            crypto_type_error(isolate, prefix + "The provided ArrayBufferView value must not be shared.");
            return reject_crypto_exception(isolate, context, caught);
        }
        if (buffer->IsResizableByUserJavaScript()) {
            crypto_type_error(isolate, prefix + "The provided " +
                (data->IsArrayBuffer() ? "ArrayBuffer" : "ArrayBufferView") +
                " value must not be resizable.");
            return reject_crypto_exception(isolate, context, caught);
        }
        std::string canonical;
        if (!normalize_digest_algorithm(isolate, context, owner_, algorithm, prefix, &canonical))
            return reject_crypto_exception(isolate, context, caught);

        // Copy after algorithm getters, but before returning the Promise.
        // Detached BufferSources intentionally hash as empty (Chrome 158).
        const std::size_t length = buffer->WasDetached() ? 0 :
            (data->IsArrayBuffer() ? buffer->ByteLength() : data.As<v8::ArrayBufferView>()->ByteLength());
        const std::size_t offset = data->IsArrayBufferView()
            ? data.As<v8::ArrayBufferView>()->ByteOffset() : 0;
        try {
            std::vector<std::uint8_t> input(length);
            if (length != 0) {
                const auto* source = static_cast<const std::uint8_t*>(buffer->Data());
                std::memcpy(input.data(), source + offset, length);
            }
            const auto digest = digest_bytes(canonical, input.data(), input.size());
            auto result = v8::ArrayBuffer::New(isolate, digest.size());
            if (!digest.empty()) std::memcpy(result->Data(), digest.data(), digest.size());
            v8::Local<v8::Promise::Resolver> resolver;
            if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
                if (caught.HasCaught()) return reject_crypto_exception(isolate, context, caught);
                return v8::Undefined(isolate);
            }
            if (!access_user_operation(isolate, [&] {
                    return resolver->Resolve(context, result);
                }).FromMaybe(false)) {
                if (caught.HasCaught()) return reject_crypto_exception(isolate, context, caught);
                return v8::Undefined(isolate);
            }
            return resolver->GetPromise();
        } catch (const std::exception&) {
            isolate->ThrowException(make_dom_exception(isolate, context, owner_,
                "OperationError", prefix + "Digest operation failed."));
            return reject_crypto_exception(isolate, context, caught);
        }
    }
private:
    PageState* owner_;
};

class CryptoHost final : public HostObject {
public:
    CryptoHost(PageState* owner, bool secure) : owner_(owner), secure_(secure) {}
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "Crypto"; }
    std::string trace_object_type() const override { return "crypto"; }
    std::string trace_label() const override { return "crypto"; }
    std::vector<std::string> property_names() const override {
        return secure_ ? std::vector<std::string>{"subtle"} : std::vector<std::string>{};
    }
    std::vector<std::string> method_names() const override {
        return secure_ ? std::vector<std::string>{"getRandomValues", "randomUUID"}
                       : std::vector<std::string>{"getRandomValues"};
    }
    void release_v8_handles() noexcept override { subtle_.Reset(); }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override {
        if (name != "subtle" || !secure_) return v8::Undefined(isolate);
        if (subtle_.IsEmpty()) {
            auto object = owner_->adopt_host_object(isolate, context,
                std::make_unique<SubtleCryptoHost>(owner_));
            subtle_.Reset(isolate, object);
        }
        return subtle_.Get(isolate);
    }
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override {
        watch_record(isolate, "crypto", name);
        if (name == "randomUUID") {
            std::uint8_t bytes[16];
            if (!fill_os_random_bytes(bytes, sizeof(bytes))) {
                isolate->ThrowException(v8::Exception::Error(
                    v8_string(isolate, "randomUUID: OS random source failed").As<v8::String>()));
                return v8::Undefined(isolate);
            }
            bytes[6] = static_cast<std::uint8_t>((bytes[6] & 0x0F) | 0x40);
            bytes[8] = static_cast<std::uint8_t>((bytes[8] & 0x3F) | 0x80);
            static const char hex[] = "0123456789abcdef";
            std::string text;
            text.reserve(36);
            for (std::size_t index = 0; index < sizeof(bytes); ++index) {
                if (index == 4 || index == 6 || index == 8 || index == 10) text += '-';
                text += hex[bytes[index] >> 4];
                text += hex[bytes[index] & 0x0F];
            }
            return v8_string(isolate, text);
        }
        if (name != "getRandomValues") return v8::Undefined(isolate);
        const std::string prefix = "Failed to execute 'getRandomValues' on 'Crypto': ";
        if (arguments.Length() < 1) {
            crypto_type_error(isolate, prefix + "1 argument required, but only 0 present.");
            return v8::Undefined(isolate);
        }
        if (!arguments[0]->IsArrayBufferView()) {
            crypto_type_error(isolate, prefix + "parameter 1 is not of type 'ArrayBufferView'.");
            return v8::Undefined(isolate);
        }
        auto value = arguments[0];
        const char* noninteger = value->IsFloat16Array() ? "Float16" :
            value->IsFloat32Array() ? "Float32" : value->IsFloat64Array() ? "Float64" :
            value->IsDataView() ? "DataView" : nullptr;
        if (noninteger != nullptr) {
            isolate->ThrowException(make_dom_exception(isolate, context, owner_,
                "TypeMismatchError", prefix + "The provided ArrayBufferView is of type '" +
                noninteger + "', which is not an integer array type."));
            return v8::Undefined(isolate);
        }
        auto view = value.As<v8::ArrayBufferView>();
        auto buffer = view->Buffer();
        const std::size_t length = view->ByteLength();
        if (length > 65536) {
            isolate->ThrowException(make_dom_exception(isolate, context, owner_,
                "QuotaExceededError", prefix + "The ArrayBufferView's byte length (" +
                std::to_string(length) + ") exceeds the number of bytes of entropy available via this API (65536)."));
            return v8::Undefined(isolate);
        }
        if (length == 0) return value;
        auto* bytes = static_cast<std::uint8_t*>(buffer->Data());
        if (!fill_os_random_bytes(bytes + view->ByteOffset(), length)) {
            isolate->ThrowException(v8::Exception::Error(
                v8_string(isolate, "getRandomValues: OS random source failed").As<v8::String>()));
            return v8::Undefined(isolate);
        }
        return value;
    }
private:
    PageState* owner_;
    bool secure_;
    v8::Global<v8::Object> subtle_;
};

// CryptoKey is exposed for browser interface shape only. This digest slice
// creates no key instances; key operations reject the unsupported profile.
class CryptoKeyDeclaration final : public HostObject {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "CryptoKey"; }
    std::vector<std::string> property_names() const override {
        return {"type", "extractable", "algorithm", "usages"};
    }
    std::vector<std::string> method_names() const override { return {}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&) override { return v8::Undefined(isolate); }
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override { return v8::Undefined(isolate); }
};

bool move_crypto_constructor_to_end(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name) {
    auto prototype = web_interface_prototype(isolate, context, name);
    if (prototype.IsEmpty()) return false;
    auto key = v8::String::NewFromUtf8Literal(isolate, "constructor");
    v8::Local<v8::Value> constructor;
    return prototype->Get(context, key).ToLocal(&constructor) &&
        prototype->Delete(context, key).FromMaybe(false) &&
        install_traced_api_data_property(isolate, context, prototype, "constructor", constructor,
            native_api_identity(isolate, prototype), "host_object", name + ".prototype", v8::DontEnum);
}

void global_crypto_getter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    const auto sequence = window_trace_record(isolate, "crypto", AccessOperation::kGet);
    AccessFunctionPropertyCompletion completion(isolate, sequence, info, AccessOperation::kGet);
    if (!info.This()->StrictEquals(context->Global())) {
        crypto_type_error(isolate, "Illegal invocation");
        return;
    }
    info.GetReturnValue().Set(info.Data());
}

void subtle_supports_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    const auto sequence = access_trace_record(isolate,
        native_api_identity(isolate, info.Data().As<v8::Object>()),
        "crypto", "SubtleCrypto", "supports", AccessOperation::kCall);
    AccessCallCompletion completion(isolate, sequence, info);
    watch_record(isolate, "SubtleCrypto", "supports");
    const std::string prefix = "Failed to execute 'supports' on 'SubtleCrypto': ";
    if (info.Length() < 2) {
        crypto_type_error(isolate, prefix + "2 arguments required, but only " +
            std::to_string(info.Length()) + " present.");
        return;
    }
    // Static operation: no receiver brand. WebIDL string conversion errors
    // propagate, while algorithm-normalization errors are a false capability.
    v8::Local<v8::String> operation_value;
    if (!algorithm_to_string(isolate, context, info[0], prefix, &operation_value)) return;
    v8::Local<v8::Value> algorithm = info[1];
    if (!algorithm->IsObject()) {
        v8::Local<v8::String> text;
        if (!algorithm_to_string(isolate, context, algorithm, prefix, &text)) return;
        algorithm = text;
    }
    v8::String::Utf8Value operation_utf8(isolate, operation_value);
    const std::string operation(*operation_utf8 != nullptr ? *operation_utf8 : "",
        *operation_utf8 != nullptr ? static_cast<std::size_t>(operation_utf8.length()) : 0);
    static const char* operations[] = {
        "decrypt", "deriveBits", "deriveKey", "digest", "encrypt", "exportKey",
        "generateKey", "importKey", "sign", "unwrapKey", "verify", "wrapKey",
        "decapsulateBits", "decapsulateKey", "encapsulateBits", "encapsulateKey",
        "getPublicKey"};
    bool known_operation = false;
    for (const char* candidate : operations) {
        if (operation == candidate) { known_operation = true; break; }
    }
    info.GetReturnValue().Set(false);
    if (!known_operation) return;

    v8::TryCatch caught(isolate);
    v8::Local<v8::Value> name = algorithm;
    if (algorithm->IsObject()) {
        if (!access_user_operation(isolate, [&] {
                return algorithm.As<v8::Object>()->Get(context,
                    v8::String::NewFromUtf8Literal(isolate, "name"));
            }).ToLocal(&name)) {
            if (!caught.CanContinue()) caught.ReThrow();
            else caught.Reset();
            return;
        }
        if (name->IsUndefined()) return;
    }
    v8::Local<v8::String> algorithm_value;
    if (!algorithm_to_string(isolate, context, name, prefix, &algorithm_value)) {
        if (!caught.CanContinue()) caught.ReThrow();
        else caught.Reset();
        return;
    }
    v8::String::Utf8Value algorithm_utf8(isolate, algorithm_value);
    std::string normalized(*algorithm_utf8 != nullptr ? *algorithm_utf8 : "",
        *algorithm_utf8 != nullptr ? static_cast<std::size_t>(algorithm_utf8.length()) : 0);
    for (char& character : normalized) {
        if (character >= 'a' && character <= 'z') character -= 'a' - 'A';
    }
    info.GetReturnValue().Set(operation == "digest" &&
        (normalized == "SHA-1" || normalized == "SHA-256" ||
         normalized == "SHA-384" || normalized == "SHA-512"));
}

}  // namespace

bool install_crypto_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner, bool secure) {
    // Chrome orders getRandomValues, constructor, then secure-only members.
    CryptoHost common(owner, false);
    install_host_interface(isolate, context, &common);
    if (!move_crypto_constructor_to_end(isolate, context, "Crypto")) return false;
    if (secure) {
        SubtleCryptoHost subtle(owner);
        CryptoKeyDeclaration key;
        install_host_interface(isolate, context, &subtle);
        install_host_interface(isolate, context, &key);
        if (!move_crypto_constructor_to_end(isolate, context, "SubtleCrypto") ||
            !move_crypto_constructor_to_end(isolate, context, "CryptoKey")) return false;
        auto prototype = web_interface_prototype(isolate, context, "SubtleCrypto");
        v8::Local<v8::Value> constructor;
        v8::Local<v8::Function> supports;
        if (prototype.IsEmpty() ||
            !prototype->Get(context, v8::String::NewFromUtf8Literal(isolate, "constructor"))
                .ToLocal(&constructor) || !constructor->IsFunction() ||
            !make_web_method(isolate, context, "supports", 2, &subtle_supports_callback,
                constructor).ToLocal(&supports) ||
            !install_traced_api_data_property(isolate, context, constructor.As<v8::Object>(),
                "supports", supports, native_api_identity(isolate, constructor.As<v8::Object>()),
                "crypto", "SubtleCrypto")) return false;
    }
    auto object = owner->adopt_host_object(isolate, context,
        std::make_unique<CryptoHost>(owner, secure));
    v8::Local<v8::Function> getter;
    if (!make_web_method(isolate, context, "get crypto", 0, &global_crypto_getter, object)
            .ToLocal(&getter)) return false;
    v8::PropertyDescriptor descriptor(getter, v8::Undefined(isolate));
    descriptor.set_enumerable(true);
    descriptor.set_configurable(true);
    return context->Global()->DefineProperty(context,
        v8::String::NewFromUtf8Literal(isolate, "crypto"), descriptor).FromMaybe(false);
}

}  // namespace iv8::browser
