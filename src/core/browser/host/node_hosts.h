#pragma once

#include <string>
#include <utility>
#include <vector>

#include "iv8/browser_compatibility.h"
#include "../dom/dom_node.h"
#include "../dom/form_controls.h"
#include "event_target.h"

namespace iv8::browser {

class DocumentHost;

class NodeHost : public EventTargetHost {
public:
    NodeHost(DomNode* node, DocumentHost* document)
        : node_(node), document_(document) {}

    bool is_node() const override { return true; }
    NodeKind node_kind() const override { return node_->kind; }
    DomNode* node() const { return node_; }
    DocumentHost* document() const { return document_; }

    // Recover the backing DomNode* from any node-host argument, or nullptr.
    static DomNode* node_of_arg(v8::Local<v8::Value> value) {
        HostObject* host = host_object_backing(value);
        if (host == nullptr || !host->is_node()) {
            return nullptr;
        }
        return static_cast<NodeHost*>(host)->node_;
    }

    // M4-A-7: bubble a dispatched event along the current tree (defined out-of-line
    // below DocumentHost, which owns the bubbling walk).
    void dispatch_event(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> event,
                        v8::Local<v8::Object> receiver) override;

protected:
    DomNode* node_;            // owned by document_ (lives for the page generation)
    DocumentHost* document_;
};


class ElementHost : public NodeHost {
public:
    ElementHost(DomNode* node, DocumentHost* document, bool html_document = true)
        : NodeHost(node, document), html_document_(html_document) {}

    std::string global_name() const override { return std::string(); }  // never global
    std::string trace_object_type() const override { return "element"; }
    std::string trace_label() const override { return node_->tag; }
    std::string prototype_key() const override { return html_document_ ? html_element_interface(node_->tag) : "Element"; }
    bool has_named_properties() const override {
        return html_document_ && node_->tag == "form";
    }
    std::vector<std::string> named_property_names() const override;
    v8::MaybeLocal<v8::Value> named_property(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name) override;
    std::vector<DomNode*> named_controls() const;
    void prune_past_named_controls();

    std::vector<std::string> property_names() const override {
        std::vector<std::string> names = {
            "baseURI", "tagName",     "nodeName",   "nodeType",   "nodeValue", "id",
            "className",   "textContent", "innerHTML",  "outerHTML",
            "parentNode",  "childNodes",  "children",
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
        if (!html_document_) return names;
        names.push_back("innerText");
        names.push_back("style");
        if (node_->tag == "canvas") names.insert(names.end(), {"width", "height"});
        if (node_->tag == "meta") {
            names.insert(names.end(), {"content", "name", "httpEquiv", "scheme"});
        }
        if (node_->tag == "script") {
            names.insert(names.end(), {"src", "type", "text"});
        }
        if (node_->tag == "a" || node_->tag == "area") {
            names.insert(names.end(), {"href", "protocol", "host", "hostname",
                "port", "pathname", "search", "hash", "origin"});
        }
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
            // M11-49 variadic tree methods / adjacent insertion.
            "append",           "prepend",         "replaceChildren",
            "before",           "after",           "replaceWith",
            "insertAdjacentHTML", "insertAdjacentElement",
            "insertAdjacentText",
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
        if (html_document_ && node_->tag == "canvas") names.push_back("getContext");
        // M6-1: reset() is exposed only on <form>; M7-1: submit(); M7-2: requestSubmit().
        if (html_document_ && node_->tag == "form") {
            names.push_back("reset");
            names.push_back("submit");
            names.push_back("requestSubmit");
            // M11-32: form-level constraint-validation entry points.
            names.push_back("checkValidity");
            names.push_back("reportValidity");
        }
        if (html_document_ && is_form_control(node_)) {
            // M11-32: control-level constraint-validation entry points.
            names.push_back("checkValidity");
            names.push_back("reportValidity");
            names.push_back("setCustomValidity");
        }
        if (html_document_ && node_->tag == "select") {
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
        std::vector<std::string> names = {"textContent", "innerHTML",
                                          "outerHTML", "id", "className"};
        if (!html_document_) return names;
        names.push_back("innerText");
        names.push_back("style");
        if (node_->tag == "canvas") names.insert(names.end(), {"width", "height"});
        if (node_->tag == "meta") {
            names.insert(names.end(), {"content", "name", "httpEquiv", "scheme"});
        }
        if (node_->tag == "script") names.insert(names.end(), {"src", "type", "text"});
        if (node_->tag == "a" || node_->tag == "area") {
            names.insert(names.end(), {"href", "protocol", "host", "hostname",
                "port", "pathname", "search", "hash"});
        }
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


private:
    // Successful single-control lookups remember names in access order. Backing
    // nodes remain owned by the DocumentHost for this complete Page generation.
    std::vector<std::pair<std::string, DomNode*>> past_named_controls_;
    bool html_document_ = true;
    // M4-A-3/M11-48: shared node-argument helper lives in NodeHost::node_of_arg.
    DomNode* node_of_arg(v8::Local<v8::Value> value) {
        return NodeHost::node_of_arg(value);
    }
};


class CharacterDataHost : public NodeHost {
public:
    CharacterDataHost(DomNode* node, DocumentHost* document)
        : NodeHost(node, document) {}

    std::string global_name() const override { return std::string(); }
    std::string prototype_key() const override {
        return node_->kind == NodeKind::Text ? "Text" : "Comment";
    }
    std::string trace_object_type() const override {
        return node_->kind == NodeKind::Text ? "text" : "comment";
    }
    std::string trace_label() const override {
        return node_->kind == NodeKind::Text ? "#text" : "#comment";
    }
    std::vector<std::string> property_names() const override {
        return {"baseURI", "nodeType",       "nodeName",     "nodeValue",
                "textContent",    "data",         "length",
                "parentNode",     "parentElement", "previousSibling",
                "nextSibling",    "childNodes",   "firstChild",
                "lastChild",      "ownerDocument", "isConnected"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"nodeValue", "textContent", "data"};
    }
    std::vector<std::string> method_names() const override {
        return {"remove",     "cloneNode", "isSameNode", "isEqualNode",
                "contains",   "hasChildNodes", "before", "after",
                "replaceWith"};
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
};


class DocumentFragmentHost : public NodeHost {
public:
    DocumentFragmentHost(DomNode* node, DocumentHost* document)
        : NodeHost(node, document) {}

    std::string global_name() const override { return std::string(); }
    std::string prototype_key() const override { return "DocumentFragment"; }
    std::string trace_object_type() const override { return "document_fragment"; }
    std::string trace_label() const override { return "#document-fragment"; }
    std::vector<std::string> property_names() const override {
        return {"baseURI", "nodeType",        "nodeName",        "nodeValue",
                "textContent",     "childNodes",      "children",
                "parentNode",      "parentElement",   "firstChild",
                "lastChild",       "firstElementChild", "lastElementChild",
                "childElementCount", "previousSibling", "nextSibling",
                "ownerDocument",   "isConnected"};
    }
    std::vector<std::string> writable_property_names() const override {
        return {"textContent"};
    }
    std::vector<std::string> method_names() const override {
        return {"appendChild",      "removeChild",    "insertBefore",
                "replaceChild",     "remove",         "hasChildNodes",
                "cloneNode",        "isSameNode",     "isEqualNode",
                "contains",         "append",         "prepend",
                "replaceChildren",  "querySelector",  "querySelectorAll",
                "getElementById"};
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
};

}  // namespace iv8::browser
