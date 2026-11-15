#include "document_host.h"

#include "attribute_hosts.h"
#include "../dom/dom_query.h"
#include "../internal/common.h"

namespace iv8::browser {

bool DocumentHost::call_namespace_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, DomNode* target, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& args, v8::Local<v8::Value>& out) {
    if (name != "createElementNS" && name != "getElementsByTagNameNS") return false;
    out = v8::Undefined(isolate);
    if (args.Length() < 2) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Namespace operation requires two arguments")));
        return true;
    }
    std::string namespace_uri;
    if (!args[0]->IsNullOrUndefined()) {
        v8::Local<v8::String> value;
        if (!args[0]->ToString(context).ToLocal(&value)) return true;
        v8::String::Utf8Value text(isolate, value);
        if (*text == nullptr) return true;
        namespace_uri.assign(*text, static_cast<std::size_t>(text.length()));
    }
    v8::Local<v8::String> name_value;
    if (!args[1]->ToString(context).ToLocal(&name_value)) return true;
    v8::String::Utf8Value name_text(isolate, name_value);
    if (*name_text == nullptr) return true;
    const std::string requested(*name_text, static_cast<std::size_t>(name_text.length()));
    if (name == "createElementNS") {
        DOMNamespaceName validated;
        if (!validate_namespace_name(isolate, namespace_uri, requested, &validated)) return true;
        auto node = std::make_unique<DomNode>();
        node->tag = validated.qualified_name;
        node->namespace_uri = validated.namespace_uri;
        node->prefix = validated.prefix;
        node->local_name = validated.local_name;
        out = wrap_created_node(isolate, context, std::move(node));
        return true;
    }
    out = live_collection(isolate, context,
        target->kind == NodeKind::Document ? nullptr : target,
        "tag-ns", requested, false, namespace_uri);
    return true;
}

}  // namespace iv8::browser
