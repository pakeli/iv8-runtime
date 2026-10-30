#include "iv8/page_state.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <random>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

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

#include "iv8/context_host.h"

// M9-1: V8 Inspector (CDP) attach base. Included after context_host.h so the
// v8_headers.h COMPILER-macro workaround has already run before any V8 header.
#include "v8-inspector.h"

namespace py = pybind11;

namespace iv8 {

// M11-37: per-generation performance timeline records. Defined in iv8 scope
// because PageState owns the store through the opaque forward declaration in
// page_state.h.
struct PerformanceEntryRecord {
    std::string name;
    std::string entry_type;
    std::int64_t start_time = 0;
    std::int64_t duration = 0;
};

struct PerformanceStore {
    std::vector<PerformanceEntryRecord> entries;
};

// Step 5g: Page-owned backings for matchMedia result objects. Keyed by media
// string + evaluated matches; cleared on every generation.
struct MediaQueryListStore {
    std::unordered_map<std::u16string, std::unique_ptr<HostObject>> by_key;
};

namespace {

// M9-2 forward declarations (definitions lower in this TU): the isolate->owning-
// ContextState recovery, and the watch-hit recorder used by the host-method
// dispatch points (some of which are defined above the recorder's definition).
ContextState* state_from_isolate(v8::Isolate* isolate);
void watch_record(v8::Isolate* isolate, const std::string& receiver,
                  const std::string& method);
void access_trace_record(v8::Isolate* isolate, const void* identity,
                         const char* object_type, const char* label,
                         const char* member, AccessOperation operation);

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

// M2-1 framework probe: a neutral, native-backed host object used ONLY to
// validate the Host Object Framework (installation, property getter plumbing,
// method callback plumbing, lifetime binding). It is NOT a browser object and
// carries no page semantics; it is expected to be removed/replaced by real host
// objects in a later M2 phase. Installed on every page as the JS global
// `hostProbe`.
class HostProbe : public HostObject {
public:
    std::string global_name() const override { return "hostProbe"; }
    std::string trace_object_type() const override { return "host_probe"; }

    std::vector<std::string> property_names() const override {
        return {"answer", "label"};
    }

    std::vector<std::string> method_names() const override {
        return {"add", "echo"};
    }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
                                      v8::Local<v8::Context>,
                                      const std::string& name) override {
        if (name == "answer") {
            return v8::Integer::New(isolate, 42);
        }
        if (name == "label") {
            return v8::String::NewFromUtf8Literal(isolate, "iv8-host-probe");
        }
        return v8::Undefined(isolate);
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& args) override {
        if (name == "add") {
            const double a =
                args.Length() > 0 ? args[0]->NumberValue(context).FromMaybe(0.0)
                                  : 0.0;
            const double b =
                args.Length() > 1 ? args[1]->NumberValue(context).FromMaybe(0.0)
                                  : 0.0;
            return v8::Number::New(isolate, a + b);
        }
        if (name == "echo") {
            if (args.Length() > 0) {
                return args[0];
            }
            return v8::Undefined(isolate);
        }
        return v8::Undefined(isolate);
    }
};

// M2-2/M11-28 minimal `console`. A host object exposing the classic logging
// methods plus debug/trace/table/dir/group/groupEnd/time/timeEnd/assert/count.
// Arguments are stringified with JS's own ToString (minimal, deterministic; NOT
// browser console format-string compatible) and joined by a single space. The
// message lands in Python `logging` on the "iv8.console" logger:
//   log/info/table/dir/group/time -> INFO ; debug/trace -> DEBUG ;
//   warn -> WARNING ; error/assert -> ERROR.
// There is no clock, stack capture, table formatting, group nesting, or timing.
// The callback runs during eval with the GIL released, so it reacquires the GIL
// only for the logging call and never lets a Python error escape into V8.
class ConsoleHost : public HostObject {
public:
    std::string global_name() const override { return "console"; }
    std::string trace_object_type() const override { return "console"; }

    std::vector<std::string> property_names() const override { return {}; }

    std::vector<std::string> method_names() const override {
        return {"log",   "info",  "warn",  "error", "debug",
                "trace", "table", "dir",   "group", "groupEnd",
                "time",  "timeEnd", "assert", "count"};
    }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate, v8::Local<v8::Context>,
                                      const std::string&) override {
        return v8::Undefined(isolate);  // console has no data properties
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& args) override {
        if (name == "groupEnd") {
            return v8::Undefined(isolate);  // no nesting state in this slice
        }
        if (name == "assert") {
            const bool condition =
                args.Length() > 0 && args[0]->BooleanValue(isolate);
            if (condition) {
                return v8::Undefined(isolate);
            }
            const std::string detail =
                join_args(isolate, context, args, 1);
            log_message("error", detail.empty()
                                     ? std::string("Assertion failed")
                                     : "Assertion failed: " + detail);
            return v8::Undefined(isolate);
        }
        if (name == "count") {
            const std::string label =
                args.Length() > 0
                    ? join_args(isolate, context, args)
                    : std::string("default");
            const int next = ++counts_[label];
            log_message("info", label + ": " + std::to_string(next));
            return v8::Undefined(isolate);
        }
        if (name == "time" || name == "timeEnd") {
            const std::string label =
                args.Length() > 0
                    ? join_args(isolate, context, args)
                    : std::string("default");
            log_message("info",
                        (name == "time" ? "Timer started: "
                                        : "Timer ended: ") + label);
            return v8::Undefined(isolate);
        }
        const std::string message = join_args(isolate, context, args);
        const char* level = "info";  // log, info, table, dir, group
        if (name == "debug" || name == "trace") {
            level = "debug";
        } else if (name == "warn") {
            level = "warning";
        } else if (name == "error") {
            level = "error";
        }
        log_message(level, message);
        return v8::Undefined(isolate);
    }

private:
    static void log_message(const char* level, const std::string& message) {
        py::gil_scoped_acquire gil;
        try {
            py::module_::import("logging")
                .attr("getLogger")("iv8.console")
                .attr(level)(py::str(message));
        } catch (const py::error_already_set&) {
            // A logging failure must never break JS execution; swallow it
            // (the GIL is held here, so clearing the Python error is safe).
        }
    }

    // Minimal deterministic stringification: JS ToString per argument, joined by
    // a single space. A throwing toString is swallowed (console stays
    // non-throwing) and rendered as a fixed placeholder. `start` supports
    // console.assert, which excludes the condition argument.
    static std::string join_args(v8::Isolate* isolate,
                                 v8::Local<v8::Context> context,
                                 const v8::FunctionCallbackInfo<v8::Value>& args,
                                 int start = 0) {
        std::string message;
        for (int i = start; i < args.Length(); ++i) {
            if (i > start) {
                message += ' ';
            }
            v8::TryCatch try_catch(isolate);
            v8::Local<v8::String> str;
            if (args[i]->ToString(context).ToLocal(&str)) {
                v8::String::Utf8Value utf8(isolate, str);
                if (*utf8 != nullptr) {
                    message.append(*utf8, static_cast<size_t>(utf8.length()));
                }
            } else {
                try_catch.Reset();
                message += "<unprintable>";
            }
        }
        return message;
    }

    // Per-console-instance count labels. ConsoleHost is recreated per generation,
    // so counts reset on Page.load(), matching the rest of the page state.
    std::unordered_map<std::string, int> counts_;
};

v8::Local<v8::Value> v8_string(v8::Isolate* isolate, const std::string& s) {
    return v8::String::NewFromUtf8(isolate, s.c_str(), v8::NewStringType::kNormal,
                                   static_cast<int>(s.size()))
        .ToLocalChecked();
}

// --- M11-7 Web Storage -------------------------------------------------------
//
// A Storage object is a plain JS object whose methods and `length` accessor carry
// an External pointer to one of PageState's two page-lifetime vectors. The
// vectors are insertion-ordered (updating a key keeps its position) and are
// re-used by every load() generation.

using StorageStore = std::vector<std::pair<std::string, std::string>>;

// V8 15.0 requires a tag on External::New / External::Value. The default tag is
// reused for every PageState-owned pointer type in this file (see also
// kMutationObserverTag / kWindowEventsTag).
constexpr v8::ExternalPointerTypeTag kStorageTag =
    v8::kExternalPointerTypeTagDefault;

StorageStore* storage_store_from(v8::Local<v8::Value> data) {
    if (!data->IsExternal()) {
        return nullptr;
    }
    return static_cast<StorageStore*>(
        data.As<v8::External>()->Value(kStorageTag));
}

std::string storage_string_arg(v8::Isolate* isolate,
                               v8::Local<v8::Context> context,
                               v8::Local<v8::Value> value) {
    v8::Local<v8::String> str;
    if (!value->ToString(context).ToLocal(&str)) {
        return std::string();
    }
    v8::String::Utf8Value utf8(isolate, str);
    return (*utf8 != nullptr)
               ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
               : std::string();
}

StorageStore::iterator storage_find(StorageStore* store,
                                    const std::string& key) {
    return std::find_if(store->begin(), store->end(),
                        [&key](const std::pair<std::string, std::string>& entry) {
                            return entry.first == key;
                        });
}

void storage_get_item(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    StorageStore* store = storage_store_from(info.Data());
    if (store == nullptr) {
        return;
    }
    access_trace_record(isolate, store, "storage", "storage", "getItem",
                        AccessOperation::kCall);
    const std::string key = storage_string_arg(isolate, context, info[0]);
    const auto it = storage_find(store, key);
    if (it == store->end()) {
        info.GetReturnValue().Set(v8::Null(isolate));
        return;
    }
    info.GetReturnValue().Set(v8_string(isolate, it->second));
}

void storage_set_item(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    StorageStore* store = storage_store_from(info.Data());
    if (store == nullptr) {
        return;
    }
    access_trace_record(isolate, store, "storage", "storage", "setItem",
                        AccessOperation::kCall);
    const std::string key = storage_string_arg(isolate, context, info[0]);
    const std::string value = storage_string_arg(isolate, context, info[1]);
    const auto it = storage_find(store, key);
    if (it == store->end()) {
        store->emplace_back(key, value);
    } else {
        it->second = value;
    }
}

void storage_remove_item(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    StorageStore* store = storage_store_from(info.Data());
    if (store == nullptr) {
        return;
    }
    access_trace_record(isolate, store, "storage", "storage", "removeItem",
                        AccessOperation::kCall);
    const std::string key = storage_string_arg(isolate, context, info[0]);
    const auto it = storage_find(store, key);
    if (it != store->end()) {
        store->erase(it);
    }
}

void storage_clear(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    StorageStore* store = storage_store_from(info.Data());
    if (store != nullptr) {
        access_trace_record(isolate, store, "storage", "storage", "clear",
                            AccessOperation::kCall);
        store->clear();
    }
}

void storage_key(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    StorageStore* store = storage_store_from(info.Data());
    if (store == nullptr) {
        return;
    }
    access_trace_record(isolate, store, "storage", "storage", "key",
                        AccessOperation::kCall);
    v8::Local<v8::Uint32> index_value;
    if (!info[0]->ToUint32(context).ToLocal(&index_value)) {
        return;
    }
    const std::uint32_t index = index_value->Value();
    if (index >= store->size()) {
        info.GetReturnValue().Set(v8::Null(isolate));
        return;
    }
    info.GetReturnValue().Set(
        v8_string(isolate, (*store)[index].first));
}

void storage_length_getter(v8::Local<v8::Name> property,
                           const v8::PropertyCallbackInfo<v8::Value>& info) {
    (void)property;
    v8::Isolate* isolate = info.GetIsolate();
    StorageStore* store = storage_store_from(info.Data());
    if (store == nullptr) {
        return;
    }
    access_trace_record(isolate, store, "storage", "storage", "length",
                        AccessOperation::kGet);
    info.GetReturnValue().Set(
        v8::Integer::NewFromUnsigned(isolate, static_cast<unsigned>(store->size())));
}

// Step 5: nested connection metadata. HostObject-backed so reads, missing
// probes, and object identity use the normal access-trace channel.
class ConnectionHost : public HostObject {
public:
    explicit ConnectionHost(const HostEnvironment* environment)
        : environment_(environment) {}

    std::string global_name() const override { return std::string(); }
    std::string trace_object_type() const override { return "connection"; }
    std::string trace_label() const override { return "navigator.connection"; }

    std::vector<std::string> property_names() const override {
        return {"effectiveType", "rtt", "downlink", "saveData"};
    }
    std::vector<std::string> method_names() const override { return {}; }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
                                      v8::Local<v8::Context>,
                                      const std::string& name) override {
        if (name == "effectiveType") {
            return v8_string(isolate,
                             environment_->connection_effective_type);
        }
        if (name == "rtt") {
            return v8::Integer::New(isolate, environment_->connection_rtt);
        }
        if (name == "downlink") {
            return v8::Number::New(isolate, environment_->connection_downlink);
        }
        if (name == "saveData") {
            return v8::Boolean::New(isolate, environment_->connection_save_data);
        }
        return v8::Undefined(isolate);
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        return v8::Undefined(isolate);
    }

private:
    const HostEnvironment* environment_;
};

// Step 5: nested screen orientation metadata.
class ScreenOrientationHost : public HostObject {
public:
    explicit ScreenOrientationHost(const HostEnvironment* environment)
        : environment_(environment) {}

    std::string global_name() const override { return std::string(); }
    std::string trace_object_type() const override { return "orientation"; }
    std::string trace_label() const override { return "screen.orientation"; }

    std::vector<std::string> property_names() const override {
        return {"type", "angle"};
    }
    std::vector<std::string> method_names() const override { return {}; }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
                                      v8::Local<v8::Context>,
                                      const std::string& name) override {
        const bool landscape =
            environment_->screen_width >= environment_->screen_height;
        if (name == "type") {
            return v8_string(isolate, landscape ? "landscape-primary"
                                                : "portrait-primary");
        }
        if (name == "angle") {
            return v8::Integer::New(isolate, landscape ? 0 : 90);
        }
        return v8::Undefined(isolate);
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        return v8::Undefined(isolate);
    }

private:
    const HostEnvironment* environment_;
};

// Step 5e: navigator.storage stays a plain object so its method descriptors,
// prototype, and cached identity match the M11-43 surface, but its native
// method callbacks record with this stable native identity. The parent
// navigator.storage getter is recorded by NavigatorHost's normal property
// accessor.
constexpr char kStorageManagerTraceToken = 0;

v8::Local<v8::Value> resolved_promise(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context,
                                      v8::Local<v8::Value> value) {
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        return v8::Undefined(isolate);
    }
    (void)resolver->Resolve(context, value);
    return resolver->GetPromise();
}

void storage_manager_estimate_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    access_trace_record(isolate, &kStorageManagerTraceToken, "storage_manager",
                        "navigator.storage", "estimate",
                        AccessOperation::kCall);
    v8::Local<v8::Object> estimate = v8::Object::New(isolate);
    (void)estimate->Set(context, v8_string(isolate, "usage"),
                        v8::Integer::New(isolate, 0));
    (void)estimate->Set(context, v8_string(isolate, "quota"),
                        v8::Number::New(isolate, 10737418240.0));
    info.GetReturnValue().Set(resolved_promise(isolate, context, estimate));
}

void storage_manager_persisted_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    access_trace_record(isolate, &kStorageManagerTraceToken, "storage_manager",
                        "navigator.storage", "persisted",
                        AccessOperation::kCall);
    info.GetReturnValue().Set(
        resolved_promise(isolate, context, v8::Boolean::New(isolate, false)));
}

void storage_manager_persist_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    access_trace_record(isolate, &kStorageManagerTraceToken, "storage_manager",
                        "navigator.storage", "persist",
                        AccessOperation::kCall);
    info.GetReturnValue().Set(
        resolved_promise(isolate, context, v8::Boolean::New(isolate, false)));
}

v8::Local<v8::Object> make_storage_manager_object(
    v8::Isolate* isolate, v8::Local<v8::Context> context) {
    v8::Local<v8::Object> manager = v8::Object::New(isolate);
    const auto define = [&](const char* name, v8::FunctionCallback callback) {
        v8::Local<v8::Function> fn =
            v8::FunctionTemplate::New(isolate, callback)
                ->GetFunction(context)
                .ToLocalChecked();
        (void)manager->DefineOwnProperty(
            context,
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(), fn,
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete));
    };
    define("estimate", &storage_manager_estimate_callback);
    define("persisted", &storage_manager_persisted_callback);
    define("persist", &storage_manager_persist_callback);
    return manager;
}

// Step 5f: navigator.permissions and its returned PermissionStatus objects stay
// plain objects (so prototypes and descriptors match M11-44), but their native
// accessors/callbacks record accesses. Status identity is the fixed per-name
// descriptor, so repeated statuses for the same permission share an object_id.
constexpr char kPermissionsTraceToken = 0;
// V8 15.0 requires a tag on External::New / External::Value.
constexpr v8::ExternalPointerTypeTag kPermissionStatusTag =
    v8::kExternalPointerTypeTagDefault;

struct PermissionNameInfo {
    const char* key;
};

const PermissionNameInfo kPermissionNames[] = {
    {"accelerometer"},      {"background-sync"},   {"camera"},
    {"clipboard-read"},     {"clipboard-write"},   {"geolocation"},
    {"gyroscope"},          {"magnetometer"},      {"microphone"},
    {"midi"},               {"notifications"},     {"persistent-storage"},
    {"push"},               {"screen-wake-lock"},  {"storage-access"},
    {"window-management"},
};

const PermissionNameInfo* find_permission_name(const std::string& name) {
    for (const PermissionNameInfo& entry : kPermissionNames) {
        if (name == entry.key) {
            return &entry;
        }
    }
    return nullptr;
}

v8::Local<v8::Value> rejected_type_error_promise(v8::Isolate* isolate,
                                                v8::Local<v8::Context> context,
                                                const char* message) {
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        return v8::Undefined(isolate);
    }
    (void)resolver->Reject(
        context,
        v8::Exception::TypeError(v8_string(isolate, message).As<v8::String>()));
    return resolver->GetPromise();
}

void permission_status_name_getter(
    v8::Local<v8::Name>,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    const auto* entry = static_cast<const PermissionNameInfo*>(
        info.Data().As<v8::External>()->Value(kPermissionStatusTag));
    access_trace_record(isolate, entry, "permission_status",
                        "permissionStatus", "name", AccessOperation::kGet);
    info.GetReturnValue().Set(v8_string(isolate, entry->key));
}

void permission_status_state_getter(
    v8::Local<v8::Name>,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    const auto* entry = static_cast<const PermissionNameInfo*>(
        info.Data().As<v8::External>()->Value(kPermissionStatusTag));
    access_trace_record(isolate, entry, "permission_status",
                        "permissionStatus", "state", AccessOperation::kGet);
    info.GetReturnValue().Set(v8_string(isolate, "prompt"));
}

void permission_status_add_listener_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    const auto* entry = static_cast<const PermissionNameInfo*>(
        info.Data().As<v8::External>()->Value(kPermissionStatusTag));
    access_trace_record(isolate, entry, "permission_status",
                        "permissionStatus", "addEventListener",
                        AccessOperation::kCall);
    info.GetReturnValue().Set(v8::Undefined(isolate));
}

void permission_status_remove_listener_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    const auto* entry = static_cast<const PermissionNameInfo*>(
        info.Data().As<v8::External>()->Value(kPermissionStatusTag));
    access_trace_record(isolate, entry, "permission_status",
                        "permissionStatus", "removeEventListener",
                        AccessOperation::kCall);
    info.GetReturnValue().Set(v8::Undefined(isolate));
}

void permission_status_dispatch_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    const auto* entry = static_cast<const PermissionNameInfo*>(
        info.Data().As<v8::External>()->Value(kPermissionStatusTag));
    access_trace_record(isolate, entry, "permission_status",
                        "permissionStatus", "dispatchEvent",
                        AccessOperation::kCall);
    info.GetReturnValue().Set(v8::Boolean::New(isolate, false));
}

v8::Local<v8::Object> make_permission_status_object(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const PermissionNameInfo* entry) {
    v8::Local<v8::Object> status = v8::Object::New(isolate);
    v8::Local<v8::Value> data = v8::External::New(
        isolate, const_cast<PermissionNameInfo*>(entry), kPermissionStatusTag);
    const auto define_accessor = [&](const char* name,
                                     v8::AccessorNameGetterCallback getter) {
        (void)status->SetNativeDataProperty(
            context,
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(), getter,
            nullptr, data,
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete));
    };
    define_accessor("name", &permission_status_name_getter);
    define_accessor("state", &permission_status_state_getter);
    (void)status->Set(
        context, v8_string(isolate, "onchange").As<v8::String>(),
        v8::Null(isolate));
    const auto define_method = [&](const char* name,
                                   v8::FunctionCallback callback) {
        v8::Local<v8::Function> fn =
            v8::FunctionTemplate::New(isolate, callback, data)
                ->GetFunction(context)
                .ToLocalChecked();
        (void)status->DefineOwnProperty(
            context,
            v8::String::NewFromUtf8(isolate, name).ToLocalChecked(), fn,
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete));
    };
    define_method("addEventListener", &permission_status_add_listener_callback);
    define_method("removeEventListener",
                  &permission_status_remove_listener_callback);
    define_method("dispatchEvent", &permission_status_dispatch_callback);
    return status;
}

void permissions_query_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    access_trace_record(isolate, &kPermissionsTraceToken, "permissions",
                        "navigator.permissions", "query",
                        AccessOperation::kCall);
    if (info.Length() < 1 || !info[0]->IsObject() || info[0]->IsFunction()) {
        info.GetReturnValue().Set(rejected_type_error_promise(
            isolate, context,
            "permissions.query: descriptor must be an object"));
        return;
    }
    v8::Local<v8::Object> descriptor = info[0].As<v8::Object>();
    v8::Local<v8::Value> name_value;
    if (!descriptor->Get(context, v8_string(isolate, "name").As<v8::String>())
             .ToLocal(&name_value)) {
        return;  // a throwing descriptor getter propagates synchronously
    }
    if (!name_value->IsString()) {
        info.GetReturnValue().Set(rejected_type_error_promise(
            isolate, context, "permissions.query: name must be a string"));
        return;
    }
    v8::String::Utf8Value utf8(isolate, name_value);
    const std::string name(*utf8 != nullptr ? *utf8 : "");
    const PermissionNameInfo* entry = find_permission_name(name);
    if (entry == nullptr) {
        info.GetReturnValue().Set(rejected_type_error_promise(
            isolate, context, "permissions.query: unknown permission name"));
        return;
    }
    info.GetReturnValue().Set(resolved_promise(
        isolate, context, make_permission_status_object(isolate, context, entry)));
}

v8::Local<v8::Object> make_permissions_object(v8::Isolate* isolate,
                                               v8::Local<v8::Context> context) {
    v8::Local<v8::Object> permissions = v8::Object::New(isolate);
    v8::Local<v8::Function> query =
        v8::FunctionTemplate::New(isolate, &permissions_query_callback)
            ->GetFunction(context)
            .ToLocalChecked();
    (void)permissions->DefineOwnProperty(
        context, v8::String::NewFromUtf8Literal(isolate, "query"), query,
        static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete));
    return permissions;
}

// Step 5g: a matchMedia result. The JS bootstrap still owns the bounded query
// evaluator and the matchMedia function; it calls a temporary native factory
// that maps (query, matches) to a cached HostObject backing and creates a fresh
// JS wrapper per call. `onchange` stays a plain per-wrapper own property.
constexpr v8::ExternalPointerTypeTag kMediaQueryFactoryTag =
    v8::kExternalPointerTypeTagDefault;

class MediaQueryListHost : public HostObject {
public:
    MediaQueryListHost(std::u16string media, bool matches)
        : media_(std::move(media)), matches_(matches) {}

    std::string global_name() const override { return std::string(); }
    std::string trace_object_type() const override { return "media_query_list"; }
    std::string trace_label() const override { return "matchMedia"; }

    std::vector<std::string> property_names() const override {
        return {"media", "matches"};
    }

    std::vector<std::string> method_names() const override {
        return {"addListener",        "removeListener", "addEventListener",
                "removeEventListener", "dispatchEvent"};
    }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
                                      v8::Local<v8::Context>,
                                      const std::string& name) override {
        if (name == "media") {
            // Preserve UTF-16 exactly (including lone surrogates).
            return v8::String::NewFromTwoByte(
                       isolate,
                       reinterpret_cast<const std::uint16_t*>(media_.data()),
                       v8::NewStringType::kNormal,
                       static_cast<int>(media_.size()))
                .ToLocalChecked();
        }
        if (name == "matches") {
            return v8::Boolean::New(isolate, matches_);
        }
        return v8::Undefined(isolate);
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        if (name == "dispatchEvent") {
            return v8::Boolean::New(isolate, false);
        }
        return v8::Undefined(isolate);
    }

private:
    std::u16string media_;
    bool matches_ = false;
};

void make_media_query_list_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kMediaQueryFactoryTag));
    if (page == nullptr) {
        info.GetReturnValue().Set(v8::Undefined(isolate));
        return;
    }
    std::u16string media;
    if (info.Length() >= 1 && info[0]->IsString()) {
        v8::String::Value utf16(isolate, info[0]);
        if (*utf16 != nullptr) {
            media.assign(reinterpret_cast<const char16_t*>(*utf16),
                         static_cast<std::size_t>(utf16.length()));
        }
    }
    const bool matches = info.Length() >= 2 && info[1]->BooleanValue(isolate);
    HostObject* host = page->media_query_list_host(media, matches);
    access_trace_record(isolate, host, "media_query_list", "matchMedia",
                        "matchMedia", AccessOperation::kCall);
    v8::Local<v8::Object> list = make_host_object(isolate, context, host);
    (void)list->Set(context, v8_string(isolate, "onchange").As<v8::String>(),
                    v8::Null(isolate));
    info.GetReturnValue().Set(list);
}

// Step 5h: legacy navigator metadata. The arrays stay plain frozen arrays (read
// through NavigatorHost's traced getter); javaEnabled stays a read-only property
// whose cached native function records the call and returns false.
constexpr char kJavaEnabledTraceToken = 0;

void navigator_java_enabled_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    access_trace_record(isolate, &kJavaEnabledTraceToken, "navigator",
                        "navigator", "javaEnabled", AccessOperation::kCall);
    info.GetReturnValue().Set(v8::Boolean::New(isolate, false));
}

// M2-3 minimal navigator (read-only), configurable since M11-1 and extended by
// M11-24/M11-27. Values come from the page's HostEnvironment, which the Python
// facade always supplies fully resolved — this class holds no defaults and never
// derives a value from the host OS, display, or locale, so a page is
// deterministic and identical across Linux/Windows. Still no UA-CH /
// userAgentData surface.
class NavigatorHost : public HostObject {
public:
    explicit NavigatorHost(const HostEnvironment* environment)
        : environment_(environment), connection_(environment) {}

    std::string global_name() const override { return "navigator"; }
    std::string trace_object_type() const override { return "navigator"; }

    std::vector<std::string> property_names() const override {
        return {"userAgent",       "platform",          "language",
                "webdriver",       "languages",         "onLine",
                "cookieEnabled",   "hardwareConcurrency", "appName",
                "appCodeName",     "product",           "vendor",
                "vendorSub",       "productSub",        "maxTouchPoints",
                "pdfViewerEnabled", "doNotTrack",        "connection",
                "storage",         "permissions",       "plugins",
                "mimeTypes",       "javaEnabled"};
    }

    std::vector<std::string> method_names() const override { return {}; }

    void release_v8_handles() noexcept override {
        storage_object_.Reset();
        permissions_object_.Reset();
        plugins_object_.Reset();
        mime_types_object_.Reset();
        java_enabled_object_.Reset();
    }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context,
                                      const std::string& name) override {
        if (name == "userAgent") {
            return v8_string(isolate, environment_->user_agent);
        }
        if (name == "platform") {
            return v8_string(isolate, environment_->platform);
        }
        if (name == "language") {
            return v8_string(isolate, environment_->language);
        }
        if (name == "webdriver") {
            return v8::Boolean::New(isolate, environment_->webdriver);
        }
        if (name == "languages") {
            const std::vector<std::string>& languages = environment_->languages;
            v8::Local<v8::Array> array =
                v8::Array::New(isolate, static_cast<int>(languages.size()));
            for (std::size_t i = 0; i < languages.size(); i++) {
                (void)array->Set(context, static_cast<std::uint32_t>(i),
                                 v8_string(isolate, languages[i]));
            }
            return array;
        }
        if (name == "onLine") {
            return v8::Boolean::New(isolate, environment_->on_line);
        }
        if (name == "cookieEnabled") {
            return v8::Boolean::New(isolate, environment_->cookie_enabled);
        }
        if (name == "hardwareConcurrency") {
            return v8::Integer::New(isolate,
                                    environment_->hardware_concurrency);
        }
        if (name == "appName") {
            return v8_string(isolate, environment_->app_name);
        }
        if (name == "appCodeName") {
            return v8_string(isolate, environment_->app_code_name);
        }
        if (name == "product") {
            return v8_string(isolate, environment_->product);
        }
        if (name == "vendor") {
            return v8_string(isolate, environment_->vendor);
        }
        if (name == "vendorSub") {
            return v8_string(isolate, environment_->vendor_sub);
        }
        if (name == "productSub") {
            return v8_string(isolate, environment_->product_sub);
        }
        // M11-35: deterministic navigator metadata additions.
        if (name == "maxTouchPoints") {
            return v8::Integer::New(isolate, environment_->max_touch_points);
        }
        if (name == "pdfViewerEnabled") {
            return v8::Boolean::New(isolate,
                                    environment_->pdf_viewer_enabled);
        }
        if (name == "doNotTrack") {
            return v8_string(isolate, environment_->do_not_track);
        }
        if (name == "connection") {
            return make_host_object(isolate, context, &connection_);
        }
        // Step 5e: cache the plain StorageManager object so repeated reads keep
        // the same identity and the read-only method descriptors unchanged.
        // The parent `navigator.storage` read itself is traced by the generic
        // HostObject getter; the method calls are traced by their callbacks.
        if (name == "storage") {
            if (storage_object_.IsEmpty()) {
                storage_object_.Reset(
                    isolate, make_storage_manager_object(isolate, context));
            }
            return storage_object_.Get(isolate);
        }
        // Step 5f: cached plain Permissions object; the parent read is traced
        // by this HostObject getter and query() is a traced native callback.
        if (name == "permissions") {
            if (permissions_object_.IsEmpty()) {
                permissions_object_.Reset(
                    isolate, make_permissions_object(isolate, context));
            }
            return permissions_object_.Get(isolate);
        }
        // Step 5h: legacy empty plugin/mime-type lists and javaEnabled. The
        // arrays and function are cached so identity and read-only descriptors
        // match the previous bootstrap behavior; reads/calls are traced.
        if (name == "plugins" || name == "mimeTypes") {
            v8::Global<v8::Array>& slot =
                name == "plugins" ? plugins_object_ : mime_types_object_;
            if (slot.IsEmpty()) {
                v8::Local<v8::Array> array = v8::Array::New(isolate, 0);
                (void)array->SetIntegrityLevel(context,
                                              v8::IntegrityLevel::kFrozen);
                slot.Reset(isolate, array);
            }
            return slot.Get(isolate);
        }
        if (name == "javaEnabled") {
            if (java_enabled_object_.IsEmpty()) {
                java_enabled_object_.Reset(
                    isolate,
                    v8::FunctionTemplate::New(isolate,
                                              &navigator_java_enabled_callback)
                        ->GetFunction(context)
                        .ToLocalChecked());
            }
            return java_enabled_object_.Get(isolate);
        }
        return v8::Undefined(isolate);
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        return v8::Undefined(isolate);  // navigator exposes no methods
    }

private:
    // Owned by PageState (outlives every generation), so this raw pointer stays
    // valid for as long as this host object is installed.
    const HostEnvironment* environment_;
    ConnectionHost connection_;
    // Step 5e/5f: per-generation cached plain navigator.storage and
    // navigator.permissions objects.
    v8::Global<v8::Object> storage_object_;
    v8::Global<v8::Object> permissions_object_;
    // Step 5h: cached legacy navigator metadata.
    v8::Global<v8::Array> plugins_object_;
    v8::Global<v8::Array> mime_types_object_;
    v8::Global<v8::Function> java_enabled_object_;
};

// M11-1/M11-25 minimal screen (read-only). Static dimensions and color/pixel
// depth sourced from the page's HostEnvironment. Deliberately NOT a full Screen
// object: no availLeft / availTop / orientation, no ScreenOrientation, and no
// media-query or layout integration. avail* are independent configured values,
// not computed from width/height.
class ScreenHost : public HostObject {
public:
    explicit ScreenHost(const HostEnvironment* environment)
        : environment_(environment), orientation_(environment) {}

    std::string global_name() const override { return "screen"; }
    std::string trace_object_type() const override { return "screen"; }

    std::vector<std::string> property_names() const override {
        return {"width",      "height",     "availWidth", "availHeight",
                "availLeft",  "availTop",   "colorDepth", "pixelDepth",
                "orientation"};
    }

    std::vector<std::string> method_names() const override { return {}; }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context,
                                      const std::string& name) override {
        if (name == "width") {
            return v8::Integer::New(isolate, environment_->screen_width);
        }
        if (name == "height") {
            return v8::Integer::New(isolate, environment_->screen_height);
        }
        if (name == "availWidth") {
            return v8::Integer::New(isolate, environment_->avail_width);
        }
        if (name == "availHeight") {
            return v8::Integer::New(isolate, environment_->avail_height);
        }
        // M11-35: reported available origin (independent configured values).
        if (name == "availLeft") {
            return v8::Integer::New(isolate, environment_->avail_left);
        }
        if (name == "availTop") {
            return v8::Integer::New(isolate, environment_->avail_top);
        }
        if (name == "colorDepth") {
            return v8::Integer::New(isolate, environment_->color_depth);
        }
        if (name == "pixelDepth") {
            return v8::Integer::New(isolate, environment_->pixel_depth);
        }
        // M11-35: deterministic orientation derived from the reported screen
        // dimensions. Step 5 exposes it as a nested HostObject.
        if (name == "orientation") {
            return make_host_object(isolate, context, &orientation_);
        }
        return v8::Undefined(isolate);
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        return v8::Undefined(isolate);  // screen exposes no methods
    }

private:
    const HostEnvironment* environment_;
    ScreenOrientationHost orientation_;
};

// Step 5d: the static M11-41 visual viewport. It is a HostObject so its property
// reads and no-op event-target methods enter the normal access-trace channel.
// Values still come from the Page environment and never change within a
// generation; onresize/onscroll store whatever the script assigns, but no events
// are dispatched. The global property remains read-only.
class VisualViewportHost : public HostObject {
public:
    explicit VisualViewportHost(const HostEnvironment* environment)
        : environment_(environment) {}

    std::string global_name() const override { return std::string(); }
    std::string trace_object_type() const override { return "visual_viewport"; }
    std::string trace_label() const override { return "visualViewport"; }

    std::vector<std::string> property_names() const override {
        return {"width",    "height",   "offsetLeft", "offsetTop",
                "pageLeft", "pageTop",  "scale",      "onresize",
                "onscroll"};
    }

    std::vector<std::string> writable_property_names() const override {
        return {"onresize", "onscroll"};
    }

    std::vector<std::string> method_names() const override {
        return {"addEventListener", "removeEventListener", "dispatchEvent"};
    }

    void release_v8_handles() noexcept override {
        onresize_.Reset();
        onscroll_.Reset();
    }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
                                      v8::Local<v8::Context>,
                                      const std::string& name) override {
        if (name == "width") {
            return v8::Integer::New(isolate, environment_->inner_width);
        }
        if (name == "height") {
            return v8::Integer::New(isolate, environment_->inner_height);
        }
        if (name == "offsetLeft" || name == "offsetTop" || name == "pageLeft" ||
            name == "pageTop") {
            return v8::Integer::New(isolate, 0);
        }
        if (name == "scale") {
            return v8::Number::New(isolate, 1.0);
        }
        if (name == "onresize") {
            return onresize_.IsEmpty() ? v8::Null(isolate)
                                       : onresize_.Get(isolate);
        }
        if (name == "onscroll") {
            return onscroll_.IsEmpty() ? v8::Null(isolate)
                                       : onscroll_.Get(isolate);
        }
        return v8::Undefined(isolate);
    }

    void set_property(v8::Isolate* isolate, v8::Local<v8::Context>,
                      const std::string& name,
                      v8::Local<v8::Value> value) override {
        if (name == "onresize") {
            onresize_.Reset(isolate, value);
        } else if (name == "onscroll") {
            onscroll_.Reset(isolate, value);
        }
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        if (name == "dispatchEvent") {
            return v8::Boolean::New(isolate, false);
        }
        return v8::Undefined(isolate);
    }

private:
    const HostEnvironment* environment_;
    v8::Global<v8::Value> onresize_;
    v8::Global<v8::Value> onscroll_;
};

// The static components of a decomposed URL (WHATWG-style names). Only the
// pieces M2-3 location exposes.
struct UrlParts {
    std::string href;
    std::string origin;
    std::string protocol;
    std::string host;
    std::string hostname;
    std::string pathname;
    std::string search;
    std::string hash;
};

// Minimal, deterministic decomposition of an absolute `scheme://host[:port]/
// path?query#hash` URL. NOT a full WHATWG URL parser (no userinfo, default-port
// elision, or percent-decoding) — just enough for the fixed page base URL.
UrlParts decompose_url(const std::string& url) {
    UrlParts parts;
    parts.href = url;

    std::string rest = url;
    const auto scheme_end = rest.find(':');
    if (scheme_end != std::string::npos) {
        parts.protocol = rest.substr(0, scheme_end + 1);  // includes ':'
        rest = rest.substr(scheme_end + 1);
    }
    if (rest.rfind("//", 0) == 0) {  // has an authority
        rest = rest.substr(2);
        const auto auth_end = rest.find_first_of("/?#");
        const std::string authority =
            (auth_end == std::string::npos) ? rest : rest.substr(0, auth_end);
        rest = (auth_end == std::string::npos) ? "" : rest.substr(auth_end);
        parts.host = authority;  // host + optional port
        const auto port = authority.find(':');
        parts.hostname =
            (port == std::string::npos) ? authority : authority.substr(0, port);
        parts.origin = parts.protocol + "//" + parts.host;
    }
    const auto hash_pos = rest.find('#');
    if (hash_pos != std::string::npos) {
        parts.hash = rest.substr(hash_pos);  // includes '#'
        rest = rest.substr(0, hash_pos);
    }
    const auto query_pos = rest.find('?');
    if (query_pos != std::string::npos) {
        parts.search = rest.substr(query_pos);  // includes '?'
        rest = rest.substr(0, query_pos);
    }
    parts.pathname = rest;
    return parts;
}

// M2-3 minimal location (read-only). Values are the static decomposition of the
// page's base URL. Read-only: exposed as getter-only accessors, so a JS-side
// write is a no-op (sloppy) / TypeError (strict) and NEVER triggers navigation
// — there is no assign/replace/reload/href-write navigation in M2-3.
class LocationHost : public HostObject {
public:
    explicit LocationHost(UrlParts parts) : parts_(std::move(parts)) {}

    std::string global_name() const override { return "location"; }
    std::string trace_object_type() const override { return "location"; }

    std::vector<std::string> property_names() const override {
        return {"href",     "origin",   "protocol", "host",
                "hostname", "pathname", "search",   "hash"};
    }

    std::vector<std::string> method_names() const override { return {"toString"}; }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate, v8::Local<v8::Context>,
                                      const std::string& name) override {
        const std::string* value = component(name);
        if (value != nullptr) {
            return v8_string(isolate, *value);
        }
        return v8::Undefined(isolate);
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        if (name == "toString") {
            return v8_string(isolate, parts_.href);
        }
        return v8::Undefined(isolate);
    }

private:
    const std::string* component(const std::string& name) const {
        if (name == "href") return &parts_.href;
        if (name == "origin") return &parts_.origin;
        if (name == "protocol") return &parts_.protocol;
        if (name == "host") return &parts_.host;
        if (name == "hostname") return &parts_.hostname;
        if (name == "pathname") return &parts_.pathname;
        if (name == "search") return &parts_.search;
        if (name == "hash") return &parts_.hash;
        return nullptr;
    }

    UrlParts parts_;
};

// Fixed internal default base URL for M2-3. The RFC 2606 `.invalid` TLD makes it
// clearly a non-routable placeholder (not a real navigation). A real page.load()
// that sets this per page is a LATER phase, deliberately not done here.
constexpr char kDefaultBaseUrl[] = "https://iv8.invalid/";

// --- M2-6 minimal document --------------------------------------------------
// A JS-global `document` host object + `element` host objects, backed by a
// MINIMAL internal HTML tree (tag / id / class / children only). This is NOT an
// HTML5 parser and NOT a DOM: it serves exactly documentElement / body / title /
// getElementById / querySelector(#id | tagname | .class). No Node/Element layer,
// no mutation, no textContent/attributes/children exposed.

std::string ascii_lower(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return s;
}

std::string ascii_upper(std::string s) {
    std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) {
        return static_cast<char>(std::toupper(c));
    });
    return s;
}

// Inner text of the first <title>...</title> (case-insensitive tags), else "".
std::string extract_title(const std::string& html) {
    const std::string lower = ascii_lower(html);
    const std::size_t open = lower.find("<title>");
    if (open == std::string::npos) {
        return std::string();
    }
    const std::size_t inner = open + 7;  // strlen("<title>")
    const std::size_t close = lower.find("</title>", inner);
    if (close == std::string::npos) {
        return std::string();
    }
    return html.substr(inner, close - inner);
}

// Naive tag strip (drop every <...> span). Used for the M2-7 textContent
// aggregate. No entity decoding / script-style handling / whitespace norm.
std::string strip_tags(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    bool in_tag = false;
    for (char c : s) {
        if (c == '<') {
            in_tag = true;
        } else if (c == '>') {
            in_tag = false;
        } else if (!in_tag) {
            out.push_back(c);
        }
    }
    return out;
}

// A minimal HTML element node. Only what M2-6/M2-7 queries need — no full DOM.
struct DomNode {
    std::string tag;                   // lowercased tag name
    std::string id;                    // id attribute value, or ""
    bool has_id = false;               // whether an id attribute was present
    std::string class_name;            // raw class attribute value, or ""
    bool has_class = false;            // whether a class attribute was present
    std::vector<std::string> classes;  // class tokens (for .class matching)
    std::string src;                   // M3-5 src attribute value (script), or ""
    bool has_src = false;              // whether a src attribute was present
    // M3-8 raw read-only attribute table: canonical lowercase name -> raw value
    // (boolean/valueless attrs map to ""). Holds every parsed attribute EXCEPT
    // id/class, which keep their dedicated fields (getAttribute routes those to
    // the fields, everything else to this table). Duplicate names: last wins.
    std::unordered_map<std::string, std::string> attributes;
    std::string text_content;          // M2-7 aggregate text (precomputed)
    // M5-3/M5-4 <input>/<textarea>.value runtime slot. Initialized ONCE at
    // parse/create time (<input> from the `value` attribute, <textarea> from its
    // text content), then decoupled: value = ... writes here only. Only meaningful
    // for <input>/<textarea> (the property is exposed only there).
    std::string value;
    // M5-5 <option>.selected runtime bool. Seeded ONCE from the `selected`
    // attribute at parse/create time, then decoupled from the attribute. Only
    // meaningful for <option>.
    bool selected = false;
    // M5-7 <input>.checked runtime bool. Seeded ONCE from the `checked` attribute
    // at parse/create time, then decoupled. Only meaningful for <input> (minimal:
    // no type distinction, no radio-group exclusivity, no defaultChecked).
    bool checked = false;
    // M7-3 <form>.method runtime slot: the normalized submission method ("get" or
    // "post"). Seeded ONCE at parse/create from the `method` attribute (normalized),
    // defaulting to "get" (no attribute / createElement('form')), then decoupled from
    // the attribute. Only meaningful for <form> (the property is exposed only there);
    // form.reset() does NOT touch it. Minimal metadata: not "dialog", no other verbs.
    std::string method = "get";
    // M7-4 <form>.action runtime slot: the raw submission-address string. Seeded ONCE
    // at parse/create from the `action` attribute verbatim (NO URL parsing / relative-
    // to-absolute resolution / normalization), defaulting to "" (no attribute /
    // createElement('form')), then decoupled from the attribute. Only meaningful for
    // <form> (the property is exposed only there); form.reset() does NOT touch it.
    std::string action;
    // M7-5 <form>.enctype runtime slot: the normalized form-submission encoding, one
    // of the three HTML enctypes. Seeded ONCE at parse/create from the `enctype`
    // attribute (normalized), defaulting to "application/x-www-form-urlencoded" (no
    // attribute / createElement('form')), then decoupled from the attribute. Only
    // meaningful for <form>; form.reset() does NOT touch it. Metadata only — no body
    // encoding is implemented.
    std::string enctype = "application/x-www-form-urlencoded";
    // M7-6 <form>.target runtime slot: the raw submission-target string. Seeded ONCE
    // at parse/create from the `target` attribute verbatim (NO browsing-context lookup
    // / window resolution / _self,_blank,... special semantics), defaulting to "" (no
    // attribute / createElement('form')), then decoupled from the attribute. Only
    // meaningful for <form>; form.reset() does NOT touch it.
    std::string target;
    // M7-7 <form>.noValidate runtime bool: the form's validation-skip switch state.
    // Seeded ONCE at parse/create from the PRESENCE of the `novalidate` boolean
    // attribute (present -> true; absent / createElement('form') -> false), then
    // decoupled from the attribute. Only meaningful for <form>; form.reset() does NOT
    // touch it. M11-32: explicit checkValidity()/reportValidity() still validate even
    // when this switch is true.
    bool no_validate = false;
    // M11-32 constraint-validation runtime slot for input/button/select/textarea:
    // the message installed by setCustomValidity(). Empty means no custom error.
    // Decoupled from attributes; form.reset() does NOT clear it.
    std::string custom_validity;
    // M6-1 form.reset() baseline: the value/selected/checked snapshot captured at
    // parse/create (after the M5-5 select normalization), FIXED thereafter (it does
    // not follow later attribute/textContent edits). reset() restores value /
    // selected / checked from these. createElement nodes default to "" / false /
    // false, matching their created state.
    std::string initial_value;
    bool initial_selected = false;
    bool initial_checked = false;
    DomNode* parent = nullptr;         // owning parent element, or null (root)
    std::vector<DomNode*> children;    // owned by the DocumentHost node pool
    std::size_t content_start = 0;     // [start,end) of inner HTML in the source
    std::size_t content_end = 0;
};

// M3-5 script descriptor extracted from HTML in document order. An inline script
// has has_src == false and its JS source in `code`; a `<script src>` has
// has_src == true, its raw src attribute in `src`, and an empty `code` (its
// source is resolved by the host via Page.load(resources=...)).
struct ScriptRecord {
    bool has_src = false;
    std::string src;
    std::string code;
    DomNode* node = nullptr;  // M3-7 backing <script> node (for document.currentScript)
};

bool is_void_element(const std::string& tag) {
    static const char* const kVoid[] = {"area", "base", "br",    "col",
                                        "embed", "hr", "img",    "input",
                                        "link",  "meta", "param", "source",
                                        "track", "wbr"};
    for (const char* v : kVoid) {
        if (tag == v) {
            return true;
        }
    }
    return false;
}

// Split a class attribute value into tokens on ASCII whitespace.
std::vector<std::string> split_class_tokens(const std::string& value) {
    std::vector<std::string> tokens;
    std::size_t j = 0;
    const std::size_t m = value.size();
    while (j < m) {
        while (j < m && std::isspace(static_cast<unsigned char>(value[j]))) j++;
        const std::size_t ts = j;
        while (j < m && !std::isspace(static_cast<unsigned char>(value[j]))) j++;
        if (j > ts) tokens.push_back(value.substr(ts, j - ts));
    }
    return tokens;
}

// Capture id + class (+ M3-5 src) from the raw attribute text inside a start tag.
// Minimal attribute tokenizer (quoted / bare values). Only id, class, and src are
// retained — every other attribute is ignored. getAttribute still answers only id
// and class (M2-7/M2-8); src is captured solely for `<script src>` resolution and
// is NOT exposed through getAttribute.
void parse_attributes(const std::string& attrs, DomNode& node) {
    const std::size_t n = attrs.size();
    std::size_t i = 0;
    while (i < n) {
        while (i < n && std::isspace(static_cast<unsigned char>(attrs[i]))) i++;
        if (i >= n) break;
        const std::size_t name_start = i;
        while (i < n && attrs[i] != '=' && attrs[i] != '/' &&
               !std::isspace(static_cast<unsigned char>(attrs[i])))
            i++;
        const std::string name = ascii_lower(attrs.substr(name_start, i - name_start));
        std::string value;
        while (i < n && std::isspace(static_cast<unsigned char>(attrs[i]))) i++;
        if (i < n && attrs[i] == '=') {
            i++;
            while (i < n && std::isspace(static_cast<unsigned char>(attrs[i]))) i++;
            if (i < n && (attrs[i] == '"' || attrs[i] == '\'')) {
                const char quote = attrs[i++];
                const std::size_t vs = i;
                while (i < n && attrs[i] != quote) i++;
                value = attrs.substr(vs, i - vs);
                if (i < n) i++;  // closing quote
            } else {
                const std::size_t vs = i;
                while (i < n && attrs[i] != '/' &&
                       !std::isspace(static_cast<unsigned char>(attrs[i])))
                    i++;
                value = attrs.substr(vs, i - vs);
            }
        }
        if (name.empty()) {
            continue;  // malformed: ignore an empty attribute name
        }
        if (name == "id") {
            node.id = value;
            node.has_id = true;
        } else if (name == "class") {
            node.class_name = value;
            node.has_class = true;
            node.classes = split_class_tokens(value);
        } else {
            if (name == "src") {  // M3-5 keeps a dedicated src for resolution
                node.src = value;
                node.has_src = true;
            }
            // M3-8: every non-id/class attribute is readable via getAttribute.
            // operator[] overwrites, so a duplicate name keeps the last value.
            node.attributes[name] = value;
        }
    }
}

// Find the next case-insensitive "</script" at or after `from`, or npos. Used to
// treat <script> as a raw-text element (its body is not tag-parsed), so inline JS
// containing '<' / '>' is captured verbatim.
std::size_t find_script_close(const std::string& html, std::size_t from) {
    static const char kNeedle[] = "</script";
    const std::size_t needle_len = sizeof(kNeedle) - 1;
    const std::size_t n = html.size();
    for (std::size_t p = from; p + needle_len <= n; ++p) {
        bool match = true;
        for (std::size_t q = 0; q < needle_len; ++q) {
            const char c = static_cast<char>(
                std::tolower(static_cast<unsigned char>(html[p + q])));
            if (c != kNeedle[q]) {
                match = false;
                break;
            }
        }
        if (match) {
            return p;
        }
    }
    return std::string::npos;
}

// Minimal tag-stack HTML parse into a node forest. Text/comments/doctype are
// skipped; only start/end tags build structure. NOT HTML5-conformant. M3-5:
// <script> is handled as a raw-text element (its body is captured verbatim, not
// tag-parsed) and every script is appended to `scripts` in document order.
void parse_html(const std::string& html,
                std::vector<std::unique_ptr<DomNode>>& pool,
                std::vector<DomNode*>& roots,
                std::vector<ScriptRecord>& scripts) {
    std::vector<DomNode*> stack;
    const std::size_t n = html.size();
    std::size_t i = 0;
    while (i < n) {
        if (html[i] != '<') {
            i++;
            continue;
        }
        if (i + 1 >= n) break;
        const char c1 = html[i + 1];
        if (c1 == '/') {  // end tag
            const std::size_t end_tag_start = i;  // '<' of the closing tag
            std::size_t j = i + 2;
            const std::size_t ns = j;
            while (j < n && html[j] != '>' &&
                   !std::isspace(static_cast<unsigned char>(html[j])))
                j++;
            const std::string name = ascii_lower(html.substr(ns, j - ns));
            while (j < n && html[j] != '>') j++;
            if (j < n) j++;
            i = j;
            for (std::size_t k = stack.size(); k-- > 0;) {
                if (stack[k]->tag == name) {
                    stack[k]->content_end = end_tag_start;  // inner HTML range end
                    stack.resize(k);
                    break;
                }
            }
            continue;
        }
        if (c1 == '!') {  // comment / doctype
            std::size_t j = i + 2;
            while (j < n && html[j] != '>') j++;
            if (j < n) j++;
            i = j;
            continue;
        }
        // start tag
        std::size_t j = i + 1;
        const std::size_t ns = j;
        while (j < n && html[j] != '>' && html[j] != '/' &&
               !std::isspace(static_cast<unsigned char>(html[j])))
            j++;
        const std::string tag = ascii_lower(html.substr(ns, j - ns));
        const std::size_t body_start = j;
        while (j < n && html[j] != '>') j++;
        std::string body = html.substr(body_start, j - body_start);
        const bool self_closing = !body.empty() && body.back() == '/';
        if (self_closing) body.pop_back();
        if (j < n) j++;  // past '>'
        i = j;
        if (tag.empty()) continue;

        auto node = std::make_unique<DomNode>();
        node->tag = tag;
        node->content_start = i;   // just past '>'
        node->content_end = i;     // default empty; set when the end tag closes it
        parse_attributes(body, *node);
        // M5-3 / M5-6: seed <input>/<button>.value once from the parsed `value`
        // attribute (absent -> ""). Thereafter the runtime .value slot is decoupled
        // from the attribute.
        if (tag == "input" || tag == "button") {
            const auto vit = node->attributes.find("value");
            if (vit != node->attributes.end()) {
                node->value = vit->second;
            }
        }
        // M5-5: seed <option>.selected once from the boolean `selected` attribute
        // (present -> true). Decoupled from the attribute thereafter.
        if (tag == "option") {
            node->selected =
                node->attributes.find("selected") != node->attributes.end();
        }
        // M5-7: seed <input>.checked once from the boolean `checked` attribute
        // (present -> true). Decoupled from the attribute thereafter.
        if (tag == "input") {
            node->checked =
                node->attributes.find("checked") != node->attributes.end();
        }
        DomNode* raw = node.get();
        pool.push_back(std::move(node));
        if (stack.empty()) {
            roots.push_back(raw);
        } else {
            raw->parent = stack.back();
            stack.back()->children.push_back(raw);
        }

        // M3-5: <script> is a raw-text element. Consume its body verbatim up to
        // </script> (so inline JS with '<'/'>' is not mis-parsed as tags), record
        // the script in document order, and do NOT push it on the stack. A
        // self-closing <script/> carries no body and no script is recorded.
        if (tag == "script" && !self_closing) {
            const std::size_t close = find_script_close(html, i);
            const std::size_t content_end = (close == std::string::npos) ? n : close;
            raw->content_end = content_end;
            ScriptRecord record;
            record.has_src = raw->has_src;
            record.src = raw->src;
            record.node = raw;  // M3-7: backing node for document.currentScript
            if (!raw->has_src) {  // inline: capture the raw JS source
                record.code = html.substr(i, content_end - i);
            }
            scripts.push_back(std::move(record));
            // Advance past the </script ...> end tag.
            std::size_t k = (close == std::string::npos) ? n : close;
            while (k < n && html[k] != '>') k++;
            if (k < n) k++;
            i = k;
            continue;
        }

        if (!self_closing && !is_void_element(tag)) {
            stack.push_back(raw);
        }
    }

    // Precompute textContent for every node: inner-HTML range with tags stripped
    // (a naive aggregate; see docs). Void/self-closing/unclosed -> empty.
    for (const std::unique_ptr<DomNode>& node : pool) {
        if (node->content_end > node->content_start) {
            node->text_content = strip_tags(html.substr(
                node->content_start, node->content_end - node->content_start));
        }
        // M5-4: seed <textarea>.value once from its initial text content (empty ->
        // ""). Thereafter the runtime .value slot is decoupled from textContent.
        if (node->tag == "textarea") {
            node->value = node->text_content;
        }
    }
}

DomNode* dfs_find(DomNode* node, const std::function<bool(const DomNode*)>& pred) {
    if (pred(node)) return node;
    for (DomNode* child : node->children) {
        if (DomNode* found = dfs_find(child, pred)) return found;
    }
    return nullptr;
}

// First node (document order / pre-order) matching `pred`, or nullptr. Returns a
// mutable node so M2-8 mutations (setAttribute / textContent) can update it.
DomNode* find_first(const std::vector<DomNode*>& roots,
                    const std::function<bool(const DomNode*)>& pred) {
    for (DomNode* root : roots) {
        if (DomNode* found = dfs_find(root, pred)) return found;
    }
    return nullptr;
}

// M3-9: append every <script> node reachable in the CURRENT tree, in document
// (pre-)order, to `out`. Walks the live parent/child tree (not the M3-5 parse-time
// script records), so a mutation that detached a script subtree (e.g. an M2-8
// textContent write) is reflected — such scripts are simply no longer reachable.
void collect_scripts(DomNode* node, std::vector<DomNode*>& out) {
    if (node->tag == "script") {
        out.push_back(node);
    }
    for (DomNode* child : node->children) {
        collect_scripts(child, out);
    }
}

// M4-A-1: append every node in the CURRENT tree matching `pred`, in document
// (pre-)order, to `out`. Live tree walk (like collect_scripts), used by
// document.querySelectorAll / getElementsByTagName so mutation-detached subtrees
// drop out on re-query.
void collect_matching(DomNode* node,
                      const std::function<bool(const DomNode*)>& pred,
                      std::vector<DomNode*>& out) {
    if (pred(node)) {
        out.push_back(node);
    }
    for (DomNode* child : node->children) {
        collect_matching(child, pred, out);
    }
}

// M11-19: an element matches a multi-token class query when its class token set
// contains every requested token. The caller guarantees tokens is non-empty.
bool has_all_class_tokens(const DomNode* node,
                          const std::vector<std::string>& tokens) {
    for (const std::string& token : tokens) {
        if (std::find(node->classes.begin(), node->classes.end(), token) ==
            node->classes.end()) {
            return false;
        }
    }
    return true;
}

// M5-5: an <option>'s value — its `value` attribute if present (read live, so
// setAttribute('value', ...) is reflected), else its text content (empty -> "").
std::string option_value_of(const DomNode* option) {
    const auto it = option->attributes.find("value");
    if (it != option->attributes.end()) {
        return it->second;
    }
    return option->text_content;
}

// M7-3: minimal <form>.method normalization. ASCII-lowercase the raw string; if it
// is exactly "get" or "post", keep it; any other value (incl. "" and "dialog") maps
// to "get". This is the whole normalization contract — no attribute reflection.
std::string normalize_form_method(const std::string& raw) {
    const std::string lower = ascii_lower(raw);
    if (lower == "get" || lower == "post") {
        return lower;
    }
    return "get";
}

// M7-5: minimal <form>.enctype normalization. ASCII-lowercase the raw string; if it
// is one of the three HTML enctypes ("application/x-www-form-urlencoded",
// "multipart/form-data", "text/plain"), keep it; any other value maps to the default
// "application/x-www-form-urlencoded". No body-encoding behaviour — metadata only.
std::string normalize_form_enctype(const std::string& raw) {
    const std::string lower = ascii_lower(raw);
    if (lower == "multipart/form-data" || lower == "text/plain") {
        return lower;
    }
    return "application/x-www-form-urlencoded";
}

// M11-18: input.type / button.type reflection. The getter lower-cases the raw
// attribute and returns it only when it is a known type; an absent, empty, or
// unknown value maps to the documented default. The setter (see ElementHost)
// writes the raw string back to the attribute.
std::string normalize_input_type(const std::string& raw) {
    static const char* const known[] = {
        "hidden", "text",     "search", "tel",      "url",
        "email",  "password", "date",   "month",    "week",
        "time",   "datetime-local", "number", "range", "color",
        "checkbox", "radio",  "file",   "submit",   "image",
        "reset",  "button"};
    const std::string lower = ascii_lower(raw);
    for (const char* candidate : known) {
        if (lower == candidate) {
            return lower;
        }
    }
    return "text";
}

std::string normalize_button_type(const std::string& raw) {
    const std::string lower = ascii_lower(raw);
    if (lower == "reset" || lower == "button") {
        return lower;
    }
    return "submit";
}

// M5-5: the <option> descendants of `root` in document order (used by
// select.value's read/write and the initial-selection normalization).
inline std::vector<DomNode*> collect_options(DomNode* root) {
    std::vector<DomNode*> out;
    collect_matching(root, [](const DomNode* n) { return n->tag == "option"; },
                     out);
    return out;
}

// --- M11-32 minimal constraint validation ------------------------------------
//
// Supported control set: input / button / select / textarea (the same four
// form.elements returns). Supported constraints: required, input type=email /
// url typeMismatch, input/textarea minlength/maxlength, and customError from
// setCustomValidity(). The remaining ValidityState flags are exposed but always
// false (pattern/min/max/step/date/number semantics are deferred).
bool is_form_control(const DomNode* node) {
    return node->tag == "input" || node->tag == "button" ||
           node->tag == "select" || node->tag == "textarea";
}

bool has_attribute(const DomNode* node, const char* name) {
    return node->attributes.find(name) != node->attributes.end();
}

std::string attribute_value_or_empty(const DomNode* node, const char* name) {
    const auto it = node->attributes.find(name);
    return it != node->attributes.end() ? it->second : std::string();
}

std::string control_type_of(const DomNode* node) {
    if (node->tag == "input") {
        return normalize_input_type(attribute_value_or_empty(node, "type"));
    }
    if (node->tag == "button") {
        return normalize_button_type(attribute_value_or_empty(node, "type"));
    }
    return std::string();
}

bool is_text_like_input_type(const std::string& type) {
    return type == "text" || type == "search" || type == "tel" ||
           type == "url" || type == "email" || type == "password";
}

bool control_will_validate(const DomNode* node) {
    if (!is_form_control(node) || has_attribute(node, "disabled")) {
        return false;
    }
    if (node->tag == "input") {
        const std::string type = control_type_of(node);
        if (type == "hidden" || type == "reset" || type == "button" ||
            type == "image") {
            return false;
        }
        if (has_attribute(node, "readonly")) {
            return false;
        }
    } else if (node->tag == "textarea") {
        if (has_attribute(node, "readonly")) {
            return false;
        }
    }
    return true;
}

std::string control_value_of(DomNode* node) {
    if (node->tag == "select") {
        for (DomNode* option : collect_options(node)) {
            if (option->selected) {
                return option_value_of(option);
            }
        }
        return std::string();
    }
    return node->value;
}

// Parse a non-negative integer attribute; absent or malformed -> -1 (ignored).
int parse_non_negative_attribute(const DomNode* node, const char* name) {
    const auto it = node->attributes.find(name);
    if (it == node->attributes.end() || it->second.empty()) {
        return -1;
    }
    long long value = 0;
    for (char c : it->second) {
        if (c < '0' || c > '9') {
            return -1;
        }
        value = value * 10 + (c - '0');
        if (value > 2147483647LL) {
            return 2147483647;
        }
    }
    return static_cast<int>(value);
}

// Value length in UTF-16 code units (the unit browsers use for minlength /
// maxlength). Invalid UTF-8 bytes count as one unit each.
std::size_t utf16_length(const std::string& text) {
    std::size_t units = 0;
    std::size_t i = 0;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        std::uint32_t code_point = 0;
        std::size_t length = 1;
        if (c < 0x80) {
            code_point = c;
        } else if ((c & 0xE0) == 0xC0) {
            code_point = c & 0x1F;
            length = 2;
        } else if ((c & 0xF0) == 0xE0) {
            code_point = c & 0x0F;
            length = 3;
        } else if ((c & 0xF8) == 0xF0) {
            code_point = c & 0x07;
            length = 4;
        } else {
            units += 1;
            i += 1;
            continue;
        }
        if (i + length > text.size()) {
            units += 1;
            break;
        }
        bool valid = true;
        for (std::size_t k = 1; k < length; k++) {
            const unsigned char continuation =
                static_cast<unsigned char>(text[i + k]);
            if ((continuation & 0xC0) != 0x80) {
                valid = false;
                break;
            }
            code_point = (code_point << 6) | (continuation & 0x3F);
        }
        if (!valid) {
            units += 1;
            i += 1;
            continue;
        }
        units += (code_point > 0xFFFF) ? 2 : 1;
        i += length;
    }
    return units;
}

bool looks_like_email(const std::string& text) {
    std::size_t at = std::string::npos;
    for (std::size_t i = 0; i < text.size(); i++) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (c == '@') {
            if (at != std::string::npos) {
                return false;  // more than one @
            }
            at = i;
        } else if (std::isspace(c) || c < 0x20) {
            return false;
        }
    }
    return at != std::string::npos && at > 0 && at + 1 < text.size();
}

bool looks_like_absolute_url(const std::string& text) {
    if (text.empty() || !std::isalpha(static_cast<unsigned char>(text[0]))) {
        return false;
    }
    std::size_t i = 1;
    while (i < text.size()) {
        const unsigned char c = static_cast<unsigned char>(text[i]);
        if (!std::isalnum(c) && c != '+' && c != '-' && c != '.') {
            break;
        }
        i++;
    }
    if (i >= text.size() || text[i] != ':') {
        return false;
    }
    for (char c : text) {
        const unsigned char u = static_cast<unsigned char>(c);
        if (std::isspace(u) || u < 0x20) {
            return false;
        }
    }
    const std::string scheme = ascii_lower(text.substr(0, i));
    if (scheme == "http" || scheme == "https") {
        const std::size_t host_start = i + 3;  // past "://"
        if (text.compare(i + 1, 2, "//") != 0 ||
            host_start >= text.size() || text[host_start] == '/' ||
            text[host_start] == '?' || text[host_start] == '#') {
            return false;
        }
    }
    return true;
}

struct ValidityInfo {
    bool value_missing = false;
    bool type_mismatch = false;
    bool pattern_mismatch = false;
    bool too_long = false;
    bool too_short = false;
    bool range_underflow = false;
    bool range_overflow = false;
    bool step_mismatch = false;
    bool bad_input = false;
    bool custom_error = false;
    int min_length = -1;
    int max_length = -1;

    bool valid() const {
        return !(value_missing || type_mismatch || pattern_mismatch ||
                 too_long || too_short || range_underflow || range_overflow ||
                 step_mismatch || bad_input || custom_error);
    }
};

ValidityInfo compute_validity(DomNode* node) {
    ValidityInfo info;
    if (!control_will_validate(node)) {
        return info;  // barred controls are always valid
    }
    const std::string value = control_value_of(node);
    const std::string type = control_type_of(node);
    if (has_attribute(node, "required") && node->tag != "button") {
        const bool checked_control = type == "checkbox" || type == "radio";
        if (checked_control ? !node->checked : value.empty()) {
            info.value_missing = true;
        }
    }
    if (node->tag == "input" && !value.empty()) {
        if (type == "email" && !looks_like_email(value)) {
            info.type_mismatch = true;
        } else if (type == "url" && !looks_like_absolute_url(value)) {
            info.type_mismatch = true;
        }
    }
    const bool length_checked =
        node->tag == "textarea" ||
        (node->tag == "input" && is_text_like_input_type(type));
    if (length_checked) {
        const std::size_t length = utf16_length(value);
        info.min_length = parse_non_negative_attribute(node, "minlength");
        info.max_length = parse_non_negative_attribute(node, "maxlength");
        if (info.min_length >= 0 && !value.empty() &&
            length < static_cast<std::size_t>(info.min_length)) {
            info.too_short = true;
        }
        if (info.max_length >= 0 &&
            length > static_cast<std::size_t>(info.max_length)) {
            info.too_long = true;
        }
    }
    info.custom_error = !node->custom_validity.empty();
    return info;
}

std::string validation_message_of(const DomNode* node,
                                  const ValidityInfo& info) {
    if (info.custom_error) {
        return node->custom_validity;
    }
    if (info.value_missing) {
        return "Please fill out this field.";
    }
    if (info.type_mismatch) {
        return control_type_of(node) == "email"
                   ? "Please enter a valid email address."
                   : "Please enter a URL.";
    }
    if (info.too_short) {
        return "Please lengthen this text to " +
               std::to_string(info.min_length) +
               " characters or more.";
    }
    if (info.too_long) {
        return "Please shorten this text to " +
               std::to_string(info.max_length) +
               " characters or fewer.";
    }
    return std::string();
}

// --- M11-34 selector engine --------------------------------------------------
//
// A small CSS Selectors subset shared by querySelector(All), matches(), and
// closest(): selector lists, compound selectors, descendant / child / adjacent-
// sibling / general-sibling combinators, attribute selectors, and `*`. Pseudo-
// classes/pseudo-elements, CSS escapes, namespaces, and case-insensitivity
// flags are intentionally unsupported; an unparseable selector matches nothing
// (stable empty / null / false, matching the historical behavior).
struct SelectorAttributeTest {
    enum class Op {
        Exists,
        Equals,
        Includes,
        DashMatch,
        Prefix,
        Suffix,
        Substring,
    };
    std::string name;
    Op op = Op::Exists;
    std::string value;
};

struct SelectorCompound {
    std::string tag;  // empty = universal / no type selector
    std::vector<std::string> ids;
    std::vector<std::string> classes;
    std::vector<SelectorAttributeTest> attributes;
};

struct SelectorComplex {
    std::vector<SelectorCompound> compounds;
    // combinators[i] joins compounds[i] and compounds[i + 1]:
    // ' ' descendant, '>' child, '+' adjacent sibling, '~' general sibling.
    std::vector<char> combinators;
};

struct SelectorList {
    std::vector<SelectorComplex> selectors;
};

bool selector_name_char(char c) {
    const unsigned char u = static_cast<unsigned char>(c);
    return std::isalnum(u) || c == '-' || c == '_';
}

bool selector_attribute_name_char(char c) {
    return selector_name_char(c) || c == ':';
}

class SelectorParser {
public:
    explicit SelectorParser(const std::string& text) : text_(text) {}

    bool parse(SelectorList* out) {
        skip_ws();
        if (at_end()) {
            return false;
        }
        while (true) {
            SelectorComplex complex;
            if (!parse_complex(&complex)) {
                return false;
            }
            out->selectors.push_back(std::move(complex));
            skip_ws();
            if (at_end()) {
                return true;
            }
            if (peek() != ',') {
                return false;
            }
            ++pos_;
            skip_ws();
            if (at_end()) {
                return false;  // trailing comma
            }
        }
    }

private:
    bool at_end() const { return pos_ >= text_.size(); }

    char peek() const {
        return at_end() ? '\0' : text_[pos_];
    }

    void skip_ws() {
        while (!at_end() &&
               std::isspace(static_cast<unsigned char>(peek()))) {
            ++pos_;
        }
    }

    std::string read_name(bool attribute_name) {
        const std::size_t start = pos_;
        while (!at_end()) {
            const char c = peek();
            const bool ok = attribute_name ? selector_attribute_name_char(c)
                                           : selector_name_char(c);
            if (!ok) {
                break;
            }
            ++pos_;
        }
        return text_.substr(start, pos_ - start);
    }

    bool parse_complex(SelectorComplex* out) {
        SelectorCompound first;
        if (!parse_compound(&first)) {
            return false;
        }
        out->compounds.push_back(std::move(first));
        while (true) {
            const std::size_t before_ws = pos_;
            skip_ws();
            const bool had_ws = pos_ != before_ws;
            if (at_end() || peek() == ',') {
                return true;
            }
            char combinator = '\0';
            const char c = peek();
            if (c == '>' || c == '+' || c == '~') {
                combinator = c;
                ++pos_;
                skip_ws();
                if (at_end()) {
                    return false;
                }
            } else if (had_ws) {
                combinator = ' ';
            } else {
                return false;
            }
            SelectorCompound next;
            if (!parse_compound(&next)) {
                return false;
            }
            out->combinators.push_back(combinator);
            out->compounds.push_back(std::move(next));
        }
    }

    bool parse_compound(SelectorCompound* out) {
        bool any = false;
        if (peek() == '*') {
            ++pos_;
            any = true;
        } else if (!at_end() && selector_name_char(peek())) {
            const std::string name = read_name(/*attribute_name=*/false);
            if (name.empty()) {
                return false;
            }
            out->tag = ascii_lower(name);
            any = true;
        }
        while (!at_end()) {
            const char c = peek();
            if (c == '#') {
                ++pos_;
                const std::string id = read_name(/*attribute_name=*/false);
                if (id.empty()) {
                    return false;
                }
                out->ids.push_back(id);
                any = true;
            } else if (c == '.') {
                ++pos_;
                const std::string cls = read_name(/*attribute_name=*/false);
                if (cls.empty()) {
                    return false;
                }
                out->classes.push_back(cls);
                any = true;
            } else if (c == '[') {
                SelectorAttributeTest attribute;
                if (!parse_attribute(&attribute)) {
                    return false;
                }
                out->attributes.push_back(std::move(attribute));
                any = true;
            } else if (c == ':') {
                // Pseudo-classes / pseudo-elements are the M11-35 slice.
                return false;
            } else {
                break;
            }
        }
        return any;
    }

    bool parse_attribute(SelectorAttributeTest* out) {
        if (peek() != '[') {
            return false;
        }
        ++pos_;
        skip_ws();
        const std::string name = read_name(/*attribute_name=*/true);
        if (name.empty()) {
            return false;
        }
        out->name = ascii_lower(name);
        skip_ws();
        if (at_end()) {
            return false;
        }
        if (peek() == ']') {
            ++pos_;
            out->op = SelectorAttributeTest::Op::Exists;
            return true;
        }
        const char first = peek();
        if (first == '=') {
            out->op = SelectorAttributeTest::Op::Equals;
            ++pos_;
        } else if (first == '~' || first == '|' || first == '^' ||
                   first == '$' || first == '*') {
            if (pos_ + 1 >= text_.size() || text_[pos_ + 1] != '=') {
                return false;
            }
            switch (first) {
                case '~':
                    out->op = SelectorAttributeTest::Op::Includes;
                    break;
                case '|':
                    out->op = SelectorAttributeTest::Op::DashMatch;
                    break;
                case '^':
                    out->op = SelectorAttributeTest::Op::Prefix;
                    break;
                case '$':
                    out->op = SelectorAttributeTest::Op::Suffix;
                    break;
                default:
                    out->op = SelectorAttributeTest::Op::Substring;
                    break;
            }
            pos_ += 2;
        } else {
            return false;
        }
        skip_ws();
        if (!read_attribute_value(&out->value)) {
            return false;
        }
        skip_ws();
        if (at_end() || peek() != ']') {
            // The optional case-sensitivity flag (i/s) is not supported.
            return false;
        }
        ++pos_;
        return true;
    }

    bool read_attribute_value(std::string* out) {
        if (at_end()) {
            return false;
        }
        const char quote = peek();
        if (quote == '"' || quote == '\'') {
            ++pos_;
            while (!at_end()) {
                const char c = peek();
                if (c == quote) {
                    ++pos_;
                    return true;
                }
                if (c == '\\') {
                    return false;  // no CSS escapes
                }
                out->push_back(c);
                ++pos_;
            }
            return false;  // unterminated quote
        }
        const std::size_t start = pos_;
        while (!at_end()) {
            const char c = peek();
            if (std::isspace(static_cast<unsigned char>(c)) || c == ']') {
                break;
            }
            if (c == '\\') {
                return false;  // no CSS escapes
            }
            ++pos_;
        }
        if (pos_ == start) {
            return false;
        }
        *out = text_.substr(start, pos_ - start);
        return true;
    }

    const std::string& text_;
    std::size_t pos_ = 0;
};

bool selector_attribute_value(const DomNode* node, const std::string& name,
                              std::string* out) {
    if (name == "id") {
        if (!node->has_id) {
            return false;
        }
        *out = node->id;
        return true;
    }
    if (name == "class") {
        if (!node->has_class) {
            return false;
        }
        *out = node->class_name;
        return true;
    }
    const auto it = node->attributes.find(name);
    if (it == node->attributes.end()) {
        return false;
    }
    *out = it->second;
    return true;
}

bool selector_match_attribute(const DomNode* node,
                              const SelectorAttributeTest& test) {
    std::string value;
    if (!selector_attribute_value(node, test.name, &value)) {
        return false;
    }
    switch (test.op) {
        case SelectorAttributeTest::Op::Exists:
            return true;
        case SelectorAttributeTest::Op::Equals:
            return value == test.value;
        case SelectorAttributeTest::Op::Includes: {
            if (test.value.empty()) {
                return false;
            }
            for (char c : test.value) {
                if (std::isspace(static_cast<unsigned char>(c))) {
                    return false;
                }
            }
            for (const std::string& token : split_class_tokens(value)) {
                if (token == test.value) {
                    return true;
                }
            }
            return false;
        }
        case SelectorAttributeTest::Op::DashMatch:
            return value == test.value ||
                   (value.size() > test.value.size() &&
                    value.compare(0, test.value.size(), test.value) == 0 &&
                    value[test.value.size()] == '-');
        case SelectorAttributeTest::Op::Prefix:
            return !test.value.empty() &&
                   value.size() >= test.value.size() &&
                   value.compare(0, test.value.size(), test.value) == 0;
        case SelectorAttributeTest::Op::Suffix:
            return !test.value.empty() &&
                   value.size() >= test.value.size() &&
                   value.compare(value.size() - test.value.size(),
                                 test.value.size(), test.value) == 0;
        case SelectorAttributeTest::Op::Substring:
            return !test.value.empty() &&
                   value.find(test.value) != std::string::npos;
    }
    return false;
}

bool selector_match_compound(const DomNode* node,
                             const SelectorCompound& selector) {
    if (!selector.tag.empty() && node->tag != selector.tag) {
        return false;
    }
    for (const std::string& id : selector.ids) {
        if (!node->has_id || node->id != id) {
            return false;
        }
    }
    for (const std::string& cls : selector.classes) {
        if (std::find(node->classes.begin(), node->classes.end(), cls) ==
            node->classes.end()) {
            return false;
        }
    }
    for (const SelectorAttributeTest& attribute : selector.attributes) {
        if (!selector_match_attribute(node, attribute)) {
            return false;
        }
    }
    return true;
}

const DomNode* selector_previous_sibling(const DomNode* node) {
    if (node->parent == nullptr) {
        return nullptr;
    }
    const std::vector<DomNode*>& siblings = node->parent->children;
    for (std::size_t i = 1; i < siblings.size(); i++) {
        if (siblings[i] == node) {
            return siblings[i - 1];
        }
    }
    return nullptr;
}

bool selector_match_complex_at(const DomNode* node,
                               const SelectorComplex& selector,
                               std::size_t index) {
    if (index >= selector.compounds.size() ||
        !selector_match_compound(node, selector.compounds[index])) {
        return false;
    }
    if (index == 0) {
        return true;
    }
    const char combinator = selector.combinators[index - 1];
    switch (combinator) {
        case '>':
            return node->parent != nullptr &&
                   selector_match_complex_at(node->parent, selector, index - 1);
        case ' ':
            for (const DomNode* parent = node->parent; parent != nullptr;
                 parent = parent->parent) {
                if (selector_match_complex_at(parent, selector, index - 1)) {
                    return true;
                }
            }
            return false;
        case '+': {
            const DomNode* previous = selector_previous_sibling(node);
            return previous != nullptr &&
                   selector_match_complex_at(previous, selector, index - 1);
        }
        case '~':
            for (const DomNode* previous = selector_previous_sibling(node);
                 previous != nullptr;
                 previous = selector_previous_sibling(previous)) {
                if (selector_match_complex_at(previous, selector, index - 1)) {
                    return true;
                }
            }
            return false;
        default:
            return false;
    }
}

bool selector_match_list(const DomNode* node, const SelectorList& list) {
    for (const SelectorComplex& selector : list.selectors) {
        if (!selector.compounds.empty() &&
            selector_match_complex_at(node, selector,
                                      selector.compounds.size() - 1)) {
            return true;
        }
    }
    return false;
}

// Selector -> node predicate, shared by document- and element-level
// querySelector / querySelectorAll and by matches() / closest(). The whole
// expression is parsed once; an unsupported/malformed selector matches nothing.
std::function<bool(const DomNode*)> selector_predicate(const std::string& raw) {
    SelectorList list;
    SelectorParser parser(raw);
    if (!parser.parse(&list) || list.selectors.empty()) {
        return [](const DomNode*) { return false; };
    }
    return [list](const DomNode* node) {
        return selector_match_list(node, list);
    };
}

// getElementsByTagName predicate: ASCII case-insensitive tag; "*" = every element.
std::function<bool(const DomNode*)> tag_predicate(const std::string& raw) {
    if (raw == "*") {
        return [](const DomNode*) { return true; };
    }
    const std::string tag = ascii_lower(raw);
    return [tag](const DomNode* n) { return n->tag == tag; };
}

// M4-A-3: unlink `child` from its current parent's children (if any) and clear
// its parent pointer. `child`'s own subtree is untouched.
void detach_from_parent(DomNode* child) {
    DomNode* parent = child->parent;
    if (parent == nullptr) {
        return;
    }
    std::vector<DomNode*>& siblings = parent->children;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), child),
                   siblings.end());
    child->parent = nullptr;
}

// M11-5: capture the immediate element siblings of `child` in its current
// parent's child vector (either or both null). Used before a detach and after an
// insertion to fill a MutationRecord's previousSibling / nextSibling.
void siblings_of(const DomNode* child, DomNode** previous, DomNode** next) {
    *previous = nullptr;
    *next = nullptr;
    const DomNode* parent = child->parent;
    if (parent == nullptr) {
        return;
    }
    const std::vector<DomNode*>& siblings = parent->children;
    const auto it = std::find(siblings.begin(), siblings.end(), child);
    if (it == siblings.end()) {
        return;
    }
    if (it != siblings.begin()) {
        *previous = *(it - 1);
    }
    const auto next_it = it + 1;
    if (next_it != siblings.end()) {
        *next = *next_it;
    }
}

// M4-A-3: true if `candidate` is `of` itself or an ancestor of `of`. Used to
// reject inserting a node into its own subtree (would create a cycle).
bool is_self_or_ancestor(const DomNode* candidate, const DomNode* of) {
    for (const DomNode* node = of; node != nullptr; node = node->parent) {
        if (node == candidate) {
            return true;
        }
    }
    return false;
}

// M3-10: whether a <script> is a minimal *classic* (executable) script, by its
// raw `type` attribute. Executable iff: no type attribute, type is empty or only
// ASCII whitespace, or type (trimmed, ASCII-lowercased) is exactly
// "text/javascript" or "application/javascript". Everything else (module,
// importmap, application/json, text/plain, any other non-empty type) is
// non-executable — it stays in the DOM / document.scripts but does not run.
bool is_executable_classic_script(const DomNode* node) {
    const auto it = node->attributes.find("type");
    if (it == node->attributes.end()) {
        return true;  // no type attribute -> classic
    }
    const std::string& raw = it->second;
    std::size_t a = 0;
    std::size_t b = raw.size();
    while (a < b && std::isspace(static_cast<unsigned char>(raw[a]))) a++;
    while (b > a && std::isspace(static_cast<unsigned char>(raw[b - 1]))) b--;
    if (a == b) {
        return true;  // empty or whitespace-only -> classic
    }
    const std::string type = ascii_lower(raw.substr(a, b - a));
    return type == "text/javascript" || type == "application/javascript";
}

class DocumentHost;  // ElementHost holds a back-pointer to its document

// --- M11-5 MutationObserver ---------------------------------------------------
// A record captured at mutation time. Node pointers are stable for the whole page
// generation (DocumentHost's pools are never reallocated); the JS MutationRecord
// object is built at delivery/takeRecords() time.
struct MutationRecordData {
    enum class Type { ChildList, Attributes };
    Type type = Type::ChildList;
    DomNode* target = nullptr;
    std::vector<DomNode*> added;
    std::vector<DomNode*> removed;
    DomNode* previous_sibling = nullptr;
    DomNode* next_sibling = nullptr;
    std::string attribute_name;
    bool has_old_value = false;
    std::string old_value;
};

struct MutationObserverRegistration {
    DomNode* target = nullptr;  // null for the document target
    bool target_is_document = false;
    bool child_list = false;
    bool attributes = false;
    bool subtree = false;
    bool attribute_old_value = false;
    bool has_attribute_filter = false;
    std::vector<std::string> attribute_filter;
};

// Per-observer native state. Owned by the generation's DocumentHost; V8 Global
// handles are released in DocumentHost::release_v8_handles() (isolate-alive
// teardown hook). No JS-GC finalizer unregisters an observer in this phase.
struct MutationObserverState {
    DocumentHost* owner = nullptr;
    v8::Global<v8::Function> callback;
    v8::Global<v8::Object> self;
    std::vector<MutationObserverRegistration> registrations;
    std::vector<MutationRecordData> pending;
    bool delivery_queued = false;
    bool alive = true;
};

void mutation_observer_delivery(const v8::FunctionCallbackInfo<v8::Value>& info);

// --- M4-A-7 minimal bubbling helpers -----------------------------------------
// Whether `event.bubbles` is truthy (missing -> false).
bool event_bubbles(v8::Isolate* isolate, v8::Local<v8::Context> context,
                   v8::Local<v8::Object> event) {
    v8::Local<v8::Value> value;
    if (event->Get(context, v8_string(isolate, "bubbles")).ToLocal(&value)) {
        return value->BooleanValue(isolate);
    }
    return false;
}

// Internal "propagation stopped" / "immediate propagation stopped" flags,
// stored as V8 Privates on the event so they are invisible to normal property
// access (not part of the public Event face).
v8::Local<v8::Private> event_stopped_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate, v8::String::NewFromUtf8Literal(isolate, "iv8::event-stopped"));
}
v8::Local<v8::Private> event_immediate_stopped_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8::event-immediate-stopped"));
}
v8::Local<v8::Private> event_passive_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate, v8::String::NewFromUtf8Literal(isolate, "iv8::event-passive"));
}
bool event_in_passive_listener(v8::Isolate* isolate,
                               v8::Local<v8::Context> context,
                               v8::Local<v8::Object> event) {
    v8::Local<v8::Value> value;
    if (event->GetPrivate(context, event_passive_key(isolate)).ToLocal(&value)) {
        return value->BooleanValue(isolate);
    }
    return false;
}
void set_event_in_passive_listener(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   v8::Local<v8::Object> event, bool passive) {
    (void)event->SetPrivate(context, event_passive_key(isolate),
                            v8::Boolean::New(isolate, passive));
}
void reset_event_stopped(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         v8::Local<v8::Object> event) {
    (void)event->SetPrivate(context, event_stopped_key(isolate),
                            v8::False(isolate));
    (void)event->SetPrivate(context, event_immediate_stopped_key(isolate),
                            v8::False(isolate));
}
bool event_is_stopped(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Object> event) {
    v8::Local<v8::Value> value;
    if (event->GetPrivate(context, event_stopped_key(isolate)).ToLocal(&value)) {
        return value->BooleanValue(isolate);
    }
    return false;
}
bool event_is_immediately_stopped(v8::Isolate* isolate,
                                  v8::Local<v8::Context> context,
                                  v8::Local<v8::Object> event) {
    v8::Local<v8::Value> value;
    if (event->GetPrivate(context, event_immediate_stopped_key(isolate))
            .ToLocal(&value)) {
        return value->BooleanValue(isolate);
    }
    return false;
}
// event.stopPropagation(): set the private flag on the receiver event. Stops
// propagation to LATER targets only; remaining listeners on the current target
// still run.
void stop_propagation_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    (void)info.This()->SetPrivate(context, event_stopped_key(isolate),
                                  v8::True(isolate));
}
// event.stopImmediatePropagation(): stop later listeners on this target and all
// later targets. Setting the ordinary stop flag as well keeps the existing
// bubbling checks correct without touching every call site.
void stop_immediate_propagation_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    (void)info.This()->SetPrivate(context, event_stopped_key(isolate),
                                  v8::True(isolate));
    (void)info.This()->SetPrivate(context, event_immediate_stopped_key(isolate),
                                  v8::True(isolate));
}
// event.preventDefault(): no-op unless the event is cancelable; otherwise mark
// defaultPrevented (an ordinary own field, visible to scripts and to the
// dispatchEvent return value).
void prevent_default_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (event_in_passive_listener(isolate, context, info.This())) {
        return;  // passive listeners cannot cancel
    }
    v8::Local<v8::Value> cancelable;
    if (!info.This()
             ->Get(context, v8_string(isolate, "cancelable"))
             .ToLocal(&cancelable) ||
        !cancelable->BooleanValue(isolate)) {
        return;
    }
    (void)info.This()->Set(context, v8_string(isolate, "defaultPrevented"),
                           v8::True(isolate));
}
bool event_default_prevented(v8::Isolate* isolate,
                             v8::Local<v8::Context> context,
                             v8::Local<v8::Object> event) {
    v8::Local<v8::Value> value;
    if (event->Get(context, v8_string(isolate, "defaultPrevented"))
            .ToLocal(&value)) {
        return value->BooleanValue(isolate);
    }
    return false;
}

// --- M3-3 minimal event model ------------------------------------------------
// EventTargetHost is a mixin for host objects that support events (document,
// element). It stores JS listener functions per event type as isolate-safe
// Global handles and implements addEventListener / removeEventListener /
// dispatchEvent.
//
// This is deliberately NOT the DOM Events spec: a single flat listener list per
// type, fired in registration order. Each target fires via fire_at (snapshot,
// swallow exceptions, `this` = the target). M4-A-7 adds minimal BUBBLING over the
// current tree (element -> ancestors -> document -> window) for events with
// bubbles === true, plus stopPropagation() (skips later targets only). Still NO
// capture phase, no preventDefault / stopImmediatePropagation / defaultPrevented,
// no listener options (once/capture/passive), and no default actions. Listeners
// are JS functions only — there is no JS->Python callback bridge in this phase.
// The retained Global handles are released via release_v8_handles() before the
// owning context's isolate is disposed (PageState wires this into ContextState's
// teardown hook).
class EventTargetHost : public HostObject {
public:
    // Reset every retained listener handle. Runs (via PageState's teardown hook)
    // while the isolate is still alive, mirroring the timer-handle reset.
    void release_v8_handles() noexcept override {
        for (auto& entry : listeners_) {
            for (Listener& listener : entry.second) {
                listener.callback.Reset();
            }
        }
        listeners_.clear();
    }

    // If `name` is an event-target method, handle it, write the JS result to
    // `out`, and return true; otherwise return false so the concrete host handles
    // its own methods. Runs inside the isolate/context scopes (during eval), under
    // the operation guard, with the Python GIL already released. Public so the
    // M3-4 window event functions (bare globals, not a host-object trampoline) can
    // share the exact same semantics as document/element.
    bool handle_event_method(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             const std::string& name,
                             const v8::FunctionCallbackInfo<v8::Value>& args,
                             v8::Local<v8::Value>& out) {
        if (name == "addEventListener") {
            add_listener(isolate, type_arg(isolate, context, args, 0),
                         listener_arg(args, 1),
                         listener_options(isolate, context, args, 2));
            out = v8::Undefined(isolate);
            return true;
        }
        if (name == "removeEventListener") {
            remove_listener(
                isolate, type_arg(isolate, context, args, 0),
                listener_arg(args, 1),
                listener_options(isolate, context, args, 2).capture);
            out = v8::Undefined(isolate);
            return true;
        }
        if (name == "dispatchEvent") {
            // A non-object event dispatches to nothing (returns true). A
            // cancelable event that called preventDefault returns false.
            bool canceled = false;
            if (args.Length() >= 1 && args[0]->IsObject()) {
                v8::Local<v8::Object> event = args[0].As<v8::Object>();
                dispatch_event(isolate, context, event, args.This());
                canceled = event_default_prevented(isolate, context, event);
            }
            out = v8::Boolean::New(isolate, !canceled);
            return true;
        }
        return false;
    }

    // M11-11: dispatch `event` starting at this target (`receiver` is the JS
    // object dispatchEvent was called on). The base does a single-target dispatch
    // (window / FileReader use this); ElementHost / DocumentHost override to walk
    // the current tree. Capture and bubble listeners both run at target, in
    // registration order, with eventPhase === 2.
    virtual void dispatch_event(v8::Isolate* isolate,
                                v8::Local<v8::Context> context,
                                v8::Local<v8::Object> event,
                                v8::Local<v8::Object> receiver) {
        (void)event->Set(context, v8_string(isolate, "target"), receiver);
        reset_event_stopped(isolate, context, event);
        fire_target_at(isolate, context, event, receiver);
        finish_dispatch(isolate, context, event);
    }

    // Phase-specific firing. Capture phase filters capture listeners, bubble phase
    // filters bubble listeners, and AT_TARGET runs both in registration order.
    void fire_capture_at(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         v8::Local<v8::Object> event,
                         v8::Local<v8::Value> receiver) {
        fire_phase(isolate, context, event, receiver, 1, true, false);
    }
    void fire_bubble_at(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> event,
                        v8::Local<v8::Value> receiver) {
        fire_phase(isolate, context, event, receiver, 3, false, true);
    }
    void fire_target_at(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> event,
                        v8::Local<v8::Value> receiver) {
        fire_phase(isolate, context, event, receiver, 2, true, true);
    }

    // Dispatch is over: eventPhase returns to NONE.
    void finish_dispatch(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         v8::Local<v8::Object> event) {
        (void)event->Set(context, v8_string(isolate, "eventPhase"),
                         v8::Integer::New(isolate, 0));
    }

    // M3-4 / M11-11: dispatch a synthetic lifecycle / FileReader event of `type`
    // from C++ (no JS caller). Single target, at-target phase, no bubbling.
    void dispatch_native(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         const std::string& type, v8::Local<v8::Object> receiver) {
        v8::Local<v8::Object> event = v8::Object::New(isolate);
        (void)event->Set(context, v8_string(isolate, "type"),
                         v8_string(isolate, type));
        (void)event->Set(context, v8_string(isolate, "target"), receiver);
        fire_target_at(isolate, context, event, receiver);
        finish_dispatch(isolate, context, event);
    }

protected:
    // The event-target method names every EventTargetHost exposes; concrete hosts
    // append these to their own method_names().
    static const std::vector<std::string>& event_method_names() {
        static const std::vector<std::string> names = {
            "addEventListener", "removeEventListener", "dispatchEvent"};
        return names;
    }

private:
    struct ListenerOptions {
        bool capture = false;
        bool once = false;
        bool passive = false;
    };
    struct Listener {
        v8::Global<v8::Function> callback;
        bool capture = false;
        bool once = false;
        bool passive = false;
    };
    struct ListenerSnapshot {
        v8::Local<v8::Function> callback;
        bool capture = false;
        bool once = false;
        bool passive = false;
    };

    // args[index] coerced to a UTF-8 string (the event `type`), or "".
    static std::string type_arg(v8::Isolate* isolate,
                                v8::Local<v8::Context> context,
                                const v8::FunctionCallbackInfo<v8::Value>& args,
                                int index) {
        if (index >= args.Length()) {
            return std::string();
        }
        v8::Local<v8::String> str;
        if (!args[index]->ToString(context).ToLocal(&str)) {
            return std::string();
        }
        v8::String::Utf8Value utf8(isolate, str);
        return *utf8 != nullptr
                   ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
                   : std::string();
    }

    // args[index] if it is a JS function, else an empty handle (non-callable
    // listeners are ignored, keeping add/removeEventListener non-throwing).
    static v8::Local<v8::Function> listener_arg(
        const v8::FunctionCallbackInfo<v8::Value>& args, int index) {
        if (index < args.Length() && args[index]->IsFunction()) {
            return args[index].As<v8::Function>();
        }
        return v8::Local<v8::Function>();
    }

    static bool listener_option_bool(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> options,
                                     const char* name) {
        v8::Local<v8::Value> value;
        if (!options->Get(context, v8_string(isolate, name)).ToLocal(&value)) {
            return false;
        }
        return value->BooleanValue(isolate);
    }

    // options may be a boolean (capture) or an object with capture / once /
    // passive. Missing / null / other values use the defaults.
    static ListenerOptions listener_options(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const v8::FunctionCallbackInfo<v8::Value>& args, int index) {
        ListenerOptions out;
        if (index >= args.Length()) {
            return out;
        }
        v8::Local<v8::Value> value = args[index];
        if (value->IsObject()) {
            v8::Local<v8::Object> options = value.As<v8::Object>();
            out.capture =
                listener_option_bool(isolate, context, options, "capture");
            out.once = listener_option_bool(isolate, context, options, "once");
            out.passive =
                listener_option_bool(isolate, context, options, "passive");
        } else if (!value->IsUndefined() && !value->IsNull()) {
            out.capture = value->BooleanValue(isolate);
        }
        return out;
    }

    void add_listener(v8::Isolate* isolate, const std::string& type,
                      v8::Local<v8::Function> callback,
                      ListenerOptions options) {
        if (callback.IsEmpty()) {
            return;  // non-callable listener: ignored
        }
        std::vector<Listener>& bucket = listeners_[type];
        for (Listener& existing : bucket) {
            if (existing.capture == options.capture &&
                existing.callback.Get(isolate)->StrictEquals(callback)) {
                return;  // dedupe by (type, callback, capture)
            }
        }
        bucket.emplace_back();
        Listener& listener = bucket.back();
        listener.callback.Reset(isolate, callback);
        listener.capture = options.capture;
        listener.once = options.once;
        listener.passive = options.passive;
    }

    void remove_listener_handle(v8::Isolate* isolate, const std::string& type,
                                v8::Local<v8::Function> callback,
                                bool capture) {
        if (callback.IsEmpty()) {
            return;
        }
        auto it = listeners_.find(type);
        if (it == listeners_.end()) {
            return;
        }
        std::vector<Listener>& bucket = it->second;
        for (auto entry = bucket.begin(); entry != bucket.end(); ++entry) {
            if (entry->capture == capture &&
                entry->callback.Get(isolate)->StrictEquals(callback)) {
                entry->callback.Reset();
                bucket.erase(entry);
                return;  // remove only the first exact identity
            }
        }
    }

    void remove_listener(v8::Isolate* isolate, const std::string& type,
                         v8::Local<v8::Function> callback, bool capture) {
        remove_listener_handle(isolate, type, callback, capture);
    }

    // Fire the matching listeners for one target/phase. Capture listeners are
    // included when include_capture is true, bubble listeners when
    // include_bubble is true (AT_TARGET passes both, in registration order).
    void fire_phase(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    v8::Local<v8::Object> event,
                    v8::Local<v8::Value> receiver, int phase,
                    bool include_capture, bool include_bubble) {
        (void)event->Set(context, v8_string(isolate, "currentTarget"),
                         receiver);
        (void)event->Set(context, v8_string(isolate, "eventPhase"),
                         v8::Integer::New(isolate, phase));

        std::string type;
        v8::Local<v8::Value> type_value;
        if (event->Get(context, v8_string(isolate, "type")).ToLocal(&type_value)) {
            v8::Local<v8::String> str;
            if (type_value->ToString(context).ToLocal(&str)) {
                v8::String::Utf8Value utf8(isolate, str);
                if (*utf8 != nullptr) {
                    type.assign(*utf8, static_cast<std::size_t>(utf8.length()));
                }
            }
        }

        auto it = listeners_.find(type);
        if (it == listeners_.end()) {
            return;
        }
        std::vector<ListenerSnapshot> snapshot;
        snapshot.reserve(it->second.size());
        for (Listener& listener : it->second) {
            if ((listener.capture && include_capture) ||
                (!listener.capture && include_bubble)) {
                snapshot.push_back(ListenerSnapshot{
                    listener.callback.Get(isolate), listener.capture,
                    listener.once, listener.passive});
            }
        }
        v8::Local<v8::Value> event_arg = event;
        for (ListenerSnapshot& listener : snapshot) {
            if (listener.once) {
                // Remove before the call so a reentrant dispatch cannot call it
                // again.
                remove_listener_handle(isolate, type, listener.callback,
                                       listener.capture);
            }
            const bool previous_passive =
                event_in_passive_listener(isolate, context, event);
            // Always set (false for active listeners) so a nested dispatch from
            // a passive listener lets its own active listeners call
            // preventDefault; restore the outer state afterwards.
            set_event_in_passive_listener(isolate, context, event,
                                          listener.passive);
            v8::TryCatch try_catch(isolate);
            (void)listener.callback->Call(context, receiver, 1, &event_arg);
            if (try_catch.HasCaught()) {
                try_catch.Reset();  // one listener's error never stops the rest
            }
            set_event_in_passive_listener(isolate, context, event,
                                          previous_passive);
            if (event_is_immediately_stopped(isolate, context, event)) {
                break;  // stopImmediatePropagation: skip later same-target listeners
            }
        }
    }

    // Per-type listener lists (registration order). Global handles are released in
    // release_v8_handles() before isolate disposal.
    std::unordered_map<std::string, std::vector<Listener>> listeners_;
};

// M3-4 window event target. window / globalThis / self stay the intrinsic global
// object (M2-2, so window === globalThis === self and globals live on it); this
// backing object ONLY holds the global's event listeners and provides the
// add/remove/dispatch semantics (inherited from EventTargetHost). It is NOT
// installed as a named global host object — the three window.* methods are bare
// global functions whose External data points at this instance (see install_page
// / window_event_method), so window stays a real object, not a host wrapper.
class WindowEventTarget : public EventTargetHost {
public:
    std::string global_name() const override { return std::string(); }  // never installed
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override { return {}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate, v8::Local<v8::Context>,
                                      const std::string&) override {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        return v8::Undefined(isolate);
    }
};

// M11-8 FileReader state. The listener store reuses EventTargetHost; the object
// itself is a plain JS instance whose prototype methods dispatch back here via a
// private slot. States are owned by DocumentHost for the generation, so pending
// microtask deliveries and listeners can be released at teardown.
class FileReaderEvents : public EventTargetHost {
public:
    std::string global_name() const override { return std::string(); }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override { return {}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate, v8::Local<v8::Context>,
                                      const std::string&) override {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        return v8::Undefined(isolate);
    }
};

struct FileReaderState {
    enum class Mode { None, Text, ArrayBuffer };
    DocumentHost* owner = nullptr;
    FileReaderEvents events;
    v8::Global<v8::Object> self;
    std::vector<std::uint8_t> bytes;
    Mode mode = Mode::None;
    bool pending = false;
    bool alive = true;
};

// M11-10: backing for one `element.classList` object. Bindings are owned by the
// generation's DocumentHost; the returned classList is a fresh plain object per
// access (the runtime's usual no-identity-guarantee model).
struct ClassListBinding {
    DocumentHost* document = nullptr;
    DomNode* node = nullptr;
};

// M2-6/M2-7 element host object. Read-only, minimal node/element surface:
// tagName / nodeName / nodeType / id / className / textContent + parentNode /
// childNodes / children + getAttribute / hasAttribute. No mutation, no full
// Node/Element layer. parent/child accessors wrap related nodes back through the
// owning DocumentHost. Methods are defined out-of-line (below DocumentHost).
class ElementHost : public EventTargetHost {
public:
    ElementHost(DomNode* node, DocumentHost* document)
        : node_(node), document_(document) {}

    std::string global_name() const override { return std::string(); }  // never global
    bool is_element() const override { return true; }  // M4-A-3 arg identification
    std::string trace_object_type() const override { return "element"; }
    std::string trace_label() const override { return node_->tag; }

    // M11-5: read-only accessors used by the MutationObserver argument checks.
    DomNode* node() const { return node_; }
    DocumentHost* document() const { return document_; }
    std::vector<std::string> property_names() const override {
        std::vector<std::string> names = {
            "tagName",     "nodeName",   "nodeType",   "id",
            "className",   "textContent", "parentNode", "childNodes",
            "children",
            // M4-A-6 connectivity / element-sibling navigation.
            "ownerDocument", "isConnected", "previousElementSibling",
            "nextElementSibling",
            // M4-B-1 structural navigation (element-only tree).
            "parentElement", "firstElementChild", "lastElementChild",
            "childElementCount",
            // M11-13 raw Node aliases (element-only tree).
            "firstChild", "lastChild", "previousSibling", "nextSibling",
            // M11-10 classList (fresh plain object per access).
            "classList"};
        // M5-1: `elements` is exposed only on <form> (so non-form elements have no
        // `.elements` property at all). M7-3..7: `method` / `action` / `enctype` /
        // `target` (string) + `noValidate` (bool) — all read-write.
        if (node_->tag == "form") {
            names.push_back("elements");
            names.push_back("method");
            names.push_back("action");
            names.push_back("enctype");
            // M11-18: alias of the enctype runtime slot.
            names.push_back("encoding");
            names.push_back("target");
            names.push_back("noValidate");
        }
        // M5-2: `form` (owner-form) is exposed only on the four form controls.
        if (node_->tag == "input" || node_->tag == "button" ||
            node_->tag == "select" || node_->tag == "textarea") {
            names.push_back("form");
            // M11-32: minimal constraint-validation read surface.
            names.push_back("willValidate");
            names.push_back("validity");
            names.push_back("validationMessage");
        }
        // M5-3/4/5/6: `value` is exposed on <input> / <textarea> / <button>
        // (writable text slot), <select> (writable, derived from selected option)
        // and <option> (read-only, derived from attr/text).
        if (node_->tag == "input" || node_->tag == "textarea" ||
            node_->tag == "button" || node_->tag == "select" ||
            node_->tag == "option") {
            names.push_back("value");
        }
        // M11-16: select collection / selection / attribute reflection.
        if (node_->tag == "select") {
            names.push_back("options");
            // M11-22: read-only option count.
            names.push_back("length");
            names.push_back("selectedIndex");
            names.push_back("multiple");
            names.push_back("size");
            // M11-21: reflected name/required/disabled.
            names.push_back("name");
            names.push_back("required");
            names.push_back("disabled");
        }
        // M5-5: `selected` (read-write bool) is exposed only on <option>;
        // M5-8: `text` (read-only) too; M6-3: `defaultSelected` (read-only reset
        // baseline) too.
        if (node_->tag == "option") {
            names.push_back("selected");
            names.push_back("text");
            names.push_back("defaultSelected");
            // M11-18: label reflection + read-only position in the owning select.
            names.push_back("label");
            names.push_back("index");
        }
        // M5-7: `checked` (read-write bool) is exposed only on <input>;
        // M6-2/M6-4: `defaultChecked` / `defaultValue` (read-only reset baselines).
        if (node_->tag == "input") {
            names.push_back("checked");
            names.push_back("defaultChecked");
            names.push_back("defaultValue");
            // M11-18/M11-21: reflected attribute surface.
            names.push_back("type");
            names.push_back("disabled");
            names.push_back("readOnly");
            names.push_back("placeholder");
            names.push_back("name");
            names.push_back("required");
        }
        if (node_->tag == "button") {
            // M11-18/M11-21: reflected attribute surface.
            names.push_back("type");
            names.push_back("disabled");
            names.push_back("name");
        }
        // M6-5: `defaultValue` (read-only reset baseline) also on <textarea> (its
        // baseline was seeded from the initial text content, M5-4/M6-1).
        if (node_->tag == "textarea") {
            names.push_back("defaultValue");
            // M11-21: reflected attribute surface.
            names.push_back("name");
            names.push_back("required");
            names.push_back("disabled");
            names.push_back("readOnly");
            names.push_back("placeholder");
        }
        return names;
    }
    std::vector<std::string> method_names() const override {
        // M2-7 read + M2-8/M4-A-4 attribute writes + M4-A-3 tree editing +
        // M4-A-5 subtree queries + M3-3 event methods.
        std::vector<std::string> names = {
            "getAttribute",     "hasAttribute",    "setAttribute",
            "removeAttribute",  "hasAttributes",   "getAttributeNames",
            "appendChild",      "removeChild",     "insertBefore",
            "replaceChild",     "remove",          "hasChildNodes",
            "cloneNode",        "isSameNode",      "isEqualNode",
            "querySelector",    "querySelectorAll", "getElementsByTagName",
            // M11-19 descendant class query.
            "getElementsByClassName",
            // M4-B-2 minimal ancestor/containment check.
            "contains",
            // M4-B-3 single-node selector match.
            "matches",
            // M4-B-4 nearest-ancestor selector match.
            "closest",
            // M11-10 boolean attribute toggle.
            "toggleAttribute"};
        // M6-1: reset() is exposed only on <form>; M7-1: submit(); M7-2: requestSubmit().
        if (node_->tag == "form") {
            names.push_back("reset");
            names.push_back("submit");
            names.push_back("requestSubmit");
            // M11-32: form-level constraint-validation entry points.
            names.push_back("checkValidity");
            names.push_back("reportValidity");
        }
        if (is_form_control(node_)) {
            // M11-32: control-level constraint-validation entry points.
            names.push_back("checkValidity");
            names.push_back("reportValidity");
            names.push_back("setCustomValidity");
        }
        if (node_->tag == "select") {
            // M11-22: option lookup helpers (the options collection stays an Array).
            names.push_back("item");
            names.push_back("namedItem");
        }
        const std::vector<std::string>& events = event_method_names();
        names.insert(names.end(), events.begin(), events.end());
        return names;
    }
    // M2-8: textContent is writable; M5-3/4/5/6: value on input/textarea/button/
    // select (option.value is READ-ONLY); M5-5: option.selected is writable.
    std::vector<std::string> writable_property_names() const override {
        std::vector<std::string> names = {"textContent", "id", "className"};
        if (node_->tag == "input" || node_->tag == "textarea" ||
            node_->tag == "button" || node_->tag == "select") {
            names.push_back("value");
        }
        if (node_->tag == "option") {
            names.push_back("selected");
        }
        if (node_->tag == "input") {
            names.push_back("checked");
            // M11-18/M11-21: reflected attribute writes.
            names.push_back("type");
            names.push_back("disabled");
            names.push_back("readOnly");
            names.push_back("placeholder");
            names.push_back("name");
            names.push_back("required");
        }
        if (node_->tag == "button") {
            // M11-18/M11-21: reflected attribute writes.
            names.push_back("type");
            names.push_back("disabled");
            names.push_back("name");
        }
        if (node_->tag == "textarea") {
            // M11-21: reflected attribute writes.
            names.push_back("name");
            names.push_back("required");
            names.push_back("disabled");
            names.push_back("readOnly");
            names.push_back("placeholder");
        }
        if (node_->tag == "select") {
            // M11-21: reflected attribute writes.
            names.push_back("name");
            names.push_back("required");
            names.push_back("disabled");
            names.push_back("selectedIndex");
        }
        if (node_->tag == "option") {
            // M11-18: label is writable; index is read-only.
            names.push_back("label");
        }
        // M7-3..7: form.method (normalized) / action (raw) / enctype (normalized) /
        // target (raw) strings + noValidate (bool coercion) are all read-write.
        if (node_->tag == "form") {
            names.push_back("method");
            names.push_back("action");
            names.push_back("enctype");
            // M11-18: alias of enctype.
            names.push_back("encoding");
            names.push_back("target");
            names.push_back("noValidate");
        }
        return names;
    }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context,
                                      const std::string& name) override;
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      const std::string& name,
                      v8::Local<v8::Value> value) override;
    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& args) override;

    // M11-32: validate this control (or this form's controls), dispatching a
    // minimal non-bubbling `invalid` event at each invalid control. Both the
    // control and form forms of checkValidity()/reportValidity() return the
    // boolean result; there is no UI or alert.
    bool validate_control(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Object> receiver);
    bool validate_form(v8::Isolate* isolate, v8::Local<v8::Context> context);
    void fire_invalid_event(v8::Isolate* isolate, v8::Local<v8::Context> context,
                            v8::Local<v8::Object> receiver);

    // M4-A-7: bubble a dispatched event along the current tree (defined out-of-line
    // below DocumentHost, which owns the bubbling walk).
    void dispatch_event(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> event,
                        v8::Local<v8::Object> receiver) override;

private:
    // M4-A-3: recover the backing DomNode* from a JS element-host argument, or
    // nullptr if it is not an element host object (a non-element host object such
    // as document, a plain JS value, etc.). Uses is_element() (no RTTI); the
    // static_cast is safe because only ElementHost returns is_element()==true.
    // Same-class access reads node_.
    DomNode* node_of_arg(v8::Local<v8::Value> value) {
        HostObject* host = host_object_backing(value);
        if (host == nullptr || !host->is_element()) {
            return nullptr;
        }
        return static_cast<ElementHost*>(host)->node_;
    }

    DomNode* node_;            // owned by document_ (lives for the page generation)
    DocumentHost* document_;
};

// M2-6 document host object (M2-7 hands out node-capable elements). Owns the
// parsed tree + element wrappers for this page generation, so a load()/dispose()
// that tears down the context invalidates everything together.
class DocumentHost : public EventTargetHost {
public:
    DocumentHost(const std::string& html, std::string url)
        : url_(std::move(url)), title_(extract_title(html)) {
        parse_html(html, pool_, roots_, scripts_);
        // M5-5: deterministic initial single-select — within each <select>, keep
        // only the document-order-first pre-selected <option>, clearing the rest.
        // (No auto-select when none is pre-selected: select.value is then "".)
        for (const std::unique_ptr<DomNode>& node : pool_) {
            if (node->tag != "select") {
                continue;
            }
            bool seen = false;
            for (DomNode* option : collect_options(node.get())) {
                if (!option->selected) {
                    continue;
                }
                if (seen) {
                    option->selected = false;
                } else {
                    seen = true;
                }
            }
        }
        // M7-3/M7-4/M7-5/M7-6: seed each <form>'s .method / .action / .enctype /
        // .target slots from the matching attributes, decoupled from the attribute
        // thereafter. method is normalized to "get"/"post" (absent -> default "get");
        // action/target are seeded verbatim (no URL/context parsing; absent -> "");
        // enctype is normalized to one of the three HTML enctypes (absent -> default).
        for (const std::unique_ptr<DomNode>& node : pool_) {
            if (node->tag != "form") {
                continue;
            }
            const auto mit = node->attributes.find("method");
            if (mit != node->attributes.end()) {
                node->method = normalize_form_method(mit->second);
            }
            const auto ait = node->attributes.find("action");
            if (ait != node->attributes.end()) {
                node->action = ait->second;
            }
            const auto eit = node->attributes.find("enctype");
            if (eit != node->attributes.end()) {
                node->enctype = normalize_form_enctype(eit->second);
            }
            const auto tit = node->attributes.find("target");
            if (tit != node->attributes.end()) {
                node->target = tit->second;
            }
            // M7-7: novalidate is a boolean attribute — seed from its PRESENCE.
            node->no_validate = node->attributes.count("novalidate") != 0;
        }
        // M6-1: snapshot the form.reset() baseline AFTER seeding + normalization —
        // the fully-seeded, normalized value/selected/checked. Fixed thereafter
        // (runtime attribute/textContent/.value/.checked/.selected edits do not
        // move it). createElement nodes keep their defaults ("" / false / false).
        for (const std::unique_ptr<DomNode>& node : pool_) {
            node->initial_value = node->value;
            node->initial_selected = node->selected;
            node->initial_checked = node->checked;
        }
    }

    // M3-5: the scripts found in the HTML, in document order (inline + external).
    const std::vector<ScriptRecord>& scripts() const { return scripts_; }

    // M3-6: this generation's JS document.readyState. Defaults to "complete" (so
    // a fresh Page()'s blank generation reads "complete"); Page.load drives the
    // "loading" -> "interactive" -> "complete" migration for an explicit load.
    void set_ready_state(const std::string& state) { ready_state_ = state; }

    // M3-7: the <script> node currently executing (document.currentScript). Set to
    // scripts_[index]'s node while that HTML script runs, cleared to null after.
    void set_current_script(std::size_t index) {
        current_script_ = index < scripts_.size() ? scripts_[index].node : nullptr;
    }
    void clear_current_script() { current_script_ = nullptr; }

    std::string global_name() const override { return "document"; }
    std::string trace_object_type() const override { return "document"; }
    std::vector<std::string> property_names() const override {
        return {"URL",  "title",           "readyState",  "documentElement",
                "head", "body",            "currentScript", "scripts",
                // M4-B-6 document's direct element children.
                "children",
                // M4-B-7 all <form> elements in the current tree.
                "forms",
                // M4-B-8 all <img> elements in the current tree.
                "images",
                // M4-B-9 <a>/<area> elements that carry an href attribute.
                "links",
                // M4-B-10 <a> elements that carry a name attribute.
                "anchors",
                // M4-B-11 all <embed> elements in the current tree.
                "embeds",
                // M4-B-12 all <applet> elements in the current tree.
                "applets",
                // M11-23 deterministic document metadata.
                "characterSet", "contentType", "compatMode", "referrer",
                "domain", "visibilityState", "hidden", "activeElement"};
    }
    std::vector<std::string> method_names() const override {
        // M2-6 document methods + M4-A-1 static queries + M4-A-2 createElement +
        // the M3-3 event methods.
        std::vector<std::string> names = {"getElementById", "querySelector",
                                          "querySelectorAll", "getElementsByTagName",
                                          "createElement",
                                          // M4-B-13 / M11-19 class query.
                                          "getElementsByClassName",
                                          // M11-20 name query.
                                          "getElementsByName",
                                          // M11-23 deterministic focus report.
                                          "hasFocus"};
        const std::vector<std::string>& events = event_method_names();
        names.insert(names.end(), events.begin(), events.end());
        return names;
    }

    // Release the document's own listener handles AND every wrapped element's
    // (the ElementHosts are owned here, not in PageState::host_objects_, so the
    // teardown hook only reaches them through the document).
    void release_v8_handles() noexcept override {
        EventTargetHost::release_v8_handles();
        for (const std::unique_ptr<ElementHost>& element : elements_) {
            element->release_v8_handles();
        }
        // M11-5: reset observer-backed persistent handles before isolate disposal.
        for (const std::unique_ptr<MutationObserverState>& observer : observers_) {
            observer->alive = false;
            observer->callback.Reset();
            observer->self.Reset();
            observer->registrations.clear();
            observer->pending.clear();
        }
        // M11-8: drop the generation's Blob constructor and any pending
        // FileReader deliveries/listeners before isolate disposal.
        blob_constructor_.Reset();
        for (const std::unique_ptr<FileReaderState>& reader : file_readers_) {
            reader->alive = false;
            reader->pending = false;
            reader->bytes.clear();
            reader->self.Reset();
            reader->events.release_v8_handles();
        }
    }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context,
                                      const std::string& name) override {
        if (name == "URL") return v8_string(isolate, url_);
        if (name == "title") return v8_string(isolate, title_);
        if (name == "readyState") return v8_string(isolate, ready_state_);
        // M11-23: deterministic document metadata (read-only).
        if (name == "characterSet") return v8_string(isolate, "UTF-8");
        if (name == "contentType") return v8_string(isolate, "text/html");
        if (name == "compatMode") return v8_string(isolate, "CSS1Compat");
        if (name == "referrer") return v8_string(isolate, std::string());
        if (name == "domain") {
            return v8_string(isolate, decompose_url(url_).hostname);
        }
        if (name == "visibilityState") return v8_string(isolate, "visible");
        if (name == "hidden") return v8::False(isolate);
        if (name == "activeElement") {
            return wrap_element(isolate, context, find_tag("body"));
        }
        if (name == "currentScript") {  // M3-7: element while a HTML script runs, else null
            return wrap_element(isolate, context, current_script_);
        }
        if (name == "scripts") {  // M3-9: current tree's <script> elements, document order
            std::vector<DomNode*> nodes;
            for (DomNode* root : roots_) {
                collect_scripts(root, nodes);
            }
            v8::Local<v8::Array> array =
                v8::Array::New(isolate, static_cast<int>(nodes.size()));
            for (std::size_t k = 0; k < nodes.size(); ++k) {
                (void)array->Set(context, static_cast<std::uint32_t>(k),
                                 wrap_element(isolate, context, nodes[k]));
            }
            return array;
        }
        if (name == "documentElement") {
            return wrap_element(isolate, context, find_tag("html"));
        }
        if (name == "head") {  // M4-A-1: first <head> in the current tree, or null
            return wrap_element(isolate, context, find_tag("head"));
        }
        if (name == "body") {
            return wrap_element(isolate, context, find_tag("body"));
        }
        if (name == "children") {  // M4-B-6: document's direct element children
            // roots_ are the top-level parsed nodes (all elements in this text-node-
            // free model), in document order — the document's direct children. A
            // blank generation has none -> []. Reuses the same plain-Array wrapping
            // as document.scripts / query collections (no HTMLCollection, no
            // identity guarantee).
            return elements_array(isolate, context, roots_);
        }
        if (name == "forms") {  // M4-B-7: all <form> elements in the current tree
            // Same live-tree collection + plain-Array wrapping as
            // getElementsByTagName('form') (shared elements_by_tag): whole tree,
            // document order, detached <form>s excluded. <form> is treated as a
            // plain element — no HTMLFormElement / submit / elements association.
            return elements_array(isolate, context, elements_by_tag("form"));
        }
        if (name == "images") {  // M4-B-8: all <img> elements in the current tree
            // Same live-tree collector as getElementsByTagName('img'): whole tree,
            // document order, detached <img>s excluded. <img> is a plain element —
            // no HTMLImageElement / .src / loading / decoding / events / network.
            return elements_array(isolate, context, elements_by_tag("img"));
        }
        if (name == "links") {  // M4-B-9: <a>/<area> elements carrying an href attr
            // Same live-tree walk (collect over roots_, document order, detached
            // excluded), filtered to <a>/<area> that have an "href" attribute
            // (presence only — value irrelevant; a valueless href counts). Reuses
            // the M3-8/M4-A-4 attribute model (href lives in node.attributes; names
            // are lowercased). <a>/<area> stay plain elements — no navigation, no
            // .href reflection, no target/rel/download semantics.
            std::vector<DomNode*> nodes = collect([](const DomNode* n) {
                return (n->tag == "a" || n->tag == "area") &&
                       n->attributes.find("href") != n->attributes.end();
            });
            return elements_array(isolate, context, nodes);
        }
        if (name == "anchors") {  // M4-B-10: <a> elements carrying a name attribute
            // Same live-tree walk (collect over roots_, document order, detached
            // excluded), filtered to <a> that have a "name" attribute (presence
            // only — value irrelevant; a valueless name counts). <area> and other
            // tags never match. Reuses the M3-8/M4-A-4 attribute model; <a> stays a
            // plain element — no navigation / .href / fragment jump / HTMLAnchorElement.
            std::vector<DomNode*> nodes = collect([](const DomNode* n) {
                return n->tag == "a" &&
                       n->attributes.find("name") != n->attributes.end();
            });
            return elements_array(isolate, context, nodes);
        }
        if (name == "embeds") {  // M4-B-11: all <embed> elements in the current tree
            // Same live-tree collector as getElementsByTagName('embed'): whole tree,
            // document order, detached <embed>s excluded. <embed> is a plain (void)
            // element — no plugin/media loading, network, .src/.type reflection,
            // events, or HTMLEmbedElement; no document.plugins.
            return elements_array(isolate, context, elements_by_tag("embed"));
        }
        if (name == "applets") {  // M4-B-12: all <applet> elements in the current tree
            // Same live-tree collector as getElementsByTagName('applet'): whole
            // tree, document order, detached <applet>s excluded. <applet> is a plain
            // element — no plugin/Java/media/network, no .code/.archive/.object
            // reflection, no events/playback/sizing, no HTMLAppletElement.
            return elements_array(isolate, context, elements_by_tag("applet"));
        }
        return v8::Undefined(isolate);
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& args) override {
        // M9-2: record the "document.<method>" call before dispatch.
        watch_record(isolate, "document", name);
        // M3-3: addEventListener / removeEventListener / dispatchEvent first.
        v8::Local<v8::Value> event_result;
        if (handle_event_method(isolate, context, name, args, event_result)) {
            return event_result;
        }
        std::string arg;
        if (args.Length() > 0) {
            v8::String::Utf8Value utf8(isolate, args[0]);
            if (*utf8 != nullptr) {
                arg.assign(*utf8, static_cast<std::size_t>(utf8.length()));
            }
        }
        if (name == "getElementById") {
            return wrap_element(isolate, context, find_id(arg));
        }
        if (name == "querySelector") {
            return wrap_element(isolate, context, query(arg));
        }
        if (name == "querySelectorAll") {  // M4-A-1: all matches, document order
            return elements_array(isolate, context, query_all(arg));
        }
        if (name == "getElementsByTagName") {  // M4-A-1
            return elements_array(isolate, context, elements_by_tag(arg));
        }
        if (name == "getElementsByClassName") {  // M4-B-13 / M11-19 class query
            // String(name) split into class tokens (same tokenizer as className /
            // setAttribute('class', ...)). Match every element whose class token
            // set contains ALL tokens (multi-token intersection). Empty /
            // whitespace-only -> [] (no throw). Case-sensitive. Live tree, document
            // order, detached elements excluded (collect walks roots_).
            std::vector<std::string> tokens = split_class_tokens(arg);
            std::vector<DomNode*> nodes;
            if (!tokens.empty()) {
                nodes = collect([&tokens](const DomNode* n) {
                    return has_all_class_tokens(n, tokens);
                });
            }
            return elements_array(isolate, context, nodes);
        }
        if (name == "getElementsByName") {  // M11-20: exact name-attribute query
            std::vector<DomNode*> nodes = collect([&arg](const DomNode* n) {
                const auto it = n->attributes.find("name");
                return it != n->attributes.end() && it->second == arg;
            });
            return elements_array(isolate, context, nodes);
        }
        if (name == "hasFocus") {  // M11-23: no focus machinery -> always false
            return v8::False(isolate);
        }
        if (name == "createElement") {  // M4-A-2: detached element (not in the tree)
            auto node = std::make_unique<DomNode>();
            node->tag = ascii_lower(arg);  // arg == String(tag); tagName is UPPER
            DomNode* raw = node.get();
            detached_pool_.push_back(std::move(node));
            return wrap_element(isolate, context, raw);
        }
        return v8::Undefined(isolate);
    }

    // M11-14: deep/shallow detached clone. The clone is inserted into the
    // detached pool (never into roots_), so document queries ignore it.
    DomNode* clone_detached(DomNode* source, bool deep) {
        auto clone = std::make_unique<DomNode>();
        DomNode* raw = clone.get();
        raw->tag = source->tag;
        raw->id = source->id;
        raw->has_id = source->has_id;
        raw->class_name = source->class_name;
        raw->has_class = source->has_class;
        raw->classes = source->classes;
        raw->src = source->src;
        raw->has_src = source->has_src;
        raw->attributes = source->attributes;
        raw->text_content = source->text_content;
        raw->value = source->value;
        raw->selected = source->selected;
        raw->checked = source->checked;
        raw->method = source->method;
        raw->action = source->action;
        raw->enctype = source->enctype;
        raw->target = source->target;
        raw->no_validate = source->no_validate;
        raw->initial_value = source->initial_value;
        raw->initial_selected = source->initial_selected;
        raw->initial_checked = source->initial_checked;
        raw->content_start = source->content_start;
        raw->content_end = source->content_end;
        if (deep) {
            for (DomNode* child : source->children) {
                DomNode* child_clone = clone_detached(child, true);
                child_clone->parent = raw;
                raw->children.push_back(child_clone);
            }
        }
        detached_pool_.push_back(std::move(clone));
        return raw;
    }

    // M11-14: structural equality used by Element.isEqualNode.
    bool nodes_equal(const DomNode* a, const DomNode* b) const {
        if (a == b) {
            return true;
        }
        if (a == nullptr || b == nullptr) {
            return false;
        }
        if (a->tag != b->tag || a->id != b->id || a->has_id != b->has_id ||
            a->class_name != b->class_name ||
            a->has_class != b->has_class || a->classes != b->classes ||
            a->src != b->src || a->has_src != b->has_src ||
            a->attributes != b->attributes ||
            a->text_content != b->text_content || a->value != b->value ||
            a->selected != b->selected || a->checked != b->checked ||
            a->method != b->method || a->action != b->action ||
            a->enctype != b->enctype || a->target != b->target ||
            a->no_validate != b->no_validate ||
            a->initial_value != b->initial_value ||
            a->initial_selected != b->initial_selected ||
            a->initial_checked != b->initial_checked) {
            return false;
        }
        if (a->children.size() != b->children.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a->children.size(); i++) {
            if (!nodes_equal(a->children[i], b->children[i])) {
                return false;
            }
        }
        return true;
    }

    // M11-32: ensure the ElementHost for a node (the same cached host used by
    // wrap_element), so form-level validation can dispatch invalid events through
    // that control's listeners even if JS never wrapped it.
    ElementHost* ensure_element_host(DomNode* node) {
        const auto it = wrappers_.find(node);
        if (it != wrappers_.end()) {
            return it->second;
        }
        auto element = std::make_unique<ElementHost>(node, this);
        ElementHost* host = element.get();
        elements_.push_back(std::move(element));
        wrappers_.emplace(node, host);
        return host;
    }

    // Wrap a node as a JS element host object (JS null for nullptr). Public so
    // ElementHost can wrap parents/children. Wrappers are cached + owned here.
    v8::Local<v8::Value> wrap_element(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context,
                                      DomNode* node) {
        if (node == nullptr) return v8::Null(isolate);
        return make_host_object(isolate, context, ensure_element_host(node));
    }

    // Build a plain JS Array of element host objects for `nodes` (same wrapping as
    // document.scripts — no NodeList/HTMLCollection, no identity guarantee). Public
    // so ElementHost's M4-A-5 subtree queries reuse it.
    v8::Local<v8::Array> elements_array(v8::Isolate* isolate,
                                        v8::Local<v8::Context> context,
                                        const std::vector<DomNode*>& nodes) {
        v8::Local<v8::Array> array =
            v8::Array::New(isolate, static_cast<int>(nodes.size()));
        for (std::size_t k = 0; k < nodes.size(); ++k) {
            (void)array->Set(context, static_cast<std::uint32_t>(k),
                             wrap_element(isolate, context, nodes[k]));
        }
        return array;
    }

    // M11-5 MutationObserver: per-generation registry. The constructor stores a
    // pointer to the state in the observer's private slot; mutation hooks below
    // queue native records and schedule one microtask delivery per observer.
    MutationObserverState* create_mutation_observer(
        v8::Isolate* isolate, v8::Local<v8::Function> callback,
        v8::Local<v8::Object> self);
    void queue_child_list(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          DomNode* parent,
                          const std::vector<DomNode*>& added,
                          const std::vector<DomNode*>& removed,
                          DomNode* previous_sibling, DomNode* next_sibling);
    void queue_attribute(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         DomNode* target, const std::string& name,
                         bool has_old_value, const std::string& old_value);
    v8::Local<v8::Array> build_records_array(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::vector<MutationRecordData>& records);
    // Fast-path guard: mutation hooks stay cheap when nobody is observing.
    bool has_mutation_observers() const { return !observers_.empty(); }

    // M4-A-6: whether `node` is connected to this document's tree — i.e. its
    // topmost ancestor (following parent) is one of roots_. Detached nodes (and
    // whole removed subtrees) are not. Public for ElementHost.isConnected.
    bool is_in_tree(const DomNode* node) const {
        const DomNode* top = node;
        while (top->parent != nullptr) {
            top = top->parent;
        }
        for (const DomNode* root : roots_) {
            if (root == top) {
                return true;
            }
        }
        return false;
    }

    // M4-A-7: remember window's event target so element/document bubbling can
    // reach it as the final hop. Set once at install time.
    void set_window_target(EventTargetHost* window_target) {
        window_target_ = window_target;
    }

    // M11-8: Blob constructor used by Blob.slice() (which builds a fresh Blob),
    // and FileReader states owned for this generation.
    void set_blob_constructor(v8::Isolate* isolate,
                              v8::Local<v8::Function> ctor) {
        blob_constructor_.Reset(isolate, ctor);
    }
    v8::Local<v8::Function> blob_constructor(v8::Isolate* isolate) {
        return blob_constructor_.Get(isolate);
    }
    FileReaderState* create_file_reader(v8::Isolate* isolate,
                                        v8::Local<v8::Object> self) {
        auto state = std::make_unique<FileReaderState>();
        state->owner = this;
        state->self.Reset(isolate, self);
        FileReaderState* raw = state.get();
        file_readers_.push_back(std::move(state));
        return raw;
    }

    // M11-10: own a classList binding for this generation.
    ClassListBinding* create_class_list_binding(DomNode* node) {
        auto binding = std::make_unique<ClassListBinding>();
        binding->document = this;
        binding->node = node;
        ClassListBinding* raw = binding.get();
        class_list_bindings_.push_back(std::move(binding));
        return raw;
    }

    // M4-A-7: dispatch `event` from an element and bubble it along the CURRENT
    // tree. `node` is the element's backing node, `element_host` its host (whose
    // listeners fire first), `receiver` its JS object (the fixed event.target).
    // Path: element -> ancestor elements (via parentNode) -> document -> window.
    // Bubbling past the element only happens when event.bubbles === true; the
    // document/window hops happen only when the element is connected to the tree
    // (a detached subtree bubbles internally but never escapes to document/window).
    // stopPropagation() blocks only LATER hops (checked between hops); the current
    // target always finishes its own listeners.
    void run_element_bubbling(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              v8::Local<v8::Object> event, DomNode* node,
                              EventTargetHost* element_host,
                              v8::Local<v8::Object> receiver) {
        (void)event->Set(context, v8_string(isolate, "target"), receiver);
        reset_event_stopped(isolate, context, event);
        const bool bubbles = event_bubbles(isolate, context, event);
        std::vector<DomNode*> ancestors;
        for (DomNode* ancestor = node->parent; ancestor != nullptr;
             ancestor = ancestor->parent) {
            ancestors.push_back(ancestor);
        }
        const bool connected = is_in_tree(node);
        const auto stopped = [&]() {
            return event_is_stopped(isolate, context, event);
        };

        // Capture: window -> document -> topmost ancestor -> ... -> parent.
        if (connected) {
            if (window_target_ != nullptr) {
                window_target_->fire_capture_at(isolate, context, event,
                                                context->Global());
                if (stopped()) {
                    finish_dispatch(isolate, context, event);
                    return;
                }
            }
            fire_capture_at(isolate, context, event,
                            document_global(isolate, context));
            if (stopped()) {
                finish_dispatch(isolate, context, event);
                return;
            }
        }
        for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
            fire_element_hop(isolate, context, event, *it, true);
            if (stopped()) {
                finish_dispatch(isolate, context, event);
                return;
            }
        }

        // AT_TARGET: capture and bubble listeners in registration order.
        element_host->fire_target_at(isolate, context, event, receiver);
        if (!bubbles || stopped()) {
            finish_dispatch(isolate, context, event);
            return;
        }

        // Bubble: parent -> ... -> topmost ancestor (connected only), then
        // document -> window.
        for (DomNode* ancestor : ancestors) {
            fire_element_hop(isolate, context, event, ancestor, false);
            if (stopped()) {
                finish_dispatch(isolate, context, event);
                return;
            }
        }
        if (connected) {
            fire_bubble_at(isolate, context, event,
                           document_global(isolate, context));
            if (stopped() || window_target_ == nullptr) {
                finish_dispatch(isolate, context, event);
                return;
            }
            window_target_->fire_bubble_at(isolate, context, event,
                                           context->Global());
        }
        finish_dispatch(isolate, context, event);
    }

    // M11-11: dispatchEvent called on `document` directly. Window capture runs
    // first; document is the target; window bubble runs last for bubbling events.
    void dispatch_event(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> event,
                        v8::Local<v8::Object> receiver) override {
        (void)event->Set(context, v8_string(isolate, "target"), receiver);
        reset_event_stopped(isolate, context, event);
        if (window_target_ != nullptr) {
            window_target_->fire_capture_at(isolate, context, event,
                                            context->Global());
            if (event_is_stopped(isolate, context, event)) {
                finish_dispatch(isolate, context, event);
                return;
            }
        }
        fire_target_at(isolate, context, event, receiver);
        if (!event_bubbles(isolate, context, event) ||
            event_is_stopped(isolate, context, event) ||
            window_target_ == nullptr) {
            finish_dispatch(isolate, context, event);
            return;
        }
        window_target_->fire_bubble_at(isolate, context, event,
                                       context->Global());
        finish_dispatch(isolate, context, event);
    }

private:
    // M11-5: create/enqueue the one pending microtask delivery for `state`.
    void schedule_mutation_delivery(v8::Isolate* isolate,
                                    v8::Local<v8::Context> context,
                                    MutationObserverState* state);

    // Fire one element node's listeners as a single capture/bubble hop: wrap the
    // node and run its matching listeners with itself as currentTarget. A node
    // with no wrapper/listeners is a no-op.
    void fire_element_hop(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Object> event, DomNode* node,
                          bool capture) {
        v8::Local<v8::Value> js = wrap_element(isolate, context, node);
        if (!js->IsObject()) {
            return;
        }
        const auto it = wrappers_.find(node);
        if (it == wrappers_.end()) {
            return;
        }
        if (capture) {
            it->second->fire_capture_at(isolate, context, event,
                                        js.As<v8::Object>());
        } else {
            it->second->fire_bubble_at(isolate, context, event,
                                       js.As<v8::Object>());
        }
    }

    // The global `document` object (event.currentTarget for the document hop), or
    // the global object as a harmless fallback if it cannot be read.
    v8::Local<v8::Value> document_global(v8::Isolate* isolate,
                                         v8::Local<v8::Context> context) {
        v8::Local<v8::Value> doc;
        if (context->Global()
                ->Get(context, v8_string(isolate, "document"))
                .ToLocal(&doc) &&
            doc->IsObject()) {
            return doc;
        }
        return context->Global();
    }

    DomNode* find_tag(const std::string& tag) const {
        return find_first(roots_,
                          [&](const DomNode* n) { return n->tag == tag; });
    }
    DomNode* find_id(const std::string& id) const {
        if (id.empty()) return nullptr;
        return find_first(roots_, [&](const DomNode* n) { return n->id == id; });
    }
    DomNode* find_class(const std::string& cls) const {
        if (cls.empty()) return nullptr;
        return find_first(roots_, [&](const DomNode* n) {
            return std::find(n->classes.begin(), n->classes.end(), cls) !=
                   n->classes.end();
        });
    }
    // document.querySelector: first match of the minimal selector subset over the
    // whole tree (document order), or nullptr. (M4-A-5 shares selector_predicate.)
    DomNode* query(const std::string& raw) const {
        return find_first(roots_, selector_predicate(raw));
    }

    // M4-A-1: collect all current-tree nodes matching `pred`, document order.
    std::vector<DomNode*> collect(
        const std::function<bool(const DomNode*)>& pred) const {
        std::vector<DomNode*> out;
        for (DomNode* root : roots_) {
            collect_matching(root, pred, out);
        }
        return out;
    }

    // document.querySelectorAll: all matches over the whole tree, document order.
    std::vector<DomNode*> query_all(const std::string& raw) const {
        return collect(selector_predicate(raw));
    }

    // document.getElementsByTagName: case-insensitive tag / "*", over the tree.
    std::vector<DomNode*> elements_by_tag(const std::string& raw) const {
        return collect(tag_predicate(raw));
    }

    std::string url_;
    std::string title_;
    std::string ready_state_ = "complete";  // M3-6 JS document.readyState
    DomNode* current_script_ = nullptr;      // M3-7 document.currentScript backing
    std::vector<std::unique_ptr<DomNode>> pool_;
    std::vector<DomNode*> roots_;
    // M4-A-2: createElement() nodes. Owned here for the page generation (stable,
    // no dangling) but NEVER linked into roots_/children, so they are detached —
    // invisible to find_*/collect_* (queries, document.scripts) and to the tree.
    std::vector<std::unique_ptr<DomNode>> detached_pool_;
    std::vector<ScriptRecord> scripts_;  // M3-5 scripts in document order
    std::vector<std::unique_ptr<ElementHost>> elements_;
    std::unordered_map<DomNode*, ElementHost*> wrappers_;
    // M11-5: per-generation MutationObserver state (Globals released through the
    // isolate-alive teardown hook in release_v8_handles()).
    std::vector<std::unique_ptr<MutationObserverState>> observers_;
    // M11-8: per-generation FileReader states and the Blob constructor backing
    // slice(). Both are released in release_v8_handles().
    std::vector<std::unique_ptr<FileReaderState>> file_readers_;
    v8::Global<v8::Function> blob_constructor_;
    // M11-10: classList bindings owned for the generation.
    std::vector<std::unique_ptr<ClassListBinding>> class_list_bindings_;
    // M4-A-7: window's event target (final bubbling hop). Observed, not owned;
    // set at install time. Null before install / in the both-modes shell.
    EventTargetHost* window_target_ = nullptr;
};

// --- M11-5 MutationObserver native registry / delivery -----------------------

// Tag for the Externals carrying MutationObserverState* / DocumentHost*. V8 15.0
// requires an ExternalPointerTypeTag on External::New / External::Value; the
// default suffices and MUST match between New and Value call sites (same rule as
// kWindowEventsTag below, see 52ded6e).
constexpr v8::ExternalPointerTypeTag kMutationObserverTag =
    v8::kExternalPointerTypeTagDefault;

MutationObserverState* DocumentHost::create_mutation_observer(
    v8::Isolate* isolate, v8::Local<v8::Function> callback,
    v8::Local<v8::Object> self) {
    auto state = std::make_unique<MutationObserverState>();
    state->owner = this;
    state->callback.Reset(isolate, callback);
    state->self.Reset(isolate, self);
    MutationObserverState* raw = state.get();
    observers_.push_back(std::move(state));
    return raw;
}

void DocumentHost::schedule_mutation_delivery(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    MutationObserverState* state) {
    if (state == nullptr || state->delivery_queued || !state->alive) {
        return;
    }
    state->delivery_queued = true;
    v8::Local<v8::Function> delivery;
    if (!v8::Function::New(context, &mutation_observer_delivery,
                           v8::External::New(isolate, state,
                                             kMutationObserverTag))
             .ToLocal(&delivery)) {
        state->delivery_queued = false;
        return;
    }
    isolate->EnqueueMicrotask(delivery);
}

void DocumentHost::queue_child_list(
    v8::Isolate* isolate, v8::Local<v8::Context> context, DomNode* parent,
    const std::vector<DomNode*>& added, const std::vector<DomNode*>& removed,
    DomNode* previous_sibling, DomNode* next_sibling) {
    if (observers_.empty()) {
        return;
    }
    for (const std::unique_ptr<MutationObserverState>& observer : observers_) {
        if (!observer->alive) {
            continue;
        }
        bool matched = false;
        for (const MutationObserverRegistration& registration :
             observer->registrations) {
            if (!registration.child_list) {
                continue;
            }
            bool target_matches = false;
            if (registration.target_is_document) {
                // The document node itself is never mutated through JS; a
                // subtree registration observes connected descendants only.
                target_matches = registration.subtree && is_in_tree(parent);
            } else {
                target_matches =
                    registration.target == parent ||
                    (registration.subtree &&
                     is_self_or_ancestor(registration.target, parent));
            }
            if (!target_matches) {
                continue;
            }
            MutationRecordData record;
            record.type = MutationRecordData::Type::ChildList;
            record.target = parent;
            record.added = added;
            record.removed = removed;
            record.previous_sibling = previous_sibling;
            record.next_sibling = next_sibling;
            observer->pending.push_back(std::move(record));
            matched = true;
            break;
        }
        if (matched) {
            schedule_mutation_delivery(isolate, context, observer.get());
        }
    }
}

void DocumentHost::queue_attribute(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   DomNode* target, const std::string& name,
                                   bool has_old_value,
                                   const std::string& old_value) {
    if (observers_.empty()) {
        return;
    }
    for (const std::unique_ptr<MutationObserverState>& observer : observers_) {
        if (!observer->alive) {
            continue;
        }
        bool matched = false;
        for (const MutationObserverRegistration& registration :
             observer->registrations) {
            if (!registration.attributes) {
                continue;
            }
            bool target_matches = false;
            if (registration.target_is_document) {
                target_matches = registration.subtree && is_in_tree(target);
            } else {
                target_matches =
                    registration.target == target ||
                    (registration.subtree &&
                     is_self_or_ancestor(registration.target, target));
            }
            if (!target_matches) {
                continue;
            }
            if (registration.has_attribute_filter &&
                std::find(registration.attribute_filter.begin(),
                          registration.attribute_filter.end(),
                          name) == registration.attribute_filter.end()) {
                continue;
            }
            MutationRecordData record;
            record.type = MutationRecordData::Type::Attributes;
            record.target = target;
            record.attribute_name = name;
            record.has_old_value =
                registration.attribute_old_value && has_old_value;
            if (record.has_old_value) {
                record.old_value = old_value;
            }
            observer->pending.push_back(std::move(record));
            matched = true;
            break;
        }
        if (matched) {
            schedule_mutation_delivery(isolate, context, observer.get());
        }
    }
}

v8::Local<v8::Array> DocumentHost::build_records_array(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const std::vector<MutationRecordData>& records) {
    v8::Local<v8::Array> array =
        v8::Array::New(isolate, static_cast<int>(records.size()));
    for (std::size_t i = 0; i < records.size(); ++i) {
        const MutationRecordData& record = records[i];
        const bool child_list =
            record.type == MutationRecordData::Type::ChildList;
        v8::Local<v8::Object> object = v8::Object::New(isolate);
        (void)object->Set(
            context, v8_string(isolate, "type"),
            v8_string(isolate, child_list ? "childList" : "attributes"));
        (void)object->Set(context, v8_string(isolate, "target"),
                          wrap_element(isolate, context, record.target));
        (void)object->Set(context, v8_string(isolate, "addedNodes"),
                          elements_array(isolate, context, record.added));
        (void)object->Set(context, v8_string(isolate, "removedNodes"),
                          elements_array(isolate, context, record.removed));
        (void)object->Set(context, v8_string(isolate, "previousSibling"),
                          wrap_element(isolate, context,
                                       record.previous_sibling));
        (void)object->Set(context, v8_string(isolate, "nextSibling"),
                          wrap_element(isolate, context, record.next_sibling));
        if (child_list) {
            (void)object->Set(context, v8_string(isolate, "attributeName"),
                              v8::Null(isolate));
        } else {
            (void)object->Set(context, v8_string(isolate, "attributeName"),
                              v8_string(isolate, record.attribute_name));
        }
        (void)object->Set(context, v8_string(isolate, "attributeNamespace"),
                          v8::Null(isolate));
        if (record.has_old_value) {
            (void)object->Set(context, v8_string(isolate, "oldValue"),
                              v8_string(isolate, record.old_value));
        } else {
            (void)object->Set(context, v8_string(isolate, "oldValue"),
                              v8::Null(isolate));
        }
        (void)array->Set(context, static_cast<std::uint32_t>(i), object);
    }
    return array;
}

void mutation_observer_delivery(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!info.Data()->IsExternal()) {
        return;
    }
    auto* state = static_cast<MutationObserverState*>(
        info.Data().As<v8::External>()->Value(kMutationObserverTag));
    if (state == nullptr || !state->alive || state->owner == nullptr) {
        return;
    }
    state->delivery_queued = false;
    if (state->pending.empty()) {
        return;
    }
    std::vector<MutationRecordData> records;
    records.swap(state->pending);
    v8::Local<v8::Array> array =
        state->owner->build_records_array(isolate, context, records);
    if (state->callback.IsEmpty() || state->self.IsEmpty()) {
        return;
    }
    v8::Local<v8::Function> callback = state->callback.Get(isolate);
    v8::Local<v8::Object> self = state->self.Get(isolate);
    v8::Local<v8::Value> argv[2] = {array.As<v8::Value>(),
                                    self.As<v8::Value>()};
    v8::TryCatch try_catch(isolate);
    (void)callback->Call(context, self.As<v8::Value>(), 2, argv);
    if (try_catch.HasCaught()) {
        try_catch.Reset();
    }
}

// --- M11-5 MutationObserver JS API -------------------------------------------

v8::Local<v8::Private> mo_private_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate, v8::String::NewFromUtf8Literal(
                     isolate, "iv8::MutationObserver::state"));
}

void throw_mo_type_error(v8::Isolate* isolate, const char* message) {
    isolate->ThrowException(v8::Exception::TypeError(
        v8_string(isolate, std::string(message)).As<v8::String>()));
}

MutationObserverState* mo_state_from_this(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> value;
    if (!info.This()
             ->GetPrivate(context, mo_private_key(isolate))
             .ToLocal(&value)) {
        return nullptr;
    }
    if (!value->IsExternal()) {
        return nullptr;
    }
    return static_cast<MutationObserverState*>(
        value.As<v8::External>()->Value(kMutationObserverTag));
}

void mutation_observer_constructor(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!info.IsConstructCall()) {
        throw_mo_type_error(isolate,
                            "MutationObserver constructor requires 'new'");
        return;
    }
    DocumentHost* document = nullptr;
    if (info.Data()->IsExternal()) {
        document = static_cast<DocumentHost*>(
            info.Data().As<v8::External>()->Value(kMutationObserverTag));
    }
    if (document == nullptr) {
        throw_mo_type_error(isolate, "MutationObserver: invalid constructor");
        return;
    }
    if (info.Length() < 1 || !info[0]->IsFunction()) {
        throw_mo_type_error(isolate,
                            "MutationObserver: callback must be a function");
        return;
    }
    v8::Local<v8::Object> self = info.This();
    MutationObserverState* state = document->create_mutation_observer(
        isolate, info[0].As<v8::Function>(), self);
    (void)self->SetPrivate(
        context, mo_private_key(isolate),
        v8::External::New(isolate, state, kMutationObserverTag));
}

void mo_observe(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    MutationObserverState* state = mo_state_from_this(info);
    if (state == nullptr) {
        throw_mo_type_error(isolate, "Illegal invocation");
        return;
    }
    if (info.Length() < 1) {
        throw_mo_type_error(isolate, "observe: target is required");
        return;
    }
    HostObject* host = host_object_backing(info[0]);
    bool target_is_document = false;
    DomNode* target_node = nullptr;
    if (host == nullptr) {
        throw_mo_type_error(isolate,
                            "observe: target must be an element or document");
        return;
    }
    if (state->owner != nullptr &&
        static_cast<HostObject*>(state->owner) == host) {
        target_is_document = true;
    } else if (host->is_element()) {
        ElementHost* element = static_cast<ElementHost*>(host);
        if (element->document() != state->owner) {
            throw_mo_type_error(isolate, "observe: target is from another page");
            return;
        }
        target_node = element->node();
    } else {
        throw_mo_type_error(isolate,
                            "observe: target must be an element or document");
        return;
    }
    if (info.Length() < 2 || !info[1]->IsObject()) {
        throw_mo_type_error(isolate, "observe: options must be an object");
        return;
    }
    v8::Local<v8::Object> options = info[1].As<v8::Object>();
    const auto get_bool = [&](const char* key, bool* out) -> bool {
        v8::Local<v8::Value> value;
        if (!options->Get(context, v8_string(isolate, key)).ToLocal(&value)) {
            return false;
        }
        *out = value->BooleanValue(isolate);
        return true;
    };
    bool child_list = false;
    bool attributes = false;
    bool character_data = false;
    bool subtree = false;
    bool attribute_old_value = false;
    bool character_data_old_value = false;
    if (!get_bool("childList", &child_list) ||
        !get_bool("attributes", &attributes) ||
        !get_bool("characterData", &character_data) ||
        !get_bool("subtree", &subtree) ||
        !get_bool("attributeOldValue", &attribute_old_value) ||
        !get_bool("characterDataOldValue", &character_data_old_value)) {
        return;  // a getter threw; keep the pending exception
    }
    v8::Maybe<bool> maybe_has =
        options->Has(context, v8_string(isolate, "attributeFilter"));
    if (maybe_has.IsNothing()) {
        return;
    }
    const bool has_filter = maybe_has.FromJust();
    if (!child_list && !attributes && !character_data) {
        throw_mo_type_error(isolate,
                            "observe: at least one of childList, attributes "
                            "or characterData must be true");
        return;
    }
    if (attribute_old_value && !attributes) {
        throw_mo_type_error(isolate,
                            "observe: attributeOldValue requires attributes");
        return;
    }
    if (has_filter && !attributes) {
        throw_mo_type_error(isolate,
                            "observe: attributeFilter requires attributes");
        return;
    }
    if (character_data_old_value && !character_data) {
        throw_mo_type_error(
            isolate,
            "observe: characterDataOldValue requires characterData");
        return;
    }
    std::vector<std::string> filter;
    if (has_filter) {
        v8::Local<v8::Value> filter_value;
        if (!options->Get(context, v8_string(isolate, "attributeFilter"))
                 .ToLocal(&filter_value)) {
            return;
        }
        if (!filter_value->IsArray()) {
            throw_mo_type_error(isolate,
                                "observe: attributeFilter must be an array");
            return;
        }
        v8::Local<v8::Array> filter_array = filter_value.As<v8::Array>();
        const std::uint32_t length = filter_array->Length();
        for (std::uint32_t i = 0; i < length; ++i) {
            v8::Local<v8::Value> item;
            if (!filter_array->Get(context, i).ToLocal(&item)) {
                return;
            }
            v8::Local<v8::String> item_string;
            if (!item->ToString(context).ToLocal(&item_string)) {
                return;
            }
            v8::String::Utf8Value utf8(isolate, item_string);
            if (*utf8 != nullptr) {
                filter.push_back(ascii_lower(std::string(
                    *utf8, static_cast<std::size_t>(utf8.length()))));
            }
        }
    }
    MutationObserverRegistration registration;
    registration.target = target_node;
    registration.target_is_document = target_is_document;
    registration.child_list = child_list;
    registration.attributes = attributes;
    registration.subtree = subtree;
    registration.attribute_old_value = attribute_old_value;
    registration.has_attribute_filter = has_filter;
    registration.attribute_filter = std::move(filter);
    bool replaced = false;
    for (MutationObserverRegistration& existing : state->registrations) {
        if (existing.target == target_node &&
            existing.target_is_document == target_is_document) {
            existing = std::move(registration);
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        state->registrations.push_back(std::move(registration));
    }
    info.GetReturnValue().SetUndefined();
}

void mo_disconnect(const v8::FunctionCallbackInfo<v8::Value>& info) {
    MutationObserverState* state = mo_state_from_this(info);
    if (state == nullptr) {
        throw_mo_type_error(info.GetIsolate(), "Illegal invocation");
        return;
    }
    state->registrations.clear();
    state->pending.clear();
    info.GetReturnValue().SetUndefined();
}

void mo_take_records(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    MutationObserverState* state = mo_state_from_this(info);
    if (state == nullptr) {
        throw_mo_type_error(isolate, "Illegal invocation");
        return;
    }
    std::vector<MutationRecordData> records;
    records.swap(state->pending);
    info.GetReturnValue().Set(
        state->owner->build_records_array(isolate, context, records)
            .As<v8::Value>());
}

// --- M11-10 classList / toggleAttribute -------------------------------------
// A classList object is a fresh plain object per access; its methods and
// length/value accessors carry an External to a generation-owned binding.

constexpr v8::ExternalPointerTypeTag kClassListTag =
    v8::kExternalPointerTypeTagDefault;

ClassListBinding* class_list_binding_from(v8::Local<v8::Value> data) {
    if (!data->IsExternal()) {
        return nullptr;
    }
    return static_cast<ClassListBinding*>(
        data.As<v8::External>()->Value(kClassListTag));
}

void throw_class_list_error(v8::Isolate* isolate, const char* message) {
    isolate->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8(isolate, message).ToLocalChecked()));
}

bool class_list_token(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Value> value, std::string* out) {
    v8::Local<v8::String> str;
    if (!value->ToString(context).ToLocal(&str)) {
        return false;
    }
    v8::String::Utf8Value utf8(isolate, str);
    *out = (*utf8 != nullptr)
               ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
               : std::string();
    if (out->empty()) {
        throw_class_list_error(isolate, "classList token must not be empty");
        return false;
    }
    for (char c : *out) {
        if (std::isspace(static_cast<unsigned char>(c))) {
            throw_class_list_error(
                isolate, "classList token must not contain whitespace");
            return false;
        }
    }
    return true;
}

void class_list_commit(v8::Isolate* isolate, v8::Local<v8::Context> context,
                       ClassListBinding* binding, const std::string& old_name,
                       bool old_has_class) {
    DomNode* node = binding->node;
    std::string joined;
    for (std::size_t i = 0; i < node->classes.size(); i++) {
        if (i > 0) {
            joined += ' ';
        }
        joined += node->classes[i];
    }
    node->class_name = joined;
    node->has_class = !node->classes.empty();
    if (node->class_name != old_name || node->has_class != old_has_class) {
        binding->document->queue_attribute(isolate, context, node, "class",
                                           old_has_class, old_name);
    }
}

void class_list_contains(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    ClassListBinding* binding = class_list_binding_from(info.Data());
    if (binding == nullptr) {
        return;
    }
    std::string token;
    if (!class_list_token(isolate, context, info[0], &token)) {
        return;
    }
    info.GetReturnValue().Set(v8::Boolean::New(
        isolate,
        std::find(binding->node->classes.begin(), binding->node->classes.end(),
                  token) != binding->node->classes.end()));
}

void class_list_add(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    ClassListBinding* binding = class_list_binding_from(info.Data());
    if (binding == nullptr) {
        return;
    }
    const std::string old_name = binding->node->class_name;
    const bool old_has_class = binding->node->has_class;
    std::vector<std::string> tokens;
    tokens.reserve(static_cast<std::size_t>(info.Length()));
    for (int i = 0; i < info.Length(); i++) {
        std::string token;
        if (!class_list_token(isolate, context, info[i], &token)) {
            return;  // validate every token before mutating the node
        }
        tokens.push_back(std::move(token));
    }
    bool changed = false;
    std::vector<std::string>& classes = binding->node->classes;
    for (const std::string& token : tokens) {
        if (std::find(classes.begin(), classes.end(), token) == classes.end()) {
            classes.push_back(token);
            changed = true;
        }
    }
    if (changed) {
        class_list_commit(isolate, context, binding, old_name, old_has_class);
    }
}

void class_list_remove(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    ClassListBinding* binding = class_list_binding_from(info.Data());
    if (binding == nullptr) {
        return;
    }
    const std::string old_name = binding->node->class_name;
    const bool old_has_class = binding->node->has_class;
    std::vector<std::string> tokens;
    tokens.reserve(static_cast<std::size_t>(info.Length()));
    for (int i = 0; i < info.Length(); i++) {
        std::string token;
        if (!class_list_token(isolate, context, info[i], &token)) {
            return;  // validate every token before mutating the node
        }
        tokens.push_back(std::move(token));
    }
    bool changed = false;
    std::vector<std::string>& classes = binding->node->classes;
    for (const std::string& token : tokens) {
        const auto it = std::find(classes.begin(), classes.end(), token);
        if (it != classes.end()) {
            classes.erase(it);
            changed = true;
        }
    }
    if (changed) {
        class_list_commit(isolate, context, binding, old_name, old_has_class);
    }
}

void class_list_toggle(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    ClassListBinding* binding = class_list_binding_from(info.Data());
    if (binding == nullptr) {
        return;
    }
    std::string token;
    if (!class_list_token(isolate, context, info[0], &token)) {
        return;
    }
    std::vector<std::string>& classes = binding->node->classes;
    const auto it = std::find(classes.begin(), classes.end(), token);
    const bool present = it != classes.end();
    const bool force_specified =
        info.Length() > 1 && !info[1]->IsUndefined();
    const bool force = force_specified ? info[1]->BooleanValue(isolate) : false;
    const bool should_exist = force_specified ? force : !present;
    if (should_exist != present) {
        const std::string old_name = binding->node->class_name;
        const bool old_has_class = binding->node->has_class;
        if (should_exist) {
            classes.push_back(token);
        } else {
            classes.erase(it);
        }
        class_list_commit(isolate, context, binding, old_name, old_has_class);
    }
    info.GetReturnValue().Set(v8::Boolean::New(isolate, should_exist));
}

void class_list_replace(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    ClassListBinding* binding = class_list_binding_from(info.Data());
    if (binding == nullptr) {
        return;
    }
    std::string old_token;
    std::string new_token;
    if (!class_list_token(isolate, context, info[0], &old_token) ||
        !class_list_token(isolate, context, info[1], &new_token)) {
        return;
    }
    std::vector<std::string>& classes = binding->node->classes;
    const auto old_it = std::find(classes.begin(), classes.end(), old_token);
    if (old_it == classes.end()) {
        info.GetReturnValue().Set(v8::False(isolate));
        return;
    }
    if (old_token != new_token) {
        const std::string old_name = binding->node->class_name;
        const bool old_has_class = binding->node->has_class;
        const auto new_it = std::find(classes.begin(), classes.end(), new_token);
        if (new_it == classes.end()) {
            *old_it = new_token;
        } else {
            classes.erase(old_it);  // the replacement already exists: dedupe
        }
        class_list_commit(isolate, context, binding, old_name, old_has_class);
    }
    info.GetReturnValue().Set(v8::True(isolate));
}

void class_list_item(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    ClassListBinding* binding = class_list_binding_from(info.Data());
    if (binding == nullptr) {
        return;
    }
    v8::Local<v8::Uint32> index_value;
    if (!info[0]->ToUint32(context).ToLocal(&index_value)) {
        return;
    }
    const std::uint32_t index = index_value->Value();
    if (index >= binding->node->classes.size()) {
        info.GetReturnValue().Set(v8::Null(isolate));
        return;
    }
    info.GetReturnValue().Set(
        v8_string(isolate, binding->node->classes[index]));
}

void class_list_length_getter(v8::Local<v8::Name> property,
                              const v8::PropertyCallbackInfo<v8::Value>& info) {
    (void)property;
    v8::Isolate* isolate = info.GetIsolate();
    ClassListBinding* binding = class_list_binding_from(info.Data());
    if (binding == nullptr) {
        return;
    }
    info.GetReturnValue().Set(v8::Integer::NewFromUnsigned(
        isolate, static_cast<unsigned>(binding->node->classes.size())));
}

void class_list_value_getter(v8::Local<v8::Name> property,
                             const v8::PropertyCallbackInfo<v8::Value>& info) {
    (void)property;
    v8::Isolate* isolate = info.GetIsolate();
    ClassListBinding* binding = class_list_binding_from(info.Data());
    if (binding == nullptr) {
        return;
    }
    info.GetReturnValue().Set(v8_string(isolate, binding->node->class_name));
}

void class_list_value_setter(v8::Local<v8::Name> property,
                             v8::Local<v8::Value> value,
                             const v8::PropertyCallbackInfo<v8::Boolean>& info) {
    (void)property;
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    ClassListBinding* binding = class_list_binding_from(info.Data());
    if (binding == nullptr) {
        info.GetReturnValue().Set(false);
        return;
    }
    v8::Local<v8::String> str;
    if (!value->ToString(context).ToLocal(&str)) {
        info.GetReturnValue().Set(false);
        return;
    }
    v8::String::Utf8Value utf8(isolate, str);
    const std::string text =
        (*utf8 != nullptr)
            ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
            : std::string();
    DomNode* node = binding->node;
    const std::string old_name = node->class_name;
    const bool old_has_class = node->has_class;
    node->class_name = text;
    node->has_class = true;
    node->classes = split_class_tokens(text);
    if (node->class_name != old_name || node->has_class != old_has_class) {
        binding->document->queue_attribute(isolate, context, node, "class",
                                           old_has_class, old_name);
    }
    info.GetReturnValue().Set(true);
}

v8::Local<v8::Value> make_class_list(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     DocumentHost* document, DomNode* node) {
    ClassListBinding* binding = document->create_class_list_binding(node);
    v8::Local<v8::Value> data =
        v8::External::New(isolate, binding, kClassListTag);
    v8::Local<v8::Object> list = v8::Object::New(isolate);
    const auto add_method = [&](const char* name, v8::FunctionCallback callback) {
        v8::Local<v8::Function> fn =
            v8::FunctionTemplate::New(isolate, callback, data)
                ->GetFunction(context)
                .ToLocalChecked();
        (void)list->Set(context, v8_string(isolate, name), fn);
    };
    add_method("contains", &class_list_contains);
    add_method("add", &class_list_add);
    add_method("remove", &class_list_remove);
    add_method("toggle", &class_list_toggle);
    add_method("replace", &class_list_replace);
    add_method("item", &class_list_item);
    (void)list->SetNativeDataProperty(
        context, v8::String::NewFromUtf8Literal(isolate, "length"),
        &class_list_length_getter, nullptr, data, v8::ReadOnly);
    (void)list->SetNativeDataProperty(
        context, v8::String::NewFromUtf8Literal(isolate, "value"),
        &class_list_value_getter, &class_list_value_setter, data, v8::None);
    return list;
}

v8::Local<v8::Value> ElementHost::get_property(v8::Isolate* isolate,
                                               v8::Local<v8::Context> context,
                                               const std::string& name) {
    if (name == "tagName" || name == "nodeName") {
        return v8_string(isolate, ascii_upper(node_->tag));
    }
    if (name == "nodeType") {
        return v8::Integer::New(isolate, 1);  // ELEMENT_NODE
    }
    if (name == "id") return v8_string(isolate, node_->id);
    if (name == "className") return v8_string(isolate, node_->class_name);
    if (name == "classList") {
        return make_class_list(isolate, context, document_, node_);
    }
    // M11-18: minimal form-control attribute reflection.
    if (name == "type" &&
        (node_->tag == "input" || node_->tag == "button")) {
        const auto it = node_->attributes.find("type");
        const std::string raw =
            it != node_->attributes.end() ? it->second : std::string();
        return v8_string(isolate, node_->tag == "input"
                                     ? normalize_input_type(raw)
                                     : normalize_button_type(raw));
    }
    if (name == "disabled" &&
        (node_->tag == "input" || node_->tag == "button" ||
         node_->tag == "textarea" || node_->tag == "select")) {
        return v8::Boolean::New(
            isolate, node_->attributes.find("disabled") !=
                         node_->attributes.end());
    }
    if (name == "readOnly" &&
        (node_->tag == "input" || node_->tag == "textarea")) {
        return v8::Boolean::New(
            isolate, node_->attributes.find("readonly") !=
                         node_->attributes.end());
    }
    if (name == "placeholder" &&
        (node_->tag == "input" || node_->tag == "textarea")) {
        const auto it = node_->attributes.find("placeholder");
        return v8_string(isolate,
                         it != node_->attributes.end() ? it->second
                                                       : std::string());
    }
    if (name == "name" &&
        (node_->tag == "input" || node_->tag == "button" ||
         node_->tag == "textarea" || node_->tag == "select")) {
        const auto it = node_->attributes.find("name");
        return v8_string(isolate,
                         it != node_->attributes.end() ? it->second
                                                       : std::string());
    }
    if (name == "required" &&
        (node_->tag == "input" || node_->tag == "textarea" ||
         node_->tag == "select")) {
        return v8::Boolean::New(
            isolate, node_->attributes.find("required") !=
                         node_->attributes.end());
    }
    // M11-32: control-level constraint-validation read surface. `validity` is a
    // fresh plain object recomputed on each access (not a live ValidityState);
    // all flags are read-only. Unsupported constraints stay false.
    if (is_form_control(node_)) {
        if (name == "willValidate") {
            return v8::Boolean::New(isolate, control_will_validate(node_));
        }
        if (name == "validationMessage") {
            const ValidityInfo info = compute_validity(node_);
            return v8_string(isolate, validation_message_of(node_, info));
        }
        if (name == "validity") {
            const ValidityInfo info = compute_validity(node_);
            v8::Local<v8::Object> validity = v8::Object::New(isolate);
            const auto define_flag = [&](const char* key, bool on) {
                (void)validity->DefineOwnProperty(
                    context,
                    v8::String::NewFromUtf8(isolate, key).ToLocalChecked(),
                    v8::Boolean::New(isolate, on),
                    static_cast<v8::PropertyAttribute>(v8::ReadOnly |
                                                       v8::DontDelete));
            };
            define_flag("valueMissing", info.value_missing);
            define_flag("typeMismatch", info.type_mismatch);
            define_flag("patternMismatch", info.pattern_mismatch);
            define_flag("tooLong", info.too_long);
            define_flag("tooShort", info.too_short);
            define_flag("rangeUnderflow", info.range_underflow);
            define_flag("rangeOverflow", info.range_overflow);
            define_flag("stepMismatch", info.step_mismatch);
            define_flag("badInput", info.bad_input);
            define_flag("customError", info.custom_error);
            define_flag("valid", info.valid());
            return validity;
        }
    }
    if (name == "label" && node_->tag == "option") {
        const auto it = node_->attributes.find("label");
        return v8_string(isolate,
                         it != node_->attributes.end() ? it->second
                                                       : node_->text_content);
    }
    if (name == "index" && node_->tag == "option") {
        DomNode* select = node_->parent;
        while (select != nullptr && select->tag != "select") {
            select = select->parent;
        }
        if (select == nullptr) {
            return v8::Integer::New(isolate, 0);
        }
        const std::vector<DomNode*> options = collect_options(select);
        const auto it = std::find(options.begin(), options.end(), node_);
        return v8::Integer::New(
            isolate, it == options.end()
                         ? 0
                         : static_cast<int>(
                               std::distance(options.begin(), it)));
    }
    // M11-16: select.options / selectedIndex / multiple / size.
    if (node_->tag == "select" && name == "options") {
        std::vector<DomNode*> options;
        collect_matching(node_, tag_predicate("option"), options);
        return document_->elements_array(isolate, context, options);
    }
    if (node_->tag == "select" && name == "length") {
        // M11-22: read-only count of descendant options.
        return v8::Integer::New(
            isolate, static_cast<int>(collect_options(node_).size()));
    }
    if (node_->tag == "select" && name == "selectedIndex") {
        std::vector<DomNode*> options;
        collect_matching(node_, tag_predicate("option"), options);
        for (std::size_t i = 0; i < options.size(); i++) {
            if (options[i]->selected) {
                return v8::Integer::New(isolate, static_cast<int>(i));
            }
        }
        return v8::Integer::New(isolate, -1);
    }
    if (node_->tag == "select" && name == "multiple") {
        return v8::Boolean::New(
            isolate, node_->attributes.find("multiple") !=
                         node_->attributes.end());
    }
    if (node_->tag == "select" && name == "size") {
        const auto it = node_->attributes.find("size");
        long size = 0;
        if (it != node_->attributes.end()) {
            size = std::strtol(it->second.c_str(), nullptr, 10);
        }
        return v8::Integer::New(isolate,
                                static_cast<int>(size < 0 ? 0 : size));
    }
    // M11-13: raw Node aliases (element-only tree, so they equal the element
    // variants).
    if (name == "firstChild") {
        return node_->children.empty()
                   ? v8::Null(isolate)
                   : document_->wrap_element(isolate, context,
                                             node_->children.front());
    }
    if (name == "lastChild") {
        return node_->children.empty()
                   ? v8::Null(isolate)
                   : document_->wrap_element(isolate, context,
                                             node_->children.back());
    }
    if (name == "previousSibling" || name == "nextSibling") {
        DomNode* previous = nullptr;
        DomNode* next = nullptr;
        siblings_of(node_, &previous, &next);
        return document_->wrap_element(isolate, context,
                                       name == "previousSibling" ? previous
                                                                 : next);
    }
    if (name == "textContent") return v8_string(isolate, node_->text_content);
    if (name == "parentNode") {
        return document_->wrap_element(isolate, context, node_->parent);
    }
    // childNodes == children here: the minimal tree has no text nodes, so both
    // return this element's child elements in document order.
    if (name == "childNodes" || name == "children") {
        v8::Local<v8::Array> array =
            v8::Array::New(isolate, static_cast<int>(node_->children.size()));
        for (std::size_t k = 0; k < node_->children.size(); ++k) {
            (void)array->Set(
                context, static_cast<std::uint32_t>(k),
                document_->wrap_element(isolate, context, node_->children[k]));
        }
        return array;
    }
    // M4-A-6 connectivity / element-sibling navigation (based on the live tree).
    if (name == "ownerDocument") {
        // The current generation's `document` — the SAME JS object installed as
        // the global, so `el.ownerDocument === document` holds.
        v8::Local<v8::Value> doc;
        if (context->Global()
                ->Get(context, v8_string(isolate, "document"))
                .ToLocal(&doc)) {
            return doc;
        }
        return v8::Null(isolate);
    }
    if (name == "isConnected") {
        return v8::Boolean::New(isolate, document_->is_in_tree(node_));
    }
    if (name == "previousElementSibling" || name == "nextElementSibling") {
        DomNode* parent = node_->parent;
        if (parent == nullptr) {
            return v8::Null(isolate);  // no parent -> no siblings
        }
        const std::vector<DomNode*>& kids = parent->children;
        const auto it = std::find(kids.begin(), kids.end(), node_);
        if (it == kids.end()) {
            return v8::Null(isolate);
        }
        if (name == "previousElementSibling") {
            if (it == kids.begin()) {
                return v8::Null(isolate);
            }
            return document_->wrap_element(isolate, context, *(it - 1));
        }
        const auto next = it + 1;
        if (next == kids.end()) {
            return v8::Null(isolate);
        }
        return document_->wrap_element(isolate, context, *next);
    }
    // M4-B-1 structural navigation (element-only tree, live). parentElement is the
    // element parent (in this model parent is always an element, so it matches
    // parentNode); first/lastElementChild are the ends of the children sequence;
    // childElementCount is children.size() (== childNodes.length here, but stated
    // as element-only). All reflect M4-A-3 tree edits at once.
    if (name == "parentElement") {
        return document_->wrap_element(isolate, context, node_->parent);
    }
    if (name == "firstElementChild") {
        if (node_->children.empty()) {
            return v8::Null(isolate);
        }
        return document_->wrap_element(isolate, context, node_->children.front());
    }
    if (name == "lastElementChild") {
        if (node_->children.empty()) {
            return v8::Null(isolate);
        }
        return document_->wrap_element(isolate, context, node_->children.back());
    }
    if (name == "childElementCount") {
        return v8::Integer::New(isolate,
                                static_cast<std::int32_t>(node_->children.size()));
    }
    // M5-1: form.elements — the form-control descendants of this <form>, in document
    // order (exposed only on <form>, see property_names). Minimal control set:
    // input / button / select / textarea (no fieldset/output/object/custom, no
    // disabled/name/type filtering). A subtree walk (element self + descendants;
    // the <form> itself is not a control so never included); a <script> in the
    // subtree is not a control -> not counted (and stays inert). Same live-tree
    // collection + plain-Array wrapping as the subtree queries; not an
    // HTMLFormControlsCollection (no item/namedItem, no identity guarantee).
    if (name == "elements") {
        std::vector<DomNode*> out;
        collect_matching(node_,
                         [](const DomNode* n) {
                             return n->tag == "input" || n->tag == "button" ||
                                    n->tag == "select" || n->tag == "textarea";
                         },
                         out);
        return document_->elements_array(isolate, context, out);
    }
    // M5-2: control.form — the nearest ancestor <form> (walking parentNode), or null
    // (exposed only on input/button/select/textarea, see property_names). Pure
    // ancestor-chain semantics over the live tree, so it follows tree edits and
    // works inside a detached <form> subtree; NOT the full HTML owner-form algorithm
    // (no form="" cross-tree association).
    if (name == "form") {
        for (DomNode* ancestor = node_->parent; ancestor != nullptr;
             ancestor = ancestor->parent) {
            if (ancestor->tag == "form") {
                return document_->wrap_element(isolate, context, ancestor);
            }
        }
        return v8::Null(isolate);
    }
    // value (exposed on input/textarea/button/select/option, see property_names):
    //  - <input>/<textarea>/<button> (M5-3/4/6): the runtime value slot (seeded
    //    once, then decoupled from the attribute / textContent);
    //  - <select> (M5-5): the value of the first selected <option> in its subtree,
    //    or "" if none — derived, not stored;
    //  - <option> (M5-5): its `value` attribute (live), else its text content.
    if (name == "value") {
        if (node_->tag == "select") {
            for (DomNode* option : collect_options(node_)) {
                if (option->selected) {
                    return v8_string(isolate, option_value_of(option));
                }
            }
            return v8_string(isolate, std::string());
        }
        if (node_->tag == "option") {
            return v8_string(isolate, option_value_of(node_));
        }
        return v8_string(isolate, node_->value);
    }
    // M5-5: option.selected — the runtime bool slot (exposed only on <option>).
    if (name == "selected") {
        return v8::Boolean::New(isolate, node_->selected);
    }
    // M5-7: input.checked — the runtime bool slot (exposed only on <input>).
    if (name == "checked") {
        return v8::Boolean::New(isolate, node_->checked);
    }
    // M6-2: input.defaultChecked — the fixed reset baseline (M6-1 initial_checked),
    // read-only, exposed only on <input>. Does not follow live .checked; it is what
    // form.reset() restores .checked to.
    if (name == "defaultChecked") {
        return v8::Boolean::New(isolate, node_->initial_checked);
    }
    // M6-3: option.defaultSelected — the fixed reset baseline (M6-1 initial_selected,
    // captured AFTER the M5-5 single-select normalization), read-only, exposed only
    // on <option>. Does not follow live .selected; it is what form.reset() restores
    // .selected to.
    if (name == "defaultSelected") {
        return v8::Boolean::New(isolate, node_->initial_selected);
    }
    // M6-4/M6-5: input/textarea.defaultValue — the fixed reset baseline (M6-1
    // initial_value), read-only, exposed only on <input>/<textarea>. Does not follow
    // live .value; it is what form.reset() restores .value to. (For <textarea> the
    // baseline was seeded from the initial text content, M5-4.)
    if (name == "defaultValue") {
        return v8_string(isolate, node_->initial_value);
    }
    // M5-8: option.text — the option's current text content (read-only, exposed only
    // on <option>; live, so it follows textContent writes). Unlike option.value it
    // ignores the `value` attribute and always reflects the text.
    if (name == "text") {
        return v8_string(isolate, node_->text_content);
    }
    // M7-3: form.method — the current normalized submission method ("get"/"post"),
    // read-write, exposed only on <form>. Decoupled from the `method` attribute:
    // reading returns the runtime slot, not getAttribute('method'). Pure metadata —
    // it does not trigger submit()/requestSubmit() behaviour (both stay no-ops).
    if (name == "method") {
        return v8_string(isolate, node_->method);
    }
    // M7-4: form.action — the current raw submission-address string, read-write,
    // exposed only on <form>. Decoupled from the `action` attribute (reading returns
    // the runtime slot, not getAttribute('action')). No URL parsing/normalization.
    // Pure metadata — it does not trigger submit()/requestSubmit() (both stay no-ops).
    if (name == "action") {
        return v8_string(isolate, node_->action);
    }
    // M7-5: form.enctype — the current normalized submission encoding, read-write,
    // exposed only on <form>. Decoupled from the `enctype` attribute (reading returns
    // the runtime slot). Metadata only — no body encoding; submit()/requestSubmit()
    // stay no-ops.
    if (name == "enctype") {
        return v8_string(isolate, node_->enctype);
    }
    // M11-18: form.encoding is an alias of the enctype runtime slot.
    if (name == "encoding") {
        return v8_string(isolate, node_->enctype);
    }
    // M7-6: form.target — the current raw submission-target string, read-write,
    // exposed only on <form>. Decoupled from the `target` attribute (reading returns
    // the runtime slot). No browsing-context / window resolution / _self,_blank,...
    // semantics. Pure metadata — submit()/requestSubmit() stay no-ops.
    if (name == "target") {
        return v8_string(isolate, node_->target);
    }
    // M7-7: form.noValidate — the current validation-skip switch state (bool),
    // read-write, exposed only on <form>. Decoupled from the `novalidate` attribute
    // (reading returns the runtime slot). Switch state only — no validation runs;
    // submit()/requestSubmit() stay no-ops.
    if (name == "noValidate") {
        return v8::Boolean::New(isolate, node_->no_validate);
    }
    return v8::Undefined(isolate);
}

// M2-8: textContent write. In the minimal (text-node-free) tree, setting
// textContent replaces all children with the given text — i.e. detach children
// and store the text. Live queries (getElementById/querySelector/children) then
// no longer see the removed nodes; no separate index needs updating.
void ElementHost::set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               const std::string& name, v8::Local<v8::Value> value) {
    // M11-16: select.selectedIndex = i — single-select model; -1 clears all,
    // out-of-range values are ignored.
    if (name == "selectedIndex") {
        v8::Local<v8::Int32> index_value;
        if (!value->ToInt32(context).ToLocal(&index_value)) {
            return;
        }
        const int index = index_value->Value();
        std::vector<DomNode*> options;
        collect_matching(node_, tag_predicate("option"), options);
        if (index < -1 || index >= static_cast<int>(options.size())) {
            return;
        }
        for (std::size_t i = 0; i < options.size(); i++) {
            options[i]->selected =
                index >= 0 && static_cast<int>(i) == index;
        }
        return;
    }
    // M11-12: id / className setters keep the dedicated fields, queries, and
    // MutationObserver attribute records consistent with setAttribute.
    if (name == "id" || name == "className") {
        v8::Local<v8::String> str;
        if (!value->ToString(context).ToLocal(&str)) {
            return;
        }
        v8::String::Utf8Value utf8(isolate, str);
        const std::string text =
            (*utf8 != nullptr)
                ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
                : std::string();
        if (name == "id") {
            const bool old_has = node_->has_id;
            const std::string old_value = node_->id;
            node_->id = text;
            node_->has_id = true;
            document_->queue_attribute(isolate, context, node_, "id", old_has,
                                       old_value);
        } else {
            const bool old_has = node_->has_class;
            const std::string old_value = node_->class_name;
            node_->class_name = text;
            node_->has_class = true;
            node_->classes = split_class_tokens(text);
            document_->queue_attribute(isolate, context, node_, "class",
                                       old_has, old_value);
        }
        return;
    }
    // M11-18: reflected form-control string/boolean attribute writes. These are
    // real attribute writes (unlike the value/checked/selected runtime slots), so
    // getAttribute/hasAttribute and MutationObserver records stay consistent.
    const auto to_reflected_string = [&](std::string& out) -> bool {
        v8::Local<v8::String> str;
        if (!value->ToString(context).ToLocal(&str)) {
            return false;
        }
        v8::String::Utf8Value utf8(isolate, str);
        out = (*utf8 != nullptr)
                  ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
                  : std::string();
        return true;
    };
    const auto reflect_raw_string = [&](const std::string& key,
                                        const std::string& text) {
        bool old_has = false;
        std::string old_value;
        const auto it = node_->attributes.find(key);
        if (it != node_->attributes.end()) {
            old_has = true;
            old_value = it->second;
        }
        node_->attributes[key] = text;
        document_->queue_attribute(isolate, context, node_, key, old_has,
                                   old_value);
    };
    const auto reflect_bool = [&](const std::string& key, bool on) {
        const auto it = node_->attributes.find(key);
        if (on) {
            bool old_has = false;
            std::string old_value;
            if (it != node_->attributes.end()) {
                old_has = true;
                old_value = it->second;
            }
            node_->attributes[key] = std::string();
            document_->queue_attribute(isolate, context, node_, key, old_has,
                                       old_value);
        } else if (it != node_->attributes.end()) {
            const std::string old_value = it->second;
            node_->attributes.erase(it);
            document_->queue_attribute(isolate, context, node_, key, true,
                                       old_value);
        }
    };
    if (name == "encoding" && node_->tag == "form") {
        std::string text;
        if (to_reflected_string(text)) {
            node_->enctype = normalize_form_enctype(text);
        }
        return;
    }
    if (name == "type" &&
        (node_->tag == "input" || node_->tag == "button")) {
        std::string text;
        if (to_reflected_string(text)) {
            reflect_raw_string("type", text);
        }
        return;
    }
    if (name == "disabled" &&
        (node_->tag == "input" || node_->tag == "button" ||
         node_->tag == "textarea" || node_->tag == "select")) {
        reflect_bool("disabled", value->BooleanValue(isolate));
        return;
    }
    if (name == "readOnly" &&
        (node_->tag == "input" || node_->tag == "textarea")) {
        reflect_bool("readonly", value->BooleanValue(isolate));
        return;
    }
    if (name == "placeholder" &&
        (node_->tag == "input" || node_->tag == "textarea")) {
        std::string text;
        if (to_reflected_string(text)) {
            reflect_raw_string("placeholder", text);
        }
        return;
    }
    if (name == "name" &&
        (node_->tag == "input" || node_->tag == "button" ||
         node_->tag == "textarea" || node_->tag == "select")) {
        std::string text;
        if (to_reflected_string(text)) {
            reflect_raw_string("name", text);
        }
        return;
    }
    if (name == "required" &&
        (node_->tag == "input" || node_->tag == "textarea" ||
         node_->tag == "select")) {
        reflect_bool("required", value->BooleanValue(isolate));
        return;
    }
    if (name == "label" && node_->tag == "option") {
        std::string text;
        if (to_reflected_string(text)) {
            reflect_raw_string("label", text);
        }
        return;
    }
    // M5-5: option.selected = ... — boolean (truthy) coercion, runtime slot only
    // (does NOT write the `selected` attribute). Writable only on <option>.
    if (name == "selected") {
        node_->selected = value->BooleanValue(isolate);
        return;
    }
    // M5-7: input.checked = ... — boolean (truthy) coercion, runtime slot only
    // (does NOT write the `checked` attribute). Writable only on <input>.
    if (name == "checked") {
        node_->checked = value->BooleanValue(isolate);
        return;
    }
    // M7-7: form.noValidate = ... — truthy/falsey -> bool, runtime slot only (does
    // NOT write the `novalidate` attribute). Writable only on <form>.
    if (name == "noValidate") {
        node_->no_validate = value->BooleanValue(isolate);
        return;
    }
    if (name != "textContent" && name != "value" && name != "method" &&
        name != "action" && name != "enctype" && name != "target") {
        return;  // textContent + value + method/action/enctype/target (M7-3..6)
    }
    std::string str_value;
    v8::Local<v8::String> str;
    if (value->ToString(context).ToLocal(&str)) {
        v8::String::Utf8Value utf8(isolate, str);
        if (*utf8 != nullptr) {
            str_value.assign(*utf8, static_cast<std::size_t>(utf8.length()));
        }
    }
    // M7-3: form.method = X -> store normalize(String(X)) in the runtime slot only
    // (does NOT write the `method` attribute). Writable only on <form>.
    if (name == "method") {
        node_->method = normalize_form_method(str_value);
        return;
    }
    // M7-4: form.action = X -> store String(X) verbatim in the runtime slot only (no
    // URL parsing/normalization; does NOT write the `action` attribute). Form-only.
    if (name == "action") {
        node_->action = str_value;
        return;
    }
    // M7-5: form.enctype = X -> store normalize(String(X)) in the runtime slot only
    // (does NOT write the `enctype` attribute). Writable only on <form>.
    if (name == "enctype") {
        node_->enctype = normalize_form_enctype(str_value);
        return;
    }
    // M7-6: form.target = X -> store String(X) verbatim in the runtime slot only (no
    // context resolution; does NOT write the `target` attribute). Form-only.
    if (name == "target") {
        node_->target = str_value;
        return;
    }
    if (name == "value") {
        // M5-5: select.value = X — select the first <option> in this select's
        // subtree whose value === String(X), clearing every other option's
        // selected in the same select; no match -> no change. Minimal single-select.
        if (node_->tag == "select") {
            std::vector<DomNode*> options = collect_options(node_);
            DomNode* match = nullptr;
            for (DomNode* option : options) {
                if (option_value_of(option) == str_value) {
                    match = option;
                    break;
                }
            }
            if (match != nullptr) {
                for (DomNode* option : options) {
                    option->selected = false;
                }
                match->selected = true;
            }
            return;
        }
        // M5-3/4/6: input/textarea/button.value = ... writes the runtime slot only
        // (does NOT touch the attribute / textContent). option.value is read-only
        // (not in writable_property_names), so <option> never reaches here.
        node_->value = str_value;
        return;
    }
    // M2-8: textContent = ... replaces all children with the given text.
    // M11-5: capture the childList mutation before detaching the children.
    std::vector<DomNode*> removed_children;
    if (document_->has_mutation_observers()) {
        removed_children = node_->children;
    }
    for (DomNode* child : node_->children) {
        child->parent = nullptr;
    }
    node_->children.clear();
    node_->text_content = str_value;
    if (!removed_children.empty()) {
        std::vector<DomNode*> added_children;
        document_->queue_child_list(isolate, context, node_, added_children,
                                    removed_children, nullptr, nullptr);
    }
}

// M9-2: record a host-method call hit if the "receiver.method" path is watched.
// Cheap no-op when watching is disabled (the common case): the enabled() check is
// a lock-free atomic read, and the path string is built only when it may match.
void watch_record(v8::Isolate* isolate, const std::string& receiver,
                  const std::string& method) {
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr) {
        return;
    }
    WatchRegistry* registry = state->watch_registry();
    if (registry == nullptr || !registry->enabled()) {
        return;
    }
    registry->maybe_record(receiver + "." + method);
}

// Step 5 call coverage for runtime-owned plain BOM objects. These are
// independent native callbacks, not HostObject methods, so they use explicit
// stable native tokens for object identity. Property reads on these objects are
// a later group and are not implied by these calls.
static char kPerformanceTraceToken;
static char kCryptoTraceToken;
static char kStorageTraceToken;

void access_trace_record(v8::Isolate* isolate, const void* identity,
                         const char* object_type, const char* label,
                         const char* member, AccessOperation operation) {
    AccessTrace* trace = access_trace_from_isolate(isolate);
    if (trace != nullptr && trace->enabled()) {
        trace->record(identity, object_type, label, member, operation);
    }
}

// M11-32: fire the minimal `invalid` event at a control. It is a target-only,
// non-bubbling event with the standard read fields; it is a plain object (like
// the M3-4 lifecycle events), not a full Event instance, so preventDefault /
// stopPropagation are not installed on it. Return values are unaffected.
void ElementHost::fire_invalid_event(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> receiver) {
    v8::Local<v8::Object> event = v8::Object::New(isolate);
    (void)event->Set(context, v8_string(isolate, "type"),
                     v8_string(isolate, "invalid"));
    (void)event->Set(context, v8_string(isolate, "bubbles"),
                     v8::Boolean::New(isolate, false));
    (void)event->Set(context, v8_string(isolate, "cancelable"),
                     v8::Boolean::New(isolate, true));
    (void)event->Set(context, v8_string(isolate, "defaultPrevented"),
                     v8::Boolean::New(isolate, false));
    (void)event->Set(context, v8_string(isolate, "eventPhase"),
                     v8::Integer::New(isolate, 0));
    (void)event->Set(context, v8_string(isolate, "target"), receiver);
    (void)event->Set(context, v8_string(isolate, "currentTarget"),
                     v8::Null(isolate));
    fire_target_at(isolate, context, event, receiver);
    finish_dispatch(isolate, context, event);
}

bool ElementHost::validate_control(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   v8::Local<v8::Object> receiver) {
    const ValidityInfo info = compute_validity(node_);
    if (info.valid()) {
        return true;
    }
    fire_invalid_event(isolate, context, receiver);
    return false;
}

bool ElementHost::validate_form(v8::Isolate* isolate,
                                v8::Local<v8::Context> context) {
    std::vector<DomNode*> controls;
    collect_matching(node_,
                     [](const DomNode* n) { return is_form_control(n); },
                     controls);
    bool valid = true;
    for (DomNode* control : controls) {
        if (!control_will_validate(control)) {
            continue;  // barred controls are skipped and never receive invalid
        }
        ElementHost* host = document_->ensure_element_host(control);
        v8::Local<v8::Object> receiver =
            document_->wrap_element(isolate, context, control).As<v8::Object>();
        if (!host->validate_control(isolate, context, receiver)) {
            valid = false;
        }
    }
    return valid;
}

v8::Local<v8::Value> ElementHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& args) {
    // M9-2: record the "<tag>.<method>" call before dispatch (covers event methods
    // handled just below too, e.g. "script.getAttribute").
    watch_record(isolate, node_->tag, name);
    // M3-3: addEventListener / removeEventListener / dispatchEvent first.
    v8::Local<v8::Value> event_result;
    if (handle_event_method(isolate, context, name, args, event_result)) {
        return event_result;
    }
    // M11-32: checkValidity()/reportValidity() on a form aggregates its controls;
    // on a control they validate just that control. reportValidity has no UI in
    // this headless runtime, so it is identical to checkValidity.
    if (name == "checkValidity" || name == "reportValidity") {
        if (node_->tag == "form") {
            return v8::Boolean::New(isolate, validate_form(isolate, context));
        }
        if (is_form_control(node_)) {
            return v8::Boolean::New(
                isolate, validate_control(isolate, context, args.This()));
        }
        return v8::Undefined(isolate);
    }
    // M11-32: setCustomValidity(message) — String coercion; empty clears the
    // custom error. The message is a runtime slot (no attribute write).
    if (name == "setCustomValidity" && is_form_control(node_)) {
        std::string message;
        if (args.Length() > 0) {
            v8::Local<v8::String> str;
            if (args[0]->ToString(context).ToLocal(&str)) {
                v8::String::Utf8Value utf8(isolate, str);
                if (*utf8 != nullptr) {
                    message.assign(*utf8,
                                   static_cast<std::size_t>(utf8.length()));
                }
            }
        }
        node_->custom_validity = message;
        return v8::Undefined(isolate);
    }
    const auto arg_string = [&](int index) -> std::string {
        if (index >= args.Length()) return std::string();
        v8::String::Utf8Value utf8(isolate, args[index]);
        return *utf8 != nullptr
                   ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
                   : std::string();
    };
    // M3-8: getAttribute / hasAttribute read any attribute parsed from the HTML.
    // id / class route to their dedicated fields (so they stay consistent with
    // .id / .className / setAttribute); every other name is looked up in the raw
    // attribute table. Name match is ASCII case-insensitive. Missing -> null /
    // false; a valueless/boolean attribute reads "" (and hasAttribute -> true).
    // M11-12: attribute presence / name enumeration.
    if (name == "hasAttributes") {
        return v8::Boolean::New(
            isolate, node_->has_id || node_->has_class ||
                         !node_->attributes.empty());
    }
    if (name == "getAttributeNames") {
        std::vector<std::string> names;
        if (node_->has_id) names.push_back("id");
        if (node_->has_class) names.push_back("class");
        std::vector<std::string> raw;
        raw.reserve(node_->attributes.size());
        for (const auto& entry : node_->attributes) {
            raw.push_back(entry.first);
        }
        std::sort(raw.begin(), raw.end());
        names.insert(names.end(), raw.begin(), raw.end());
        v8::Local<v8::Array> array =
            v8::Array::New(isolate, static_cast<int>(names.size()));
        for (std::size_t i = 0; i < names.size(); i++) {
            (void)array->Set(context, static_cast<std::uint32_t>(i),
                             v8_string(isolate, names[i]));
        }
        return array;
    }
    if (name == "getAttribute" || name == "hasAttribute") {
        const std::string key = ascii_lower(arg_string(0));
        bool present = false;
        const std::string* value = nullptr;
        if (key == "id") {
            present = node_->has_id;
            value = &node_->id;
        } else if (key == "class") {
            present = node_->has_class;
            value = &node_->class_name;
        } else {
            const auto it = node_->attributes.find(key);
            if (it != node_->attributes.end()) {
                present = true;
                value = &it->second;
            }
        }
        if (name == "hasAttribute") {
            return v8::Boolean::New(isolate, present);
        }
        return present ? v8_string(isolate, *value) : v8::Null(isolate);
    }
    // M4-A-4 setAttribute(name, value): write ANY attribute (name lowercased,
    // value = String(value)). id/class keep their dedicated fields (so .id /
    // .className / id/class-based queries stay consistent); every other name goes
    // in the raw attribute table (M3-8). Not a full attribute system (no order/
    // serialization / reflection).
    if (name == "setAttribute") {
        const std::string key = ascii_lower(arg_string(0));
        const std::string val = arg_string(1);
        // M11-5: capture the pre-write value for a MutationObserver record.
        bool has_old_value = false;
        std::string old_value;
        if (key == "id") {
            has_old_value = node_->has_id;
            old_value = node_->id;
        } else if (key == "class") {
            has_old_value = node_->has_class;
            old_value = node_->class_name;
        } else {
            const auto old = node_->attributes.find(key);
            if (old != node_->attributes.end()) {
                has_old_value = true;
                old_value = old->second;
            }
        }
        if (key == "id") {
            node_->id = val;
            node_->has_id = true;
        } else if (key == "class") {
            node_->class_name = val;
            node_->has_class = true;
            node_->classes = split_class_tokens(val);
        } else {
            node_->attributes[key] = val;
        }
        // setAttribute records even when the value is unchanged.
        document_->queue_attribute(isolate, context, node_, key, has_old_value,
                                   old_value);
        return v8::Undefined(isolate);
    }
    // M4-A-4 removeAttribute(name): remove the attribute if present, else no-op.
    // id/class clear their dedicated fields (queries update); other names are
    // erased from the raw attribute table. Returns undefined.
    if (name == "removeAttribute") {
        const std::string key = ascii_lower(arg_string(0));
        // M11-5: only an actually-present attribute produces a record.
        bool has_old_value = false;
        std::string old_value;
        if (key == "id") {
            has_old_value = node_->has_id;
            old_value = node_->id;
        } else if (key == "class") {
            has_old_value = node_->has_class;
            old_value = node_->class_name;
        } else {
            const auto old = node_->attributes.find(key);
            if (old != node_->attributes.end()) {
                has_old_value = true;
                old_value = old->second;
            }
        }
        if (key == "id") {
            node_->id.clear();
            node_->has_id = false;
        } else if (key == "class") {
            node_->class_name.clear();
            node_->has_class = false;
            node_->classes.clear();
        } else {
            node_->attributes.erase(key);
        }
        if (has_old_value) {
            document_->queue_attribute(isolate, context, node_, key, true,
                                       old_value);
        }
        return v8::Undefined(isolate);
    }
    // M11-10 toggleAttribute(name, force?): add an empty attribute or remove it;
    // an explicit boolean force chooses the outcome. Queues an attribute record
    // only when presence actually changes. id/class keep their dedicated fields.
    if (name == "toggleAttribute") {
        const std::string key = ascii_lower(arg_string(0));
        bool present = false;
        std::string old_value;
        if (key == "id") {
            present = node_->has_id;
            old_value = node_->id;
        } else if (key == "class") {
            present = node_->has_class;
            old_value = node_->class_name;
        } else {
            const auto it = node_->attributes.find(key);
            if (it != node_->attributes.end()) {
                present = true;
                old_value = it->second;
            }
        }
        const bool force_specified =
            args.Length() > 1 && !args[1]->IsUndefined();
        const bool force =
            force_specified ? args[1]->BooleanValue(isolate) : false;
        const bool should_exist = force_specified ? force : !present;
        if (should_exist != present) {
            if (should_exist) {
                if (key == "id") {
                    node_->id.clear();
                    node_->has_id = true;
                } else if (key == "class") {
                    node_->class_name.clear();
                    node_->has_class = true;
                    node_->classes.clear();
                } else {
                    node_->attributes[key] = std::string();
                }
            } else {
                if (key == "id") {
                    node_->id.clear();
                    node_->has_id = false;
                } else if (key == "class") {
                    node_->class_name.clear();
                    node_->has_class = false;
                    node_->classes.clear();
                } else {
                    node_->attributes.erase(key);
                }
            }
            document_->queue_attribute(isolate, context, node_, key, present,
                                       old_value);
        }
        return v8::Boolean::New(isolate, should_exist);
    }
    // M4-A-3 minimal tree editing. child/ref must be element host objects (ref may
    // be null for insertBefore). Errors throw a JS TypeError (via the eval
    // TryCatch this surfaces as iv8.JSError name "TypeError"). Mutations touch the
    // live DomNode parent/children, so document-level queries reflect them at once.
    // NOTE: textContent is NOT recomputed here — it stays the M2-7 stored aggregate
    // (this minimal tree has no text nodes; see docs/m4_a_3_tree_editing.md).
    const auto throw_type_error = [&](const char* message) -> v8::Local<v8::Value> {
        isolate->ThrowException(v8::Exception::TypeError(v8_string(isolate, message)
                                                            .As<v8::String>()));
        return v8::Undefined(isolate);
    };
    // M11-13: detach this element from its parent.
    if (name == "remove") {
        DomNode* parent = node_->parent;
        if (parent != nullptr) {
            DomNode* previous = nullptr;
            DomNode* next = nullptr;
            siblings_of(node_, &previous, &next);
            detach_from_parent(node_);
            std::vector<DomNode*> added;
            std::vector<DomNode*> removed = {node_};
            document_->queue_child_list(isolate, context, parent, added,
                                        removed, previous, next);
        }
        return v8::Undefined(isolate);
    }
    if (name == "hasChildNodes") {
        return v8::Boolean::New(isolate, !node_->children.empty());
    }
    // M11-14: detached shallow/deep clone and node identity/equality.
    if (name == "cloneNode") {
        const bool deep =
            args.Length() > 0 && args[0]->BooleanValue(isolate);
        DomNode* clone = document_->clone_detached(node_, deep);
        return document_->wrap_element(isolate, context, clone);
    }
    if (name == "isSameNode" || name == "isEqualNode") {
        DomNode* other =
            node_of_arg(args.Length() > 0 ? args[0]
                                          : v8::Local<v8::Value>());
        if (other == nullptr) {
            return v8::False(isolate);
        }
        return v8::Boolean::New(
            isolate, name == "isSameNode"
                         ? other == node_
                         : document_->nodes_equal(node_, other));
    }
    // M11-13: replace a direct child with another element node.
    if (name == "replaceChild") {
        DomNode* new_child = node_of_arg(args.Length() > 0
                                             ? args[0]
                                             : v8::Local<v8::Value>());
        DomNode* old_child = node_of_arg(args.Length() > 1
                                             ? args[1]
                                             : v8::Local<v8::Value>());
        if (new_child == nullptr || old_child == nullptr) {
            return throw_type_error("replaceChild: arguments must be elements");
        }
        if (old_child->parent != node_) {
            return throw_type_error(
                "replaceChild: oldChild is not a child of this node");
        }
        if (new_child == old_child) {
            return args[1];
        }
        if (is_self_or_ancestor(new_child, node_)) {
            return throw_type_error(
                "replaceChild: cannot insert a node into its own subtree");
        }
        if (new_child->parent != nullptr) {
            DomNode* old_parent = new_child->parent;
            DomNode* previous = nullptr;
            DomNode* next = nullptr;
            siblings_of(new_child, &previous, &next);
            detach_from_parent(new_child);
            std::vector<DomNode*> added;
            std::vector<DomNode*> removed = {new_child};
            document_->queue_child_list(isolate, context, old_parent, added,
                                        removed, previous, next);
        }
        DomNode* old_previous = nullptr;
        DomNode* old_next = nullptr;
        siblings_of(old_child, &old_previous, &old_next);
        std::size_t index = 0;
        const auto old_it =
            std::find(node_->children.begin(), node_->children.end(), old_child);
        if (old_it != node_->children.end()) {
            index = static_cast<std::size_t>(
                std::distance(node_->children.begin(), old_it));
        }
        detach_from_parent(old_child);
        std::vector<DomNode*> added;
        std::vector<DomNode*> removed = {old_child};
        document_->queue_child_list(isolate, context, node_, added, removed,
                                    old_previous, old_next);
        if (index > node_->children.size()) {
            index = node_->children.size();
        }
        new_child->parent = node_;
        node_->children.insert(node_->children.begin() + index, new_child);
        DomNode* new_previous = nullptr;
        DomNode* new_next = nullptr;
        siblings_of(new_child, &new_previous, &new_next);
        added = {new_child};
        removed.clear();
        document_->queue_child_list(isolate, context, node_, added, removed,
                                    new_previous, new_next);
        return old_child == nullptr ? v8::Undefined(isolate) : args[1];
    }
    if (name == "appendChild" || name == "removeChild" ||
        name == "insertBefore") {
        DomNode* child = node_of_arg(args.Length() > 0 ? args[0]
                                                       : v8::Local<v8::Value>());
        if (child == nullptr) {
            return throw_type_error("argument is not an element");
        }
        if (name == "removeChild") {
            if (child->parent != node_) {
                return throw_type_error("removeChild: not a child of this node");
            }
            DomNode* previous = nullptr;
            DomNode* next = nullptr;
            siblings_of(child, &previous, &next);
            detach_from_parent(child);
            // M11-5: childList removal record, siblings captured before detach.
            std::vector<DomNode*> added;
            std::vector<DomNode*> removed = {child};
            document_->queue_child_list(isolate, context, node_, added,
                                        removed, previous, next);
            return args[0];
        }
        // appendChild / insertBefore: resolve ref (insertBefore only; null = end).
        DomNode* ref = nullptr;
        if (name == "insertBefore") {
            const bool ref_is_null =
                args.Length() > 1 && (args[1]->IsNull() || args[1]->IsUndefined());
            if (!ref_is_null) {
                ref = node_of_arg(args.Length() > 1 ? args[1]
                                                    : v8::Local<v8::Value>());
                if (ref == nullptr) {
                    return throw_type_error("insertBefore: ref is not an element");
                }
                if (ref->parent != node_) {
                    return throw_type_error(
                        "insertBefore: ref is not a child of this node");
                }
                if (child == ref) {
                    return args[0];  // stable no-op
                }
            }
        }
        if (is_self_or_ancestor(child, node_)) {
            return throw_type_error("cannot insert a node into its own subtree");
        }
        // M11-5: capture the old parent's siblings before a possible move.
        DomNode* old_parent = child->parent;
        DomNode* old_previous = nullptr;
        DomNode* old_next = nullptr;
        if (old_parent != nullptr) {
            siblings_of(child, &old_previous, &old_next);
        }
        detach_from_parent(child);
        child->parent = node_;
        if (ref == nullptr) {
            node_->children.push_back(child);  // append
        } else {
            const auto pos =
                std::find(node_->children.begin(), node_->children.end(), ref);
            node_->children.insert(pos, child);
        }
        DomNode* new_previous = nullptr;
        DomNode* new_next = nullptr;
        siblings_of(child, &new_previous, &new_next);
        // M11-5: a move queues removal (old parent) before addition (new parent).
        if (old_parent != nullptr) {
            std::vector<DomNode*> added;
            std::vector<DomNode*> removed = {child};
            document_->queue_child_list(isolate, context, old_parent, added,
                                        removed, old_previous, old_next);
        }
        std::vector<DomNode*> added = {child};
        std::vector<DomNode*> removed;
        document_->queue_child_list(isolate, context, node_, added, removed,
                                    new_previous, new_next);
        return args[0];
    }
    // M4-A-5 subtree queries: same minimal selector subset / tag rules as the
    // document-level ones, but scoped to THIS element's current subtree (the
    // element itself + its descendants, so the root may match). Plain JS Array
    // for the *All / by-tag forms; null / first-match for querySelector.
    if (name == "querySelector") {
        return document_->wrap_element(
            isolate, context, dfs_find(node_, selector_predicate(arg_string(0))));
    }
    if (name == "querySelectorAll") {
        std::vector<DomNode*> out;
        collect_matching(node_, selector_predicate(arg_string(0)), out);
        return document_->elements_array(isolate, context, out);
    }
    if (name == "getElementsByTagName") {
        std::vector<DomNode*> out;
        collect_matching(node_, tag_predicate(arg_string(0)), out);
        return document_->elements_array(isolate, context, out);
    }
    if (name == "getElementsByClassName") {  // M11-19: descendants only
        std::vector<std::string> tokens = split_class_tokens(arg_string(0));
        std::vector<DomNode*> out;
        if (!tokens.empty()) {
            for (DomNode* child : node_->children) {
                collect_matching(
                    child,
                    [&tokens](const DomNode* n) {
                        return has_all_class_tokens(n, tokens);
                    },
                    out);
            }
        }
        return document_->elements_array(isolate, context, out);
    }
    if (node_->tag == "select" && (name == "item" || name == "namedItem")) {
        // M11-22: option lookup helpers. The collection itself stays a plain array.
        const std::vector<DomNode*> options = collect_options(node_);
        if (name == "item") {
            int index = 0;
            if (args.Length() > 0) {
                v8::Local<v8::Int32> converted;
                if (!args[0]->ToInt32(context).ToLocal(&converted)) {
                    return v8::Null(isolate);
                }
                index = converted->Value();
            }
            if (index < 0 || index >= static_cast<int>(options.size())) {
                return v8::Null(isolate);
            }
            return document_->wrap_element(
                isolate, context, options[static_cast<std::size_t>(index)]);
        }
        const std::string wanted = arg_string(0);
        for (DomNode* option : options) {
            if (option->id == wanted) {
                return document_->wrap_element(isolate, context, option);
            }
            const auto it = option->attributes.find("name");
            if (it != option->attributes.end() && it->second == wanted) {
                return document_->wrap_element(isolate, context, option);
            }
        }
        return v8::Null(isolate);
    }
    // M4-B-2 minimal ancestor/containment: true iff the argument is an element in
    // this tree that is this node itself or a descendant of it. A non-element
    // argument (null / undefined / primitive / plain object / document / window /
    // Event) yields nullptr from node_of_arg -> the walk never starts -> false.
    // Purely structural over the current tree (reflects M4-A-3 edits at once);
    // independent of isConnected (a detached parent contains its detached child).
    if (name == "contains") {
        DomNode* other =
            node_of_arg(args.Length() > 0 ? args[0] : v8::Local<v8::Value>());
        bool result = false;
        for (DomNode* n = other; n != nullptr; n = n->parent) {
            if (n == node_) {
                result = true;  // self (n == node_ on the first step) or ancestor
                break;
            }
        }
        return v8::Boolean::New(isolate, result);
    }
    // M4-B-3 single-node selector match: apply the SAME minimal selector predicate
    // used by document/element querySelector[All] (#id / .class / tagname) to this
    // node only. A complex/empty/unsupported selector yields a match-nothing
    // predicate -> false (no syntax error). Depends only on this element's own
    // tag/id/class (live), not on its position in the tree, so it works on a
    // detached element too.
    if (name == "matches") {
        return v8::Boolean::New(isolate,
                                selector_predicate(arg_string(0))(node_));
    }
    // M4-B-4 nearest-ancestor selector match: check this node first, then walk the
    // parent chain, returning the first node whose own tag/id/class matches the
    // same minimal selector (shared with matches / querySelector[All]); JS null if
    // none. Complex/empty selector -> match-nothing predicate -> null. Walks the
    // live parent chain, so it follows tree edits and works within a detached
    // subtree (independent of isConnected). Wrapping reuses wrap_element.
    if (name == "closest") {
        const std::function<bool(const DomNode*)> pred =
            selector_predicate(arg_string(0));
        for (DomNode* n = node_; n != nullptr; n = n->parent) {
            if (pred(n)) {
                return document_->wrap_element(isolate, context, n);
            }
        }
        return v8::Null(isolate);
    }
    // M6-1 form.reset() (exposed only on <form>): restore the supported control
    // state (input/textarea/button.value, input.checked, option.selected) of every
    // such control in this form's CURRENT subtree to its parse/create-time initial
    // seed. select.value has no own slot, so it recovers via option.selected.
    // Restoring all three slots on each matched control is harmless (a control only
    // reads the slot it exposes). Reads the live subtree, so a reparented control
    // outside the form is unaffected; works on a detached <form>. Returns undefined;
    // no reset event, no validation, no submission.
    if (name == "reset") {
        std::vector<DomNode*> controls;
        collect_matching(node_,
                         [](const DomNode* n) {
                             return n->tag == "input" || n->tag == "textarea" ||
                                    n->tag == "button" || n->tag == "option";
                         },
                         controls);
        for (DomNode* control : controls) {
            control->value = control->initial_value;
            control->checked = control->initial_checked;
            control->selected = control->initial_selected;
        }
        return v8::Undefined(isolate);
    }
    // M7-1 form.submit() (exposed only on <form>): a minimal "exists and is
    // callable" submission entry point. No arguments; returns undefined. It does
    // NOT navigate, make a network request, dispatch a submit event, validate, or
    // change any control's live value / default baseline — a deliberate no-op this
    // phase. Callable on attached and detached forms.
    if (name == "submit") {
        return v8::Undefined(isolate);
    }
    // M7-2 form.requestSubmit() (exposed only on <form>): like submit(), a minimal
    // "exists and is callable" entry point. This phase supports only the no-argument
    // call; it returns undefined and is a deliberate no-op — no submitter handling,
    // no submit/input/change/reset event, no validation, no navigation / network /
    // FormData, and no change to any control's live value / default baseline.
    // Callable on attached and detached forms; result matches submit().
    if (name == "requestSubmit") {
        return v8::Undefined(isolate);
    }
    return v8::Undefined(isolate);
}

// M4-A-7: an element's dispatchEvent bubbles along the current tree. The owning
// document runs the walk (element -> ancestors -> document -> window), because it
// owns the element wrappers, is_in_tree, and the window target. `receiver` is this
// element's JS object and stays event.target throughout.
void ElementHost::dispatch_event(v8::Isolate* isolate,
                                 v8::Local<v8::Context> context,
                                 v8::Local<v8::Object> event,
                                 v8::Local<v8::Object> receiver) {
    document_->run_element_bubbling(isolate, context, event, node_, this, receiver);
}

// --- M2-4 timers: JS-visible setTimeout/clearTimeout/setInterval/clearInterval.
// These are bare global functions (not a host object). Each recovers the owning
// ContextState from the isolate embedder-data slot and delegates to its timer
// registry. They run during eval, i.e. under the context operation guard.

ContextState* state_from_isolate(v8::Isolate* isolate) {
    return static_cast<ContextState*>(isolate->GetData(kIsolateStateSlot));
}

void register_timer_callback(const v8::FunctionCallbackInfo<v8::Value>& info,
                             bool repeating) {
    v8::Isolate* isolate = info.GetIsolate();
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr || info.Length() < 1 || !info[0]->IsFunction()) {
        return;  // invalid call: no timer registered, returns undefined
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Function> callback = info[0].As<v8::Function>();
    std::int32_t delay = 0;
    if (info.Length() > 1) {
        delay = info[1]->Int32Value(context).FromMaybe(0);
    }
    const std::int64_t id = state->register_timer(callback, delay, repeating);
    info.GetReturnValue().Set(static_cast<double>(id));
}

void set_timeout_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "setTimeout");  // M9-2
    register_timer_callback(info, /*repeating=*/false);
}

void set_interval_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "setInterval");  // M9-2
    register_timer_callback(info, /*repeating=*/true);
}

// --- M11-4 microtasks: JS-visible queueMicrotask.
//
// queueMicrotask(callback) appends a callback to the current context's V8
// microtask queue. It deliberately has no cancel form (the standard API has
// none) and no new Python pump: ContextState already installs
// MicrotasksPolicy::kExplicit, so pending microtasks run only when
// Page.run_jobs() performs the checkpoint. The callback must be a function;
// unlike the permissive rAF/idle host functions, a non-callable argument throws
// a JS TypeError before anything is queued.
void queue_microtask_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "queueMicrotask");  // M9-2
    v8::Isolate* isolate = info.GetIsolate();
    if (info.Length() < 1 || !info[0]->IsFunction()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(
                isolate, "queueMicrotask: callback must be a function")));
        return;
    }
    isolate->EnqueueMicrotask(info[0].As<v8::Function>());
}

// --- M11-2 animation frames: JS-visible requestAnimationFrame /
// cancelAnimationFrame. Bare global functions like the M2-4 timers, reaching the
// owning ContextState through the isolate embedder-data slot. They run during eval
// (or during a frame pump), i.e. under the context operation guard.
//
// requestAnimationFrame(callback) -> handle (a Number). The callback queue is FIFO
// and independent of the timer registry. M11-31 passes the current virtual
// timestamp as the callback's only argument; legacy frames do not advance that
// clock. Nothing fires until Page.run_animation_frame() (or the M11-30 facade)
// runs the queue.
void request_animation_frame_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "requestAnimationFrame");  // M9-2
    v8::Isolate* isolate = info.GetIsolate();
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr || info.Length() < 1 || !info[0]->IsFunction()) {
        return;  // invalid call: nothing queued, returns undefined
    }
    const std::int64_t handle =
        state->register_animation_frame(info[0].As<v8::Function>());
    info.GetReturnValue().Set(static_cast<double>(handle));
}

// cancelAnimationFrame(handle): drop a not-yet-run callback. Its handle space is
// separate from the timer ids, so it does NOT cancel a setTimeout/setInterval.
void cancel_animation_frame_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "cancelAnimationFrame");  // M9-2
    v8::Isolate* isolate = info.GetIsolate();
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr || info.Length() < 1) {
        return;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::int64_t handle =
        static_cast<std::int64_t>(info[0]->IntegerValue(context).FromMaybe(0));
    state->cancel_animation_frame(handle);
}

// --- M11-3 idle callbacks: JS-visible requestIdleCallback / cancelIdleCallback.
// Bare global functions like the M2-4 timers and the M11-2 frame globals, reaching
// the owning ContextState through the isolate embedder-data slot. They run during
// eval (or during an idle pump), i.e. under the context operation guard.
//
// requestIdleCallback(callback[, options]) -> handle (a Number). The queue is FIFO
// and independent of both the timer registry and the animation-frame queue. Nothing
// fires until Page.run_idle_callbacks() is called.
//
// options handling: `options` may be absent / undefined / null (no options), or an
// object whose OPTIONAL `timeout` must be a Number. A non-object options, or a
// present-but-non-Number / non-finite / negative `timeout`, throws a TypeError —
// this phase rejects a malformed value loudly rather than silently reinterpreting
// it. A valid `timeout` is RECORDED ONLY: no real timeout firing semantics exist
// this phase (there is no clock). Unknown option keys are ignored.
void request_idle_callback_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "requestIdleCallback");  // M9-2
    v8::Isolate* isolate = info.GetIsolate();
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr || info.Length() < 1 || !info[0]->IsFunction()) {
        return;  // invalid call: nothing queued, returns undefined
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::int32_t timeout = -1;  // absent
    if (info.Length() > 1 && !info[1]->IsUndefined() && !info[1]->IsNull()) {
        if (!info[1]->IsObject()) {
            isolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8Literal(
                    isolate, "requestIdleCallback: options must be an object")));
            return;
        }
        v8::Local<v8::Object> options = info[1].As<v8::Object>();
        v8::Local<v8::Value> value;
        if (!options->Get(context, v8_string(isolate, "timeout")).ToLocal(&value)) {
            return;  // the getter threw
        }
        if (!value->IsUndefined()) {
            if (!value->IsNumber()) {
                isolate->ThrowException(v8::Exception::TypeError(
                    v8::String::NewFromUtf8Literal(
                        isolate,
                        "requestIdleCallback: options.timeout must be a number")));
                return;
            }
            const double ms = value.As<v8::Number>()->Value();
            if (std::isnan(ms) || std::isinf(ms) || ms < 0.0) {
                isolate->ThrowException(v8::Exception::TypeError(
                    v8::String::NewFromUtf8Literal(
                        isolate,
                        "requestIdleCallback: options.timeout must be a "
                        "non-negative finite number")));
                return;
            }
            // Clamp to int32 range; the value is recorded only, never a deadline.
            timeout = static_cast<std::int32_t>(
                ms > 2147483647.0 ? 2147483647.0 : ms);
        }
    }
    const std::int64_t handle =
        state->register_idle_callback(info[0].As<v8::Function>(), timeout);
    info.GetReturnValue().Set(static_cast<double>(handle));
}

// cancelIdleCallback(handle): drop a not-yet-run callback. Its handle space is
// separate from the timer ids and the animation-frame handles, so it cancels
// neither a setTimeout/setInterval nor a requestAnimationFrame.
void cancel_idle_callback_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "cancelIdleCallback");  // M9-2
    v8::Isolate* isolate = info.GetIsolate();
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr || info.Length() < 1) {
        return;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::int64_t handle =
        static_cast<std::int64_t>(info[0]->IntegerValue(context).FromMaybe(0));
    state->cancel_idle_callback(handle);
}

// Shared by clearTimeout and clearInterval (single id space, like browsers).
void clear_timer_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr || info.Length() < 1) {
        return;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::int64_t id =
        static_cast<std::int64_t>(info[0]->IntegerValue(context).FromMaybe(0));
    state->clear_timer(id);
}

// --- M11-36 deterministic dialogs ------------------------------------------
//
// There is no windowing system or Python callback bridge in this runtime. The
// dialog globals are deterministic stubs: alert/print do nothing, confirm
// returns false, and prompt returns null. They never block and never mutate
// page state.
void alert_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "alert");
}

void confirm_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "confirm");
    info.GetReturnValue().Set(v8::False(info.GetIsolate()));
}

void prompt_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "prompt");
    info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
}

void print_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "print");
}

// --- M11-31/M11-37 virtual performance timeline ------------------------------
//
// performance.now() reports the M11-29/M11-30 facade's virtual clock. This is not
// a wall clock: it starts at 0, advances only through tick()/advance()/
// run_until_idle() (including timer/frame jumps), and resets with each load()
// generation. The legacy pumps do not advance it. Date remains V8's real clock;
// only this runtime-owned clock is virtual.
//
// M11-37 adds a minimal performance entry timeline: mark()/measure()/getEntries*
// and clearMarks()/clearMeasures(). Entries are plain read-only objects stored
// per generation; there is no PerformanceObserver, resource timing, navigation
// timing, or real timeline.
constexpr v8::ExternalPointerTypeTag kPerformanceTag =
    v8::kExternalPointerTypeTagDefault;

PerformanceStore* performance_store_from(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    return static_cast<PerformanceStore*>(
        info.Data().As<v8::External>()->Value(kPerformanceTag));
}

std::string performance_arg_string(
    const v8::FunctionCallbackInfo<v8::Value>& info, int index) {
    if (index >= info.Length()) {
        return std::string();
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::String> str;
    if (!info[index]->ToString(isolate->GetCurrentContext()).ToLocal(&str)) {
        return std::string();
    }
    v8::String::Utf8Value utf8(isolate, str);
    return *utf8 != nullptr
               ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
               : std::string();
}

v8::Local<v8::Value> make_performance_entry(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const PerformanceEntryRecord& entry) {
    v8::Local<v8::Object> object = v8::Object::New(isolate);
    const auto define = [&](const char* key, v8::Local<v8::Value> value) {
        (void)object->DefineOwnProperty(
            context,
            v8::String::NewFromUtf8(isolate, key).ToLocalChecked(), value,
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete));
    };
    define("name", v8_string(isolate, entry.name));
    define("entryType", v8_string(isolate, entry.entry_type));
    define("startTime",
           v8::Number::New(isolate, static_cast<double>(entry.start_time)));
    define("duration",
           v8::Number::New(isolate, static_cast<double>(entry.duration)));
    return object;
}

std::int64_t performance_clock_ms(v8::Isolate* isolate) {
    ContextState* state = state_from_isolate(isolate);
    return state != nullptr ? state->event_loop_time_ms() : 0;
}

void performance_time_origin_getter(
    v8::Local<v8::Name>,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    access_trace_record(isolate, &kPerformanceTraceToken, "performance",
                        "performance", "timeOrigin", AccessOperation::kGet);
    info.GetReturnValue().Set(v8::Number::New(isolate, 0.0));
}

void performance_now_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "performance", "now");
    access_trace_record(info.GetIsolate(), &kPerformanceTraceToken,
                        "performance", "performance", "now",
                        AccessOperation::kCall);  // M9-2 style
    ContextState* state = state_from_isolate(info.GetIsolate());
    const double ms = state == nullptr
                          ? 0.0
                          : static_cast<double>(state->event_loop_time_ms());
    info.GetReturnValue().Set(ms);
}

const PerformanceEntryRecord* performance_find_mark(
    const PerformanceStore* store, const std::string& name) {
    for (auto it = store->entries.rbegin(); it != store->entries.rend(); ++it) {
        if (it->entry_type == "mark" && it->name == name) {
            return &*it;
        }
    }
    return nullptr;
}

v8::Local<v8::Array> performance_entries_array(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const PerformanceStore* store, const std::string& type,
    const std::string& name) {
    std::size_t count = 0;
    for (const PerformanceEntryRecord& entry : store->entries) {
        if ((type.empty() || entry.entry_type == type) &&
            (name.empty() || entry.name == name)) {
            count++;
        }
    }
    v8::Local<v8::Array> array =
        v8::Array::New(isolate, static_cast<int>(count));
    std::uint32_t index = 0;
    for (const PerformanceEntryRecord& entry : store->entries) {
        if ((type.empty() || entry.entry_type == type) &&
            (name.empty() || entry.name == name)) {
            (void)array->Set(context, index++,
                             make_performance_entry(isolate, context, entry));
        }
    }
    return array;
}

void performance_mark_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "performance", "mark");
    access_trace_record(info.GetIsolate(), &kPerformanceTraceToken,
                        "performance", "performance", "mark",
                        AccessOperation::kCall);
    PerformanceStore* store = performance_store_from(info);
    if (store == nullptr) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    PerformanceEntryRecord entry;
    entry.name = performance_arg_string(info, 0);
    entry.entry_type = "mark";
    entry.start_time = performance_clock_ms(isolate);
    store->entries.push_back(entry);
    info.GetReturnValue().Set(
        make_performance_entry(isolate, context, store->entries.back()));
}

void performance_measure_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "performance", "measure");
    access_trace_record(info.GetIsolate(), &kPerformanceTraceToken,
                        "performance", "performance", "measure",
                        AccessOperation::kCall);
    PerformanceStore* store = performance_store_from(info);
    if (store == nullptr) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    PerformanceEntryRecord entry;
    entry.name = performance_arg_string(info, 0);
    entry.entry_type = "measure";
    entry.start_time = 0;
    entry.duration = 0;
    std::int64_t end_time = performance_clock_ms(isolate);
    if (info.Length() > 1 && !info[1]->IsUndefined()) {
        const std::string start_mark = performance_arg_string(info, 1);
        const PerformanceEntryRecord* mark =
            performance_find_mark(store, start_mark);
        if (mark == nullptr) {
            isolate->ThrowException(v8::Exception::Error(
                v8::String::NewFromUtf8(
                    isolate,
                    ("measure: unknown start mark: " + start_mark).c_str())
                    .ToLocalChecked()));
            return;
        }
        entry.start_time = mark->start_time;
    }
    if (info.Length() > 2 && !info[2]->IsUndefined()) {
        const std::string end_mark = performance_arg_string(info, 2);
        const PerformanceEntryRecord* mark =
            performance_find_mark(store, end_mark);
        if (mark == nullptr) {
            isolate->ThrowException(v8::Exception::Error(
                v8::String::NewFromUtf8(
                    isolate,
                    ("measure: unknown end mark: " + end_mark).c_str())
                    .ToLocalChecked()));
            return;
        }
        end_time = mark->start_time;
    }
    entry.duration = end_time - entry.start_time;
    store->entries.push_back(entry);
    info.GetReturnValue().Set(
        make_performance_entry(isolate, context, store->entries.back()));
}

void performance_get_entries_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "performance", "getEntries");
    access_trace_record(info.GetIsolate(), &kPerformanceTraceToken,
                        "performance", "performance", "getEntries",
                        AccessOperation::kCall);
    PerformanceStore* store = performance_store_from(info);
    if (store == nullptr) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    info.GetReturnValue().Set(performance_entries_array(
        isolate, isolate->GetCurrentContext(), store, std::string(),
        std::string()));
}

void performance_get_entries_by_name_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "performance", "getEntriesByName");
    access_trace_record(info.GetIsolate(), &kPerformanceTraceToken,
                        "performance", "performance", "getEntriesByName",
                        AccessOperation::kCall);
    PerformanceStore* store = performance_store_from(info);
    if (store == nullptr) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    info.GetReturnValue().Set(performance_entries_array(
        isolate, isolate->GetCurrentContext(), store, std::string(),
        performance_arg_string(info, 0)));
}

void performance_get_entries_by_type_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "performance", "getEntriesByType");
    access_trace_record(info.GetIsolate(), &kPerformanceTraceToken,
                        "performance", "performance", "getEntriesByType",
                        AccessOperation::kCall);
    PerformanceStore* store = performance_store_from(info);
    if (store == nullptr) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    info.GetReturnValue().Set(performance_entries_array(
        isolate, isolate->GetCurrentContext(), store,
        performance_arg_string(info, 0), std::string()));
}

void performance_clear_entries(PerformanceStore* store, const std::string& type,
                               bool has_name, const std::string& name) {
    store->entries.erase(
        std::remove_if(store->entries.begin(), store->entries.end(),
                       [&](const PerformanceEntryRecord& entry) {
                           return entry.entry_type == type &&
                                  (!has_name || entry.name == name);
                       }),
        store->entries.end());
}

void performance_clear_marks_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "performance", "clearMarks");
    access_trace_record(info.GetIsolate(), &kPerformanceTraceToken,
                        "performance", "performance", "clearMarks",
                        AccessOperation::kCall);
    PerformanceStore* store = performance_store_from(info);
    if (store == nullptr) {
        return;
    }
    performance_clear_entries(store, "mark", info.Length() > 0,
                              performance_arg_string(info, 0));
}

void performance_clear_measures_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "performance", "clearMeasures");
    access_trace_record(info.GetIsolate(), &kPerformanceTraceToken,
                        "performance", "performance", "clearMeasures",
                        AccessOperation::kCall);
    PerformanceStore* store = performance_store_from(info);
    if (store == nullptr) {
        return;
    }
    performance_clear_entries(store, "measure", info.Length() > 0,
                              performance_arg_string(info, 0));
}

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

// --- M11-40 scheduler.postTask ------------------------------------------------
//
// A small Promise-based wrapper over the existing timer queue. There is no
// priority queue, AbortSignal, or automatic loop: the task runs on the next
// run_timers()/facade turn, and the Promise settles through the normal
// microtask checkpoint.
constexpr char kSchedulerBootstrap[] = R"JS(
(function () {
  'use strict';
  function postTask(callback, options) {
    if (typeof callback !== 'function') {
      throw new TypeError('scheduler.postTask: callback must be a function');
    }
    options = options || {};
    var delay = 0;
    if (options.delay !== undefined) {
      delay = Number(options.delay);
      if (!isFinite(delay) || delay < 0) {
        throw new TypeError(
          'scheduler.postTask: delay must be a non-negative finite number');
      }
    }
    return new Promise(function (resolve, reject) {
      setTimeout(function () {
        try {
          resolve(callback());
        } catch (error) {
          reject(error);
        }
      }, delay);
    });
  }
  var scheduler = Object.create(null);
  Object.defineProperty(scheduler, 'postTask', {
    value: postTask, writable: false, enumerable: true, configurable: false
  });
  Object.defineProperty(globalThis, 'scheduler', {
    value: scheduler, writable: false, enumerable: true, configurable: false
  });
})();
)JS";

// --- M11-41..M11-45 BOM metadata / device stubs ------------------------------
//
// The remaining deterministic BOM surfaces that need no layout, network, OS
// permission prompt, or background event source: the visual viewport, a small
// media-query evaluator, deterministic StorageManager/Permissions stubs, and
// legacy empty plugin metadata. Everything is installed as read-only JS
// properties; Promises settle through the normal Page.run_jobs() checkpoint.
constexpr char kBomExtrasBootstrap[] = R"JS(
(function () {
  'use strict';

  function defineReadonly(target, name, value) {
    Object.defineProperty(target, name, {
      value: value,
      writable: false,
      enumerable: true,
      configurable: false
    });
  }

  // Step 5g: capture the temporary native result factory and remove its global
  // binding. matchMedia itself stays a JS function (its toString behavior is
  // unchanged); only the returned list is HostObject-backed.
  var makeMediaQueryList = globalThis.__iv8MakeMediaQueryList;
  delete globalThis.__iv8MakeMediaQueryList;

  // The reported viewport is fixed for the generation, so read it once during
  // the internal bootstrap instead of on every media-query evaluation. These
  // two reads are visible to the access trace as internal-source events.
  var viewportWidth = Number(globalThis.innerWidth || 0);
  var viewportHeight = Number(globalThis.innerHeight || 0);

  // M11-45: navigator plugins/mimeTypes/javaEnabled are installed natively
  // through NavigatorHost (see its get_property) so reads/calls can be traced.

  // M11-42: bounded media-query evaluation. Supported media types are all,
  // screen, and print; supported features are width/height (exact/min/max in
  // px), orientation, prefers-color-scheme, prefers-reduced-motion, hover,
  // pointer, display-mode, update, and scripting. Unsupported queries are
  // simply false. There are no dynamic viewport changes, so no change events.
  function lengthPx(value) {
    var match = /^([0-9]*\.?[0-9]+)px$/i.exec(value.trim());
    return match === null ? NaN : parseFloat(match[1]);
  }

  function featureMatches(rawFeature) {
    var feature = rawFeature.trim().toLowerCase();
    var colon = feature.indexOf(':');
    var name = colon < 0 ? feature : feature.slice(0, colon).trim();
    var value = colon < 0 ? '' : feature.slice(colon + 1).trim();
    var width = viewportWidth;
    var height = viewportHeight;

    if (name === 'width' || name === 'min-width' || name === 'max-width') {
      var w = lengthPx(value);
      if (isNaN(w)) return false;
      return name === 'width' ? width === w
           : name === 'min-width' ? width >= w : width <= w;
    }
    if (name === 'height' || name === 'min-height' || name === 'max-height') {
      var h = lengthPx(value);
      if (isNaN(h)) return false;
      return name === 'height' ? height === h
           : name === 'min-height' ? height >= h : height <= h;
    }
    if (name === 'orientation') {
      if (value === 'portrait') return height >= width;
      if (value === 'landscape') return width > height;
      return false;
    }
    if (name === 'prefers-color-scheme') {
      return value === 'light';
    }
    if (name === 'prefers-reduced-motion') {
      return value === 'no-preference';
    }
    if (name === 'hover' || name === 'any-hover') {
      return value === 'hover';
    }
    if (name === 'pointer' || name === 'any-pointer') {
      return value === 'fine';
    }
    if (name === 'display-mode') {
      return value === 'browser';
    }
    if (name === 'update') {
      return value === 'fast';
    }
    if (name === 'scripting') {
      return value === 'enabled';
    }
    return false;
  }

  function singleQueryMatches(rawQuery) {
    var query = rawQuery.trim();
    if (query === '') return true;
    var negate = false;
    if (/^only\s+/i.test(query)) {
      query = query.replace(/^only\s+/i, '').trim();
    }
    if (/^not\s+/i.test(query)) {
      negate = true;
      query = query.replace(/^not\s+/i, '').trim();
    }
    var parts = query.split(/\s+and\s+/i);
    var matched = true;
    for (var i = 0; i < parts.length; i++) {
      var part = parts[i].trim();
      if (part === '') {
        matched = false;
        break;
      }
      if (part.charAt(0) === '(') {
        if (part.charAt(part.length - 1) !== ')' ||
            !featureMatches(part.slice(1, -1))) {
          matched = false;
          break;
        }
        continue;
      }
      var type = part.toLowerCase();
      if (type === 'all' || type === 'screen') {
        continue;
      }
      matched = false;
      break;
    }
    return negate ? !matched : matched;
  }

  function mediaMatches(rawQuery) {
    var queries = String(rawQuery).split(',');
    for (var i = 0; i < queries.length; i++) {
      if (singleQueryMatches(queries[i])) return true;
    }
    return false;
  }

  defineReadonly(globalThis, 'matchMedia', function (query) {
    var text = String(query);
    return makeMediaQueryList(text, mediaMatches(text));
  });

  // M11-43: deterministic StorageManager. It is installed natively through
  // NavigatorHost (see make_storage_manager_object) so its reads/calls can be
  // access-traced while keeping the plain-object shape.

  // M11-44: navigator.permissions is installed natively through NavigatorHost
  // (see make_permissions_object) so its parent read, query() calls, and returned
  // PermissionStatus reads/calls can be access-traced while keeping the plain
  // descriptor/prototype shape.
})();
)JS";

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

void crypto_get_random_values_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "crypto", "getRandomValues");
    access_trace_record(info.GetIsolate(), &kCryptoTraceToken,
                        "crypto", "crypto", "getRandomValues",
                        AccessOperation::kCall);
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
    access_trace_record(info.GetIsolate(), &kCryptoTraceToken,
                        "crypto", "crypto", "randomUUID",
                        AccessOperation::kCall);
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

void install_global_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             v8::Local<v8::Object> global, const std::string& name,
                             v8::FunctionCallback callback) {
    v8::Local<v8::Function> fn =
        v8::FunctionTemplate::New(isolate, callback)->GetFunction(context)
            .ToLocalChecked();
    (void)global->Set(context, v8_string(isolate, name), fn);
}

// M3-3 / M4-A-7 / M11-9 minimal Event constructor. `new Event(type, init?)`
// yields a plain JS object with `type` (string, coerced from the first
// argument), `bubbles` / `cancelable` (from init: truthy -> true; a missing /
// non-object init -> false), `defaultPrevented` (false), `target` /
// `currentTarget` (null until a dispatch sets them), and the propagation /
// cancellation methods `stopPropagation`, `stopImmediatePropagation`, and
// `preventDefault`. Still no capture phase, eventPhase / composed / timeStamp,
// or listener options.
void event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Object> self = info.This();
    std::string type;
    if (info.Length() > 0) {
        v8::Local<v8::String> str;
        if (info[0]->ToString(context).ToLocal(&str)) {
            v8::String::Utf8Value utf8(isolate, str);
            if (*utf8 != nullptr) {
                type.assign(*utf8, static_cast<std::size_t>(utf8.length()));
            }
        }
    }
    // M4-A-7 / M11-9: bubbles / cancelable from init (only when it is an object).
    bool bubbles = false;
    bool cancelable = false;
    if (info.Length() > 1 && info[1]->IsObject()) {
        v8::Local<v8::Value> value;
        if (info[1].As<v8::Object>()
                ->Get(context, v8_string(isolate, "bubbles"))
                .ToLocal(&value)) {
            bubbles = value->BooleanValue(isolate);
        }
        if (info[1].As<v8::Object>()
                ->Get(context, v8_string(isolate, "cancelable"))
                .ToLocal(&value)) {
            cancelable = value->BooleanValue(isolate);
        }
    }
    (void)self->Set(context, v8_string(isolate, "type"), v8_string(isolate, type));
    (void)self->Set(context, v8_string(isolate, "bubbles"),
                    v8::Boolean::New(isolate, bubbles));
    (void)self->Set(context, v8_string(isolate, "cancelable"),
                    v8::Boolean::New(isolate, cancelable));
    (void)self->Set(context, v8_string(isolate, "defaultPrevented"),
                    v8::False(isolate));
    // M11-11: NONE outside dispatch; 1/2/3 during capturing/at-target/bubbling.
    (void)self->Set(context, v8_string(isolate, "eventPhase"),
                    v8::Integer::New(isolate, 0));
    (void)self->Set(context, v8_string(isolate, "target"), v8::Null(isolate));
    (void)self->Set(context, v8_string(isolate, "currentTarget"),
                    v8::Null(isolate));
    // Propagation / cancellation methods.
    v8::Local<v8::Function> stop_fn =
        v8::FunctionTemplate::New(isolate, &stop_propagation_callback)
            ->GetFunction(context)
            .ToLocalChecked();
    (void)self->Set(context, v8_string(isolate, "stopPropagation"), stop_fn);
    v8::Local<v8::Function> stop_immediate_fn =
        v8::FunctionTemplate::New(isolate, &stop_immediate_propagation_callback)
            ->GetFunction(context)
            .ToLocalChecked();
    (void)self->Set(context, v8_string(isolate, "stopImmediatePropagation"),
                    stop_immediate_fn);
    v8::Local<v8::Function> prevent_default_fn =
        v8::FunctionTemplate::New(isolate, &prevent_default_callback)
            ->GetFunction(context)
            .ToLocalChecked();
    (void)self->Set(context, v8_string(isolate, "preventDefault"),
                    prevent_default_fn);
}

// --- M11-6 event subclasses --------------------------------------------------
//
// The four constructors below reuse event_constructor() for the inherited Event
// fields (type / bubbles / target / currentTarget / stopPropagation) and add a
// small set of own read-only fields. They intentionally do NOT change Event
// itself: no capture phase, cancellation, listener options, native event
// generation, or Python surface is added.

bool event_construct_check(const v8::FunctionCallbackInfo<v8::Value>& info,
                           const char* name) {
    if (info.IsConstructCall()) {
        return true;
    }
    v8::Isolate* isolate = info.GetIsolate();
    const std::string message =
        std::string(name) + " constructor requires 'new'";
    isolate->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8(isolate, message.c_str()).ToLocalChecked()));
    return false;
}

// Reads init[name] when init is an object; absent / non-object init yields
// undefined. Returns false only when the getter threw (exception left pending).
bool event_init_field(v8::Local<v8::Context> context,
                      const v8::FunctionCallbackInfo<v8::Value>& info,
                      const char* name, v8::Local<v8::Value>* out) {
    v8::Isolate* isolate = info.GetIsolate();
    if (info.Length() < 2 || !info[1]->IsObject()) {
        *out = v8::Undefined(isolate);
        return true;
    }
    return info[1]
        .As<v8::Object>()
        ->Get(context, v8_string(isolate, name))
        .ToLocal(out);
}

// Subclass fields are own read-only data properties (sloppy assignment is a
// silent no-op; strict assignment throws). The inherited Event fields stay
// writable because dispatch still sets target / currentTarget on the object.
void set_event_field(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Object> self, const char* name,
                     v8::Local<v8::Value> value) {
    (void)self->DefineOwnProperty(
        context, v8::String::NewFromUtf8(isolate, name).ToLocalChecked(), value,
        v8::ReadOnly);
}

// WebIDL-lite integer conversion: ToNumber, non-finite -> 0, truncate toward
// zero. Deliberately no 32-bit wrap-around (see the boundary note).
bool event_int_field(v8::Local<v8::Context> context, v8::Local<v8::Value> value,
                     double* out) {
    v8::Local<v8::Number> number;
    if (!value->ToNumber(context).ToLocal(&number)) {
        return false;
    }
    const double number_value = number->Value();
    *out = std::isfinite(number_value) ? std::trunc(number_value) : 0.0;
    return true;
}

bool event_string_field(v8::Local<v8::Context> context, v8::Isolate* isolate,
                        v8::Local<v8::Value> value, std::string* out) {
    if (value->IsUndefined()) {
        out->clear();
        return true;
    }
    v8::Local<v8::String> str;
    if (!value->ToString(context).ToLocal(&str)) {
        return false;
    }
    v8::String::Utf8Value utf8(isolate, str);
    *out = (*utf8 != nullptr)
               ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
               : std::string();
    return true;
}

void custom_event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!event_construct_check(info, "CustomEvent")) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    event_constructor(info);
    v8::Local<v8::Value> detail;
    if (!event_init_field(context, info, "detail", &detail)) {
        return;
    }
    if (detail->IsUndefined()) {
        detail = v8::Null(isolate);
    }
    set_event_field(isolate, context, info.This(), "detail", detail);
}

void mouse_event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!event_construct_check(info, "MouseEvent")) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    event_constructor(info);
    v8::Local<v8::Object> self = info.This();
    const char* const fields[] = {"clientX", "clientY", "button", "buttons"};
    for (const char* field : fields) {
        v8::Local<v8::Value> value;
        if (!event_init_field(context, info, field, &value)) {
            return;
        }
        double number = 0;
        if (!event_int_field(context, value, &number)) {
            return;
        }
        set_event_field(isolate, context, self, field,
                        v8::Number::New(isolate, number));
    }
}

void keyboard_event_constructor(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!event_construct_check(info, "KeyboardEvent")) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    event_constructor(info);
    v8::Local<v8::Object> self = info.This();
    const char* const fields[] = {"key", "code"};
    for (const char* field : fields) {
        v8::Local<v8::Value> value;
        if (!event_init_field(context, info, field, &value)) {
            return;
        }
        std::string text;
        if (!event_string_field(context, isolate, value, &text)) {
            return;
        }
        set_event_field(isolate, context, self, field, v8_string(isolate, text));
    }
    v8::Local<v8::Value> repeat_value;
    if (!event_init_field(context, info, "repeat", &repeat_value)) {
        return;
    }
    set_event_field(isolate, context, self, "repeat",
                    v8::Boolean::New(
                        isolate, repeat_value->BooleanValue(isolate)));
}

void focus_event_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!event_construct_check(info, "FocusEvent")) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    event_constructor(info);
    v8::Local<v8::Value> related_target;
    if (!event_init_field(context, info, "relatedTarget", &related_target)) {
        return;
    }
    if (related_target->IsUndefined()) {
        related_target = v8::Null(isolate);
    }
    set_event_field(isolate, context, info.This(), "relatedTarget",
                    related_target);
}

// Tag for the External carrying the WindowEventTarget* in the window event
// functions' Data. V8 15.0 requires an ExternalPointerTypeTag on External::New /
// External::Value; the default suffices for this single pointer type and MUST
// match between New (install_page) and Value (below).
constexpr v8::ExternalPointerTypeTag kWindowEventsTag =
    v8::kExternalPointerTypeTagDefault;

// M3-4 window event target functions. window is the intrinsic global object (not
// a host object), so its addEventListener / removeEventListener / dispatchEvent
// are installed as bare global functions whose FunctionTemplate Data is an
// External pointing at the page's WindowEventTarget. They forward to the shared
// EventTargetHost semantics (dedupe, snapshot dispatch, swallow, order); because
// they are called as `window.method(...)`, args.This() == window, so
// event.target == window.
void window_event_method(const v8::FunctionCallbackInfo<v8::Value>& info,
                         const char* method_name) {
    v8::Isolate* isolate = info.GetIsolate();
    watch_record(isolate, "window", method_name);  // M9-2
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    auto* target = static_cast<EventTargetHost*>(
        info.Data().As<v8::External>()->Value(kWindowEventsTag));
    v8::Local<v8::Value> out;
    if (target->handle_event_method(isolate, context, method_name, info, out)) {
        info.GetReturnValue().Set(out);
    }
}

void window_add_event_listener(const v8::FunctionCallbackInfo<v8::Value>& info) {
    window_event_method(info, "addEventListener");
}

void window_remove_event_listener(const v8::FunctionCallbackInfo<v8::Value>& info) {
    window_event_method(info, "removeEventListener");
}

void window_dispatch_event(const v8::FunctionCallbackInfo<v8::Value>& info) {
    window_event_method(info, "dispatchEvent");
}

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

// M8-2 global URL — a minimal read-only URL object built on the same decompose_url
// decomposition that backs `location`, so the field 口径 is identical. A string is
// "absolute" iff it matches `scheme://…` (a valid scheme followed by `://`).
bool url_is_absolute(const std::string& s) {
    const auto pos = s.find("://");
    if (pos == std::string::npos || pos == 0) {
        return false;
    }
    if (!std::isalpha(static_cast<unsigned char>(s[0]))) {
        return false;
    }
    for (std::size_t i = 0; i < pos; i++) {
        const char c = s[i];
        if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '-' ||
              c == '.')) {
            return false;
        }
    }
    return true;
}

// Resolve `input` against optional `base` under the project's minimal URL 口径:
// an absolute input is taken as-is (base ignored); otherwise a base that is itself
// absolute-with-authority is required, and the reference is merged (protocol-relative
// `//`, absolute-path `/`, query `?`, fragment `#`, or a directory-relative path — no
// dot-segment collapsing). Returns false (=> constructor throws TypeError) when the
// input is relative and no usable base is available.
bool resolve_url(const std::string& input, bool has_base, const std::string& base,
                 std::string& out) {
    if (url_is_absolute(input)) {
        out = input;
        return true;
    }
    if (!has_base || !url_is_absolute(base)) {
        return false;
    }
    const UrlParts b = decompose_url(base);
    if (b.origin.empty()) {
        return false;  // base had no authority to resolve against
    }
    if (input.empty()) {
        out = b.protocol + "//" + b.host + b.pathname + b.search;  // drop base fragment
        return true;
    }
    const char c0 = input[0];
    if (c0 == '#') {
        out = b.protocol + "//" + b.host + b.pathname + b.search + input;
        return true;
    }
    if (c0 == '?') {
        out = b.protocol + "//" + b.host + b.pathname + input;
        return true;
    }
    if (c0 == '/') {
        out = (input.rfind("//", 0) == 0) ? (b.protocol + input)  // protocol-relative
                                          : (b.origin + input);   // absolute path
        return true;
    }
    std::string dir = b.pathname;
    const auto slash = dir.rfind('/');
    dir = (slash == std::string::npos) ? std::string("/") : dir.substr(0, slash + 1);
    if (dir.empty()) {
        dir = "/";
    }
    out = b.origin + dir + input;  // directory-relative merge
    return true;
}

// `new URL(input, base?)`. input / base go through String(...); resolution failure
// (relative input with no usable base) is a TypeError. Fields are read-only own data
// properties matching location's decomposition. Requires a construct call.
void url_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!info.IsConstructCall()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate,
                                           "URL constructor requires 'new'")));
        return;
    }
    const auto to_std = [&](v8::Local<v8::Value> v, std::string& out) -> bool {
        v8::Local<v8::String> str;
        if (!v->ToString(context).ToLocal(&str)) {
            return false;
        }
        v8::String::Utf8Value utf8(isolate, str);
        out = (*utf8 != nullptr)
                  ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
                  : std::string();
        return true;
    };
    std::string input;
    if (!to_std(info[0], input)) {
        return;  // ToString threw
    }
    const bool has_base = info.Length() > 1 && !info[1]->IsUndefined();
    std::string base;
    if (has_base && !to_std(info[1], base)) {
        return;  // ToString threw
    }
    std::string href;
    if (!resolve_url(input, has_base, base, href)) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Invalid URL")));
        return;
    }
    const UrlParts parts = decompose_url(href);
    v8::Local<v8::Object> self = info.This();
    const auto define = [&](const char* name, const std::string& value) {
        (void)self->DefineOwnProperty(
            context, v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
            v8_string(isolate, value), v8::ReadOnly);
    };
    define("href", parts.href);
    define("origin", parts.origin);
    define("protocol", parts.protocol);
    define("host", parts.host);
    define("hostname", parts.hostname);
    define("pathname", parts.pathname);
    define("search", parts.search);
    define("hash", parts.hash);
    // M11-15: URLSearchParams snapshot of this URL's search. It is NOT live:
    // mutating the snapshot does not rewrite the URL (no live-link state).
    v8::Local<v8::Value> usp_value;
    v8::Local<v8::Value> search_params = v8::Null(isolate);
    if (context->Global()
            ->Get(context, v8_string(isolate, "URLSearchParams"))
            .ToLocal(&usp_value) &&
        usp_value->IsFunction()) {
        v8::Local<v8::Value> search_arg = v8_string(isolate, parts.search);
        v8::Local<v8::Object> parsed;
        if (usp_value.As<v8::Function>()
                ->NewInstance(context, 1, &search_arg)
                .ToLocal(&parsed)) {
            search_params = parsed;
        }
    }
    (void)self->DefineOwnProperty(
        context, v8::String::NewFromUtf8Literal(isolate, "searchParams"),
        search_params, v8::ReadOnly);
}

// url.toString() / url.toJSON() — both return this.href.
void url_to_href(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> href;
    if (info.This()->Get(context, v8_string(isolate, "href")).ToLocal(&href)) {
        info.GetReturnValue().Set(href);
    } else {
        info.GetReturnValue().Set(v8::String::Empty(isolate));
    }
}

// M8-3 URLSearchParams — a minimal application/x-www-form-urlencoded query-parameter
// object. The parameter list is kept as a canonical (re-serialized) query string in a
// private slot on the instance (GC-managed, invisible to JS); each method parses it,
// operates, and (for mutators) writes it back. percent 口径: decode '+' -> space and
// %XX -> byte (bad/short % left literal); encode passes [A-Za-z0-9*-._] through, space
// -> '+', every other byte -> %XX (uppercase). No iterator / entries / keys / values /
// forEach / sort / size, and no link to URL.searchParams.
int usp_hex_val(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

std::string usp_decode(const std::string& s) {
    std::string out;
    for (std::size_t i = 0; i < s.size(); i++) {
        const char c = s[i];
        if (c == '+') {
            out += ' ';
        } else if (c == '%' && i + 2 < s.size()) {
            const int hi = usp_hex_val(s[i + 1]);
            const int lo = usp_hex_val(s[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
            } else {
                out += c;
            }
        } else {
            out += c;
        }
    }
    return out;
}

std::string usp_encode(const std::string& s) {
    static const char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (unsigned char c : s) {
        // Explicit ASCII test — std::isalnum is locale-dependent for bytes > 0x7F
        // (e.g. it treats the UTF-8 lead byte 0xC3 as alphanumeric under some Windows
        // locales), which would leak raw multi-byte bytes into the output.
        const bool unreserved =
            (c >= '0' && c <= '9') || (c >= 'A' && c <= 'Z') ||
            (c >= 'a' && c <= 'z') || c == '*' || c == '-' || c == '.' || c == '_';
        if (unreserved) {
            out += static_cast<char>(c);
        } else if (c == ' ') {
            out += '+';
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0x0F];
        }
    }
    return out;
}

std::vector<std::pair<std::string, std::string>> usp_parse(const std::string& q) {
    std::vector<std::pair<std::string, std::string>> out;
    std::size_t start = 0;
    while (true) {
        const std::size_t amp = q.find('&', start);
        const std::string token = (amp == std::string::npos)
                                      ? q.substr(start)
                                      : q.substr(start, amp - start);
        if (!token.empty()) {
            const auto eq = token.find('=');
            if (eq == std::string::npos) {
                out.emplace_back(usp_decode(token), std::string());
            } else {
                out.emplace_back(usp_decode(token.substr(0, eq)),
                                 usp_decode(token.substr(eq + 1)));
            }
        }
        if (amp == std::string::npos) break;
        start = amp + 1;
    }
    return out;
}

std::string usp_serialize(
    const std::vector<std::pair<std::string, std::string>>& pairs) {
    std::string out;
    for (std::size_t i = 0; i < pairs.size(); i++) {
        if (i > 0) out += '&';
        out += usp_encode(pairs[i].first);
        out += '=';
        out += usp_encode(pairs[i].second);
    }
    return out;
}

v8::Local<v8::Private> usp_private_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8::URLSearchParams::state"));
}

std::string usp_read_state(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           v8::Local<v8::Object> self) {
    v8::Local<v8::Value> v;
    if (self->GetPrivate(context, usp_private_key(isolate)).ToLocal(&v) &&
        v->IsString()) {
        v8::String::Utf8Value utf8(isolate, v);
        if (*utf8 != nullptr) {
            return std::string(*utf8, static_cast<std::size_t>(utf8.length()));
        }
    }
    return std::string();
}

void usp_write_state(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Object> self, const std::string& s) {
    (void)self->SetPrivate(context, usp_private_key(isolate), v8_string(isolate, s));
}

std::string usp_arg_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           v8::Local<v8::Value> v) {
    v8::Local<v8::String> str;
    if (!v->ToString(context).ToLocal(&str)) {
        return std::string();
    }
    v8::String::Utf8Value utf8(isolate, str);
    return (*utf8 != nullptr)
               ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
               : std::string();
}

// `new URLSearchParams(init?)`. undefined/null/absent -> empty; another
// URLSearchParams -> copy; a string (via String()) -> parsed (a leading '?' is
// dropped); any other object (record / pair-list / etc.) -> TypeError. Requires new.
void url_search_params_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!info.IsConstructCall()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(
                isolate, "URLSearchParams constructor requires 'new'")));
        return;
    }
    v8::Local<v8::Object> self = info.This();
    v8::Local<v8::Value> init = info[0];
    std::string canonical;
    if (info.Length() == 0 || init->IsUndefined() || init->IsNull()) {
        canonical = std::string();
    } else if (init->IsObject()) {
        v8::Local<v8::Object> obj = init.As<v8::Object>();
        const v8::Maybe<bool> has = obj->HasPrivate(context, usp_private_key(isolate));
        if (has.IsJust() && has.FromJust()) {
            canonical = usp_read_state(isolate, context, obj);  // copy another USP
        } else {
            isolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8Literal(
                    isolate, "URLSearchParams: unsupported init (only a string or "
                             "another URLSearchParams)")));
            return;
        }
    } else {
        std::string str = usp_arg_string(isolate, context, init);
        if (!str.empty() && str[0] == '?') {
            str = str.substr(1);
        }
        canonical = usp_serialize(usp_parse(str));  // canonicalize
    }
    usp_write_state(isolate, context, self, canonical);
}

void usp_get(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::string name = usp_arg_string(isolate, context, info[0]);
    for (const auto& p : usp_parse(usp_read_state(isolate, context, info.This()))) {
        if (p.first == name) {
            info.GetReturnValue().Set(v8_string(isolate, p.second));
            return;
        }
    }
    info.GetReturnValue().Set(v8::Null(isolate));
}

void usp_get_all(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::string name = usp_arg_string(isolate, context, info[0]);
    v8::Local<v8::Array> arr = v8::Array::New(isolate);
    std::uint32_t idx = 0;
    for (const auto& p : usp_parse(usp_read_state(isolate, context, info.This()))) {
        if (p.first == name) {
            (void)arr->Set(context, idx++, v8_string(isolate, p.second));
        }
    }
    info.GetReturnValue().Set(arr);
}

void usp_has(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::string name = usp_arg_string(isolate, context, info[0]);
    for (const auto& p : usp_parse(usp_read_state(isolate, context, info.This()))) {
        if (p.first == name) {
            info.GetReturnValue().Set(true);
            return;
        }
    }
    info.GetReturnValue().Set(false);
}

void usp_append(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::string name = usp_arg_string(isolate, context, info[0]);
    const std::string value = usp_arg_string(isolate, context, info[1]);
    auto pairs = usp_parse(usp_read_state(isolate, context, info.This()));
    pairs.emplace_back(name, value);
    usp_write_state(isolate, context, info.This(), usp_serialize(pairs));
}

void usp_set(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::string name = usp_arg_string(isolate, context, info[0]);
    const std::string value = usp_arg_string(isolate, context, info[1]);
    const auto pairs = usp_parse(usp_read_state(isolate, context, info.This()));
    std::vector<std::pair<std::string, std::string>> out;
    bool replaced = false;
    for (const auto& p : pairs) {
        if (p.first == name) {
            if (!replaced) {  // keep the first occurrence's position, drop the rest
                out.emplace_back(name, value);
                replaced = true;
            }
        } else {
            out.push_back(p);
        }
    }
    if (!replaced) {
        out.emplace_back(name, value);
    }
    usp_write_state(isolate, context, info.This(), usp_serialize(out));
}

void usp_delete(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::string name = usp_arg_string(isolate, context, info[0]);
    const auto pairs = usp_parse(usp_read_state(isolate, context, info.This()));
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& p : pairs) {
        if (p.first != name) {
            out.push_back(p);
        }
    }
    usp_write_state(isolate, context, info.This(), usp_serialize(out));
}

void usp_to_string(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    info.GetReturnValue().Set(
        v8_string(isolate, usp_read_state(isolate, context, info.This())));
}

// --- M11-8 Blob / File / FileReader ------------------------------------------
//
// Blob/File are immutable value objects whose bytes live in a private
// ArrayBuffer slot. FileReader is a plain JS instance whose private slot points
// at a generation-owned state; reads complete on the existing run_jobs()
// microtask checkpoint, so no new pump is introduced.

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

// M8-4 atob / btoa — minimal standard Base64. btoa operates on a "binary string"
// (each UTF-16 code unit must be <= 0xFF; its low 8 bits is the byte); atob returns a
// binary string (each output byte becomes a Latin-1 code unit 0..255, via
// NewFromOneByte). ASCII whitespace in atob input is ignored. Both error paths throw a
// JS TypeError (the project has no DOMException/InvalidCharacterError). No base64url /
// ArrayBuffer API / streaming / auto UTF-8.
std::string base64_encode(const std::string& in) {
    static const char kTbl[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    std::size_t i = 0;
    const std::size_t n = in.size();
    while (i + 3 <= n) {
        const std::uint32_t x = (static_cast<std::uint8_t>(in[i]) << 16) |
                                (static_cast<std::uint8_t>(in[i + 1]) << 8) |
                                static_cast<std::uint8_t>(in[i + 2]);
        out += kTbl[(x >> 18) & 63];
        out += kTbl[(x >> 12) & 63];
        out += kTbl[(x >> 6) & 63];
        out += kTbl[x & 63];
        i += 3;
    }
    const std::size_t rem = n - i;
    if (rem == 1) {
        const std::uint32_t x = static_cast<std::uint8_t>(in[i]) << 16;
        out += kTbl[(x >> 18) & 63];
        out += kTbl[(x >> 12) & 63];
        out += "==";
    } else if (rem == 2) {
        const std::uint32_t x = (static_cast<std::uint8_t>(in[i]) << 16) |
                                (static_cast<std::uint8_t>(in[i + 1]) << 8);
        out += kTbl[(x >> 18) & 63];
        out += kTbl[(x >> 12) & 63];
        out += kTbl[(x >> 6) & 63];
        out += "=";
    }
    return out;
}

int base64_val(char c) {
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    return -1;
}

// Forgiving-base64 decode (WHATWG口径): strip ASCII whitespace; ONLY when the length
// is a multiple of 4, remove up to two trailing '='; then a length % 4 == 1 is a
// failure, and any remaining non-alphabet code point (including a stray '=') is a
// failure. Returns false on any violation.
bool base64_decode(const std::string& in, std::string& out) {
    std::string s;
    for (char c : in) {
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' ||
            c == '\v') {
            continue;
        }
        s += c;
    }
    if (s.size() % 4 == 0) {
        std::size_t k = s.size();
        int pad = 0;
        while (k > 0 && s[k - 1] == '=' && pad < 2) {
            pad++;
            k--;
        }
        s.resize(k);
    }
    if (s.size() % 4 == 1) {
        return false;
    }
    std::uint32_t buf = 0;
    int bits = 0;
    for (char c : s) {
        const int v = base64_val(c);
        if (v < 0) {
            return false;  // a stray '=' or a non-alphabet character
        }
        buf = (buf << 6) | static_cast<std::uint32_t>(v);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            out += static_cast<char>((buf >> bits) & 0xFF);
        }
        buf &= (bits > 0) ? ((1u << bits) - 1) : 0u;  // keep only pending bits
    }
    return true;
}

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

// M9-1: convert an Inspector StringView (CDP protocol message) to a UTF-8
// std::string. CDP JSON is normally 8-bit; a 16-bit buffer is transcoded via V8
// (safe here — sendResponse/sendNotification run inside dispatchProtocolMessage,
// which we call with the isolate entered and a HandleScope active).
std::string inspector_sv_to_utf8(v8::Isolate* isolate,
                                 const v8_inspector::StringView& view) {
    if (view.is8Bit()) {
        return std::string(reinterpret_cast<const char*>(view.characters8()),
                           view.length());
    }
    v8::Local<v8::String> str;
    if (v8::String::NewFromTwoByte(isolate, view.characters16(),
                                   v8::NewStringType::kNormal,
                                   static_cast<int>(view.length()))
            .ToLocal(&str)) {
        v8::String::Utf8Value utf8(isolate, str);
        if (*utf8 != nullptr) {
            return std::string(*utf8, static_cast<std::size_t>(utf8.length()));
        }
    }
    return std::string();
}

// Minimal Inspector client. Every V8InspectorClient method has a default
// implementation. M9-3: runMessageLoopOnPause is overridden to COUNT pauses and
// return immediately (no nested loop — never blocks). V8 calls it only when the
// debugger actually pauses, so the count is direct evidence that a break happened.
class DevToolsClient : public v8_inspector::V8InspectorClient {
public:
    void runMessageLoopOnPause(int /*contextGroupId*/) override {
        pause_count_.fetch_add(1, std::memory_order_relaxed);
        // Return immediately: there is no message loop this phase, so the paused
        // state is instantaneous (observable, non-blocking).
    }
    int pause_count() const {
        return pause_count_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<int> pause_count_{0};
};

// Channel that accumulates the session's synchronous outbound CDP messages
// (responses + notifications) into a buffer the dispatch call drains and returns.
class DevToolsChannel : public v8_inspector::V8Inspector::Channel {
public:
    void set_isolate(v8::Isolate* isolate) { isolate_ = isolate; }
    std::vector<std::string> take() {
        std::vector<std::string> out;
        out.swap(outbox_);
        return out;
    }
    void sendResponse(int /*callId*/,
                      std::unique_ptr<v8_inspector::StringBuffer> message) override {
        push(message.get());
    }
    void sendNotification(
        std::unique_ptr<v8_inspector::StringBuffer> message) override {
        push(message.get());
    }
    void flushProtocolNotifications() override {}

private:
    void push(v8_inspector::StringBuffer* message) {
        if (message != nullptr) {
            outbox_.push_back(inspector_sv_to_utf8(isolate_, message->string()));
        }
    }
    v8::Isolate* isolate_ = nullptr;
    std::vector<std::string> outbox_;
};

// M9-4: stringify a Reflect.get propertyKey without ever throwing. A string/number
// key uses ToString ("x" / "5"); a Symbol key uses its description ("Symbol(desc)")
// so a symbol-keyed Reflect.get is not turned into a TypeError.
std::string reflect_property_string(v8::Isolate* isolate,
                                    v8::Local<v8::Context> context,
                                    v8::Local<v8::Value> key) {
    if (key->IsSymbol()) {
        v8::Local<v8::Value> desc = key.As<v8::Symbol>()->Description(isolate);
        std::string inner;
        if (desc->IsString()) {
            v8::String::Utf8Value utf8(isolate, desc);
            if (*utf8 != nullptr) {
                inner.assign(*utf8, static_cast<std::size_t>(utf8.length()));
            }
        }
        return "Symbol(" + inner + ")";
    }
    v8::Local<v8::String> str;
    if (key->ToString(context).ToLocal(&str)) {
        v8::String::Utf8Value utf8(isolate, str);
        if (*utf8 != nullptr) {
            return std::string(*utf8, static_cast<std::size_t>(utf8.length()));
        }
    }
    return std::string();
}

// M9-4: record a Reflect.get hit if reflect-get watching is enabled (else no-op).
void reflect_record(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    v8::Local<v8::Value> key) {
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr) {
        return;
    }
    WatchRegistry* registry = state->watch_registry();
    if (registry == nullptr || !registry->reflect_get_enabled()) {
        return;
    }
    registry->record_reflect_get(reflect_property_string(isolate, context, key));
}

// M9-4: the Reflect.get hook — records the propertyKey (arg 1), then delegates to
// the original Reflect.get (carried as this function's Data) so semantics are
// exactly preserved. Recording is gated inside reflect_record, so a disabled hook
// is a transparent pass-through.
void reflect_get_wrapper(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (info.Length() > 1) {
        reflect_record(isolate, context, info[1]);
    }
    v8::Local<v8::Function> original = info.Data().As<v8::Function>();
    std::vector<v8::Local<v8::Value>> args;
    args.reserve(static_cast<std::size_t>(info.Length()));
    for (int i = 0; i < info.Length(); i++) {
        args.push_back(info[i]);
    }
    v8::Local<v8::Value> result;
    if (original
            ->Call(context, v8::Undefined(isolate), info.Length(), args.data())
            .ToLocal(&result)) {
        info.GetReturnValue().Set(result);
    }
    // else: the original threw; the pending exception propagates unchanged.
}

// M9-5: record a Reflect.has hit if reflect-has watching is enabled (mirrors
// reflect_record).
void reflect_has_record(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Value> key) {
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr) {
        return;
    }
    WatchRegistry* registry = state->watch_registry();
    if (registry == nullptr || !registry->reflect_has_enabled()) {
        return;
    }
    registry->record_reflect_has(reflect_property_string(isolate, context, key));
}

// M9-5: the Reflect.has hook — records the propertyKey (arg 1), then delegates to
// the original Reflect.has (carried as this function's Data).
void reflect_has_wrapper(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (info.Length() > 1) {
        reflect_has_record(isolate, context, info[1]);
    }
    v8::Local<v8::Function> original = info.Data().As<v8::Function>();
    std::vector<v8::Local<v8::Value>> args;
    args.reserve(static_cast<std::size_t>(info.Length()));
    for (int i = 0; i < info.Length(); i++) {
        args.push_back(info[i]);
    }
    v8::Local<v8::Value> result;
    if (original
            ->Call(context, v8::Undefined(isolate), info.Length(), args.data())
            .ToLocal(&result)) {
        info.GetReturnValue().Set(result);
    }
    // else: the original threw; the pending exception propagates unchanged.
}

// M10-1: record a Reflect.ownKeys hit if reflect-ownKeys watching is enabled.
// Reflect.ownKeys(target) has no propertyKey, so the hit carries only the resource
// name (recorded inside the registry).
void reflect_ownkeys_record(v8::Isolate* isolate) {
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr) {
        return;
    }
    WatchRegistry* registry = state->watch_registry();
    if (registry == nullptr || !registry->reflect_ownkeys_enabled()) {
        return;
    }
    registry->record_reflect_ownkeys();
}

// M10-1: the Reflect.ownKeys hook — records the call, then delegates to the original
// Reflect.ownKeys (carried as this function's Data).
void reflect_ownkeys_wrapper(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    reflect_ownkeys_record(isolate);
    v8::Local<v8::Function> original = info.Data().As<v8::Function>();
    std::vector<v8::Local<v8::Value>> args;
    args.reserve(static_cast<std::size_t>(info.Length()));
    for (int i = 0; i < info.Length(); i++) {
        args.push_back(info[i]);
    }
    v8::Local<v8::Value> result;
    if (original
            ->Call(context, v8::Undefined(isolate), info.Length(), args.data())
            .ToLocal(&result)) {
        info.GetReturnValue().Set(result);
    }
    // else: the original threw; the pending exception propagates unchanged.
}

// M10-2: record an Object.keys hit if Object.keys watching is enabled. Object.keys
// (target) has no propertyKey, so the hit carries only the resource name.
void object_keys_record(v8::Isolate* isolate) {
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr) {
        return;
    }
    WatchRegistry* registry = state->watch_registry();
    if (registry == nullptr || !registry->object_keys_enabled()) {
        return;
    }
    registry->record_object_keys();
}

// M10-2: the Object.keys hook — records the call, then delegates to the original
// Object.keys (carried as this function's Data).
void object_keys_wrapper(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    object_keys_record(isolate);
    v8::Local<v8::Function> original = info.Data().As<v8::Function>();
    std::vector<v8::Local<v8::Value>> args;
    args.reserve(static_cast<std::size_t>(info.Length()));
    for (int i = 0; i < info.Length(); i++) {
        args.push_back(info[i]);
    }
    v8::Local<v8::Value> result;
    if (original
            ->Call(context, v8::Undefined(isolate), info.Length(), args.data())
            .ToLocal(&result)) {
        info.GetReturnValue().Set(result);
    }
    // else: the original threw; the pending exception propagates unchanged.
}

}  // namespace

// M9-1: the current generation's V8 Inspector objects (opaque in the header). The
// channel is owned by the embedder and must outlive the session; declaration order
// (channel before session) ensures the session is destroyed first.
struct DevToolsInspector {
    DevToolsClient client;
    DevToolsChannel channel;
    std::unique_ptr<v8_inspector::V8Inspector> inspector;
    std::unique_ptr<v8_inspector::V8InspectorSession> session;
};

PageState::PageState(HostEnvironment environment)
    : environment_(std::move(environment)) {
    // M9-3: the watch registry requests an Inspector pause through this Page (set
    // once; the registry outlives no generation and is a member of this Page).
    watch_registry_.set_pause_requester([this]() { request_inspector_pause(); });
    // Initial page state uses the fixed default base URL and empty document seed.
    install_page(kDefaultBaseUrl, std::string());
}

void PageState::request_inspector_pause() {
    // 口径 1: without an attached Inspector session, break_on_hit records only —
    // no pause, no error.
    if (inspector_ == nullptr || !inspector_->session) {
        return;
    }
    static const char kReason[] = "watch_apis";
    // breakProgram pauses immediately at the current point (we are on the JS stack
    // inside the watched host-method call, like a `debugger` statement). When the
    // Debugger domain is enabled on the session, V8 emits Debugger.paused and calls
    // the client's runMessageLoopOnPause (which counts the pause and returns).
    inspector_->session->breakProgram(
        v8_inspector::StringView(reinterpret_cast<const uint8_t*>(kReason),
                                 sizeof(kReason) - 1),
        v8_inspector::StringView());
}

int PageState::devtools_pause_count() {
    // Internal probe: how many times the current generation's Inspector has paused
    // (0 if devtools was never enabled). Reset per generation with the Inspector.
    return inspector_ != nullptr ? inspector_->client.pause_count() : 0;
}

void PageState::install_inspector(v8::Isolate* isolate,
                                  v8::Local<v8::Context> context) {
    auto inspector = std::make_unique<DevToolsInspector>();
    inspector->inspector =
        v8_inspector::V8Inspector::create(isolate, &inspector->client);
    inspector->channel.set_isolate(isolate);
    static const char kName[] = "iv8 page";
    inspector->inspector->contextCreated(v8_inspector::V8ContextInfo(
        context, /*contextGroupId=*/1,
        v8_inspector::StringView(reinterpret_cast<const uint8_t*>(kName),
                                 sizeof(kName) - 1)));
    inspector_ = std::move(inspector);
}

bool PageState::devtools_enable() {
    // Idempotent: once enabled, install_page keeps a fresh Inspector per
    // generation, so a later call just confirms.
    if (inspector_) {
        devtools_enabled_ = true;
        return true;
    }
    // with_scope runs under the operation guard — a disposed page raises
    // ContextDisposedError here (translated to JSContextDisposedError).
    state_->with_scope([this](v8::Isolate* isolate, v8::Local<v8::Context> context) {
        install_inspector(isolate, context);
    });
    devtools_enabled_ = true;
    return true;
}

py::list PageState::devtools_dispatch(const std::string& message) {
    if (inspector_ == nullptr) {
        // Not enabled (devtools_url() never called): nothing to attach to.
        return py::list();
    }
    state_->with_scope([this, &message](v8::Isolate* isolate,
                                        v8::Local<v8::Context> context) {
        (void)context;
        if (!inspector_->session) {
            inspector_->session = inspector_->inspector->connect(
                /*contextGroupId=*/1, &inspector_->channel,
                v8_inspector::StringView(),
                v8_inspector::V8Inspector::kFullyTrusted);
        }
        inspector_->channel.set_isolate(isolate);
        inspector_->session->dispatchProtocolMessage(v8_inspector::StringView(
            reinterpret_cast<const uint8_t*>(message.data()), message.size()));
    });
    // Drain the synchronously-produced outbound messages (GIL is held — this runs
    // on the caller's thread inside the pybind trampoline).
    py::list out;
    for (const std::string& frame : inspector_->channel.take()) {
        out.append(py::str(frame));
    }
    return out;
}

void PageState::watch_apis(const std::vector<std::string>& paths,
                           bool break_on_hit) {
    // Registration is a plain member operation (no V8), but a disposed page reuses
    // the M1 error path (JSContextDisposedError).
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    // dedup + set break mode + enable (replaces any prior set).
    watch_registry_.set_paths(paths, break_on_hit);
}

py::list PageState::read_watch_api_hits() {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    py::list out;
    for (const WatchHit& hit : watch_registry_.take_hits()) {  // read-and-clear
        py::dict record;
        record["path"] = hit.path;
        record["resource_name"] = hit.resource_name;
        record["type"] = "call";
        out.append(record);
    }
    return out;
}

void PageState::install_reflect_get_hook(v8::Isolate* isolate,
                                         v8::Local<v8::Context> context) {
    v8::Local<v8::Value> reflect_value;
    if (!context->Global()
             ->Get(context, v8_string(isolate, "Reflect"))
             .ToLocal(&reflect_value) ||
        !reflect_value->IsObject()) {
        return;
    }
    v8::Local<v8::Object> reflect = reflect_value.As<v8::Object>();
    v8::Local<v8::Value> get_value;
    if (!reflect->Get(context, v8_string(isolate, "get")).ToLocal(&get_value) ||
        !get_value->IsFunction()) {
        return;
    }
    // Carry the ORIGINAL Reflect.get as the wrapper's Data so it can delegate.
    v8::Local<v8::Function> wrapper;
    if (v8::Function::New(context, &reflect_get_wrapper, get_value)
            .ToLocal(&wrapper)) {
        (void)reflect->Set(context, v8_string(isolate, "get"), wrapper);
        reflect_hook_installed_ = true;
    }
}

void PageState::watch_reflect_get(bool enabled, bool break_on_hit) {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    watch_registry_.set_reflect_get(enabled, break_on_hit);
    // Install the hook on the current generation now (once). When disabled, the
    // existing wrapper stays but is a transparent pass-through (gated on the flag);
    // a later load starts fresh (no hook installed unless still enabled).
    if (enabled && !reflect_hook_installed_) {
        state_->with_scope(
            [this](v8::Isolate* isolate, v8::Local<v8::Context> context) {
                install_reflect_get_hook(isolate, context);
            });
    }
}

py::list PageState::read_reflect_get_hits() {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    py::list out;
    for (const WatchHit& hit : watch_registry_.take_reflect_get_hits()) {
        py::dict record;
        record["property"] = hit.path;  // WatchHit.path carries the propertyKey
        record["resource_name"] = hit.resource_name;
        record["type"] = "Reflect.get";
        out.append(record);
    }
    return out;
}

void PageState::install_reflect_has_hook(v8::Isolate* isolate,
                                         v8::Local<v8::Context> context) {
    v8::Local<v8::Value> reflect_value;
    if (!context->Global()
             ->Get(context, v8_string(isolate, "Reflect"))
             .ToLocal(&reflect_value) ||
        !reflect_value->IsObject()) {
        return;
    }
    v8::Local<v8::Object> reflect = reflect_value.As<v8::Object>();
    v8::Local<v8::Value> has_value;
    if (!reflect->Get(context, v8_string(isolate, "has")).ToLocal(&has_value) ||
        !has_value->IsFunction()) {
        return;
    }
    v8::Local<v8::Function> wrapper;
    if (v8::Function::New(context, &reflect_has_wrapper, has_value)
            .ToLocal(&wrapper)) {
        (void)reflect->Set(context, v8_string(isolate, "has"), wrapper);
        reflect_has_hook_installed_ = true;
    }
}

void PageState::watch_reflect_has(bool enabled, bool break_on_hit) {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    watch_registry_.set_reflect_has(enabled, break_on_hit);
    if (enabled && !reflect_has_hook_installed_) {
        state_->with_scope(
            [this](v8::Isolate* isolate, v8::Local<v8::Context> context) {
                install_reflect_has_hook(isolate, context);
            });
    }
}

py::list PageState::read_reflect_has_hits() {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    py::list out;
    for (const WatchHit& hit : watch_registry_.take_reflect_has_hits()) {
        py::dict record;
        record["property"] = hit.path;
        record["resource_name"] = hit.resource_name;
        record["type"] = "Reflect.has";
        out.append(record);
    }
    return out;
}

void PageState::install_reflect_ownkeys_hook(v8::Isolate* isolate,
                                             v8::Local<v8::Context> context) {
    v8::Local<v8::Value> reflect_value;
    if (!context->Global()
             ->Get(context, v8_string(isolate, "Reflect"))
             .ToLocal(&reflect_value) ||
        !reflect_value->IsObject()) {
        return;
    }
    v8::Local<v8::Object> reflect = reflect_value.As<v8::Object>();
    v8::Local<v8::Value> ownkeys_value;
    if (!reflect->Get(context, v8_string(isolate, "ownKeys"))
             .ToLocal(&ownkeys_value) ||
        !ownkeys_value->IsFunction()) {
        return;
    }
    v8::Local<v8::Function> wrapper;
    if (v8::Function::New(context, &reflect_ownkeys_wrapper, ownkeys_value)
            .ToLocal(&wrapper)) {
        (void)reflect->Set(context, v8_string(isolate, "ownKeys"), wrapper);
        reflect_ownkeys_hook_installed_ = true;
    }
}

void PageState::watch_reflect_ownkeys(bool enabled, bool break_on_hit) {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    watch_registry_.set_reflect_ownkeys(enabled, break_on_hit);
    if (enabled && !reflect_ownkeys_hook_installed_) {
        state_->with_scope(
            [this](v8::Isolate* isolate, v8::Local<v8::Context> context) {
                install_reflect_ownkeys_hook(isolate, context);
            });
    }
}

py::list PageState::read_reflect_ownkeys_hits() {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    py::list out;
    for (const WatchHit& hit : watch_registry_.take_reflect_ownkeys_hits()) {
        py::dict record;  // Reflect.ownKeys has no propertyKey -> resource_name only
        record["resource_name"] = hit.resource_name;
        record["type"] = "Reflect.ownKeys";
        out.append(record);
    }
    return out;
}

void PageState::install_object_keys_hook(v8::Isolate* isolate,
                                         v8::Local<v8::Context> context) {
    v8::Local<v8::Value> object_value;
    if (!context->Global()
             ->Get(context, v8_string(isolate, "Object"))
             .ToLocal(&object_value) ||
        !object_value->IsObject()) {
        return;
    }
    v8::Local<v8::Object> object_ctor = object_value.As<v8::Object>();
    v8::Local<v8::Value> keys_value;
    if (!object_ctor->Get(context, v8_string(isolate, "keys"))
             .ToLocal(&keys_value) ||
        !keys_value->IsFunction()) {
        return;
    }
    v8::Local<v8::Function> wrapper;
    if (v8::Function::New(context, &object_keys_wrapper, keys_value)
            .ToLocal(&wrapper)) {
        (void)object_ctor->Set(context, v8_string(isolate, "keys"), wrapper);
        object_keys_hook_installed_ = true;
    }
}

void PageState::watch_object_keys(bool enabled, bool break_on_hit) {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    watch_registry_.set_object_keys(enabled, break_on_hit);
    if (enabled && !object_keys_hook_installed_) {
        state_->with_scope(
            [this](v8::Isolate* isolate, v8::Local<v8::Context> context) {
                install_object_keys_hook(isolate, context);
            });
    }
}

py::list PageState::read_object_keys_hits() {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    py::list out;
    for (const WatchHit& hit : watch_registry_.take_object_keys_hits()) {
        py::dict record;  // Object.keys has no propertyKey -> resource_name only
        record["resource_name"] = hit.resource_name;
        record["type"] = "Object.keys";
        out.append(record);
    }
    return out;
}

void PageState::start_access_trace(
    std::size_t max_events,
    std::optional<std::vector<std::string>> object_types) {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    access_trace_.start(max_events, std::move(object_types));
}

void PageState::stop_access_trace() {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    access_trace_.stop();
}

py::dict PageState::read_access_trace(bool clear) {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    const AccessSnapshot snapshot = access_trace_.snapshot(clear);

    py::dict report;
    py::list events;
    for (const AccessEvent& event : snapshot.events) {
        py::dict record;
        record["seq"] = py::cast(event.seq);
        record["generation"] = py::cast(event.generation);
        record["object_id"] = py::cast(event.object_id);
        record["object_type"] = event.object_type;
        record["label"] = event.label;
        record["member"] = event.member;
        record["operation"] = event.operation;
        record["phase"] = event.phase;
        if (!event.resolution.empty()) {
            record["resolution"] = event.resolution;
        }
        record["resource_name"] = event.resource_name;
        record["source_kind"] = event.source_kind;
        events.append(record);
    }
    report["events"] = events;
    report["enabled"] = snapshot.enabled;
    report["generation"] = py::cast(snapshot.generation);
    report["max_events"] = py::cast(snapshot.max_events);
    report["dropped_events"] = py::cast(snapshot.dropped_events);
    if (snapshot.filter_object_types.has_value()) {
        py::list filter;
        for (const std::string& type : *snapshot.filter_object_types) {
            filter.append(type);
        }
        report["filter_object_types"] = filter;
    } else {
        report["filter_object_types"] = py::none();
    }

    py::dict coverage;
    coverage["scope"] =
        "installed HostObject getter/setter/method callbacks";
    py::list object_types;
    for (const std::string& type : AccessTrace::supported_object_types()) {
        object_types.append(type);
    }
    coverage["object_types"] = object_types;
    py::list operations;
    operations.append("get");
    operations.append("set");
    operations.append("call");
    operations.append("get_probe");
    coverage["operations"] = operations;
    coverage["phase"] = "enter";
    py::list not_covered;
    for (const std::string& gap : AccessTrace::not_covered()) {
        not_covered.append(gap);
    }
    coverage["not_covered"] = not_covered;
    report["coverage"] = coverage;
    return report;
}

// M11-35: window.name is a Page-level mutable string. The accessor data carries
// the owning PageState pointer; the PageState outlives every generation, and the
// context is torn down before it, so no callback can outlive it.
constexpr v8::ExternalPointerTypeTag kWindowNameTag =
    v8::kExternalPointerTypeTagDefault;

void window_name_getter(v8::Local<v8::Name>,
                        const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kWindowNameTag));
    access_trace_record(isolate, page, "window", "window", "name",
                        AccessOperation::kGet);
    info.GetReturnValue().Set(v8_string(isolate, page->window_name()));
}

void window_name_setter(v8::Local<v8::Name>, v8::Local<v8::Value> value,
                        const v8::PropertyCallbackInfo<void>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kWindowNameTag));
    access_trace_record(isolate, page, "window", "window", "name",
                        AccessOperation::kSet);
    v8::Local<v8::String> str;
    if (!value->ToString(context).ToLocal(&str)) {
        return;  // coercion threw; leave the stored name unchanged
    }
    v8::String::Utf8Value utf8(isolate, str);
    page->set_window_name(
        *utf8 != nullptr
            ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
            : std::string());
}

// Step 5i: window.parent / top / frames / opener are traced accessors.
// parent/frames/opener are replaceable (the setter stores the new value per
// generation); top stays read-only. Values and assignment results are unchanged.
constexpr v8::ExternalPointerTypeTag kWindowAliasTag =
    v8::kExternalPointerTypeTagDefault;

void window_alias_getter(v8::Local<v8::Name> property,
                         const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kWindowAliasTag));
    if (page == nullptr) {
        return;
    }
    v8::String::Utf8Value utf8(isolate, property);
    const std::string name(*utf8 != nullptr ? *utf8 : "");
    access_trace_record(isolate, page, "window", "window", name.c_str(),
                        AccessOperation::kGet);
    info.GetReturnValue().Set(page->window_alias_value(
        isolate, isolate->GetCurrentContext(), name));
}

void window_alias_setter(v8::Local<v8::Name> property,
                         v8::Local<v8::Value> value,
                         const v8::PropertyCallbackInfo<void>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kWindowAliasTag));
    if (page == nullptr) {
        return;
    }
    v8::String::Utf8Value utf8(isolate, property);
    const std::string name(*utf8 != nullptr ? *utf8 : "");
    access_trace_record(isolate, page, "window", "window", name.c_str(),
                        AccessOperation::kSet);
    page->set_window_alias(isolate, name, value);
}

// M11-35: a deterministic secure-context bit derived from the base URL (no host
// OS or network access): https/wss/file are secure; http/ws are secure only for
// loopback hosts; everything else is insecure.
bool is_secure_context_url(const std::string& base_url) {
    const std::string lower = ascii_lower(base_url);
    if (lower.rfind("https://", 0) == 0 || lower.rfind("wss://", 0) == 0 ||
        lower.rfind("file://", 0) == 0) {
        return true;
    }
    if (lower.rfind("http://", 0) == 0 || lower.rfind("ws://", 0) == 0) {
        const UrlParts parts = decompose_url(base_url);
        const std::string host = ascii_lower(parts.hostname);
        return host == "localhost" || host == "127.0.0.1" ||
               host == "::1" || host == "[::1]";
    }
    return false;
}

// Host-access trace, step 5: the value source for the generic window metadata
// accessors. Values are computed from fixed Page state; no JS object is touched.
v8::Local<v8::Value> PageState::window_metadata_value(
    v8::Isolate* isolate, const std::string& name) const {
    if (name == "devicePixelRatio") {
        return v8::Number::New(isolate, environment_.device_pixel_ratio);
    }
    if (name == "innerWidth") {
        return v8::Integer::New(isolate, environment_.inner_width);
    }
    if (name == "innerHeight") {
        return v8::Integer::New(isolate, environment_.inner_height);
    }
    if (name == "outerWidth") {
        return v8::Integer::New(isolate, environment_.outer_width);
    }
    if (name == "outerHeight") {
        return v8::Integer::New(isolate, environment_.outer_height);
    }
    if (name == "screenX" || name == "screenLeft") {
        return v8::Integer::New(isolate, environment_.screen_x);
    }
    if (name == "screenY" || name == "screenTop") {
        return v8::Integer::New(isolate, environment_.screen_y);
    }
    if (name == "length") {
        return v8::Integer::New(isolate, 0);
    }
    if (name == "isSecureContext") {
        return v8::Boolean::New(isolate,
                               is_secure_context_url(bootstrap_.base_url));
    }
    if (name == "origin") {
        return v8_string(isolate, decompose_url(bootstrap_.base_url).origin);
    }
    if (name == "crossOriginIsolated" || name == "closed") {
        return v8::False(isolate);
    }
    if (name == "frameElement") {
        return v8::Null(isolate);
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> PageState::window_alias_value(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const std::string& name) const {
    if (name == "parent") {
        return window_parent_override_.IsEmpty()
                   ? v8::Local<v8::Value>(context->Global())
                   : window_parent_override_.Get(isolate);
    }
    if (name == "frames") {
        return window_frames_override_.IsEmpty()
                   ? v8::Local<v8::Value>(context->Global())
                   : window_frames_override_.Get(isolate);
    }
    if (name == "top") {
        return context->Global();
    }
    if (name == "opener") {
        return window_opener_override_.IsEmpty()
                   ? v8::Local<v8::Value>(v8::Null(isolate))
                   : window_opener_override_.Get(isolate);
    }
    return v8::Undefined(isolate);
}

void PageState::set_window_alias(v8::Isolate* isolate, const std::string& name,
                                 v8::Local<v8::Value> value) {
    if (name == "parent") {
        window_parent_override_.Reset(isolate, value);
    } else if (name == "frames") {
        window_frames_override_.Reset(isolate, value);
    } else if (name == "opener") {
        window_opener_override_.Reset(isolate, value);
    }
}

void PageState::reset_window_aliases() noexcept {
    window_parent_override_.Reset();
    window_frames_override_.Reset();
    window_opener_override_.Reset();
}

constexpr v8::ExternalPointerTypeTag kWindowTraceTag =
    v8::kExternalPointerTypeTagDefault;

void window_metadata_getter(v8::Local<v8::Name> property,
                            const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kWindowTraceTag));
    v8::String::Utf8Value utf8(isolate, property);
    if (*utf8 == nullptr) {
        return;
    }
    const std::string name(*utf8, static_cast<std::size_t>(utf8.length()));
    access_trace_record(isolate, page, "window", "window", name.c_str(),
                        AccessOperation::kGet);
    info.GetReturnValue().Set(page->window_metadata_value(isolate, name));
}

HostObject* PageState::media_query_list_host(const std::u16string& media,
                                              bool matches) {
    if (!media_query_lists_) {
        media_query_lists_ = std::make_unique<MediaQueryListStore>();
    }
    std::u16string key = media;
    key.push_back(u'\x01');
    key.push_back(matches ? u'1' : u'0');
    auto& by_key = media_query_lists_->by_key;
    auto it = by_key.find(key);
    if (it == by_key.end()) {
        it = by_key
                 .emplace(key, std::make_unique<MediaQueryListHost>(media,
                                                                    matches))
                 .first;
    }
    return it->second.get();
}

void PageState::install_page(const std::string& base_url,
                             const std::string& html) {
    bootstrap_ = PageBootstrap{html, base_url};

    state_ = std::make_shared<ContextState>();
    // Host-access tracing is Page-level. Each generation starts with fresh
    // native identity bindings; the trace configuration/events themselves persist.
    ++generation_;
    access_trace_.begin_generation(generation_);
    state_->set_access_trace(&access_trace_);
    // M11-37: the performance timeline is per-generation.
    performance_store_ = std::make_unique<PerformanceStore>();
    // M9-4/M9-5/M10-1: a fresh generation has pristine (unwrapped) Reflect.get /
    // .has / .ownKeys.
    reflect_hook_installed_ = false;
    reflect_has_hook_installed_ = false;
    reflect_ownkeys_hook_installed_ = false;
    object_keys_hook_installed_ = false;  // M10-2
    // M9-2: point the new generation at the Page-level watch registry so its
    // host-method dispatch points can record hits and eval() can stamp the current
    // script's resource name. The registry itself outlives generations.
    state_->set_watch_registry(&watch_registry_);
    // M3-3/M3-4: before this context's isolate is disposed, release the event-
    // listener Global handles (teardown ordering, mirroring timers). The hook
    // reads host_objects_ + window_events_ lazily at teardown time, so it sees
    // whatever this generation installed below. DocumentHost forwards to its
    // wrapped elements; window_events_ holds window's listeners.
    state_->set_on_teardown([this]() {
        // M9-1: tear down this generation's Inspector (session before inspector)
        // while the isolate is still alive, before releasing host handles.
        if (inspector_) {
            inspector_->session.reset();
            inspector_->inspector.reset();
            inspector_.reset();
        }
        for (const std::unique_ptr<HostObject>& host : host_objects_) {
            host->release_v8_handles();
        }
        // Step 5i: drop replaced parent/frames/opener values while the isolate is
        // still alive.
        reset_window_aliases();
        if (window_events_) {
            window_events_->release_v8_handles();
        }
    });
    host_objects_.clear();
    // Step 5g: matchMedia backings are per-generation; their JS wrappers died
    // with the previous context above.
    media_query_lists_.reset();
    host_objects_.push_back(std::make_unique<HostProbe>());    // M2-1 probe
    host_objects_.push_back(std::make_unique<ConsoleHost>());  // M2-2 console
    // M2-3 navigator + M11-1 screen, both sourced from the Page-level environment_
    // (which outlives generations), so every load() reports identical values.
    host_objects_.push_back(std::make_unique<NavigatorHost>(&environment_));
    host_objects_.push_back(std::make_unique<ScreenHost>(&environment_));  // M11-1
    host_objects_.push_back(  // M2-3 location, sourced from this page's base URL
        std::make_unique<LocationHost>(decompose_url(base_url)));
    auto document = std::make_unique<DocumentHost>(html, base_url);  // M2-6
    document_host_ = document.get();  // M3-4 observing ptr for lifecycle dispatch
    host_objects_.push_back(std::move(document));
    // M3-4 window event target: NOT installed as a named global (window stays the
    // intrinsic global object); it only backs window's listener store.
    window_events_ = std::make_unique<WindowEventTarget>();
    // M4-A-7: give the document the window target so element/document bubbling can
    // reach window as the final hop. document_host_ is the DocumentHost just added.
    static_cast<DocumentHost*>(document_host_)
        ->set_window_target(static_cast<EventTargetHost*>(window_events_.get()));

    // Install the host objects and the browser-like global roots into the
    // context. window / self are aliases of the global object; globalThis is the
    // intrinsic global — so window === globalThis and self === window.
    {
        // Bootstrap accesses are internal, not target-script accesses.
        AccessTrace::SourceScope trace_scope(&access_trace_, std::string(),
                                             "internal");
        state_->with_scope(
            [this, &base_url](v8::Isolate* isolate,
                              v8::Local<v8::Context> context) {
            for (const auto& host : host_objects_) {
                install_host_object(isolate, context, host.get());
            }
            v8::Local<v8::Object> global = context->Global();
            (void)global->Set(
                context, v8::String::NewFromUtf8Literal(isolate, "window"), global);
            (void)global->Set(
                context, v8::String::NewFromUtf8Literal(isolate, "self"), global);
            // M11-35: mutable window.name, persisted by PageState across load().
            (void)global->SetNativeDataProperty(
                context, v8::String::NewFromUtf8Literal(isolate, "name"),
                &window_name_getter, &window_name_setter,
                v8::External::New(isolate, this, kWindowNameTag));
            // M2-4 JS-visible timers (manual-pump; see PageState::run_timers).
            install_global_function(isolate, context, global, "setTimeout",
                                    &set_timeout_callback);
            install_global_function(isolate, context, global, "clearTimeout",
                                    &clear_timer_callback);
            install_global_function(isolate, context, global, "setInterval",
                                    &set_interval_callback);
            install_global_function(isolate, context, global, "clearInterval",
                                    &clear_timer_callback);
            // M11-4 JS-visible microtasks. This feeds the existing V8 microtask
            // queue; Page.run_jobs() is the only trigger (no new pump).
            install_global_function(isolate, context, global, "queueMicrotask",
                                    &queue_microtask_callback);
            // M11-2 JS-visible animation frames (manual-pump; see
            // PageState::run_animation_frame). No clock, no background thread — a
            // queued callback runs only when Page.run_animation_frame() is called.
            install_global_function(isolate, context, global,
                                    "requestAnimationFrame",
                                    &request_animation_frame_callback);
            install_global_function(isolate, context, global,
                                    "cancelAnimationFrame",
                                    &cancel_animation_frame_callback);
            // M11-3 JS-visible idle callbacks (manual-pump; see
            // PageState::run_idle_callbacks). Independent of the timer and
            // animation-frame queues; nothing fires until the pump is called.
            install_global_function(isolate, context, global,
                                    "requestIdleCallback",
                                    &request_idle_callback_callback);
            install_global_function(isolate, context, global,
                                    "cancelIdleCallback",
                                    &cancel_idle_callback_callback);
            // M11-36 deterministic dialog stubs (no UI, no blocking).
            install_global_function(isolate, context, global, "alert",
                                    &alert_callback);
            install_global_function(isolate, context, global, "confirm",
                                    &confirm_callback);
            install_global_function(isolate, context, global, "prompt",
                                    &prompt_callback);
            install_global_function(isolate, context, global, "print",
                                    &print_callback);
            // M11-38 bounded structured clone.
            install_global_function(isolate, context, global, "structuredClone",
                                    &structured_clone_callback);
            // M11-31/M11-37 virtual performance clock and entry timeline:
            // performance.now() reads the facade clock; timeOrigin is the virtual
            // epoch (0); mark/measure/getEntries*/clear* use virtual timestamps.
            // There is no PerformanceObserver, resource/navigation timing, or
            // real clock.
            {
                v8::Local<v8::Object> performance = v8::Object::New(isolate);
                const auto define_performance_readonly =
                    [&](const char* name, v8::Local<v8::Value> value) {
                        (void)performance->DefineOwnProperty(
                            context,
                            v8::String::NewFromUtf8(isolate, name)
                                .ToLocalChecked(),
                            value,
                            static_cast<v8::PropertyAttribute>(
                                v8::ReadOnly | v8::DontDelete));
                    };
                v8::Local<v8::Function> now_fn =
                    v8::FunctionTemplate::New(isolate, &performance_now_callback)
                        ->GetFunction(context)
                        .ToLocalChecked();
                define_performance_readonly("now", now_fn);
                (void)performance->SetNativeDataProperty(
                    context,
                    v8::String::NewFromUtf8Literal(isolate, "timeOrigin"),
                    &performance_time_origin_getter, nullptr,
                    v8::Local<v8::Value>(),
                    static_cast<v8::PropertyAttribute>(v8::ReadOnly |
                                                       v8::DontDelete));
                v8::Local<v8::External> performance_data =
                    v8::External::New(isolate, performance_store_.get(),
                                      kPerformanceTag);
                const auto add_performance_method =
                    [&](const char* name, v8::FunctionCallback callback) {
                        v8::Local<v8::Function> fn =
                            v8::FunctionTemplate::New(isolate, callback,
                                                      performance_data)
                                ->GetFunction(context)
                                .ToLocalChecked();
                        define_performance_readonly(name, fn);
                    };
                add_performance_method("mark", &performance_mark_callback);
                add_performance_method("measure", &performance_measure_callback);
                add_performance_method("getEntries",
                                       &performance_get_entries_callback);
                add_performance_method(
                    "getEntriesByName",
                    &performance_get_entries_by_name_callback);
                add_performance_method(
                    "getEntriesByType",
                    &performance_get_entries_by_type_callback);
                add_performance_method("clearMarks",
                                       &performance_clear_marks_callback);
                add_performance_method("clearMeasures",
                                       &performance_clear_measures_callback);
                (void)global->DefineOwnProperty(
                    context, v8::String::NewFromUtf8Literal(isolate, "performance"),
                    performance,
                    static_cast<v8::PropertyAttribute>(v8::ReadOnly |
                                                       v8::DontDelete));
            }
            // M11-33 OS-backed Crypto slice: getRandomValues + randomUUID.
            // crypto.subtle is intentionally absent in this milestone.
            {
                v8::Local<v8::Object> crypto = v8::Object::New(isolate);
                const auto add_crypto_method = [&](const char* name,
                                                   v8::FunctionCallback callback) {
                    v8::Local<v8::Function> fn =
                        v8::FunctionTemplate::New(isolate, callback)
                            ->GetFunction(context)
                            .ToLocalChecked();
                    (void)crypto->DefineOwnProperty(
                        context,
                        v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
                        fn,
                        static_cast<v8::PropertyAttribute>(v8::ReadOnly |
                                                           v8::DontDelete));
                };
                add_crypto_method("getRandomValues",
                                  &crypto_get_random_values_callback);
                add_crypto_method("randomUUID", &crypto_random_uuid_callback);
                (void)global->DefineOwnProperty(
                    context, v8::String::NewFromUtf8Literal(isolate, "crypto"),
                    crypto,
                    static_cast<v8::PropertyAttribute>(v8::ReadOnly |
                                                       v8::DontDelete));
            }
            // M11-1/M11-26 window geometry. window is the intrinsic global object;
            // these are read-only native accessors on it. Values come from the
            // Page-level environment_ and are re-applied on every generation. M11-35 adds window.length,
            // screenLeft/screenTop aliases, and isSecureContext. Geometry and
            // capabilities are read-only (ReadOnly|DontDelete); no scroll /
            // resize / matchMedia surface.
            {
                // Host-access trace step 5: these were read-only data properties;
                // they are now read-only native accessors so their reads can be
                // observed. Values and assignment behavior are unchanged.
                const auto define_traced_readonly = [&](const char* name) {
                    (void)global->SetNativeDataProperty(
                        context,
                        v8::String::NewFromUtf8(isolate, name)
                            .ToLocalChecked(),
                        &window_metadata_getter, nullptr,
                        v8::External::New(isolate, this, kWindowTraceTag),
                        static_cast<v8::PropertyAttribute>(
                            v8::ReadOnly | v8::DontDelete));
                };
                define_traced_readonly("devicePixelRatio");
                define_traced_readonly("innerWidth");
                define_traced_readonly("innerHeight");
                // M11-26: reported window geometry (read-only; no layout engine).
                define_traced_readonly("outerWidth");
                define_traced_readonly("outerHeight");
                define_traced_readonly("screenX");
                define_traced_readonly("screenY");
                // M11-35: no child browsing contexts -> length 0; screenLeft/Top
                // alias the reported screen coordinates.
                define_traced_readonly("length");
                define_traced_readonly("screenLeft");
                define_traced_readonly("screenTop");
                define_traced_readonly("isSecureContext");
                // M11-41 window flags, now sourced through the same accessor.
                define_traced_readonly("origin");
                define_traced_readonly("crossOriginIsolated");
                define_traced_readonly("closed");
                define_traced_readonly("frameElement");
                // M11-39: this runtime has exactly one top-level browsing
                // context. parent/top/frames alias the window itself; there is
                // no opener and no child frame collection. parent and frames are
                // replaceable; top is the [LegacyUnforgeable] read-only alias.
                // Step 5i: these are traced accessors now. parent/frames/opener
                // keep their writable+configurable behavior (the setter stores a
                // per-generation replacement, and `delete` / top-level `let`
                // declarations still work); top stays read-only. Descriptor reads
                // evaluate the getter.
                const auto define_traced_alias = [&](const char* name,
                                                     bool writable) {
                    v8::Local<v8::Name> key =
                        v8::String::NewFromUtf8(isolate, name).ToLocalChecked();
                    v8::Local<v8::Value> data =
                        v8::External::New(isolate, this, kWindowAliasTag);
                    if (writable) {
                        (void)global->SetNativeDataProperty(
                            context, key, &window_alias_getter,
                            &window_alias_setter, data,
                            v8::PropertyAttribute::None);
                    } else {
                        (void)global->SetNativeDataProperty(
                            context, key, &window_alias_getter, nullptr, data,
                            static_cast<v8::PropertyAttribute>(
                                v8::ReadOnly | v8::DontDelete));
                    }
                };
                define_traced_alias("parent", true);
                define_traced_alias("top", false);
                define_traced_alias("frames", true);
                define_traced_alias("opener", true);
            }
            // M11-40 scheduler.postTask bootstrap. Compiled after setTimeout and
            // Promise are available; scheduler tasks therefore use the existing
            // timer queue and explicit pumps.
            {
                v8::TryCatch try_catch(isolate);
                v8::Local<v8::String> source = v8::String::NewFromUtf8(
                    isolate, kSchedulerBootstrap,
                    v8::NewStringType::kNormal,
                    static_cast<int>(sizeof(kSchedulerBootstrap) - 1))
                    .ToLocalChecked();
                v8::Local<v8::Script> script;
                if (v8::Script::Compile(context, source).ToLocal(&script)) {
                    (void)script->Run(context);
                }
                if (try_catch.HasCaught()) {
                    try_catch.Reset();  // fixed bootstrap; never poison install
                }
            }
            // Step 5g: temporary native factory the bootstrap captures in a
            // closure and deletes immediately, so matchMedia stays a JS function
            // while each result object gets a traceable HostObject backing.
            {
                v8::Local<v8::Function> factory =
                    v8::FunctionTemplate::New(
                        isolate, &make_media_query_list_callback,
                        v8::External::New(isolate, this, kMediaQueryFactoryTag))
                        ->GetFunction(context)
                        .ToLocalChecked();
                (void)global->Set(
                    context,
                    v8::String::NewFromUtf8Literal(
                        isolate, "__iv8MakeMediaQueryList"),
                    factory);
            }
            // M11-41..M11-45 BOM metadata/device stubs. Installed after the
            // window dimensions and navigator so the bootstrap can derive the
            // static visual viewport and media-query results from them.
            {
                v8::TryCatch try_catch(isolate);
                v8::Local<v8::String> source = v8::String::NewFromUtf8(
                    isolate, kBomExtrasBootstrap,
                    v8::NewStringType::kNormal,
                    static_cast<int>(sizeof(kBomExtrasBootstrap) - 1))
                    .ToLocalChecked();
                v8::Local<v8::Script> script;
                if (v8::Script::Compile(context, source).ToLocal(&script)) {
                    (void)script->Run(context);
                }
                if (try_catch.HasCaught()) {
                    try_catch.Reset();  // fixed bootstrap; never poison install
                }
            }
            // Never leave the temporary factory visible to page scripts.
            (void)global->Delete(
                context,
                v8::String::NewFromUtf8Literal(isolate,
                                               "__iv8MakeMediaQueryList"));
            // Step 5d: the static visual viewport is a HostObject (the bootstrap
            // above no longer creates it), so get/set/call/probe entries are
            // observable. Width/height still come from environment_; the global
            // property stays read-only. Internal descriptor change: inner
            // metadata fields are now native accessors instead of data
            // properties; values and write behavior are unchanged.
            {
                auto viewport =
                    std::make_unique<VisualViewportHost>(&environment_);
                v8::Local<v8::Object> viewport_object =
                    make_host_object(isolate, context, viewport.get());
                host_objects_.push_back(std::move(viewport));
                (void)global->DefineOwnProperty(
                    context,
                    v8::String::NewFromUtf8Literal(isolate, "visualViewport"),
                    viewport_object,
                    static_cast<v8::PropertyAttribute>(v8::ReadOnly |
                                                       v8::DontDelete));
            }
            // M11-7 Web Storage: two independent, per-Page in-memory stores.
            // The backings live in PageState (so they survive load()); each
            // generation installs fresh JS Storage objects over them. Methods and
            // the read-only `length` accessor carry an External to the backing.
            {
                const auto install_storage = [&](const char* name,
                                                 StorageStore* store) {
                    v8::Local<v8::Object> storage = v8::Object::New(isolate);
                    v8::Local<v8::Value> data =
                        v8::External::New(isolate, store, kStorageTag);
                    const auto add_method = [&](const char* method,
                                                v8::FunctionCallback callback) {
                        v8::Local<v8::Function> fn =
                            v8::FunctionTemplate::New(isolate, callback, data)
                                ->GetFunction(context)
                                .ToLocalChecked();
                        (void)storage->Set(context, v8_string(isolate, method),
                                           fn);
                    };
                    add_method("getItem", &storage_get_item);
                    add_method("setItem", &storage_set_item);
                    add_method("removeItem", &storage_remove_item);
                    add_method("clear", &storage_clear);
                    add_method("key", &storage_key);
                    (void)storage->SetNativeDataProperty(
                        context,
                        v8::String::NewFromUtf8Literal(isolate, "length"),
                        &storage_length_getter, nullptr, data, v8::ReadOnly);
                    (void)global->DefineOwnProperty(
                        context,
                        v8::String::NewFromUtf8(isolate, name)
                            .ToLocalChecked(),
                        storage,
                        static_cast<v8::PropertyAttribute>(v8::ReadOnly |
                                                           v8::DontDelete));
                };
                install_storage("localStorage", &local_storage_);
                install_storage("sessionStorage", &session_storage_);
            }
            // M3-3 minimal Event constructor (global `Event`). document/element
            // expose addEventListener/removeEventListener/dispatchEvent via their
            // host-object methods; this is the event value they dispatch.
            v8::Local<v8::FunctionTemplate> event_template =
                v8::FunctionTemplate::New(isolate, &event_constructor);
            // SetClassName requires a Local<String> (v8_string yields
            // Local<Value>), so build the name string directly.
            event_template->SetClassName(
                v8::String::NewFromUtf8Literal(isolate, "Event"));
            // M11-6: the subclass templates inherit Event and must be created
            // before either template is instantiated. Each subclass reuses
            // event_constructor for type / bubbles / target / currentTarget /
            // stopPropagation.
            auto make_event_subclass = [&](const char* name,
                                           v8::FunctionCallback callback) {
                v8::Local<v8::FunctionTemplate> tpl =
                    v8::FunctionTemplate::New(isolate, callback);
                tpl->SetClassName(
                    v8::String::NewFromUtf8(isolate, name).ToLocalChecked());
                tpl->Inherit(event_template);
                return tpl;
            };
            const auto custom_event_template =
                make_event_subclass("CustomEvent", &custom_event_constructor);
            const auto mouse_event_template =
                make_event_subclass("MouseEvent", &mouse_event_constructor);
            const auto keyboard_event_template = make_event_subclass(
                "KeyboardEvent", &keyboard_event_constructor);
            const auto focus_event_template =
                make_event_subclass("FocusEvent", &focus_event_constructor);
            (void)global->Set(
                context, v8_string(isolate, "Event"),
                event_template->GetFunction(context).ToLocalChecked());
            const auto install_event_subclass =
                [&](const char* name,
                    v8::Local<v8::FunctionTemplate> tpl) {
                    (void)global->Set(
                        context, v8_string(isolate, name),
                        tpl->GetFunction(context).ToLocalChecked());
                };
            install_event_subclass("CustomEvent", custom_event_template);
            install_event_subclass("MouseEvent", mouse_event_template);
            install_event_subclass("KeyboardEvent", keyboard_event_template);
            install_event_subclass("FocusEvent", focus_event_template);
            // M3-4: window becomes an event target. window is the intrinsic global
            // object, so its three event methods are bare globals whose External
            // data points at window_events_ (created above). They therefore share
            // EventTargetHost's exact semantics with document/element.
            v8::Local<v8::External> window_data = v8::External::New(
                isolate, static_cast<EventTargetHost*>(window_events_.get()),
                kWindowEventsTag);
            auto install_window_event_fn = [&](const char* name,
                                               v8::FunctionCallback callback) {
                v8::Local<v8::Function> fn =
                    v8::FunctionTemplate::New(isolate, callback, window_data)
                        ->GetFunction(context)
                        .ToLocalChecked();
                (void)global->Set(context, v8_string(isolate, name), fn);
            };
            install_window_event_fn("addEventListener",
                                    &window_add_event_listener);
            install_window_event_fn("removeEventListener",
                                    &window_remove_event_listener);
            install_window_event_fn("dispatchEvent", &window_dispatch_event);

            // M8-1 TextEncoder / TextDecoder (UTF-8 only): constructor functions with
            // a prototype method + a read-only `encoding` prototype data property.
            auto install_encoding_ctor =
                [&](const char* class_name, v8::FunctionCallback ctor,
                    const char* method_name, v8::FunctionCallback method) {
                    v8::Local<v8::FunctionTemplate> tpl =
                        v8::FunctionTemplate::New(isolate, ctor);
                    tpl->SetClassName(
                        v8::String::NewFromUtf8(isolate, class_name)
                            .ToLocalChecked());
                    v8::Local<v8::ObjectTemplate> proto = tpl->PrototypeTemplate();
                    proto->Set(isolate, method_name,
                               v8::FunctionTemplate::New(isolate, method));
                    proto->Set(isolate, "encoding",
                               v8::String::NewFromUtf8Literal(isolate, "utf-8"),
                               v8::ReadOnly);
                    (void)global->Set(
                        context, v8_string(isolate, class_name),
                        tpl->GetFunction(context).ToLocalChecked());
                };
            install_encoding_ctor("TextEncoder", &text_encoder_constructor,
                                  "encode", &text_encoder_encode);
            install_encoding_ctor("TextDecoder", &text_decoder_constructor,
                                  "decode", &text_decoder_decode);

            // M8-2 global URL: constructor with read-only per-instance fields (set in
            // the constructor) + toString / toJSON prototype methods returning href.
            {
                v8::Local<v8::FunctionTemplate> url_tpl =
                    v8::FunctionTemplate::New(isolate, &url_constructor);
                url_tpl->SetClassName(
                    v8::String::NewFromUtf8Literal(isolate, "URL"));
                v8::Local<v8::ObjectTemplate> url_proto =
                    url_tpl->PrototypeTemplate();
                url_proto->Set(isolate, "toString",
                               v8::FunctionTemplate::New(isolate, &url_to_href));
                url_proto->Set(isolate, "toJSON",
                               v8::FunctionTemplate::New(isolate, &url_to_href));
                (void)global->Set(
                    context, v8_string(isolate, "URL"),
                    url_tpl->GetFunction(context).ToLocalChecked());
            }

            // M8-3 global URLSearchParams: constructor + the seven query-parameter
            // methods on the prototype. Per-instance state lives in a private slot.
            {
                v8::Local<v8::FunctionTemplate> usp_tpl =
                    v8::FunctionTemplate::New(isolate,
                                              &url_search_params_constructor);
                usp_tpl->SetClassName(
                    v8::String::NewFromUtf8Literal(isolate, "URLSearchParams"));
                v8::Local<v8::ObjectTemplate> usp_proto =
                    usp_tpl->PrototypeTemplate();
                const auto add_method = [&](const char* name,
                                            v8::FunctionCallback cb) {
                    usp_proto->Set(isolate, name,
                                   v8::FunctionTemplate::New(isolate, cb));
                };
                add_method("get", &usp_get);
                add_method("getAll", &usp_get_all);
                add_method("has", &usp_has);
                add_method("set", &usp_set);
                add_method("append", &usp_append);
                add_method("delete", &usp_delete);
                add_method("toString", &usp_to_string);
                (void)global->Set(
                    context, v8_string(isolate, "URLSearchParams"),
                    usp_tpl->GetFunction(context).ToLocalChecked());
            }

            // M11-8 Blob / File / FileReader. Blob/File expose immutable fields
            // and Promise-returning readers; FileReader is a plain JS instance
            // backed by a generation-owned state and completes on run_jobs().
            {
                DocumentHost* document =
                    static_cast<DocumentHost*>(document_host_);
                v8::Local<v8::Value> blob_data =
                    v8::External::New(isolate, document, kBlobTag);
                v8::Local<v8::FunctionTemplate> blob_tpl =
                    v8::FunctionTemplate::New(isolate, &blob_constructor);
                blob_tpl->SetClassName(
                    v8::String::NewFromUtf8Literal(isolate, "Blob"));
                v8::Local<v8::ObjectTemplate> blob_proto =
                    blob_tpl->PrototypeTemplate();
                blob_proto->Set(isolate, "slice",
                                v8::FunctionTemplate::New(
                                    isolate, &blob_slice, blob_data));
                blob_proto->Set(isolate, "text",
                                v8::FunctionTemplate::New(
                                    isolate, &blob_text, blob_data));
                blob_proto->Set(isolate, "arrayBuffer",
                                v8::FunctionTemplate::New(
                                    isolate, &blob_array_buffer, blob_data));
                v8::Local<v8::FunctionTemplate> file_tpl =
                    v8::FunctionTemplate::New(isolate, &file_constructor);
                file_tpl->SetClassName(
                    v8::String::NewFromUtf8Literal(isolate, "File"));
                file_tpl->Inherit(blob_tpl);
                v8::Local<v8::Function> blob_fn =
                    blob_tpl->GetFunction(context).ToLocalChecked();
                document->set_blob_constructor(isolate, blob_fn);
                (void)global->Set(context, v8_string(isolate, "Blob"), blob_fn);
                (void)global->Set(
                    context, v8_string(isolate, "File"),
                    file_tpl->GetFunction(context).ToLocalChecked());
                v8::Local<v8::FunctionTemplate> reader_tpl =
                    v8::FunctionTemplate::New(
                        isolate, &file_reader_constructor, blob_data);
                reader_tpl->SetClassName(
                    v8::String::NewFromUtf8Literal(isolate, "FileReader"));
                v8::Local<v8::ObjectTemplate> reader_proto =
                    reader_tpl->PrototypeTemplate();
                const auto add_reader_method = [&](const char* name,
                                                   v8::FunctionCallback cb) {
                    reader_proto->Set(
                        isolate, name, v8::FunctionTemplate::New(isolate, cb));
                };
                add_reader_method("addEventListener",
                                  &file_reader_add_event_listener);
                add_reader_method("removeEventListener",
                                  &file_reader_remove_event_listener);
                add_reader_method("dispatchEvent",
                                  &file_reader_dispatch_event);
                add_reader_method("readAsText", &file_reader_read_as_text);
                add_reader_method("readAsArrayBuffer",
                                  &file_reader_read_as_array_buffer);
                add_reader_method("abort", &file_reader_abort);
                (void)global->Set(
                    context, v8_string(isolate, "FileReader"),
                    reader_tpl->GetFunction(context).ToLocalChecked());
            }

            // M11-5 global MutationObserver: constructor + prototype methods.
            // Per-instance state lives in a private external slot; the constructor
            // carries the owning DocumentHost as its FunctionTemplate data.
            {
                v8::Local<v8::FunctionTemplate> mo_tpl =
                    v8::FunctionTemplate::New(
                        isolate, &mutation_observer_constructor,
                        v8::External::New(isolate, document_host_,
                                          kMutationObserverTag));
                mo_tpl->SetClassName(v8::String::NewFromUtf8Literal(
                    isolate, "MutationObserver"));
                v8::Local<v8::ObjectTemplate> mo_proto =
                    mo_tpl->PrototypeTemplate();
                mo_proto->Set(
                    isolate, "observe",
                    v8::FunctionTemplate::New(isolate, &mo_observe));
                mo_proto->Set(
                    isolate, "disconnect",
                    v8::FunctionTemplate::New(isolate, &mo_disconnect));
                mo_proto->Set(
                    isolate, "takeRecords",
                    v8::FunctionTemplate::New(isolate, &mo_take_records));
                (void)global->Set(
                    context, v8_string(isolate, "MutationObserver"),
                    mo_tpl->GetFunction(context).ToLocalChecked());
            }

            // M8-4 global atob / btoa (minimal standard Base64).
            install_global_function(isolate, context, global, "btoa",
                                    &btoa_callback);
            install_global_function(isolate, context, global, "atob",
                                    &atob_callback);

            // M9-1: if DevTools was enabled on a prior generation, give this fresh
            // generation its own Inspector + registered context (the WS URL stays
            // stable Python-side). When never enabled, this is skipped entirely.
            if (devtools_enabled_) {
                install_inspector(isolate, context);
            }
            // M9-4: re-wrap Reflect.get for this fresh generation if reflect-get
            // watching is enabled (config persists across load). install_page reset
            // reflect_hook_installed_ to false above, so this wraps the pristine
            // native Reflect.get exactly once.
            if (watch_registry_.reflect_get_enabled()) {
                install_reflect_get_hook(isolate, context);
            }
            // M9-5: same for Reflect.has.
            if (watch_registry_.reflect_has_enabled()) {
                install_reflect_has_hook(isolate, context);
            }
            // M10-1: same for Reflect.ownKeys.
            if (watch_registry_.reflect_ownkeys_enabled()) {
                install_reflect_ownkeys_hook(isolate, context);
            }
            // M10-2: same for Object.keys.
            if (watch_registry_.object_keys_enabled()) {
                install_object_keys_hook(isolate, context);
            }
            });
    }
}

void PageState::dispatch_lifecycle_events() {
    // On a SUCCESSFUL load, auto-run the load-completion lifecycle. Called by
    // Page.load's success path AFTER the scripts run; a load that failed (a script
    // raised) never reaches this. Runs under the operation guard, GIL released
    // around the JS listener calls (like the M2-4 timer pump).
    //
    // M3-6 fixed sequence on `document` (readyState migration + readystatechange)
    // then `window`:
    //   1) document.readyState = "interactive"
    //   2) readystatechange on document   (listeners observe "interactive")
    //   3) DOMContentLoaded on document
    //   4) document.readyState = "complete"
    //   5) readystatechange on document   (listeners observe "complete")
    //   6) load on window
    // readyState is set (a plain C++ member write, GIL-free) BEFORE each dispatch
    // so a listener reading document.readyState sees the value for that step.
    state_->with_scope(
        [this](v8::Isolate* isolate, v8::Local<v8::Context> context) {
            py::gil_scoped_release release_gil;
            v8::Local<v8::Object> global = context->Global();

            // Resolve the document host + its JS object once (event.target).
            auto* document = static_cast<DocumentHost*>(document_host_);
            v8::Local<v8::Object> document_object;
            bool have_document = false;
            if (document != nullptr) {
                v8::Local<v8::Value> document_value;
                if (global->Get(context, v8_string(isolate, "document"))
                        .ToLocal(&document_value) &&
                    document_value->IsObject()) {
                    document_object = document_value.As<v8::Object>();
                    have_document = true;
                }
            }

            if (have_document) {
                document->set_ready_state("interactive");
                document->dispatch_native(isolate, context, "readystatechange",
                                          document_object);
                document->dispatch_native(isolate, context, "DOMContentLoaded",
                                          document_object);
                document->set_ready_state("complete");
                document->dispatch_native(isolate, context, "readystatechange",
                                          document_object);
            }
            // load on window (event.target = the global object).
            if (window_events_) {
                static_cast<EventTargetHost*>(window_events_.get())
                    ->dispatch_native(isolate, context, "load", global);
            }
        });
}

py::list PageState::html_scripts() {
    // M3-5: the scripts the current document parsed from its HTML, in document
    // order. Each entry is a dict {"src": str|None, "code": str}: an inline
    // script has src None and its JS in code; a `<script src>` has its raw src
    // string and an empty code (the host resolves it via resources in Page.load).
    // Pure data read (no V8/isolate access, no operation guard) — the GIL is held
    // by the Python caller.
    py::list result;
    if (document_host_ != nullptr) {
        auto* document = static_cast<DocumentHost*>(document_host_);
        for (const ScriptRecord& script : document->scripts()) {
            py::dict entry;
            if (script.has_src) {
                entry["src"] = script.src;  // pybind casts std::string -> str
            } else {
                entry["src"] = py::none();
            }
            entry["code"] = script.code;
            // M3-10: only executable classic scripts run; Page.load skips the rest
            // (they stay in the DOM / document.scripts but never execute).
            entry["executable"] =
                script.node == nullptr || is_executable_classic_script(script.node);
            result.append(entry);
        }
    }
    return result;
}

void PageState::run_html_script(std::size_t index, const std::string& code,
                                const std::string& name) {
    // M3-7: run one HTML script (Page.load resolves its code/name — inline source
    // or a <script src> looked up in resources) with document.currentScript set to
    // this script's element for the duration, then cleared to null. The guard
    // clears even if eval throws, so the JSError propagates with currentScript
    // already back to null. Host scripts=[...] use plain eval() (no currentScript).
    auto* document = document_host_ != nullptr
                         ? static_cast<DocumentHost*>(document_host_)
                         : nullptr;
    if (document != nullptr) {
        document->set_current_script(index);
    }
    struct CurrentScriptGuard {
        DocumentHost* document;
        ~CurrentScriptGuard() {
            if (document != nullptr) {
                document->clear_current_script();
            }
        }
    } guard{document};
    state_->eval(code, false, name, "html_script");
}

void PageState::load(const std::string& html, const std::string& base_url) {
    // dispose() is terminal for a Page: a load after it uses the M1 error path.
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    // Replace the current page state. dispose() enforces the busy rule
    // (JSContextBusyError if an operation is active) and tears down the current
    // context, which invalidates any retained page-bound JSValues per the M1
    // rules. Then install a fresh context whose location reflects base_url.
    state_->dispose();
    install_page(base_url, html);
    // M3-6: an explicit load enters the load lifecycle — this generation is
    // "loading" (scripts observe it) until dispatch_lifecycle_events() migrates it
    // to "interactive" then "complete" on success. (The constructor installs the
    // default generation via install_page directly, so a fresh Page() stays
    // "complete" and never enters "loading".) A plain member write — no V8 access.
    if (document_host_ != nullptr) {
        static_cast<DocumentHost*>(document_host_)->set_ready_state("loading");
    }
}

PageState::~PageState() {
    // Release V8 resources immediately on page GC (api_contract §8), even if
    // JSValue wrappers still observe the state. The context is reset here; the
    // host objects are freed afterwards (member destruction order), so no JS
    // object with a dangling internal-field pointer can survive.
    state_->teardown();
}

bool PageState::disposed() const { return state_->disposed(); }

py::object PageState::eval(const std::string& source, bool to_py,
                           const std::string& name) {
    return state_->eval(source, to_py, name);
}

void PageState::dispose() { state_->dispose(); }

void PageState::run_timers() { state_->run_timers(); }

// M11-29: deterministic event-loop facade; delegates to the owned context so
// the M1 operation guard / disposed / busy rules apply unchanged.
bool PageState::tick() { return state_->tick(); }

bool PageState::advance(std::int64_t ms, std::uint32_t max_steps) {
    return state_->advance(ms, max_steps);
}

bool PageState::run_until_idle(std::uint32_t max_steps) {
    return state_->run_until_idle(max_steps);
}

std::int64_t PageState::event_loop_time_ms() const {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    return state_->event_loop_time_ms();
}

// M11-2: pump exactly one animation frame (delegates to the owned context, like
// run_timers / run_jobs — so the disposed/busy rules are the M1 ones).
void PageState::run_animation_frame() { state_->run_animation_frame(); }

// M11-3: pump one idle round (delegates to the owned context, like
// run_animation_frame — so the disposed/busy rules are the M1 ones).
void PageState::run_idle_callbacks() { state_->run_idle_callbacks(); }

void PageState::run_jobs() { state_->run_jobs(); }

}  // namespace iv8
