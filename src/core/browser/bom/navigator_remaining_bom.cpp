#include "navigator_remaining_bom.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "iv8/access_origin.h"
#include "iv8/dom_url.h"
#include "iv8/page_state.h"
#include "iv8/web_interface.h"
#include "iv8/web_method.h"
#include "../internal/common.h"
#include "../internal/trace_hooks.h"
#include "../internal/web_idl_primitive.h"
#include "../web/dom_exception.h"
#include "../window/window_shell_objects.h"
#include "../window/window_trace_support.h"
#include "navigator_parts.h"

namespace iv8::browser {
namespace {

bool throw_type_error(v8::Isolate* isolate, const std::string& prefix,
    const std::string& detail) {
    const auto message = prefix + detail;
    v8::Local<v8::String> text;
    if (v8::String::NewFromUtf8(isolate, message.data(), v8::NewStringType::kNormal,
            static_cast<int>(message.size())).ToLocal(&text)) {
        isolate->ThrowException(v8::Exception::TypeError(text));
    }
    return false;
}

bool throw_dom_exception(v8::Isolate* isolate, v8::Local<v8::Context> context,
    PageState* owner, std::string_view name, const std::string& message) {
    auto exception = make_dom_exception(isolate, context, owner, name, message);
    if (!exception.IsEmpty()) {
        isolate->ThrowException(exception);
    }
    return false;
}

v8::Local<v8::Value> rejected_promise(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> reason) {
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver) ||
        !resolver->Reject(context, reason).FromMaybe(false)) {
        return v8::Undefined(isolate);
    }
    return resolver->GetPromise();
}

v8::Local<v8::Value> reject_from_try_catch(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::TryCatch& caught) {
    if (!caught.CanContinue()) {
        caught.ReThrow();
        return v8::Undefined(isolate);
    }
    auto exception = caught.Exception();
    caught.Reset();
    return rejected_promise(isolate, context, exception);
}

bool is_callable_object(v8::Local<v8::Value> value) {
    return value->IsObject() && value.As<v8::Object>()->IsCallable();
}

v8::MaybeLocal<v8::Value> user_get(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> object,
    v8::Local<v8::Value> key) {
    return access_user_operation(isolate, [&] {
        return object->Get(context, key);
    });
}

v8::MaybeLocal<v8::Value> user_call(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> callable,
    v8::Local<v8::Value> receiver, int count = 0,
    v8::Local<v8::Value>* arguments = nullptr) {
    return access_user_operation(isolate, [&] {
        return callable.As<v8::Object>()->CallAsFunction(
            context, receiver, count, arguments);
    });
}

template <typename ItemConverter>
bool convert_sequence(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, const std::string& prefix,
    ItemConverter&& convert_item) {
    if (!input->IsObject()) {
        return throw_type_error(isolate, prefix,
            "The provided value cannot be converted to a sequence.");
    }
    auto object = input.As<v8::Object>();
    v8::Local<v8::Value> method;
    if (!user_get(isolate, context, object, v8::Symbol::GetIterator(isolate))
            .ToLocal(&method)) {
        return false;
    }
    if (method->IsNullOrUndefined()) {
        return throw_type_error(isolate, prefix,
            "The object must have a callable @@iterator property.");
    }
    if (!is_callable_object(method)) {
        return throw_type_error(isolate, prefix, "@@iterator must be a callable.");
    }
    v8::Local<v8::Value> iterator_value;
    if (!user_call(isolate, context, method, input).ToLocal(&iterator_value)) {
        return false;
    }
    if (!iterator_value->IsObject()) {
        return throw_type_error(isolate, prefix,
            "Iterator object must be an object.");
    }
    auto iterator = iterator_value.As<v8::Object>();
    v8::Local<v8::Value> next;
    if (!user_get(isolate, context, iterator, v8_string(isolate, "next"))
            .ToLocal(&next)) {
        return false;
    }
    if (!is_callable_object(next)) {
        return throw_type_error(isolate, prefix,
            "Expected next() function on iterator.");
    }
    for (;;) {
        v8::Local<v8::Value> step;
        if (!user_call(isolate, context, next, iterator).ToLocal(&step)) {
            return false;
        }
        if (!step->IsObject()) {
            return throw_type_error(isolate, prefix,
                "Expected iterator.next() to return an Object.");
        }
        auto record = step.As<v8::Object>();
        v8::Local<v8::Value> item;
        v8::Local<v8::Value> done;
        // Blink's UnpackIteratorResult reads `value` before `done`.
        if (!user_get(isolate, context, record, v8_string(isolate, "value"))
                .ToLocal(&item) ||
            !user_get(isolate, context, record, v8_string(isolate, "done"))
                .ToLocal(&done)) {
            return false;
        }
        if (done->BooleanValue(isolate)) {
            return true;
        }
        if (!convert_item(item)) {
            return false;
        }
    }
}

void deliver_task_value(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto runtime_origin = access_runtime_scope(isolate);
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> resolver_val;
    v8::Local<v8::Value> is_reject_val;
    v8::Local<v8::Value> payload;
    if (!data->Get(context, 0).ToLocal(&resolver_val) ||
        !data->Get(context, 1).ToLocal(&is_reject_val) ||
        !data->Get(context, 2).ToLocal(&payload)) {
        return;
    }
    auto resolver = resolver_val.As<v8::Promise::Resolver>();
    (void)access_user_operation(isolate, [&] {
        return is_reject_val->BooleanValue(isolate)
            ? resolver->Reject(context, payload)
            : resolver->Resolve(context, payload);
    });
}

v8::Local<v8::Value> schedule_task_value(v8::Isolate* isolate,
    v8::Local<v8::Context> context, bool reject, v8::Local<v8::Value> payload) {
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        return v8::Undefined(isolate);
    }
    auto* state = state_from_isolate(isolate);
    if (state == nullptr) return v8::Undefined(isolate);
    auto data = v8::Array::New(isolate, 3);
    if (!data->CreateDataProperty(context, 0, resolver).FromMaybe(false) ||
        !data->CreateDataProperty(context, 1, v8::Boolean::New(isolate, reject)).FromMaybe(false) ||
        !data->CreateDataProperty(context, 2, payload).FromMaybe(false)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Function> delivery;
    if (!v8::Function::New(context, &deliver_task_value, data, 0,
            v8::ConstructorBehavior::kThrow).ToLocal(&delivery)) {
        return v8::Undefined(isolate);
    }
    (void)state->register_timer(delivery, 0, false, false);
    return resolver->GetPromise();
}

double js_math_min(double a, double b) noexcept {
    if (std::isnan(a) || std::isnan(b)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (a == 0.0 && b == 0.0) {
        return (std::signbit(a) || std::signbit(b)) ? -0.0 : 0.0;
    }
    return a < b ? a : b;
}

double js_math_max(double a, double b) noexcept {
    if (std::isnan(a) || std::isnan(b)) {
        return std::numeric_limits<double>::quiet_NaN();
    }
    if (a == 0.0 && b == 0.0) {
        return (std::signbit(a) && std::signbit(b)) ? -0.0 : 0.0;
    }
    return a > b ? a : b;
}

bool convert_unrestricted_double(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> input, double* out) {
    double number = 0.0;
    if (!access_user_operation(isolate, [&] {
            return input->NumberValue(context);
        }).To(&number)) {
        return false;
    }
    *out = number;
    return true;
}

bool convert_enforce_range_ull(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Value> input,
    const std::string& prefix, double* out) {
    double number = 0.0;
    if (!access_user_operation(isolate, [&] {
            return input->NumberValue(context);
        }).To(&number)) {
        return false;
    }
    if (!std::isfinite(number)) {
        return throw_type_error(isolate, prefix, "Value is not a finite number.");
    }
    number = std::trunc(number);
    constexpr double kMaxSafeUll = 18446744073709551615.0;
    if (number < 0.0 || number > kMaxSafeUll) {
        return throw_type_error(isolate, prefix,
            "Value is outside the 'unsigned long long' value range.");
    }
    *out = number == 0.0 ? 0.0 : number;
    return true;
}

bool convert_dom_rect_init(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> input, const std::string& prefix,
    double* x, double* y, double* width, double* height) {
    *x = 0.0;
    *y = 0.0;
    *width = 0.0;
    *height = 0.0;
    if (input.IsEmpty() || input->IsNullOrUndefined()) {
        return true;
    }
    if (!input->IsObject()) {
        return throw_type_error(isolate, prefix,
            "The provided value is not of type 'DOMRectInit'.");
    }
    auto object = input.As<v8::Object>();
    // WebIDL dictionary members are read in lexicographical order: height, width, x, y.
    const std::pair<const char*, double*> fields[] = {
        {"height", height}, {"width", width}, {"x", x}, {"y", y}};
    for (const auto& [name, target] : fields) {
        v8::Local<v8::Value> value;
        if (!user_get(isolate, context, object, v8_string(isolate, name))
                .ToLocal(&value)) {
            return false;
        }
        if (!value->IsUndefined() &&
            !convert_unrestricted_double(isolate, context, value, target)) {
            return false;
        }
    }
    return true;
}

bool convert_dom_rect_ctor_args(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Array> arguments,
    double* x, double* y, double* width, double* height) {
    double* slots[] = {x, y, width, height};
    for (std::uint32_t index = 0; index < 4; ++index) {
        *slots[index] = 0.0;
        if (index >= arguments->Length()) continue;
        v8::Local<v8::Value> arg;
        if (!arguments->Get(context, index).ToLocal(&arg)) return false;
        if (!arg->IsUndefined() &&
            !convert_unrestricted_double(isolate, context, arg, slots[index])) {
            return false;
        }
    }
    return true;
}

void dom_rect_from_rect_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> owner_ext;
    v8::Local<v8::Value> writable_val;
    if (!data->Get(context, 0).ToLocal(&owner_ext) || !owner_ext->IsExternal() ||
        !data->Get(context, 1).ToLocal(&writable_val)) {
        return;
    }
    auto* owner = static_cast<PageState*>(
        owner_ext.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    const bool writable = writable_val->BooleanValue(isolate);
    const char* iface = writable ? "DOMRect" : "DOMRectReadOnly";
    watch_record(isolate, iface, "fromRect");
    const auto seq = access_trace_record(isolate,
        native_api_identity(isolate, info.This()->IsObject()
            ? info.This().As<v8::Object>() : context->Global()),
        "host_object", iface, "fromRect", AccessOperation::kCall);
    AccessCallCompletion completion(isolate, seq, info);
    const std::string prefix =
        std::string("Failed to execute 'fromRect' on '") + iface + "': ";
    double x = 0.0, y = 0.0, width = 0.0, height = 0.0;
    const v8::Local<v8::Value> arg0 = info.Length() >= 1
        ? info[0] : v8::Local<v8::Value>(v8::Undefined(isolate));
    if (!convert_dom_rect_init(isolate, context, arg0, prefix, &x, &y, &width, &height)) {
        return;
    }
    if (writable) {
        info.GetReturnValue().Set(owner->adopt_host_object(isolate, context,
            std::make_unique<DOMRectHost>(x, y, width, height)));
    } else {
        info.GetReturnValue().Set(owner->adopt_host_object(isolate, context,
            std::make_unique<DOMRectReadOnlyHost>(x, y, width, height)));
    }
}

constexpr std::array<const char*, 36> kSupportedConstraints = {{
    "aspectRatio", "autoGainControl", "brightness", "channelCount",
    "colorTemperature", "contrast", "deviceId", "displaySurface",
    "echoCancellation", "exposureCompensation", "exposureMode", "exposureTime",
    "facingMode", "focusDistance", "focusMode", "frameRate", "groupId",
    "height", "iso", "latency", "noiseSuppression", "pan", "pointsOfInterest",
    "resizeMode", "restrictOwnAudio", "sampleRate", "sampleSize", "saturation",
    "sharpness", "suppressLocalAudioPlayback", "tilt", "torch",
    "voiceIsolation", "whiteBalanceMode", "width", "zoom",
}};

bool is_numeric_track_constraint(std::string_view key) {
    return key == "aspectRatio" || key == "brightness" || key == "channelCount" ||
        key == "colorTemperature" || key == "contrast" ||
        key == "exposureCompensation" || key == "exposureTime" ||
        key == "focusDistance" || key == "frameRate" || key == "height" ||
        key == "iso" || key == "latency" || key == "pan" ||
        key == "sampleRate" || key == "sampleSize" || key == "saturation" ||
        key == "sharpness" || key == "tilt" || key == "width" || key == "zoom";
}

bool convert_media_track_constraint_set(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> track,
    const std::string& prefix, bool include_top_level_fields) {
    static constexpr const char* kTrackKeys[] = {
        "aspectRatio", "autoGainControl", "brightness", "channelCount",
        "colorTemperature", "contrast", "deviceId", "displaySurface",
        "echoCancellation", "exposureCompensation", "exposureMode",
        "exposureTime", "facingMode", "focusDistance", "focusMode",
        "frameRate", "groupId", "height", "iso", "latency", "mandatory",
        "noiseSuppression", "optional", "pan", "pointsOfInterest",
        "resizeMode", "restrictOwnAudio", "sampleRate", "sampleSize",
        "saturation", "sharpness", "suppressLocalAudioPlayback", "tilt",
        "torch", "voiceIsolation", "whiteBalanceMode", "width", "zoom",
        "advanced",
    };
    for (const char* key : kTrackKeys) {
        const std::string_view name(key);
        if (!include_top_level_fields &&
            (name == "mandatory" || name == "optional" || name == "advanced")) {
            continue;
        }
        v8::Local<v8::Value> field;
        if (!user_get(isolate, context, track, v8_string(isolate, key))
                .ToLocal(&field) ||
            field->IsUndefined()) {
            if (field.IsEmpty()) return false;
            continue;
        }
        if (name == "advanced") {
            const std::string adv_prefix = prefix +
                "Failed to read the 'advanced' property from 'MediaTrackConstraints': ";
            if (!convert_sequence(isolate, context, field, adv_prefix,
                    [&](v8::Local<v8::Value> item) {
                        if (item->IsNullOrUndefined()) return true;
                        if (!item->IsObject()) {
                            return throw_type_error(isolate, adv_prefix,
                                "The provided value is not of type 'MediaTrackConstraintSet'.");
                        }
                        return convert_media_track_constraint_set(
                            isolate, context, item.As<v8::Object>(), adv_prefix, false);
                    })) {
                return false;
            }
            continue;
        }
        if (name == "mandatory") {
            if (!field->IsNull() && !field->IsObject()) {
                return throw_type_error(isolate, prefix,
                    "Failed to read the 'mandatory' property from 'MediaTrackConstraints': The provided value is not of type 'Object'.");
            }
            continue;
        }
        if (name == "optional") {
            const std::string opt_prefix = prefix +
                "Failed to read the 'optional' property from 'MediaTrackConstraints': ";
            if (!convert_sequence(isolate, context, field, opt_prefix,
                    [&](v8::Local<v8::Value> item) {
                        if (!item->IsNullOrUndefined() && !item->IsObject()) {
                            return throw_type_error(isolate, opt_prefix,
                                "The provided value is not of type 'Object'.");
                        }
                        return true;
                    })) {
                return false;
            }
            continue;
        }
        const std::string field_prefix = prefix +
            "Failed to read the '" + std::string(name) +
            "' property from 'MediaTrackConstraintSet': ";
        if (name == "pointsOfInterest") {
            if (field->IsObject()) {
                auto obj = field.As<v8::Object>();
                v8::Local<v8::Value> iter;
                if (!user_get(isolate, context, obj, v8::Symbol::GetIterator(isolate))
                        .ToLocal(&iter)) {
                    return false;
                }
                if (iter->IsNullOrUndefined()) {
                    for (const char* sub : {"exact", "ideal"}) {
                        v8::Local<v8::Value> sub_val;
                        if (!user_get(isolate, context, obj, v8_string(isolate, sub))
                                .ToLocal(&sub_val)) {
                            return false;
                        }
                        if (!sub_val->IsUndefined() &&
                            !convert_sequence(isolate, context, sub_val, field_prefix,
                                [&](v8::Local<v8::Value>) { return true; })) {
                            return false;
                        }
                    }
                    continue;
                }
            }
            if (!convert_sequence(isolate, context, field, field_prefix,
                    [&](v8::Local<v8::Value>) { return true; })) {
                return false;
            }
            continue;
        }
        if (is_numeric_track_constraint(name)) {
            if (field->IsObject()) {
                auto obj = field.As<v8::Object>();
                for (const char* sub : {"exact", "ideal", "max", "min"}) {
                    v8::Local<v8::Value> sub_val;
                    if (!user_get(isolate, context, obj, v8_string(isolate, sub))
                            .ToLocal(&sub_val)) {
                        return false;
                    }
                    double unused = 0.0;
                    if (!sub_val->IsUndefined() &&
                        !convert_unrestricted_double(isolate, context, sub_val, &unused)) {
                        return false;
                    }
                }
            } else {
                double unused = 0.0;
                if (!convert_unrestricted_double(isolate, context, field, &unused)) {
                    return false;
                }
            }
            continue;
        }
        // Boolean / DOMString constraints (`ConstrainBoolean` / `ConstrainDOMString` /
        // `ConstrainBooleanOrDOMString`): Chrome converts non-boolean, non-object
        // primitives via ToString (so Symbols throw TypeError).
        if (field->IsObject()) {
            auto obj = field.As<v8::Object>();
            for (const char* sub : {"exact", "ideal"}) {
                v8::Local<v8::Value> sub_val;
                if (!user_get(isolate, context, obj, v8_string(isolate, sub))
                        .ToLocal(&sub_val)) {
                    return false;
                }
                if (!sub_val->IsUndefined() && !sub_val->IsBoolean() && !sub_val->IsObject()) {
                    std::string unused;
                    if (!web_idl_dom_string(isolate, context, sub_val, field_prefix, &unused)) {
                        return false;
                    }
                }
            }
        } else if (!field->IsBoolean()) {
            std::string unused;
            if (!web_idl_dom_string(isolate, context, field, field_prefix, &unused)) {
                return false;
            }
        }
    }
    return true;
}

bool convert_track_constraints_object(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> track,
    const std::string& prefix) {
    return convert_media_track_constraint_set(isolate, context, track, prefix, true);
}

bool has_forbidden_sw_escape(std::string_view path) {
    for (std::size_t index = 0; index + 2 < path.size(); ++index) {
        if (path[index] == '%' && path[index + 1] == '2' &&
            (path[index + 2] == 'f' || path[index + 2] == 'F')) {
            return true;
        }
        if (path[index] == '%' && path[index + 1] == '5' &&
            (path[index + 2] == 'c' || path[index + 2] == 'C')) {
            return true;
        }
    }
    return false;
}

bool is_valid_bucket_name(std::string_view name) {
    if (name.empty() || name.size() >= 64) return false;
    const auto is_lower_alnum = [](char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9');
    };
    if (!is_lower_alnum(name.front())) return false;
    for (char ch : name) {
        if (!is_lower_alnum(ch) && ch != '_' && ch != '-') {
            return false;
        }
    }
    return true;
}

constexpr double kOfflineBucketQuota = 10.0 * 1024.0 * 1024.0 * 1024.0;

double current_epoch_ms() noexcept {
    return std::chrono::duration<double, std::milli>(
        std::chrono::system_clock::now().time_since_epoch()).count();
}

class ServiceWorkerDeclHost : public EventTargetHost {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "ServiceWorker"; }
    std::string trace_object_type() const override { return "service_worker"; }
    std::vector<std::string> property_names() const override {
        return {"scriptURL", "state", "onstatechange", "onerror"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"onstatechange", "onerror"};
    }
    std::vector<std::string> method_names() const override {
        auto names = event_method_names();
        names.push_back("postMessage");
        return names;
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&) override {
        return v8::Undefined(isolate);
    }
    void set_property(v8::Isolate*, v8::Local<v8::Context>,
        const std::string&, v8::Local<v8::Value>) override {}
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        return v8::Undefined(isolate);
    }
};

class ServiceWorkerRegistrationDeclHost : public EventTargetHost {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "ServiceWorkerRegistration"; }
    std::string trace_object_type() const override {
        return "service_worker_registration";
    }
    std::vector<std::string> property_names() const override {
        return {
            "installing", "waiting", "active", "navigationPreload", "scope",
            "updateViaCache", "onupdatefound", "sync", "backgroundFetch",
            "periodicSync", "paymentManager", "pushManager", "cookies"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"onupdatefound"};
    }
    std::vector<std::string> method_names() const override {
        auto names = event_method_names();
        names.insert(names.end(), {
            "unregister", "update", "getNotifications", "showNotification"});
        return names;
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&) override {
        return v8::Undefined(isolate);
    }
    void set_property(v8::Isolate*, v8::Local<v8::Context>,
        const std::string&, v8::Local<v8::Value>) override {}
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override {
        return v8::Undefined(isolate);
    }
};

bool reorder_prototype_keys(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> prototype,
    const std::string& interface_name,
    std::initializer_list<const char*> ordered_keys) {
    struct SavedProperty {
        std::string name;
        bool is_data = false;
        v8::Local<v8::Value> value;
        v8::Local<v8::Value> getter;
        v8::Local<v8::Value> setter;
    };
    std::vector<SavedProperty> saved;
    saved.reserve(ordered_keys.size());
    for (const char* raw_name : ordered_keys) {
        auto key = v8_string(isolate, raw_name);
        v8::Local<v8::Value> desc_val;
        if (!prototype->GetOwnPropertyDescriptor(context, key.As<v8::Name>())
                .ToLocal(&desc_val) ||
            !desc_val->IsObject()) {
            return false;
        }
        auto desc = desc_val.As<v8::Object>();
        SavedProperty entry{raw_name};
        if (desc->HasOwnProperty(context, v8_string(isolate, "value").As<v8::String>())
                .FromMaybe(false)) {
            entry.is_data = true;
            if (!desc->Get(context, v8_string(isolate, "value")).ToLocal(&entry.value)) {
                return false;
            }
        } else {
            if (!desc->Get(context, v8_string(isolate, "get")).ToLocal(&entry.getter) ||
                !desc->Get(context, v8_string(isolate, "set")).ToLocal(&entry.setter)) {
                return false;
            }
        }
        if (!prototype->Delete(context, key).FromMaybe(false)) {
            return false;
        }
        saved.push_back(std::move(entry));
    }
    for (const auto& entry : saved) {
        if (entry.is_data) {
            const auto attrs = entry.name == "constructor"
                ? v8::DontEnum : v8::None;
            if (!install_traced_api_data_property(isolate, context, prototype,
                    entry.name, entry.value, native_api_identity(isolate, prototype),
                    "host_object", interface_name + ".prototype", attrs)) {
                return false;
            }
        } else {
            v8::PropertyDescriptor descriptor(entry.getter, entry.setter);
            descriptor.set_enumerable(true);
            descriptor.set_configurable(true);
            if (!prototype->DefineProperty(context,
                    v8_string(isolate, entry.name).As<v8::Name>(), descriptor)
                    .FromMaybe(false)) {
                return false;
            }
        }
    }
    return true;
}

bool move_constructor_to_end(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> prototype,
    const std::string& interface_name) {
    return reorder_prototype_keys(isolate, context, prototype, interface_name,
        {"constructor"});
}

}  // namespace

int navigator_cpu_performance_tier(int hardware_concurrency) noexcept {
    if (hardware_concurrency <= 0) return 0;
    if (hardware_concurrency <= 2) return 1;
    if (hardware_concurrency <= 4) return 2;
    if (hardware_concurrency < 8) return 3;
    return 4;
}

// --- DOMRectReadOnly & DOMRect ---

v8::Local<v8::Value> DOMRectReadOnlyHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name) {
    if (name == "x") return v8::Number::New(isolate, x_);
    if (name == "y") return v8::Number::New(isolate, y_);
    if (name == "width") return v8::Number::New(isolate, width_);
    if (name == "height") return v8::Number::New(isolate, height_);
    if (name == "top") return v8::Number::New(isolate, js_math_min(y_, y_ + height_));
    if (name == "right") return v8::Number::New(isolate, js_math_max(x_, x_ + width_));
    if (name == "bottom") return v8::Number::New(isolate, js_math_max(y_, y_ + height_));
    if (name == "left") return v8::Number::New(isolate, js_math_min(x_, x_ + width_));
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> DOMRectReadOnlyHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>&) {
    watch_record(isolate, trace_label(), name);
    if (name != "toJSON") return v8::Undefined(isolate);
    auto json = v8::Object::New(isolate);
    const std::pair<const char*, double> entries[] = {
        {"x", x_},
        {"y", y_},
        {"width", width_},
        {"height", height_},
        {"top", js_math_min(y_, y_ + height_)},
        {"right", js_math_max(x_, x_ + width_)},
        {"bottom", js_math_max(y_, y_ + height_)},
        {"left", js_math_min(x_, x_ + width_)},
    };
    for (const auto& [key, val] : entries) {
        (void)json->CreateDataProperty(context,
            v8_string(isolate, key).As<v8::Name>(), v8::Number::New(isolate, val));
    }
    return json;
}

void DOMRectHost::set_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    v8::Local<v8::Value> value) {
    double converted = 0.0;
    if (!convert_unrestricted_double(isolate, context, value, &converted)) {
        return;
    }
    if (name == "x") x_ = converted;
    else if (name == "y") y_ = converted;
    else if (name == "width") width_ = converted;
    else if (name == "height") height_ = converted;
}

v8::Local<v8::Value> construct_dom_rect_read_only(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    v8::Local<v8::Array> arguments) {
    double x = 0.0, y = 0.0, width = 0.0, height = 0.0;
    if (!convert_dom_rect_ctor_args(isolate, context, arguments, &x, &y, &width, &height)) {
        return v8::Undefined(isolate);
    }
    return owner->adopt_host_object(isolate, context,
        std::make_unique<DOMRectReadOnlyHost>(x, y, width, height));
}

v8::Local<v8::Value> construct_dom_rect(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    v8::Local<v8::Array> arguments) {
    double x = 0.0, y = 0.0, width = 0.0, height = 0.0;
    if (!convert_dom_rect_ctor_args(isolate, context, arguments, &x, &y, &width, &height)) {
        return v8::Undefined(isolate);
    }
    return owner->adopt_host_object(isolate, context,
        std::make_unique<DOMRectHost>(x, y, width, height));
}

// --- MediaDeviceInfo, InputDeviceInfo, MediaDevices ---

v8::Local<v8::Value> MediaDeviceInfoHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name) {
    if (name == "deviceId") return v8_string(isolate, device_id_);
    if (name == "kind") return v8_string(isolate, kind_);
    if (name == "label") return v8_string(isolate, label_);
    if (name == "groupId") return v8_string(isolate, group_id_);
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> MediaDeviceInfoHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>&) {
    watch_record(isolate, trace_label(), name);
    if (name != "toJSON") return v8::Undefined(isolate);
    auto json = v8::Object::New(isolate);
    (void)json->CreateDataProperty(context,
        v8_string(isolate, "deviceId").As<v8::Name>(), v8_string(isolate, device_id_));
    (void)json->CreateDataProperty(context,
        v8_string(isolate, "kind").As<v8::Name>(), v8_string(isolate, kind_));
    (void)json->CreateDataProperty(context,
        v8_string(isolate, "label").As<v8::Name>(), v8_string(isolate, label_));
    (void)json->CreateDataProperty(context,
        v8_string(isolate, "groupId").As<v8::Name>(), v8_string(isolate, group_id_));
    return json;
}

v8::Local<v8::Value> InputDeviceInfoHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    if (name == "getCapabilities") {
        watch_record(isolate, trace_label(), name);
        return v8::Object::New(isolate);
    }
    return MediaDeviceInfoHost::call_method(isolate, context, name, arguments);
}

v8::Local<v8::Value> MediaDevicesHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name) {
    if (name == "ondevicechange") {
        return get_event_handler(isolate, name);
    }
    return v8::Undefined(isolate);
}

void MediaDevicesHost::set_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name, v8::Local<v8::Value> value) {
    if (name == "ondevicechange") {
        set_event_handler(isolate, name, value);
    }
}

v8::Local<v8::Value> MediaDevicesHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (!validate_bom_event_dispatch(isolate, context, name, arguments)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> event_result;
    if (handle_event_method(isolate, context, name, arguments, event_result)) {
        return event_result;
    }
    if (name == "getSupportedConstraints") {
        auto result = v8::Object::New(isolate);
        for (const char* key : kSupportedConstraints) {
            (void)result->CreateDataProperty(context,
                v8_string(isolate, key).As<v8::Name>(), v8::True(isolate));
        }
        return result;
    }
    if (name == "enumerateDevices") {
        auto list = v8::Array::New(isolate, 3);
        auto audio_in = owner_->adopt_host_object(isolate, context,
            std::make_unique<InputDeviceInfoHost>("", "audioinput", "", ""));
        auto video_in = owner_->adopt_host_object(isolate, context,
            std::make_unique<InputDeviceInfoHost>("", "videoinput", "", ""));
        auto audio_out = owner_->adopt_host_object(isolate, context,
            std::make_unique<MediaDeviceInfoHost>("", "audiooutput", "", ""));
        (void)list->CreateDataProperty(context, 0, audio_in);
        (void)list->CreateDataProperty(context, 1, video_in);
        (void)list->CreateDataProperty(context, 2, audio_out);
        return schedule_task_value(isolate, context, false, list);
    }
    if (name == "getUserMedia") {
        const std::string prefix =
            "Failed to execute 'getUserMedia' on 'MediaDevices': ";
        v8::TryCatch caught(isolate);
        bool audio_requested = false;
        bool video_requested = false;
        if (arguments.Length() >= 1 && !arguments[0]->IsNullOrUndefined()) {
            if (!arguments[0]->IsObject()) {
                throw_type_error(isolate, prefix,
                    "The provided value is not of type 'UserMediaStreamConstraints'.");
                return reject_from_try_catch(isolate, context, caught);
            }
            auto dict = arguments[0].As<v8::Object>();
            v8::Local<v8::Value> audio;
            v8::Local<v8::Value> video;
            if (!user_get(isolate, context, dict, v8_string(isolate, "audio"))
                    .ToLocal(&audio)) {
                return reject_from_try_catch(isolate, context, caught);
            }
            if (!audio->IsUndefined()) {
                if (audio->IsObject()) {
                    const std::string audio_prefix = prefix +
                        "Failed to read the 'audio' property from 'UserMediaStreamConstraints': ";
                    if (!convert_track_constraints_object(
                            isolate, context, audio.As<v8::Object>(), audio_prefix)) {
                        return reject_from_try_catch(isolate, context, caught);
                    }
                    audio_requested = true;
                } else {
                    audio_requested = audio->BooleanValue(isolate);
                }
            }
            if (!user_get(isolate, context, dict, v8_string(isolate, "video"))
                    .ToLocal(&video)) {
                return reject_from_try_catch(isolate, context, caught);
            }
            if (!video->IsUndefined()) {
                if (video->IsObject()) {
                    const std::string video_prefix = prefix +
                        "Failed to read the 'video' property from 'UserMediaStreamConstraints': ";
                    if (!convert_track_constraints_object(
                            isolate, context, video.As<v8::Object>(), video_prefix)) {
                        return reject_from_try_catch(isolate, context, caught);
                    }
                    video_requested = true;
                } else {
                    video_requested = video->BooleanValue(isolate);
                }
            }
        }
        if (!audio_requested && !video_requested) {
            throw_type_error(isolate, prefix,
                "At least one of audio and video must be requested");
            return reject_from_try_catch(isolate, context, caught);
        }
        auto err = make_dom_exception(isolate, context, owner_,
            "NotAllowedError", "Permission denied");
        return schedule_task_value(isolate, context, true, err);
    }
    if (name == "getDisplayMedia") {
        const std::string prefix =
            "Failed to execute 'getDisplayMedia' on 'MediaDevices': ";
        v8::TryCatch caught(isolate);
        bool audio_requested = false;
        bool video_requested = true;
        if (arguments.Length() >= 1 && !arguments[0]->IsNullOrUndefined()) {
            if (!arguments[0]->IsObject()) {
                throw_type_error(isolate, prefix,
                    "The provided value is not of type 'DisplayMediaStreamOptions'.");
                return reject_from_try_catch(isolate, context, caught);
            }
            auto dict = arguments[0].As<v8::Object>();
            // Lexicographical DisplayMediaStreamOptions order verified in Chrome 158:
            // audio, audioSelection, controller, monitorTypeSurfaces, preferCurrentTab,
            // selfBrowserSurface, surfaceSwitching, systemAudio, video, windowAudio.
            v8::Local<v8::Value> audio;
            if (!user_get(isolate, context, dict, v8_string(isolate, "audio"))
                    .ToLocal(&audio)) {
                return reject_from_try_catch(isolate, context, caught);
            }
            if (!audio->IsUndefined()) {
                if (audio->IsObject()) {
                    const std::string audio_prefix = prefix +
                        "Failed to read the 'audio' property from 'DisplayMediaStreamOptions': ";
                    if (!convert_track_constraints_object(
                            isolate, context, audio.As<v8::Object>(), audio_prefix)) {
                        return reject_from_try_catch(isolate, context, caught);
                    }
                    audio_requested = true;
                } else {
                    audio_requested = audio->BooleanValue(isolate);
                }
            }
            v8::Local<v8::Value> audio_sel;
            if (!user_get(isolate, context, dict, v8_string(isolate, "audioSelection"))
                    .ToLocal(&audio_sel)) {
                return reject_from_try_catch(isolate, context, caught);
            }
            if (!audio_sel->IsNullOrUndefined() && !audio_sel->IsObject()) {
                throw_type_error(isolate, prefix,
                    "Failed to read the 'audioSelection' property from 'DisplayMediaStreamOptions': The provided value is not of type 'AudioSelection'.");
                return reject_from_try_catch(isolate, context, caught);
            }
            v8::Local<v8::Value> ctrl;
            if (!user_get(isolate, context, dict, v8_string(isolate, "controller"))
                    .ToLocal(&ctrl)) {
                return reject_from_try_catch(isolate, context, caught);
            }
            if (!ctrl->IsUndefined()) {
                auto* backing = host_object_backing(ctrl);
                if (backing == nullptr || backing->prototype_key() != "CaptureController") {
                    throw_type_error(isolate, prefix,
                        "Failed to read the 'controller' property from 'DisplayMediaStreamOptions': Failed to convert value to 'CaptureController'.");
                    return reject_from_try_catch(isolate, context, caught);
                }
            }
            const auto check_inc_exc = [&](const char* prop) {
                v8::Local<v8::Value> val;
                if (!user_get(isolate, context, dict, v8_string(isolate, prop))
                        .ToLocal(&val)) {
                    return false;
                }
                if (val->IsUndefined()) return true;
                const std::string prop_prefix = prefix +
                    "Failed to read the '" + prop +
                    "' property from 'DisplayMediaStreamOptions': ";
                std::string token;
                if (!web_idl_dom_string(isolate, context, val, prop_prefix, &token)) {
                    return false;
                }
                if (token != "include" && token != "exclude") {
                    return throw_type_error(isolate, prop_prefix,
                        "The provided value '" + token +
                        "' is not a valid enum value of type DisplayMediaIncludeOrExclude.");
                }
                return true;
            };
            if (!check_inc_exc("monitorTypeSurfaces")) {
                return reject_from_try_catch(isolate, context, caught);
            }
            v8::Local<v8::Value> prefer_tab;
            if (!user_get(isolate, context, dict, v8_string(isolate, "preferCurrentTab"))
                    .ToLocal(&prefer_tab)) {
                return reject_from_try_catch(isolate, context, caught);
            }
            (void)prefer_tab->BooleanValue(isolate);
            if (!check_inc_exc("selfBrowserSurface") ||
                !check_inc_exc("surfaceSwitching") ||
                !check_inc_exc("systemAudio")) {
                return reject_from_try_catch(isolate, context, caught);
            }
            v8::Local<v8::Value> video;
            if (!user_get(isolate, context, dict, v8_string(isolate, "video"))
                    .ToLocal(&video)) {
                return reject_from_try_catch(isolate, context, caught);
            }
            if (!video->IsUndefined()) {
                if (video->IsObject()) {
                    const std::string video_prefix = prefix +
                        "Failed to read the 'video' property from 'DisplayMediaStreamOptions': ";
                    if (!convert_track_constraints_object(
                            isolate, context, video.As<v8::Object>(), video_prefix)) {
                        return reject_from_try_catch(isolate, context, caught);
                    }
                    video_requested = true;
                } else {
                    video_requested = video->BooleanValue(isolate);
                }
            }
            v8::Local<v8::Value> win_audio;
            if (!user_get(isolate, context, dict, v8_string(isolate, "windowAudio"))
                    .ToLocal(&win_audio)) {
                return reject_from_try_catch(isolate, context, caught);
            }
            if (!win_audio->IsUndefined()) {
                const std::string wa_prefix = prefix +
                    "Failed to read the 'windowAudio' property from 'DisplayMediaStreamOptions': ";
                std::string token;
                if (!web_idl_dom_string(isolate, context, win_audio, wa_prefix, &token)) {
                    return reject_from_try_catch(isolate, context, caught);
                }
                if (token != "system" && token != "window" && token != "exclude") {
                    throw_type_error(isolate, wa_prefix,
                        "The provided value '" + token +
                        "' is not a valid enum value of type DisplayMediaSystemWindowOrExclude.");
                    return reject_from_try_catch(isolate, context, caught);
                }
            }
        }
        if (!audio_requested && !video_requested) {
            throw_type_error(isolate, prefix,
                "either audio or video must be requested");
            return reject_from_try_catch(isolate, context, caught);
        }
        auto err = make_dom_exception(isolate, context, owner_,
            "NotAllowedError", "Permission denied");
        return schedule_task_value(isolate, context, true, err);
    }
    if (name == "setCaptureHandleConfig") {
        const std::string prefix =
            "Failed to execute 'setCaptureHandleConfig' on 'MediaDevices': ";
        if (arguments.Length() < 1 || arguments[0]->IsNullOrUndefined()) {
            return v8::Undefined(isolate);
        }
        if (!arguments[0]->IsObject()) {
            throw_type_error(isolate, prefix,
                "The provided value is not of type 'CaptureHandleConfig'.");
            return v8::Undefined(isolate);
        }
        auto dict = arguments[0].As<v8::Object>();
        // Lexicographical dictionary order: exposeOrigin, handle, permittedOrigins.
        v8::Local<v8::Value> expose_val;
        v8::Local<v8::Value> handle_val;
        v8::Local<v8::Value> origins_val;
        if (!user_get(isolate, context, dict, v8_string(isolate, "exposeOrigin"))
                .ToLocal(&expose_val)) {
            return v8::Undefined(isolate);
        }
        (void)expose_val->BooleanValue(isolate);
        std::u16string handle_u16;
        if (!user_get(isolate, context, dict, v8_string(isolate, "handle"))
                .ToLocal(&handle_val)) {
            return v8::Undefined(isolate);
        }
        if (!handle_val->IsUndefined()) {
            v8::Local<v8::String> str;
            if (!access_user_operation(isolate, [&] {
                    return handle_val->ToString(context);
                }).ToLocal(&str)) {
                return v8::Undefined(isolate);
            }
            if (str->Length() > 1024) {
                throw_type_error(isolate, prefix,
                    "Handle length exceeds 1024 16-bit characters.");
                return v8::Undefined(isolate);
            }
        }
        if (!user_get(isolate, context, dict, v8_string(isolate, "permittedOrigins"))
                .ToLocal(&origins_val)) {
            return v8::Undefined(isolate);
        }
        std::vector<std::string> origins;
        if (!origins_val->IsUndefined()) {
            if (!convert_sequence(isolate, context, origins_val, prefix,
                    [&](v8::Local<v8::Value> item) {
                        std::string origin_str;
                        if (!web_idl_dom_string(isolate, context, item, prefix, &origin_str)) {
                            return false;
                        }
                        origins.push_back(std::move(origin_str));
                        return true;
                    })) {
                return v8::Undefined(isolate);
            }
        }
        const bool has_wildcard = std::find(origins.begin(), origins.end(), "*") != origins.end();
        if (has_wildcard && origins.size() > 1) {
            throw_dom_exception(isolate, context, owner_, "NotSupportedError",
                prefix + "Wildcard only valid in isolation.");
            return v8::Undefined(isolate);
        }
        for (const auto& entry : origins) {
            if (entry == "*") continue;
            const DomUrl parsed = resolve_dom_url(entry, "");
            if (!parsed.valid || parsed.origin == "null" ||
                !parsed.search.empty() || !parsed.hash.empty() ||
                (parsed.pathname != "/" && !parsed.pathname.empty())) {
                throw_dom_exception(isolate, context, owner_, "NotSupportedError",
                    prefix + "Invalid origin encountered.");
                return v8::Undefined(isolate);
            }
        }
        return v8::Undefined(isolate);
    }
    return v8::Undefined(isolate);
}

// --- ServiceWorkerContainer ---

v8::Local<v8::Value> ServiceWorkerContainerHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name) {
    if (name == "controller") {
        return v8::Null(isolate);
    }
    if (name == "ready") {
        if (ready_promise_.IsEmpty()) {
            auto runtime_origin = access_runtime_scope(isolate);
            v8::Local<v8::Promise::Resolver> resolver;
            if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
                return v8::Undefined(isolate);
            }
            ready_promise_.Reset(isolate, resolver->GetPromise());
        }
        return ready_promise_.Get(isolate);
    }
    if (name == "oncontrollerchange" || name == "onmessage" ||
        name == "onmessageerror") {
        return get_event_handler(isolate, name);
    }
    return v8::Undefined(isolate);
}

void ServiceWorkerContainerHost::set_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name, v8::Local<v8::Value> value) {
    if (name == "oncontrollerchange" || name == "onmessage" ||
        name == "onmessageerror") {
        set_event_handler(isolate, name, value);
    }
}

v8::Local<v8::Value> ServiceWorkerContainerHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (!validate_bom_event_dispatch(isolate, context, name, arguments)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> event_result;
    if (handle_event_method(isolate, context, name, arguments, event_result)) {
        return event_result;
    }
    if (name == "startMessages") {
        return v8::Undefined(isolate);
    }
    if (name == "getRegistrations") {
        return schedule_task_value(isolate, context, false, v8::Array::New(isolate, 0));
    }
    if (name == "getRegistration") {
        const std::string prefix =
            "Failed to execute 'getRegistration' on 'ServiceWorkerContainer': ";
        v8::TryCatch caught(isolate);
        std::string client_url;
        if (arguments.Length() >= 1 && !arguments[0]->IsUndefined()) {
            if (!web_idl_usv_string(isolate, context, arguments[0], prefix, &client_url)) {
                return reject_from_try_catch(isolate, context, caught);
            }
        }
        const std::string& live_url = owner_ ? owner_->current_url() : base_url_;
        const DomUrl current = resolve_dom_url(live_url, "");
        const DomUrl target = resolve_dom_url(client_url, live_url);
        const std::string target_origin = target.valid ? target.origin : "null";
        if (!target.valid || target_origin == "null" || target_origin != current.origin) {
            auto err = make_dom_exception(isolate, context, owner_, "SecurityError",
                "Failed to get a ServiceWorkerRegistration: The origin of the provided documentURL ('" +
                target_origin + "') does not match the current origin ('" +
                current.origin + "').");
            return rejected_promise(isolate, context, err);
        }
        return schedule_task_value(isolate, context, false, v8::Undefined(isolate));
    }
    if (name == "register") {
        const std::string prefix =
            "Failed to execute 'register' on 'ServiceWorkerContainer': ";
        v8::TryCatch caught(isolate);
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix,
                "1 argument required, but only 0 present.");
            return reject_from_try_catch(isolate, context, caught);
        }
        std::string script_raw;
        if (!web_idl_usv_string(isolate, context, arguments[0], prefix, &script_raw)) {
            return reject_from_try_catch(isolate, context, caught);
        }
        bool has_scope = false;
        std::string scope_raw;
        if (arguments.Length() >= 2 && !arguments[1]->IsNullOrUndefined()) {
            if (!arguments[1]->IsObject()) {
                throw_type_error(isolate, prefix,
                    "The provided value is not of type 'RegistrationOptions'.");
                return reject_from_try_catch(isolate, context, caught);
            }
            auto opts = arguments[1].As<v8::Object>();
            // Lexicographical order: scope, type, updateViaCache.
            v8::Local<v8::Value> scope_val;
            v8::Local<v8::Value> type_val;
            v8::Local<v8::Value> uvc_val;
            if (!user_get(isolate, context, opts, v8_string(isolate, "scope"))
                    .ToLocal(&scope_val)) {
                return reject_from_try_catch(isolate, context, caught);
            }
            if (!scope_val->IsUndefined()) {
                const std::string scope_prefix = prefix +
                    "Failed to read the 'scope' property from 'RegistrationOptions': ";
                if (!web_idl_usv_string(isolate, context, scope_val, scope_prefix, &scope_raw)) {
                    return reject_from_try_catch(isolate, context, caught);
                }
                has_scope = true;
            }
            if (!user_get(isolate, context, opts, v8_string(isolate, "type"))
                    .ToLocal(&type_val)) {
                return reject_from_try_catch(isolate, context, caught);
            }
            if (!type_val->IsUndefined()) {
                const std::string type_prefix = prefix +
                    "Failed to read the 'type' property from 'RegistrationOptions': ";
                std::string worker_type;
                if (!web_idl_dom_string(isolate, context, type_val, type_prefix, &worker_type)) {
                    return reject_from_try_catch(isolate, context, caught);
                }
                if (worker_type != "classic" && worker_type != "module") {
                    throw_type_error(isolate, type_prefix,
                        "The provided value '" + worker_type +
                        "' is not a valid enum value of type WorkerType.");
                    return reject_from_try_catch(isolate, context, caught);
                }
            }
            if (!user_get(isolate, context, opts, v8_string(isolate, "updateViaCache"))
                    .ToLocal(&uvc_val)) {
                return reject_from_try_catch(isolate, context, caught);
            }
            if (!uvc_val->IsUndefined()) {
                const std::string uvc_prefix = prefix +
                    "Failed to read the 'updateViaCache' property from 'RegistrationOptions': ";
                std::string uvc;
                if (!web_idl_dom_string(isolate, context, uvc_val, uvc_prefix, &uvc)) {
                    return reject_from_try_catch(isolate, context, caught);
                }
                if (uvc != "imports" && uvc != "all" && uvc != "none") {
                    throw_type_error(isolate, uvc_prefix,
                        "The provided value '" + uvc +
                        "' is not a valid enum value of type ServiceWorkerUpdateViaCache.");
                    return reject_from_try_catch(isolate, context, caught);
                }
            }
        }
        const std::string& live_url = owner_ ? owner_->current_url() : base_url_;
        const DomUrl current = resolve_dom_url(live_url, "");
        const DomUrl script = resolve_dom_url(script_raw, live_url);
        if (!script.valid) {
            throw_type_error(isolate, "Failed to register a ServiceWorker: ",
                "Invalid URL");
            return reject_from_try_catch(isolate, context, caught);
        }
        if (script.protocol != "http:" && script.protocol != "https:") {
            throw_type_error(isolate, "Failed to register a ServiceWorker: ",
                "The URL protocol of the script ('" + script.href + "') is not supported.");
            return reject_from_try_catch(isolate, context, caught);
        }
        if (has_forbidden_sw_escape(script.pathname)) {
            throw_type_error(isolate, "Failed to register a ServiceWorker: ",
                "The provided scriptURL ('" + script.href + "') is invalid.");
            return reject_from_try_catch(isolate, context, caught);
        }
        std::string scope_href;
        std::string scope_origin;
        std::string scope_pathname;
        if (has_scope) {
            const DomUrl scope = resolve_dom_url(scope_raw, live_url);
            if (!scope.valid) {
                throw_type_error(isolate, "Failed to register a ServiceWorker: ",
                    "Invalid scope URL");
                return reject_from_try_catch(isolate, context, caught);
            }
            if (scope.protocol != "http:" && scope.protocol != "https:") {
                throw_type_error(isolate, "Failed to register a ServiceWorker: ",
                    "The URL protocol of the scope ('" + scope.href + "') is not supported.");
                return reject_from_try_catch(isolate, context, caught);
            }
            if (has_forbidden_sw_escape(scope.pathname)) {
                throw_type_error(isolate, "Failed to register a ServiceWorker: ",
                    "The provided scope ('" + scope.href + "') is invalid.");
                return reject_from_try_catch(isolate, context, caught);
            }
            scope_href = scope.href;
            scope_origin = scope.origin;
            scope_pathname = scope.pathname;
        } else {
            const auto slash = script.pathname.rfind('/');
            scope_pathname = slash == std::string::npos
                ? "/" : script.pathname.substr(0, slash + 1);
            scope_origin = script.origin;
            scope_href = script.origin + scope_pathname;
        }
        if (script.origin != current.origin) {
            auto err = make_dom_exception(isolate, context, owner_, "SecurityError",
                "Failed to register a ServiceWorker: The origin of the provided scriptURL ('" +
                script.origin + "') does not match the current origin ('" +
                current.origin + "').");
            return rejected_promise(isolate, context, err);
        }
        if (scope_origin != current.origin) {
            auto err = make_dom_exception(isolate, context, owner_, "SecurityError",
                "Failed to register a ServiceWorker: The origin of the provided scope ('" +
                scope_origin + "') does not match the current origin ('" +
                current.origin + "').");
            return rejected_promise(isolate, context, err);
        }
        const std::string fetch_msg =
            "Failed to register a ServiceWorker for scope ('" + scope_href +
            "') with script ('" + script.href +
            "'): ServiceWorker script fetch is disabled in the offline Page profile.";
        v8::Local<v8::String> msg_v8;
        if (!v8::String::NewFromUtf8(isolate, fetch_msg.data(),
                v8::NewStringType::kNormal, static_cast<int>(fetch_msg.size()))
                .ToLocal(&msg_v8)) {
            return v8::Undefined(isolate);
        }
        return schedule_task_value(isolate, context, true,
            v8::Exception::TypeError(msg_v8));
    }
    return v8::Undefined(isolate);
}

// --- StorageBucket & StorageBucketManager ---

v8::Local<v8::Value> StorageBucketHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name) {
    if (name == "name") {
        return v8_string(isolate, state_ ? state_->name : std::string());
    }
    if (name == "indexedDB") {
        if (indexed_db_.IsEmpty()) {
            auto obj = create_window_shell_instance(owner_, isolate, context, "indexedDB");
            if (obj.IsEmpty()) return v8::Undefined(isolate);
            indexed_db_.Reset(isolate, obj);
        }
        return indexed_db_.Get(isolate);
    }
    if (name == "caches") {
        if (caches_.IsEmpty()) {
            auto obj = create_window_shell_instance(owner_, isolate, context, "caches");
            if (obj.IsEmpty()) return v8::Undefined(isolate);
            caches_.Reset(isolate, obj);
        }
        return caches_.Get(isolate);
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> StorageBucketHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (name == "persisted" || name == "persist") {
        return schedule_task_value(isolate, context, false, v8::False(isolate));
    }
    if (name == "estimate") {
        auto est = v8::Object::New(isolate);
        const double quota = state_ ? state_->quota : kOfflineBucketQuota;
        (void)est->CreateDataProperty(context,
            v8_string(isolate, "quota").As<v8::Name>(), v8::Number::New(isolate, quota));
        (void)est->CreateDataProperty(context,
            v8_string(isolate, "usage").As<v8::Name>(), v8::Integer::New(isolate, 0));
        (void)est->CreateDataProperty(context,
            v8_string(isolate, "usageDetails").As<v8::Name>(), v8::Object::New(isolate));
        return schedule_task_value(isolate, context, false, est);
    }
    if (name == "expires") {
        v8::Local<v8::Value> value = (state_ && state_->expires.has_value())
            ? v8::Local<v8::Value>(v8::Number::New(isolate, *state_->expires))
            : v8::Local<v8::Value>(v8::Null(isolate));
        return schedule_task_value(isolate, context, false, value);
    }
    if (name == "setExpires") {
        const std::string prefix =
            "Failed to execute 'setExpires' on 'StorageBucket': ";
        v8::TryCatch caught(isolate);
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix,
                "1 argument required, but only 0 present.");
            return reject_from_try_catch(isolate, context, caught);
        }
        double expires_ms = 0.0;
        if (!convert_enforce_range_ull(isolate, context, arguments[0], prefix, &expires_ms)) {
            return reject_from_try_catch(isolate, context, caught);
        }
        if (expires_ms <= current_epoch_ms()) {
            throw_type_error(isolate, "", "The bucket expiration is invalid.");
            return reject_from_try_catch(isolate, context, caught);
        }
        if (state_) {
            state_->expires = expires_ms;
        }
        return schedule_task_value(isolate, context, false, v8::Undefined(isolate));
    }
    if (name == "getDirectory") {
        auto err = make_dom_exception(isolate, context, owner_,
            "NotSupportedError",
            "File System Access API is unavailable in the offline Page profile.");
        return schedule_task_value(isolate, context, true, err);
    }
    return v8::Undefined(isolate);
}

void StorageBucketManagerHost::prune_expired_buckets() {
    const double now = current_epoch_ms();
    for (auto it = buckets_.begin(); it != buckets_.end(); ) {
        if (it->second && it->second->expires.has_value() &&
            *it->second->expires <= now) {
            it = buckets_.erase(it);
        } else {
            ++it;
        }
    }
}

v8::Local<v8::Value> StorageBucketManagerHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string&) {
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> StorageBucketManagerHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (name == "keys") {
        prune_expired_buckets();
        auto arr = v8::Array::New(isolate, static_cast<int>(buckets_.size()));
        std::uint32_t index = 0;
        for (const auto& [bucket_name, _] : buckets_) {
            (void)arr->CreateDataProperty(context, index++, v8_string(isolate, bucket_name));
        }
        return schedule_task_value(isolate, context, false, arr);
    }
    if (name == "open") {
        const std::string prefix =
            "Failed to execute 'open' on 'StorageBucketManager': ";
        v8::TryCatch caught(isolate);
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix,
                "1 argument required, but only 0 present.");
            return reject_from_try_catch(isolate, context, caught);
        }
        std::string bucket_name;
        if (!web_idl_dom_string(isolate, context, arguments[0], prefix, &bucket_name)) {
            return reject_from_try_catch(isolate, context, caught);
        }
        std::optional<double> opt_expires;
        std::optional<double> opt_quota;
        if (arguments.Length() >= 2 && !arguments[1]->IsNullOrUndefined()) {
            if (!arguments[1]->IsObject()) {
                throw_type_error(isolate, prefix,
                    "The provided value is not of type 'StorageBucketOptions'.");
                return reject_from_try_catch(isolate, context, caught);
            }
            auto opts = arguments[1].As<v8::Object>();
            // Lexicographical order: expires, persisted, quota.
            v8::Local<v8::Value> expires_val;
            v8::Local<v8::Value> persisted_val;
            v8::Local<v8::Value> quota_val;
            if (!user_get(isolate, context, opts, v8_string(isolate, "expires"))
                    .ToLocal(&expires_val)) {
                return reject_from_try_catch(isolate, context, caught);
            }
            if (!expires_val->IsUndefined()) {
                const std::string exp_prefix = prefix +
                    "Failed to read the 'expires' property from 'StorageBucketOptions': ";
                double expires_ms = 0.0;
                if (!convert_enforce_range_ull(isolate, context, expires_val, exp_prefix, &expires_ms)) {
                    return reject_from_try_catch(isolate, context, caught);
                }
                opt_expires = expires_ms;
            }
            if (!user_get(isolate, context, opts, v8_string(isolate, "persisted"))
                    .ToLocal(&persisted_val)) {
                return reject_from_try_catch(isolate, context, caught);
            }
            (void)persisted_val->BooleanValue(isolate);
            if (!user_get(isolate, context, opts, v8_string(isolate, "quota"))
                    .ToLocal(&quota_val)) {
                return reject_from_try_catch(isolate, context, caught);
            }
            if (!quota_val->IsUndefined()) {
                const std::string quota_prefix = prefix +
                    "Failed to read the 'quota' property from 'StorageBucketOptions': ";
                double quota_bytes = 0.0;
                if (!convert_enforce_range_ull(isolate, context, quota_val, quota_prefix, &quota_bytes)) {
                    return reject_from_try_catch(isolate, context, caught);
                }
                opt_quota = quota_bytes;
            }
        }
        if (!is_valid_bucket_name(bucket_name)) {
            throw_type_error(isolate, "",
                "The bucket name '" + bucket_name + "' is not a valid name.");
            return reject_from_try_catch(isolate, context, caught);
        }
        if (opt_quota.has_value() && *opt_quota == 0.0) {
            throw_type_error(isolate, "",
                "The bucket's quota cannot equal zero.");
            return reject_from_try_catch(isolate, context, caught);
        }
        if (opt_expires.has_value() && *opt_expires <= current_epoch_ms()) {
            throw_type_error(isolate, "",
                "The bucket expiration is invalid.");
            return reject_from_try_catch(isolate, context, caught);
        }
        prune_expired_buckets();
        auto found = buckets_.find(bucket_name);
        std::shared_ptr<StorageBucketState> state;
        if (found == buckets_.end()) {
            state = std::make_shared<StorageBucketState>();
            state->name = bucket_name;
            state->quota = opt_quota.value_or(kOfflineBucketQuota);
            state->expires = opt_expires;
            buckets_.emplace(bucket_name, state);
        } else {
            state = found->second;
            if (opt_expires.has_value()) {
                state->expires = opt_expires;
            }
        }
        auto wrapper = owner_->adopt_host_object(isolate, context,
            std::make_unique<StorageBucketHost>(owner_, std::move(state)));
        return schedule_task_value(isolate, context, false, wrapper);
    }
    if (name == "delete") {
        const std::string prefix =
            "Failed to execute 'delete' on 'StorageBucketManager': ";
        v8::TryCatch caught(isolate);
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix,
                "1 argument required, but only 0 present.");
            return reject_from_try_catch(isolate, context, caught);
        }
        std::string bucket_name;
        if (!web_idl_dom_string(isolate, context, arguments[0], prefix, &bucket_name)) {
            return reject_from_try_catch(isolate, context, caught);
        }
        if (!is_valid_bucket_name(bucket_name)) {
            // Verified in Chrome 158: delete() omits quotes around bucket_name.
            throw_type_error(isolate, "",
                "The bucket name " + bucket_name + " is not a valid name.");
            return reject_from_try_catch(isolate, context, caught);
        }
        buckets_.erase(bucket_name);
        return schedule_task_value(isolate, context, false, v8::Undefined(isolate));
    }
    return v8::Undefined(isolate);
}

// --- NavigatorLogin & NavigatorManagedData ---

v8::Local<v8::Value> NavigatorLoginHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string&) {
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> NavigatorLoginHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (name != "setStatus") return v8::Undefined(isolate);
    const std::string prefix =
        "Failed to execute 'setStatus' on 'NavigatorLogin': ";
    v8::TryCatch caught(isolate);
    if (arguments.Length() < 1) {
        throw_type_error(isolate, prefix,
            "1 argument required, but only 0 present.");
        return reject_from_try_catch(isolate, context, caught);
    }
    std::string status;
    if (!web_idl_dom_string(isolate, context, arguments[0], prefix, &status)) {
        return reject_from_try_catch(isolate, context, caught);
    }
    if (status != "logged-in" && status != "logged-out") {
        throw_type_error(isolate, prefix,
            "The provided value '" + status +
            "' is not a valid enum value of type LoginStatus.");
        return reject_from_try_catch(isolate, context, caught);
    }
    status_ = std::move(status);
    return schedule_task_value(isolate, context, false, v8::Undefined(isolate));
}

v8::Local<v8::Value> NavigatorManagedDataHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name) {
    if (name == "onmanagedconfigurationchange") {
        return get_event_handler(isolate, name);
    }
    return v8::Undefined(isolate);
}

void NavigatorManagedDataHost::set_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name, v8::Local<v8::Value> value) {
    if (name == "onmanagedconfigurationchange") {
        set_event_handler(isolate, name, value);
    }
}

v8::Local<v8::Value> NavigatorManagedDataHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (!validate_bom_event_dispatch(isolate, context, name, arguments)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> event_result;
    if (handle_event_method(isolate, context, name, arguments, event_result)) {
        return event_result;
    }
    if (name == "getManagedConfiguration") {
        const std::string prefix =
            "Failed to execute 'getManagedConfiguration' on 'NavigatorManagedData': ";
        v8::TryCatch caught(isolate);
        if (arguments.Length() < 1) {
            throw_type_error(isolate, prefix,
                "1 argument required, but only 0 present.");
            return reject_from_try_catch(isolate, context, caught);
        }
        if (!convert_sequence(isolate, context, arguments[0], prefix,
                [&](v8::Local<v8::Value> item) {
                    std::string key;
                    return web_idl_dom_string(isolate, context, item, prefix, &key);
                })) {
            return reject_from_try_catch(isolate, context, caught);
        }
        auto err = make_dom_exception(isolate, context, owner_,
            "NotAllowedError",
            "Managed configuration is empty. This API is available only for managed apps.");
        return schedule_task_value(isolate, context, true, err);
    }
    return v8::Undefined(isolate);
}

// --- Ink & DelegatedInkTrailPresenter ---

v8::Local<v8::Value> DelegatedInkTrailPresenterHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name) {
    if (name == "presentationArea") {
        return presentation_area_.IsEmpty()
            ? v8::Local<v8::Value>(v8::Null(isolate))
            : v8::Local<v8::Value>(presentation_area_.Get(isolate));
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> DelegatedInkTrailPresenterHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (name != "updateInkTrailStartPoint") return v8::Undefined(isolate);
    const std::string prefix =
        "Failed to execute 'updateInkTrailStartPoint' on 'DelegatedInkTrailPresenter': ";
    if (arguments.Length() < 2) {
        throw_type_error(isolate, prefix,
            "2 arguments required, but only " +
            std::to_string(arguments.Length()) + " present.");
        return v8::Undefined(isolate);
    }
    auto* backing = host_object_backing(arguments[0]);
    if (backing == nullptr || backing->prototype_key() != "PointerEvent") {
        throw_type_error(isolate, prefix,
            "parameter 1 is not of type 'PointerEvent'.");
        return v8::Undefined(isolate);
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> InkHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string&) {
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> InkHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (name != "requestPresenter") return v8::Undefined(isolate);
    const std::string prefix = "Failed to execute 'requestPresenter' on 'Ink': ";
    v8::TryCatch caught(isolate);
    v8::Local<v8::Object> area;
    if (arguments.Length() >= 1 && !arguments[0]->IsNullOrUndefined()) {
        if (!arguments[0]->IsObject()) {
            throw_type_error(isolate, prefix,
                "The provided value is not of type 'InkPresenterParam'.");
            return reject_from_try_catch(isolate, context, caught);
        }
        v8::Local<v8::Value> area_val;
        if (!user_get(isolate, context, arguments[0].As<v8::Object>(),
                v8_string(isolate, "presentationArea")).ToLocal(&area_val)) {
            return reject_from_try_catch(isolate, context, caught);
        }
        if (!area_val->IsNullOrUndefined()) {
            auto* backing = host_object_backing(area_val);
            if (backing == nullptr || !backing->is_node() ||
                !web_interface_inherits(backing->prototype_key(), "Element")) {
                throw_type_error(isolate, prefix,
                    "Failed to read the 'presentationArea' property from 'InkPresenterParam': Failed to convert value to 'Element'.");
                return reject_from_try_catch(isolate, context, caught);
            }
            area = area_val.As<v8::Object>();
        }
    }
    auto presenter = owner_->adopt_host_object(isolate, context,
        std::make_unique<DelegatedInkTrailPresenterHost>(isolate, area));
    return resolved_promise(isolate, context, presenter);
}

// --- DevicePosture, VirtualKeyboard, WindowControlsOverlay ---

v8::Local<v8::Value> DevicePostureHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name) {
    if (name == "type") {
        return v8_string(isolate, "continuous");
    }
    if (name == "onchange") {
        return get_event_handler(isolate, name);
    }
    return v8::Undefined(isolate);
}

void DevicePostureHost::set_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name, v8::Local<v8::Value> value) {
    if (name == "onchange") {
        set_event_handler(isolate, name, value);
    }
}

v8::Local<v8::Value> DevicePostureHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (!validate_bom_event_dispatch(isolate, context, name, arguments)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> event_result;
    if (handle_event_method(isolate, context, name, arguments, event_result)) {
        return event_result;
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> VirtualKeyboardHost::get_property(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name) {
    if (name == "boundingRect") {
        if (bounding_rect_.IsEmpty()) {
            auto rect = owner_->adopt_host_object(isolate, context,
                std::make_unique<DOMRectHost>(0.0, 0.0, 0.0, 0.0));
            bounding_rect_.Reset(isolate, rect);
        }
        return bounding_rect_.Get(isolate);
    }
    if (name == "overlaysContent") {
        return v8::Boolean::New(isolate, overlays_content_);
    }
    if (name == "ongeometrychange") {
        return get_event_handler(isolate, name);
    }
    return v8::Undefined(isolate);
}

void VirtualKeyboardHost::set_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name, v8::Local<v8::Value> value) {
    if (name == "overlaysContent") {
        overlays_content_ = value->BooleanValue(isolate);
    } else if (name == "ongeometrychange") {
        set_event_handler(isolate, name, value);
    }
}

v8::Local<v8::Value> VirtualKeyboardHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (!validate_bom_event_dispatch(isolate, context, name, arguments)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> event_result;
    if (handle_event_method(isolate, context, name, arguments, event_result)) {
        return event_result;
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> WindowControlsOverlayHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name) {
    if (name == "visible") {
        return v8::False(isolate);
    }
    if (name == "ongeometrychange") {
        return get_event_handler(isolate, name);
    }
    return v8::Undefined(isolate);
}

void WindowControlsOverlayHost::set_property(v8::Isolate* isolate,
    v8::Local<v8::Context>, const std::string& name, v8::Local<v8::Value> value) {
    if (name == "ongeometrychange") {
        set_event_handler(isolate, name, value);
    }
}

v8::Local<v8::Value> WindowControlsOverlayHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    watch_record(isolate, trace_label(), name);
    if (!validate_bom_event_dispatch(isolate, context, name, arguments)) {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> event_result;
    if (handle_event_method(isolate, context, name, arguments, event_result)) {
        return event_result;
    }
    if (name == "getTitlebarAreaRect") {
        return owner_->adopt_host_object(isolate, context,
            std::make_unique<DOMRectHost>(0.0, 0.0, 0.0, 0.0));
    }
    return v8::Undefined(isolate);
}

// --- Interface installation ---

bool install_navigator_bom07_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner, bool secure_context) {
    // 1. DOMRectReadOnly & DOMRect (exposed in all contexts).
    DOMRectReadOnlyHost rect_ro;
    DOMRectHost rect_rw;
    for (HostObject* host : {static_cast<HostObject*>(&rect_ro),
                             static_cast<HostObject*>(&rect_rw)}) {
        auto proto = web_interface_prototype(isolate, context, host->prototype_key());
        if (proto.IsEmpty()) return false;
        install_host_interface(isolate, context, host);
        if (!move_constructor_to_end(isolate, context, proto, host->prototype_key())) {
            return false;
        }
        v8::Local<v8::Value> ctor_val;
        if (!proto->Get(context, v8_string(isolate, "constructor")).ToLocal(&ctor_val) ||
            !ctor_val->IsFunction()) {
            return false;
        }
        auto data = v8::Array::New(isolate, 2);
        if (!data->CreateDataProperty(context, 0,
                v8::External::New(isolate, owner, v8::kExternalPointerTypeTagDefault))
                .FromMaybe(false) ||
            !data->CreateDataProperty(context, 1,
                v8::Boolean::New(isolate, host == &rect_rw)).FromMaybe(false)) {
            return false;
        }
        v8::Local<v8::Function> from_rect_fn;
        if (!make_web_method(isolate, context, "fromRect", 0,
                &dom_rect_from_rect_callback, data).ToLocal(&from_rect_fn) ||
            !install_traced_api_data_property(isolate, context,
                ctor_val.As<v8::Object>(), "fromRect", from_rect_fn,
                native_api_identity(isolate, ctor_val.As<v8::Object>()),
                "host_object", host->prototype_key())) {
            return false;
        }
    }

    // 2. MediaDeviceInfo (exposed globally in secure contexts only, but always
    // created as the prototype parent of InputDeviceInfo) and InputDeviceInfo
    // (exposed in all contexts in Chrome 158).
    auto mdi_proto = web_interface_prototype(
        isolate, context, "MediaDeviceInfo", secure_context);
    if (mdi_proto.IsEmpty()) return false;
    MediaDeviceInfoHost mdi_decl("", "", "", "");
    install_host_interface(isolate, context, &mdi_decl);
    if (!move_constructor_to_end(isolate, context, mdi_proto, "MediaDeviceInfo")) {
        return false;
    }

    auto idi_proto = web_interface_prototype(isolate, context, "InputDeviceInfo", true);
    if (idi_proto.IsEmpty()) return false;
    InputDeviceInfoHost idi_decl("", "", "", "");
    install_host_interface(isolate, context, &idi_decl);
    if (!move_constructor_to_end(isolate, context, idi_proto, "InputDeviceInfo")) {
        return false;
    }

    // 3. Ink, DelegatedInkTrailPresenter, WindowControlsOverlay (all contexts).
    InkHost ink_decl(owner);
    DelegatedInkTrailPresenterHost ink_presenter_decl(isolate, {});
    WindowControlsOverlayHost wco_decl(owner);
    for (HostObject* host : {static_cast<HostObject*>(&ink_decl),
                             static_cast<HostObject*>(&ink_presenter_decl),
                             static_cast<HostObject*>(&wco_decl)}) {
        auto proto = web_interface_prototype(isolate, context, host->prototype_key());
        if (proto.IsEmpty()) return false;
        install_host_interface(isolate, context, host);
        if (!move_constructor_to_end(isolate, context, proto, host->prototype_key())) {
            return false;
        }
    }

    if (!secure_context) {
        return true;
    }

    // 4. [SecureContext]-only interfaces with standard constructor-at-end order.
    MediaDevicesHost md_decl(owner);
    ServiceWorkerContainerHost swc_decl(owner, {});
    StorageBucketManagerHost sbm_decl(owner);
    NavigatorLoginHost login_decl;
    NavigatorManagedDataHost managed_decl(owner);
    DevicePostureHost posture_decl;
    VirtualKeyboardHost vk_decl(owner);
    for (HostObject* host : {static_cast<HostObject*>(&md_decl),
                             static_cast<HostObject*>(&swc_decl),
                             static_cast<HostObject*>(&sbm_decl),
                             static_cast<HostObject*>(&login_decl),
                             static_cast<HostObject*>(&managed_decl),
                             static_cast<HostObject*>(&posture_decl),
                             static_cast<HostObject*>(&vk_decl)}) {
        auto proto = web_interface_prototype(isolate, context, host->prototype_key());
        if (proto.IsEmpty()) return false;
        install_host_interface(isolate, context, host);
        if (!move_constructor_to_end(isolate, context, proto, host->prototype_key())) {
            return false;
        }
    }

    // 5. StorageBucket: `persist` appears after `constructor` in Chrome 158.
    StorageBucketHost sb_decl(owner, nullptr);
    auto sb_proto = web_interface_prototype(isolate, context, "StorageBucket");
    if (sb_proto.IsEmpty()) return false;
    install_host_interface(isolate, context, &sb_decl);
    if (!reorder_prototype_keys(isolate, context, sb_proto, "StorageBucket",
            {"constructor", "persist"})) {
        return false;
    }

    // 6. ServiceWorker: `onerror` appears after `constructor` in Chrome 158.
    ServiceWorkerDeclHost sw_decl;
    auto sw_proto = web_interface_prototype(isolate, context, "ServiceWorker");
    if (sw_proto.IsEmpty()) return false;
    install_host_interface(isolate, context, &sw_decl);
    if (!reorder_prototype_keys(isolate, context, sw_proto, "ServiceWorker",
            {"constructor", "onerror"})) {
        return false;
    }

    // 7. ServiceWorkerRegistration: interleaved partial-interface members +
    // `cookies` after `constructor` in Chrome 158.
    ServiceWorkerRegistrationDeclHost swr_decl;
    auto swr_proto = web_interface_prototype(
        isolate, context, "ServiceWorkerRegistration");
    if (swr_proto.IsEmpty()) return false;
    install_host_interface(isolate, context, &swr_decl);
    if (!reorder_prototype_keys(isolate, context, swr_proto,
            "ServiceWorkerRegistration", {
                "installing", "waiting", "active", "navigationPreload",
                "scope", "updateViaCache", "onupdatefound", "sync",
                "unregister", "update", "backgroundFetch", "periodicSync",
                "paymentManager", "pushManager", "getNotifications",
                "showNotification", "constructor", "cookies"})) {
        return false;
    }

    return true;
}

}  // namespace iv8::browser
