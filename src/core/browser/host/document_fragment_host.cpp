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

v8::Local<v8::Value> DocumentFragmentHost::get_property(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const std::string& name) {
    if (name == "baseURI") return document_->get_property(isolate, context, "baseURI");
    if (name == "nodeType") {
        return v8::Integer::New(isolate,
                                static_cast<int>(NodeKind::DocumentFragment));
    }
    if (name == "nodeName") return v8_string(isolate, "#document-fragment");
    if (name == "nodeValue") return v8::Null(isolate);
    if (name == "textContent") {
        return v8_string(isolate, text_content_of(node_));
    }
    if (name == "childNodes") {
        return document_->nodes_array(isolate, context, node_->children);
    }
    if (name == "children") {
        return document_->elements_array(isolate, context,
                                         element_children_of(node_));
    }
    if (name == "parentNode" || name == "parentElement" ||
        name == "previousSibling" || name == "nextSibling") {
        return v8::Null(isolate);
    }
    if (name == "firstChild" || name == "lastChild") {
        if (node_->children.empty()) {
            return v8::Null(isolate);
        }
        return document_->wrap_node(isolate, context,
                                    name == "firstChild"
                                        ? node_->children.front()
                                        : node_->children.back());
    }
    if (name == "firstElementChild" || name == "lastElementChild") {
        const std::vector<DomNode*> children = element_children_of(node_);
        if (children.empty()) {
            return v8::Null(isolate);
        }
        return document_->wrap_node(isolate, context,
                                    name == "firstElementChild"
                                        ? children.front()
                                        : children.back());
    }
    if (name == "childElementCount") {
        return v8::Integer::New(
            isolate,
            static_cast<std::int32_t>(element_children_of(node_).size()));
    }
    if (name == "ownerDocument") {
        return document_->document_global(isolate, context);
    }
    if (name == "isConnected") {
        return v8::Boolean::New(isolate, document_->is_in_tree(node_));
    }
    return v8::Undefined(isolate);
}

void DocumentFragmentHost::set_property(v8::Isolate* isolate,
                                        v8::Local<v8::Context> context,
                                        const std::string& name,
                                        v8::Local<v8::Value> value) {
    if (name != "textContent") {
        return;
    }
    v8::Local<v8::String> str;
    if (!value->ToString(context).ToLocal(&str)) {
        return;
    }
    v8::String::Utf8Value utf8(isolate, str);
    document_->set_text_content(
        isolate, context, node_,
        *utf8 != nullptr
            ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
            : std::string());
}

v8::Local<v8::Value> DocumentFragmentHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Local<v8::Value> result;
    if (handle_event_method(isolate, context, name, args, result)) return result;
    if (document_->call_node_method(isolate, context, node_, name, args,
                                    result)) {
        return result;
    }
    // M11-49 ParentNode methods on fragments.
    if (name == "append" || name == "prepend" || name == "replaceChildren") {
        return document_->call_mutation_method(isolate, context, node_, name,
                                               args);
    }
    const auto arg_string = [&](int index) {
        std::string text;
        if (args.Length() > index) {
            v8::String::Utf8Value utf8(isolate, args[index]);
            if (*utf8 != nullptr) {
                text.assign(*utf8, static_cast<std::size_t>(utf8.length()));
            }
        }
        return text;
    };
    if (name == "querySelector") {
        return document_->wrap_node(
            isolate, context,
            find_first(node_->children, selector_predicate(arg_string(0))));
    }
    if (name == "querySelectorAll") {
        std::vector<DomNode*> out;
        collect_matching(node_, selector_predicate(arg_string(0)), out);
        return document_->elements_array(isolate, context, out);
    }
    if (name == "getElementById") {
        const std::string id = arg_string(0);
        if (id.empty()) {
            return v8::Null(isolate);
        }
        return document_->wrap_node(
            isolate, context,
            find_first(node_->children, [&id](const DomNode* n) {
                return is_element_node(n) && n->has_id && n->id == id;
            }));
    }
    return v8::Undefined(isolate);
}

}  // namespace iv8::browser
