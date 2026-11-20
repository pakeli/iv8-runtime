#include "navigator_parts.h"

#include <cstring>
#include <unordered_set>

#include "iv8/access_origin.h"
#include "iv8/query_parameters.h"
#include "iv8/url_parser.h"
#include "visual_viewport.h"
#include "legacy_quota.h"
#include "../internal/trace_hooks.h"
#include "../window/window_trace_support.h"

namespace iv8::browser {

namespace {

v8::Local<v8::Value> rejected_bom_promise(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> error) {
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver) ||
        !resolver->Reject(context, error).FromMaybe(false)) return v8::Undefined(isolate);
    return resolver->GetPromise();
}

v8::Local<v8::Value> orientation_lock(v8::Isolate* isolate,
    v8::Local<v8::Context> context,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    const std::string prefix = "Failed to execute 'lock' on 'ScreenOrientation': ";
    if (arguments.Length() == 0) {
        return rejected_bom_promise(isolate, context,
            v8::Exception::TypeError(v8_string(isolate,
                prefix + "1 argument required, but only 0 present.").As<v8::String>()));
    }
    v8::Local<v8::String> kind;
    {
        v8::TryCatch conversion(isolate);
        if (!access_user_operation(isolate, [&] {
                return arguments[0]->ToString(context);
            }).ToLocal(&kind)) {
            if (!conversion.HasCaught() || !conversion.CanContinue()) {
                if (conversion.HasCaught()) conversion.ReThrow();
                return v8::Undefined(isolate);
            }
            const auto error = conversion.Exception();
            conversion.Reset();
            return rejected_bom_promise(isolate, context, error);
        }
    }
    v8::String::Utf8Value text(isolate, kind);
    if (*text == nullptr) return v8::Undefined(isolate);
    const std::string value(*text, static_cast<std::size_t>(text.length()));
    static const std::unordered_set<std::string> kinds = {
        "any", "natural", "portrait", "landscape", "portrait-primary",
        "portrait-secondary", "landscape-primary", "landscape-secondary"};
    if (!kinds.contains(value)) {
        return rejected_bom_promise(isolate, context,
            v8::Exception::TypeError(v8_string(isolate, prefix + "The provided value '" +
                value + "' is not a valid enum value of type OrientationLockType.")
                .As<v8::String>()));
    }
    auto error = v8::Exception::Error(v8_string(isolate,
        "screen.orientation.lock() is not available on this device.").As<v8::String>());
    if (!error.As<v8::Object>()->CreateDataProperty(context,
            v8_string(isolate, "name").As<v8::String>(), v8_string(isolate, "NotSupportedError"))
            .FromMaybe(false)) return v8::Undefined(isolate);
    return rejected_bom_promise(isolate, context, error);
}

}  // namespace

bool validate_bom_event_dispatch(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    if (name != "dispatchEvent") return true;
    if (arguments.Length() != 0 && is_native_event(isolate, context, arguments[0])) return true;
    const char* detail = arguments.Length() == 0
        ? "1 argument required, but only 0 present."
        : "parameter 1 is not of type 'Event'.";
    isolate->ThrowException(v8::Exception::TypeError(v8_string(isolate,
        std::string("Failed to execute 'dispatchEvent' on 'EventTarget': ") + detail)
        .As<v8::String>()));
    return false;
}

v8::Local<v8::Value> ConnectionHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (!validate_bom_event_dispatch(isolate, context, name, arguments))
        return v8::Undefined(isolate);
    v8::Local<v8::Value> result;
    if (handle_event_method(isolate, context, name, arguments, result)) return result;
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> ScreenOrientationHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (name == "lock") return orientation_lock(isolate, context, arguments);
    if (!validate_bom_event_dispatch(isolate, context, name, arguments))
        return v8::Undefined(isolate);
    v8::Local<v8::Value> result;
    if (handle_event_method(isolate, context, name, arguments, result)) return result;
    // unlock has no device backend and never mutates configured metadata.
    return v8::Undefined(isolate);
}

bool install_bom_event_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const HostEnvironment* environment) {
    ConnectionHost connection(environment);
    ScreenOrientationHost orientation(environment);
    VisualViewportHost viewport(environment);
    v8::TryCatch installation(isolate);
    for (HostObject* host : {static_cast<HostObject*>(&connection),
            static_cast<HostObject*>(&orientation), static_cast<HostObject*>(&viewport)}) {
        install_host_interface(isolate, context, host);
        if (installation.HasCaught()) {
            installation.ReThrow();
            return false;
        }
        auto prototype = web_interface_prototype(isolate, context, host->prototype_key());
        if (prototype.IsEmpty()) {
            if (installation.HasCaught()) installation.ReThrow();
            return false;
        }
        // Chrome orders constructor after the interface's ordinary members.
        // Its original function and flags are preserved while moving the key.
        const auto key = v8_string(isolate, "constructor").As<v8::String>();
        v8::Local<v8::Value> constructor;
        if (!prototype->Get(context, key).ToLocal(&constructor) ||
            !prototype->Delete(context, key).FromMaybe(false) ||
            !install_traced_api_data_property(isolate, context, prototype,
                "constructor", constructor, native_api_identity(isolate, prototype),
                "host_object", host->prototype_key() + ".prototype", v8::DontEnum)) {
            if (installation.HasCaught()) installation.ReThrow();
            return false;
        }
    }
    return !isolate->IsExecutionTerminating();
}

v8::Local<v8::Value> resolved_promise(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context,
                                      v8::Local<v8::Value> value) {
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        return v8::Undefined(isolate);
    }
    (void)access_user_operation(isolate, [&] {
        return resolver->Resolve(context, value);
    });
    return resolver->GetPromise();
}

void storage_manager_estimate_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    watch_record(isolate, "navigator.storage", "estimate");
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::uint64_t seq = access_trace_record(
        isolate, &kStorageManagerTraceToken, "storage_manager",
        "navigator.storage", "estimate", AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
    v8::Local<v8::Object> estimate = v8::Object::New(isolate);
    (void)access_user_operation(isolate, [&] {
        return estimate->Set(context, v8_string(isolate, "usage"),
            v8::Integer::New(isolate, 0));
    });
    (void)access_user_operation(isolate, [&] {
        return estimate->Set(context, v8_string(isolate, "quota"),
            v8::Number::New(isolate, kOfflineStorageQuota));
    });
    info.GetReturnValue().Set(resolved_promise(isolate, context, estimate));
}

void storage_manager_persisted_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    watch_record(isolate, "navigator.storage", "persisted");
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::uint64_t seq = access_trace_record(
        isolate, &kStorageManagerTraceToken, "storage_manager",
        "navigator.storage", "persisted", AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
    info.GetReturnValue().Set(
        resolved_promise(isolate, context, v8::Boolean::New(isolate, false)));
}

void storage_manager_persist_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    watch_record(isolate, "navigator.storage", "persist");
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::uint64_t seq = access_trace_record(
        isolate, &kStorageManagerTraceToken, "storage_manager",
        "navigator.storage", "persist", AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
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
        (void)install_traced_api_data_property(isolate, context, manager, name, fn,
            &kStorageManagerTraceToken, "storage_manager", "navigator.storage",
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
// kPermissionsTraceToken is defined in browser/internal/common.h.
// V8 15.0 requires a tag on External::New / External::Value.
constexpr v8::ExternalPointerTypeTag kPermissionStatusTag =
    v8::kExternalPointerTypeTagDefault;

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
    const std::uint64_t seq = access_trace_record(
        isolate, entry, "permission_status", "permissionStatus", "name",
        AccessOperation::kGet);
    AccessPropertyCompletion completion(isolate, seq, info);
    info.GetReturnValue().Set(v8_string(isolate, entry->key));
}

void permission_status_state_getter(
    v8::Local<v8::Name>,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    const auto* entry = static_cast<const PermissionNameInfo*>(
        info.Data().As<v8::External>()->Value(kPermissionStatusTag));
    const std::uint64_t seq = access_trace_record(
        isolate, entry, "permission_status", "permissionStatus", "state",
        AccessOperation::kGet);
    AccessPropertyCompletion completion(isolate, seq, info);
    info.GetReturnValue().Set(v8_string(isolate, "prompt"));
}

void permission_status_add_listener_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    watch_record(isolate, "permissionStatus", "addEventListener");
    const auto* entry = static_cast<const PermissionNameInfo*>(
        info.Data().As<v8::External>()->Value(kPermissionStatusTag));
    const std::uint64_t seq = access_trace_record(
        isolate, entry, "permission_status", "permissionStatus", "addEventListener",
        AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
    info.GetReturnValue().Set(v8::Undefined(isolate));
}

void permission_status_remove_listener_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    watch_record(isolate, "permissionStatus", "removeEventListener");
    const auto* entry = static_cast<const PermissionNameInfo*>(
        info.Data().As<v8::External>()->Value(kPermissionStatusTag));
    const std::uint64_t seq = access_trace_record(
        isolate, entry, "permission_status", "permissionStatus", "removeEventListener",
        AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
    info.GetReturnValue().Set(v8::Undefined(isolate));
}

void permission_status_dispatch_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    watch_record(isolate, "permissionStatus", "dispatchEvent");
    const auto* entry = static_cast<const PermissionNameInfo*>(
        info.Data().As<v8::External>()->Value(kPermissionStatusTag));
    const std::uint64_t seq = access_trace_record(
        isolate, entry, "permission_status", "permissionStatus", "dispatchEvent",
        AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
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
    (void)install_traced_api_data_property(isolate, context, status, "onchange",
        v8::Null(isolate), entry, "permission_status", "permissionStatus");
    const auto define_method = [&](const char* name,
                                   v8::FunctionCallback callback) {
        v8::Local<v8::Function> fn =
            v8::FunctionTemplate::New(isolate, callback, data)
                ->GetFunction(context)
                .ToLocalChecked();
        (void)install_traced_api_data_property(isolate, context, status, name, fn,
            entry, "permission_status", "permissionStatus",
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
    watch_record(isolate, "navigator.permissions", "query");
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::uint64_t seq = access_trace_record(
        isolate, &kPermissionsTraceToken, "permissions",
        "navigator.permissions", "query", AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
    if (info.Length() < 1 || !info[0]->IsObject() || info[0]->IsFunction()) {
        info.GetReturnValue().Set(rejected_type_error_promise(
            isolate, context,
            "permissions.query: descriptor must be an object"));
        return;
    }
    v8::Local<v8::Object> descriptor = info[0].As<v8::Object>();
    v8::Local<v8::Value> name_value;
    if (!access_user_operation(isolate, [&] {
            return descriptor->Get(context, v8_string(isolate, "name").As<v8::String>());
        }).ToLocal(&name_value)) {
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
    (void)install_traced_api_data_property(isolate, context, permissions, "query", query,
        &kPermissionsTraceToken, "permissions", "navigator.permissions",
        static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete));
    return permissions;
}

}  // namespace iv8::browser
