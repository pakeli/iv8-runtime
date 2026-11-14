#include "document_host.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstddef>
#include <cstdlib>
#include <iterator>
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

std::string reflected_element_attribute(const DomNode* node, const std::string& name) {
    if (node->tag == "meta") {
        if (name == "httpEquiv") return "http-equiv";
        if (name == "content" || name == "name" || name == "scheme") return name;
    }
    if (node->tag == "script" && (name == "src" || name == "type")) return name;
    return {};
}

v8::Local<v8::Value> ElementHost::get_property(v8::Isolate* isolate,
                                               v8::Local<v8::Context> context,
                                               const std::string& name) {
    if (name == "baseURI") return document_->get_property(isolate, context, "baseURI");
    if (node_->tag == "canvas" && (name == "width" || name == "height")) {
        const auto found = node_->attributes.find(name);
        long value = name == "width" ? 300 : 150;
        if (found != node_->attributes.end()) {
            char* end = nullptr;
            const long parsed = std::strtol(found->second.c_str(), &end, 10);
            if (end != found->second.c_str() && parsed >= 0 && parsed <= 2147483647) value = parsed;
        }
        return v8::Integer::New(isolate, static_cast<int>(value));
    }

    if (name == "style") return make_inline_style(isolate, context,
        document_->wrap_node(isolate, context, node_));
    if (name == "innerText" || (node_->tag == "script" && name == "text"))
        return v8_string(isolate, text_content_of(node_));
    const auto reflected = reflected_element_attribute(node_, name);
    if (!reflected.empty()) {
        const auto found = node_->attributes.find(reflected);
        if (found == node_->attributes.end()) return v8_string(isolate, "");
        if (node_->tag == "script" && name == "src") {
            const auto url = resolve_dom_url(found->second, document_->base_url());
            return v8_string(isolate, url.valid ? url.href : found->second);
        }
        return v8_string(isolate, found->second);
    }
    if ((node_->tag == "a" || node_->tag == "area") &&
        (name == "href" || name == "protocol" || name == "host" ||
         name == "hostname" || name == "port" || name == "pathname" ||
         name == "search" || name == "hash" || name == "origin")) {
        const auto found = node_->attributes.find("href");
        const auto url = found == node_->attributes.end() ? DomUrl{} :
            resolve_dom_url(found->second, document_->base_url());
        std::string value;
        if (name == "href") value = url.valid ? url.href :
            (found == node_->attributes.end() ? "" : found->second);
        else if (name == "protocol") value = url.valid ? url.protocol : ":";
        else if (name == "host") value = url.host;
        else if (name == "hostname") value = url.hostname;
        else if (name == "port") value = url.port;
        else if (name == "pathname") value = url.pathname;
        else if (name == "search") value = url.search;
        else if (name == "hash") value = url.hash;
        else if (name == "origin") value = url.origin;
        return v8_string(isolate, value);
    }

    if (name == "tagName" || name == "nodeName") {
        return v8_string(isolate, html_document_ ? ascii_upper(node_->tag) : node_->tag);
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
                                                       : text_content_of(node_));
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
                   : document_->wrap_node(isolate, context,
                                             node_->children.front());
    }
    if (name == "lastChild") {
        return node_->children.empty()
                   ? v8::Null(isolate)
                   : document_->wrap_node(isolate, context,
                                             node_->children.back());
    }
    if (name == "previousSibling" || name == "nextSibling") {
        DomNode* previous = nullptr;
        DomNode* next = nullptr;
        siblings_of(node_, &previous, &next);
        return document_->wrap_node(isolate, context,
                                       name == "previousSibling" ? previous
                                                                 : next);
    }
    if (name == "textContent") {
        return v8_string(isolate, text_content_of(node_));
    }
    if (name == "innerHTML") {
        return v8_string(isolate, serialize_dom_children(node_));
    }
    if (name == "outerHTML") {
        return v8_string(isolate, serialize_dom_node(node_));
    }
    if (name == "nodeValue") return v8::Null(isolate);  // Element nodes have no value
    if (name == "parentNode") {
        return document_->wrap_node(isolate, context, node_->parent);
    }
    // M11-48: childNodes includes every node kind; children filters elements.
    if (name == "childNodes") {
        return document_->nodes_array(isolate, context, node_->children);
    }
    if (name == "children") {
        return document_->elements_array(isolate, context,
                                         element_children_of(node_));
    }
    // M4-A-6 connectivity / element-sibling navigation (based on the live tree).
    if (name == "ownerDocument") {
        // The current generation's `document` — the SAME JS object installed as
        // the global, so `el.ownerDocument === document` holds.
        return document_->document_global(isolate, context);
    }
    if (name == "isConnected") {
        return v8::Boolean::New(isolate, document_->is_in_tree(node_));
    }
    if (name == "previousElementSibling" || name == "nextElementSibling") {
        return document_->wrap_node(
            isolate, context,
            element_sibling_node(node_, name == "previousElementSibling" ? -1
                                                                        : 1));
    }
    // M11-48 structural navigation over all node kinds: parentElement is null
    // when the parent is not an element; the *ElementChild/Count and element
    // sibling variants skip Text/Comment nodes.
    if (name == "parentElement") {
        return document_->wrap_node(
            isolate, context, is_element_node(node_->parent) ? node_->parent
                                                             : nullptr);
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
                return document_->wrap_node(isolate, context, ancestor);
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
        return v8_string(isolate, text_content_of(node_));
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

void ElementHost::set_property(v8::Isolate* isolate, v8::Local<v8::Context> context,
                               const std::string& name, v8::Local<v8::Value> value) {
    if (name == "nodeValue") return;
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
    if (name == "style") {
        auto style = make_inline_style(isolate, context, document_->wrap_node(isolate, context, node_));
        if (style->IsObject()) (void)style.As<v8::Object>()->Set(context,
            v8::String::NewFromUtf8Literal(isolate, "cssText"), value);
        return;
    }
    if (node_->tag == "canvas" && (name == "width" || name == "height")) {
        auto converted = value->Uint32Value(context);
        if (!converted.IsNothing()) reflect_raw_string(name, std::to_string(converted.FromJust()));
        return;
    }
    if (name == "innerText" || (node_->tag == "script" && name == "text")) {
        std::string text;
        if (to_reflected_string(text)) document_->set_text_content(isolate, context, node_, text);
        return;
    }
    const auto reflected = reflected_element_attribute(node_, name);
    if (!reflected.empty()) {
        std::string text;
        if (to_reflected_string(text)) reflect_raw_string(reflected, text);
        return;
    }
    if ((node_->tag == "a" || node_->tag == "area") &&
        (name == "href" || name == "protocol" || name == "host" ||
         name == "hostname" || name == "port" || name == "pathname" ||
         name == "search" || name == "hash")) {
        std::string text;
        if (!to_reflected_string(text)) return;
        if (name == "href") reflect_raw_string("href", text);
        else {
            const auto found = node_->attributes.find("href");
            if (found == node_->attributes.end()) return;
            const auto current = resolve_dom_url(found->second, document_->base_url());
            const auto changed = set_dom_url_component(current, name, text);
            if (changed.valid) reflect_raw_string("href", changed.href);
        }
        return;
    }
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
    if (name != "textContent" && name != "innerHTML" &&
        name != "outerHTML" && name != "value" && name != "method" &&
        name != "action" && name != "enctype" && name != "target") {
        return;  // textContent/innerHTML/outerHTML + value + form slots
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
    // M11-49: innerHTML/outerHTML parse an HTML fragment and replace the target /
    // the element itself. Parsed scripts stay inert (their ScriptRecords are
    // discarded).
    if (name == "innerHTML") {
        std::vector<DomNode*> roots = document_->parse_html_fragment(str_value);
        document_->replace_all_children(isolate, context, node_, roots);
        return;
    }
    if (name == "outerHTML") {
        if (node_->parent != nullptr && node_->parent->kind == NodeKind::Document) {
            isolate->ThrowException(dom_exception(isolate, "NoModificationAllowedError", "Cannot replace the document element using outerHTML"));
            return;
        }
        DomNode* parent = node_->parent;
        if (parent == nullptr) {
            return;  // parentless element: no-op, as in the DOM
        }
        std::vector<DomNode*> roots = document_->parse_html_fragment(str_value);
        DomNode* fragment = document_->make_detached_fragment();
        for (DomNode* root : roots) {
            root->parent = fragment;
            fragment->children.push_back(root);
        }
        std::string error;
        if (!document_->replace_dom_node(isolate, context, parent, fragment,
                                         node_, &error)) {
            isolate->ThrowException(v8::Exception::TypeError(
                v8_string(isolate, error).As<v8::String>()));
        }
        return;
    }
    // M2-8/M11-48: textContent = ... replaces all children with zero or one Text
    // node (shared helper also used by fragments and document.title).
    document_->set_text_content(isolate, context, node_, str_value);
}

}  // namespace iv8::browser
