#include "document_host.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "iv8/access_summary.h"
#include "iv8/dom_url.h"
#include "iv8/query_parameters.h"
#include "iv8/url_parser.h"
#include "../dom/dom_query.h"
#include "../dom/form_controls.h"
#include "../dom/html_serializer.h"
#include "../dom/selector_match.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {

v8::Local<v8::Value> CharacterDataHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const std::string& name) {
    if (name == "baseURI") return document_->get_property(isolate, context, "baseURI");
    if (name == "nodeType") {
        return v8::Integer::New(isolate, static_cast<int>(node_->kind));
    }
    if (name == "nodeName") {
        return v8_string(isolate, node_->kind == NodeKind::Text ? "#text"
                                                               : "#comment");
    }
    if (name == "nodeValue" || name == "textContent" || name == "data") {
        return v8_string(isolate, node_->data);
    }
    if (name == "length") {
        return v8::Integer::New(
            isolate, static_cast<std::int32_t>(utf16_length(node_->data)));
    }
    if (name == "parentNode") {
        return document_->wrap_node(isolate, context, node_->parent);
    }
    if (name == "parentElement") {
        return document_->wrap_node(
            isolate, context, is_element_node(node_->parent) ? node_->parent
                                                             : nullptr);
    }
    if (name == "previousSibling" || name == "nextSibling") {
        return document_->wrap_node(
            isolate, context,
            sibling_node(node_, name == "previousSibling" ? -1 : 1));
    }
    if (name == "childNodes") {
        return v8::Array::New(isolate, 0);  // character data has no children
    }
    if (name == "firstChild" || name == "lastChild") {
        return v8::Null(isolate);
    }
    if (name == "ownerDocument") {
        return document_->document_global(isolate, context);
    }
    if (name == "isConnected") {
        return v8::Boolean::New(isolate, document_->is_in_tree(node_));
    }
    return v8::Undefined(isolate);
}

void CharacterDataHost::set_property(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     const std::string& name,
                                     v8::Local<v8::Value> value) {
    if (name != "nodeValue" && name != "textContent" && name != "data") {
        return;
    }
    v8::Local<v8::String> str;
    if (!value->ToString(context).ToLocal(&str)) {
        return;
    }
    v8::String::Utf8Value utf8(isolate, str);
    const std::string text =
        *utf8 != nullptr
            ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
            : std::string();
    const std::string old_value = node_->data;
    node_->data = text;
    document_->queue_character_data(isolate, context, node_, old_value);
}

v8::Local<v8::Value> CharacterDataHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Local<v8::Value> result;
    if (handle_event_method(isolate, context, name, args, result)) return result;
    if (document_->call_node_method(isolate, context, node_, name, args,
                                    result)) {
        return result;
    }
    // M11-49 ChildNode methods on Text/Comment.
    if (name == "before" || name == "after" || name == "replaceWith") {
        return document_->call_mutation_method(isolate, context, node_, name,
                                               args);
    }
    return v8::Undefined(isolate);
}

}  // namespace iv8::browser
