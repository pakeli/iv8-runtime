#include "iv8/page_state.h"
#include "window_shell_objects.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "iv8/web_interface.h"
#include "iv8/web_method.h"
#include "../host/event_target.h"
#include "../internal/common.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {
namespace {

struct MethodSpec {
    std::string name;
    int length = 0;
    bool rejected_promise = false;
};

struct ShellSpec {
    std::string name;
    std::string interface_name;
    std::vector<MethodSpec> methods;
    std::vector<std::string> properties;
    std::vector<std::string> handlers;
    bool event_target = false;
    bool namespace_object = false;
    bool enumerable_global = true;
    bool replaceable_global = false;
};

v8::Local<v8::Value> unsupported_error(v8::Isolate* isolate,
    const std::string& operation) {
    auto error = v8::Exception::Error(v8_string(isolate,
        operation + " is unavailable in the offline Window shell").As<v8::String>());
    (void)error.As<v8::Object>()->CreateDataProperty(isolate->GetCurrentContext(),
        v8_string(isolate, "name").As<v8::String>(), v8_string(isolate, "NotSupportedError"));
    return error;
}

v8::Local<v8::Value> css_escape(v8::Isolate* isolate,
    v8::Local<v8::Context> context,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    if (arguments.Length() == 0) {
        isolate->ThrowException(v8::Exception::TypeError(v8_string(isolate,
            "Failed to execute 'escape' on 'CSS': 1 argument required, but only 0 present.").As<v8::String>()));
        return v8::Undefined(isolate);
    }
    v8::Local<v8::String> input;
    if (!arguments[0]->ToString(context).ToLocal(&input)) return v8::Undefined(isolate);
    v8::String::Value units(isolate, input);
    if (*units == nullptr) return v8::Undefined(isolate);
    std::u16string result;
    const auto digit = [](std::uint16_t character) { return character >= '0' && character <= '9'; };
    constexpr char16_t hex[] = u"0123456789abcdef";
    for (std::uint32_t index = 0; index < units.length(); ++index) {
        const std::uint16_t character = (*units)[index];
        if (character == 0) {
            result += u'\ufffd';
        } else if ((character >= 1 && character <= 31) || character == 127 ||
                   (index == 0 && digit(character)) ||
                   (index == 1 && digit(character) && (*units)[0] == '-')) {
            result += u'\\';
            if (character >= 16) result += hex[character >> 4];
            result += hex[character & 15];
            result += u' ';
        } else if (index == 0 && character == '-' && units.length() == 1) {
            result += u"\\-";
        } else if (character >= 128 || character == '-' || character == '_' || digit(character) ||
                   (character >= 'a' && character <= 'z') || (character >= 'A' && character <= 'Z')) {
            result += static_cast<char16_t>(character);
        } else {
            result += u'\\'; result += static_cast<char16_t>(character);
        }
    }
    v8::Local<v8::String> escaped;
    if (!v8::String::NewFromTwoByte(isolate,
            reinterpret_cast<const std::uint16_t*>(result.data()),
            v8::NewStringType::kNormal, static_cast<int>(result.size())).ToLocal(&escaped)) return v8::Undefined(isolate);
    return escaped;
}

class WindowShellHost : public EventTargetHost {
public:
    explicit WindowShellHost(ShellSpec specification) : specification_(std::move(specification)) {}
    std::string global_name() const override { return specification_.name; }
    std::string prototype_key() const override {
        return specification_.namespace_object ? "" : specification_.interface_name;
    }
    std::string trace_object_type() const override { return "window_shell"; }
    std::string trace_label() const override { return specification_.name; }
    std::vector<std::string> property_names() const override {
        if (specification_.namespace_object) return {};
        auto names = specification_.properties;
        names.insert(names.end(), specification_.handlers.begin(), specification_.handlers.end());
        return names;
    }
    std::vector<std::string> writable_property_names() const override { return specification_.handlers; }
    // Native methods are defined explicitly with their reference arities below,
    // on this same canonical prototype (or on a namespace as own data methods).
    std::vector<std::string> method_names() const override { return {}; }

    void release_v8_handles() noexcept override {
        EventTargetHost::release_v8_handles();
        global_value_.Reset(); chrome_app_.Reset();
    }
    void set_global_value(v8::Isolate* isolate, v8::Local<v8::Value> value) { global_value_.Reset(isolate, value); }
    v8::Local<v8::Value> global_value(v8::Isolate* isolate) const { return global_value_.Get(isolate); }
    const ShellSpec& specification() const { return specification_; }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name) override {
        if (std::find(specification_.handlers.begin(), specification_.handlers.end(), name) != specification_.handlers.end())
            return get_event_handler(isolate, name);
        if (specification_.name == "styleMedia" && name == "type") return v8_string(isolate, "screen");
        if (specification_.name == "trustedTypes" && name == "defaultPolicy") return v8::Null(isolate);
        if (specification_.name == "chrome" && name == "app") {
            if (chrome_app_.IsEmpty()) chrome_app_.Reset(isolate, v8::Object::New(isolate));
            return chrome_app_.Get(isolate);
        }
        (void)context;
        isolate->ThrowException(unsupported_error(isolate, specification_.name + "." + name));
        return v8::Undefined(isolate);
    }

    void set_property(v8::Isolate* isolate, v8::Local<v8::Context>,
        const std::string& name, v8::Local<v8::Value> value) override {
        if (std::find(specification_.handlers.begin(), specification_.handlers.end(), name) != specification_.handlers.end())
            set_event_handler(isolate, name, value);
    }

    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override {
        v8::Local<v8::Value> value;
        if (specification_.event_target && handle_event_method(isolate, context, name, arguments, value)) return value;
        if (specification_.name == "CSS" && name == "escape") return css_escape(isolate, context, arguments);
        if (specification_.name == "trustedTypes" && (name == "isHTML" || name == "isScript" || name == "isScriptURL"))
            return v8::False(isolate); // This slice cannot create any trusted-value objects.
        const auto method = std::find_if(specification_.methods.begin(), specification_.methods.end(),
            [&name](const MethodSpec& item) { return item.name == name; });
        const auto error = unsupported_error(isolate, specification_.name + "." + name);
        if (method != specification_.methods.end() && method->rejected_promise) {
            v8::Local<v8::Promise::Resolver> resolver;
            if (!v8::Promise::Resolver::New(context).ToLocal(&resolver) ||
                !resolver->Reject(context, error).FromMaybe(false)) return v8::Undefined(isolate);
            return resolver->GetPromise();
        }
        isolate->ThrowException(error);
        return v8::Undefined(isolate);
    }

private:
    ShellSpec specification_;
    v8::Global<v8::Value> global_value_;
    v8::Global<v8::Object> chrome_app_;
};

WindowShellHost* external_host(v8::Local<v8::Value> value) {
    return static_cast<WindowShellHost*>(value.As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
}

void global_shell_getter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.This() != info.GetIsolate()->GetCurrentContext()->Global()) {
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(v8_string(info.GetIsolate(), "Illegal invocation").As<v8::String>())); return;
    }
    info.GetReturnValue().Set(external_host(info.Data())->global_value(info.GetIsolate()));
}

void global_shell_setter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (info.This() != info.GetIsolate()->GetCurrentContext()->Global()) {
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(v8_string(info.GetIsolate(), "Illegal invocation").As<v8::String>())); return;
    }
    external_host(info.Data())->set_global_value(info.GetIsolate(), info.Length() ? info[0] : v8::Local<v8::Value>(v8::Undefined(info.GetIsolate())));
}

void shell_method(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate(); auto context = isolate->GetCurrentContext();
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> interface_value, member_value, bound_host;
    if (!data->Get(context, 0).ToLocal(&interface_value) || !data->Get(context, 1).ToLocal(&member_value) ||
        !data->Get(context, 2).ToLocal(&bound_host)) return;
    v8::String::Utf8Value interface_name(isolate, interface_value), member(isolate, member_value);
    if (*interface_name == nullptr || *member == nullptr) return;
    WindowShellHost* host = nullptr;
    if (interface_name.length() == 0) host = external_host(bound_host);
    else {
        auto* candidate = host_object_backing(info.This());
        if (candidate == nullptr || candidate->trace_object_type() != "window_shell" ||
            !web_interface_inherits(candidate->prototype_key(), *interface_name)) {
            isolate->ThrowException(v8::Exception::TypeError(v8_string(isolate, "Illegal invocation").As<v8::String>())); return;
        }
        host = static_cast<WindowShellHost*>(candidate);
    }
    const auto sequence = access_trace_record(isolate, host, "window_shell", host->global_name().c_str(), *member, AccessOperation::kCall);
    AccessCallCompletion completion(isolate, sequence, info);
    info.GetReturnValue().Set(host->call_method(isolate, context, *member, info));
}

void namespace_getter(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto context = info.GetIsolate()->GetCurrentContext();
    auto data = info.Data().As<v8::Array>();
    v8::Local<v8::Value> owner, member;
    if (!data->Get(context, 0).ToLocal(&owner) || !data->Get(context, 1).ToLocal(&member)) return;
    v8::String::Utf8Value name(info.GetIsolate(), member);
    if (*name != nullptr) info.GetReturnValue().Set(external_host(owner)->get_property(info.GetIsolate(), context, *name));
}

bool install_methods(v8::Isolate* isolate, v8::Local<v8::Context> context,
    WindowShellHost* host, v8::Local<v8::Object> target) {
    for (const auto& method : host->specification().methods) {
        auto data = v8::Array::New(isolate, 3);
        if (!data->Set(context, 0, v8_string(isolate, host->prototype_key())).FromMaybe(false) ||
            !data->Set(context, 1, v8_string(isolate, method.name)).FromMaybe(false) ||
            !data->Set(context, 2, v8::External::New(isolate, host, v8::kExternalPointerTypeTagDefault)).FromMaybe(false)) return false;
        v8::Local<v8::Function> function;
        if (!make_web_method(isolate, context, method.name, method.length, &shell_method, data).ToLocal(&function) ||
            !target->DefineOwnProperty(context, v8_string(isolate, method.name).As<v8::String>(), function).FromMaybe(false)) return false;
    }
    return true;
}

bool install_shell(v8::Isolate* isolate, v8::Local<v8::Context> context,
    std::vector<std::unique_ptr<HostObject>>& owners, ShellSpec specification) {
    auto owner = std::make_unique<WindowShellHost>(std::move(specification));
    auto* host = owner.get(); const auto& spec = host->specification();
    owners.push_back(std::move(owner));
    v8::Local<v8::Object> prototype;
    if (!spec.namespace_object && !spec.interface_name.empty()) {
        prototype = web_interface_prototype(isolate, context, spec.interface_name, false);
        if (prototype.IsEmpty()) return false;
    }
    auto object = make_host_object(isolate, context, host);
    if (spec.namespace_object) {
        // ObjectTemplate may supply an intermediate template prototype. Web IDL
        // namespaces and the Chrome namespace directly inherit Object.prototype.
        if (!object->SetPrototype(context, v8::Object::New(isolate)->GetPrototype()).FromMaybe(false)) return false;
        if (spec.name != "chrome" && !object->DefineOwnProperty(context, v8::Symbol::GetToStringTag(isolate),
                v8_string(isolate, spec.name), static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum)).FromMaybe(false)) return false;
        for (const auto& property : spec.properties) {
            auto data = v8::Array::New(isolate, 2);
            if (!data->Set(context, 0, v8::External::New(isolate, host, v8::kExternalPointerTypeTagDefault)).FromMaybe(false) ||
                !data->Set(context, 1, v8_string(isolate, property)).FromMaybe(false)) return false;
            v8::Local<v8::Function> getter;
            if (!make_web_method(isolate, context, "get " + property, 0, &namespace_getter, data).ToLocal(&getter)) return false;
            v8::PropertyDescriptor descriptor(getter, v8::Undefined(isolate));
            descriptor.set_enumerable(true); descriptor.set_configurable(true);
            if (!object->DefineProperty(context, v8_string(isolate, property).As<v8::String>(), descriptor).FromMaybe(false)) return false;
        }
        if (spec.name == "chrome" && !object->CreateDataProperty(context, v8_string(isolate, "app").As<v8::String>(),
                host->get_property(isolate, context, "app")).FromMaybe(false)) return false;
    }
    if (!install_methods(isolate, context, host, spec.namespace_object ? object : prototype)) return false;
    host->set_global_value(isolate, spec.name == "fence" ? v8::Local<v8::Value>(v8::Null(isolate)) : v8::Local<v8::Value>(object));
    if (spec.namespace_object) return context->Global()->DefineOwnProperty(context, v8_string(isolate, spec.name).As<v8::String>(), object,
        spec.enumerable_global ? v8::None : v8::DontEnum).FromMaybe(false);
    auto data = v8::External::New(isolate, host, v8::kExternalPointerTypeTagDefault);
    v8::Local<v8::Function> getter, setter;
    if (!make_web_method(isolate, context, "get " + spec.name, 0, &global_shell_getter, data).ToLocal(&getter)) return false;
    if (spec.replaceable_global && !make_web_method(isolate, context, "set " + spec.name, 1, &global_shell_setter, data).ToLocal(&setter)) return false;
    v8::PropertyDescriptor descriptor(getter, setter.IsEmpty() ? v8::Local<v8::Value>(v8::Undefined(isolate)) : v8::Local<v8::Value>(setter));
    descriptor.set_enumerable(true); descriptor.set_configurable(true);
    return context->Global()->DefineProperty(context, v8_string(isolate, spec.name).As<v8::String>(), descriptor).FromMaybe(false);
}

bool constants(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Object> object, const std::vector<std::pair<std::string, double>>& values) {
    for (const auto& [name, value] : values)
        if (!object->DefineOwnProperty(context, v8_string(isolate, name).As<v8::String>(), v8::Number::New(isolate, value),
                static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete)).FromMaybe(false)) return false;
    return true;
}

void node_filter_call(const v8::FunctionCallbackInfo<v8::Value>& info) {
    info.GetIsolate()->ThrowException(v8::Exception::TypeError(v8_string(info.GetIsolate(), "Illegal constructor").As<v8::String>()));
}

}  // namespace

bool install_window_shell_objects(v8::Isolate* isolate, v8::Local<v8::Context> context,
    std::vector<std::unique_ptr<HostObject>>& owners) {
    ShellSpec css{"CSS", "", {}, {"highlights", "paintWorklet"}, {}, false, true, false};
    for (const char* name : {"Hz","Q","cap","ch","cm","cqb","cqh","cqi","cqmax","cqmin","cqw","deg","dpcm","dpi","dppx",
            "dvb","dvh","dvi","dvmax","dvmin","dvw","em","escape","ex","fr","grad","ic","in","kHz","lh","lvb","lvh","lvi",
            "lvmax","lvmin","lvw","mm","ms","number","pc","percent","pt","px","rad","rcap","rch","registerProperty","rem","rex",
            "ric","rlh","s","supports","svb","svh","svi","svmax","svmin","svw","turn","vb","vh","vi","vmax","vmin","vw","x"}) css.methods.push_back({name, 1});
    if (!install_shell(isolate, context, owners, std::move(css))) return false;
    const std::vector<std::pair<std::string, std::vector<std::pair<std::string, double>>>> gpu = {
        {"GPUBufferUsage", {{"MAP_READ",1},{"MAP_WRITE",2},{"COPY_SRC",4},{"COPY_DST",8},{"INDEX",16},{"VERTEX",32},{"UNIFORM",64},{"STORAGE",128},{"INDIRECT",256},{"QUERY_RESOLVE",512}}},
        {"GPUColorWrite", {{"RED",1},{"GREEN",2},{"BLUE",4},{"ALPHA",8},{"ALL",15}}},
        {"GPUMapMode", {{"READ",1},{"WRITE",2}}}, {"GPUShaderStage", {{"VERTEX",1},{"FRAGMENT",2},{"COMPUTE",4}}},
        {"GPUTextureUsage", {{"COPY_SRC",1},{"COPY_DST",2},{"TEXTURE_BINDING",4},{"STORAGE_BINDING",8},{"RENDER_ATTACHMENT",16},{"TRANSIENT_ATTACHMENT",32}}},
    };
    for (const auto& [name, values] : gpu) {
        if (!install_shell(isolate, context, owners, {name, "", {}, {}, {}, false, true, false})) return false;
        v8::Local<v8::Value> object;
        if (!context->Global()->Get(context, v8_string(isolate, name)).ToLocal(&object) || !object->IsObject() ||
            !constants(isolate, context, object.As<v8::Object>(), values)) return false;
    }
    v8::Local<v8::Function> node_filter;
    if (!make_web_method(isolate, context, "NodeFilter", 0, &node_filter_call).ToLocal(&node_filter) ||
        !constants(isolate, context, node_filter, {{"FILTER_ACCEPT",1},{"FILTER_REJECT",2},{"FILTER_SKIP",3},{"SHOW_ALL",4294967295.0},
            {"SHOW_ELEMENT",1},{"SHOW_ATTRIBUTE",2},{"SHOW_TEXT",4},{"SHOW_CDATA_SECTION",8},{"SHOW_ENTITY_REFERENCE",16},{"SHOW_ENTITY",32},
            {"SHOW_PROCESSING_INSTRUCTION",64},{"SHOW_COMMENT",128},{"SHOW_DOCUMENT",256},{"SHOW_DOCUMENT_TYPE",512},
            {"SHOW_DOCUMENT_FRAGMENT",1024},{"SHOW_NOTATION",2048}}) ||
        !context->Global()->DefineOwnProperty(context, v8_string(isolate, "NodeFilter").As<v8::String>(), node_filter, v8::DontEnum).FromMaybe(false)) return false;
    const std::vector<ShellSpec> specifications = {
        {"chrome","",{{"csi",0},{"loadTimes",0}},{},{},false,true},
        {"external","External",{{"AddSearchProvider",0},{"IsSearchProviderInstalled",0}}},
        {"caches","CacheStorage",{{"delete",1,true},{"has",1,true},{"keys",0,true},{"match",1,true},{"open",1,true}}},
        {"cookieStore","CookieStore",{{"delete",1,true},{"get",0,true},{"getAll",0,true},{"set",1,true}},{},{"onchange"},true},
        {"indexedDB","IDBFactory",{{"cmp",2},{"databases",0,true},{"deleteDatabase",1},{"open",1}}},
        {"navigation","Navigation",{{"back",0},{"entries",0},{"forward",0},{"navigate",1},{"reload",0},{"traverseTo",1},{"updateCurrentEntry",1}},
            {"activation","canGoBack","canGoForward","currentEntry","transition"},{"oncurrententrychange","onnavigate","onnavigateerror","onnavigatesuccess"},true},
        {"documentPictureInPicture","DocumentPictureInPicture",{{"requestWindow",0,true}},{"window"},{"onenter"},true},
        {"fence",""}, {"launchQueue","LaunchQueue",{{"setConsumer",1}}},
        {"speechSynthesis","SpeechSynthesis",{{"cancel",0},{"getVoices",0},{"pause",0},{"resume",0},{"speak",1}},
            {"paused","pending","speaking"},{"onvoiceschanged"},true},
        {"styleMedia","StyleMedia",{{"matchMedium",0}},{"type"}},
        {"trustedTypes","TrustedTypePolicyFactory",{{"createPolicy",1},{"getAttributeType",2},{"getPropertyType",2},{"getTypeMapping",0},{"isHTML",1},{"isScript",1},{"isScriptURL",1}},
            {"defaultPolicy","emptyHTML","emptyScript"}},
        {"viewport","Viewport",{},{"segments"},{},false,false,true,true},
        {"customElements","CustomElementRegistry",{{"define",2},{"get",1},{"getName",1},{"initialize",1},{"upgrade",1},{"whenDefined",1,true}}},
        {"crashReport","CrashReportContext",{{"delete",1},{"initialize",1},{"set",2}}},
    };
    for (const auto& specification : specifications)
        if (!install_shell(isolate, context, owners, specification)) return false;
    return true;
}

}  // namespace iv8::browser
