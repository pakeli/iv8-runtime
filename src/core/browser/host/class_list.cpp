#include "class_list.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "iv8/web_method.h"
#include "../dom/dom_query.h"
#include "../dom/selector_match.h"
#include "../internal/trace_hooks.h"
#include "document_host.h"

namespace iv8::browser {

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

}  // namespace iv8::browser
