#include "navigator_parts.h"

#include <cstring>
#include <unordered_set>

#include "iv8/query_parameters.h"
#include "iv8/url_parser.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {

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
    const std::uint64_t seq = access_trace_record(
        isolate, &kStorageManagerTraceToken, "storage_manager",
        "navigator.storage", "estimate", AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
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

}  // namespace iv8::browser
