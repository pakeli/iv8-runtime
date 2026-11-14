#include "document_host.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <cstddef>
#include <functional>
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

void ElementHost::fire_invalid_event(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> receiver) {
    v8::Local<v8::Object> event = v8::Object::New(isolate);
    (void)event->Set(context, v8_string(isolate, "type"),
                     v8_string(isolate, "invalid"));
    (void)event->Set(context, v8_string(isolate, "bubbles"),
                     v8::Boolean::New(isolate, false));
    (void)event->Set(context, v8_string(isolate, "cancelable"),
                     v8::Boolean::New(isolate, true));
    (void)event->Set(context, v8_string(isolate, "defaultPrevented"),
                     v8::Boolean::New(isolate, false));
    (void)event->Set(context, v8_string(isolate, "eventPhase"),
                     v8::Integer::New(isolate, 0));
    (void)event->Set(context, v8_string(isolate, "target"), receiver);
    (void)event->Set(context, v8_string(isolate, "currentTarget"),
                     v8::Null(isolate));
    fire_target_at(isolate, context, event, receiver);
    finish_dispatch(isolate, context, event);
}

bool ElementHost::validate_control(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   v8::Local<v8::Object> receiver) {
    const ValidityInfo info = compute_validity(node_);
    if (info.valid()) {
        return true;
    }
    fire_invalid_event(isolate, context, receiver);
    return false;
}

bool ElementHost::validate_form(v8::Isolate* isolate,
                                v8::Local<v8::Context> context) {
    std::vector<DomNode*> controls;
    collect_matching(node_,
                     [](const DomNode* n) { return is_form_control(n); },
                     controls);
    bool valid = true;
    for (DomNode* control : controls) {
        if (!control_will_validate(control)) {
            continue;  // barred controls are skipped and never receive invalid
        }
        ElementHost* host = document_->ensure_element_host(control);
        v8::Local<v8::Object> receiver =
            document_->wrap_node(isolate, context, control).As<v8::Object>();
        if (!host->validate_control(isolate, context, receiver)) {
            valid = false;
        }
    }
    return valid;
}

v8::Local<v8::Value> ElementHost::call_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& args) {
    // M9-2: record the "<tag>.<method>" call before dispatch (covers event methods
    // handled just below too, e.g. "script.getAttribute").
    watch_record(isolate, node_->tag, name);
    // M3-3: addEventListener / removeEventListener / dispatchEvent first.
    v8::Local<v8::Value> event_result;
    if (handle_event_method(isolate, context, name, args, event_result)) {
        return event_result;
    }
    // M11-48: shared Node methods (remove, clone, equality, contains, and all
    // tree edits) accept every node kind, including DocumentFragment insertion.
    v8::Local<v8::Value> node_result;
    if (document_->call_node_method(isolate, context, node_, name, args,
                                    node_result)) {
        return node_result;
    }
    // M11-49: variadic tree methods and Element.insertAdjacent*.
    if (name == "append" || name == "prepend" || name == "replaceChildren" ||
        name == "before" || name == "after" || name == "replaceWith") {
        return document_->call_mutation_method(isolate, context, node_, name,
                                               args);
    }
    if (name == "insertAdjacentHTML" || name == "insertAdjacentElement" ||
        name == "insertAdjacentText") {
        return document_->call_insert_adjacent(isolate, context, node_, name,
                                               args);
    }
    if (node_->tag == "canvas" && name == "getContext") {
        if (args.Length() == 0) {
            isolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8Literal(isolate, "A context identifier is required")));
            return v8::Undefined(isolate);
        }
        v8::Local<v8::String> identifier;
        if (!args[0]->ToString(context).ToLocal(&identifier)) return v8::Undefined(isolate);
        // Context allocation is explicitly unavailable in this headless runtime.
        return v8::Null(isolate);
    }
    // M11-32: checkValidity()/reportValidity() on a form aggregates its controls;
    // on a control they validate just that control. reportValidity has no UI in
    // this headless runtime, so it is identical to checkValidity.
    if (name == "checkValidity" || name == "reportValidity") {
        if (node_->tag == "form") {
            return v8::Boolean::New(isolate, validate_form(isolate, context));
        }
        if (is_form_control(node_)) {
            return v8::Boolean::New(
                isolate, validate_control(isolate, context, args.This()));
        }
        return v8::Undefined(isolate);
    }
    // M11-32: setCustomValidity(message) — String coercion; empty clears the
    // custom error. The message is a runtime slot (no attribute write).
    if (name == "setCustomValidity" && is_form_control(node_)) {
        std::string message;
        if (args.Length() > 0) {
            v8::Local<v8::String> str;
            if (args[0]->ToString(context).ToLocal(&str)) {
                v8::String::Utf8Value utf8(isolate, str);
                if (*utf8 != nullptr) {
                    message.assign(*utf8,
                                   static_cast<std::size_t>(utf8.length()));
                }
            }
        }
        node_->custom_validity = message;
        return v8::Undefined(isolate);
    }
    const auto arg_string = [&](int index) -> std::string {
        if (index >= args.Length()) return std::string();
        v8::String::Utf8Value utf8(isolate, args[index]);
        return *utf8 != nullptr
                   ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
                   : std::string();
    };
    // M3-8: getAttribute / hasAttribute read any attribute parsed from the HTML.
    // id / class route to their dedicated fields (so they stay consistent with
    // .id / .className / setAttribute); every other name is looked up in the raw
    // attribute table. Name match is ASCII case-insensitive. Missing -> null /
    // false; a valueless/boolean attribute reads "" (and hasAttribute -> true).
    // M11-12: attribute presence / name enumeration.
    if (name == "hasAttributes") {
        return v8::Boolean::New(
            isolate, node_->has_id || node_->has_class ||
                         !node_->attributes.empty());
    }
    if (name == "getAttributeNames") {
        std::vector<std::string> names;
        if (node_->has_id) names.push_back("id");
        if (node_->has_class) names.push_back("class");
        std::vector<std::string> raw;
        raw.reserve(node_->attributes.size());
        for (const auto& entry : node_->attributes) {
            raw.push_back(entry.first);
        }
        std::sort(raw.begin(), raw.end());
        names.insert(names.end(), raw.begin(), raw.end());
        v8::Local<v8::Array> array =
            v8::Array::New(isolate, static_cast<int>(names.size()));
        for (std::size_t i = 0; i < names.size(); i++) {
            (void)array->Set(context, static_cast<std::uint32_t>(i),
                             v8_string(isolate, names[i]));
        }
        return array;
    }
    if (name == "getAttribute" || name == "hasAttribute") {
        const std::string key = ascii_lower(arg_string(0));
        bool present = false;
        const std::string* value = nullptr;
        if (key == "id") {
            present = node_->has_id;
            value = &node_->id;
        } else if (key == "class") {
            present = node_->has_class;
            value = &node_->class_name;
        } else {
            const auto it = node_->attributes.find(key);
            if (it != node_->attributes.end()) {
                present = true;
                value = &it->second;
            }
        }
        if (name == "hasAttribute") {
            return v8::Boolean::New(isolate, present);
        }
        return present ? v8_string(isolate, *value) : v8::Null(isolate);
    }
    // M4-A-4 setAttribute(name, value): write ANY attribute (name lowercased,
    // value = String(value)). id/class keep their dedicated fields (so .id /
    // .className / id/class-based queries stay consistent); every other name goes
    // in the raw attribute table (M3-8). Not a full attribute system (no order/
    // serialization / reflection).
    if (name == "setAttribute") {
        const std::string key = ascii_lower(arg_string(0));
        const std::string val = arg_string(1);
        // M11-5: capture the pre-write value for a MutationObserver record.
        bool has_old_value = false;
        std::string old_value;
        if (key == "id") {
            has_old_value = node_->has_id;
            old_value = node_->id;
        } else if (key == "class") {
            has_old_value = node_->has_class;
            old_value = node_->class_name;
        } else {
            const auto old = node_->attributes.find(key);
            if (old != node_->attributes.end()) {
                has_old_value = true;
                old_value = old->second;
            }
        }
        if (key == "id") {
            node_->id = val;
            node_->has_id = true;
        } else if (key == "class") {
            node_->class_name = val;
            node_->has_class = true;
            node_->classes = split_class_tokens(val);
        } else {
            node_->attributes[key] = val;
        }
        // setAttribute records even when the value is unchanged.
        document_->queue_attribute(isolate, context, node_, key, has_old_value,
                                   old_value);
        return v8::Undefined(isolate);
    }
    // M4-A-4 removeAttribute(name): remove the attribute if present, else no-op.
    // id/class clear their dedicated fields (queries update); other names are
    // erased from the raw attribute table. Returns undefined.
    if (name == "removeAttribute") {
        const std::string key = ascii_lower(arg_string(0));
        // M11-5: only an actually-present attribute produces a record.
        bool has_old_value = false;
        std::string old_value;
        if (key == "id") {
            has_old_value = node_->has_id;
            old_value = node_->id;
        } else if (key == "class") {
            has_old_value = node_->has_class;
            old_value = node_->class_name;
        } else {
            const auto old = node_->attributes.find(key);
            if (old != node_->attributes.end()) {
                has_old_value = true;
                old_value = old->second;
            }
        }
        if (key == "id") {
            node_->id.clear();
            node_->has_id = false;
        } else if (key == "class") {
            node_->class_name.clear();
            node_->has_class = false;
            node_->classes.clear();
        } else {
            node_->attributes.erase(key);
        }
        if (has_old_value) {
            document_->queue_attribute(isolate, context, node_, key, true,
                                       old_value);
        }
        return v8::Undefined(isolate);
    }
    // M11-10 toggleAttribute(name, force?): add an empty attribute or remove it;
    // an explicit boolean force chooses the outcome. Queues an attribute record
    // only when presence actually changes. id/class keep their dedicated fields.
    if (name == "toggleAttribute") {
        const std::string key = ascii_lower(arg_string(0));
        bool present = false;
        std::string old_value;
        if (key == "id") {
            present = node_->has_id;
            old_value = node_->id;
        } else if (key == "class") {
            present = node_->has_class;
            old_value = node_->class_name;
        } else {
            const auto it = node_->attributes.find(key);
            if (it != node_->attributes.end()) {
                present = true;
                old_value = it->second;
            }
        }
        const bool force_specified =
            args.Length() > 1 && !args[1]->IsUndefined();
        const bool force =
            force_specified ? args[1]->BooleanValue(isolate) : false;
        const bool should_exist = force_specified ? force : !present;
        if (should_exist != present) {
            if (should_exist) {
                if (key == "id") {
                    node_->id.clear();
                    node_->has_id = true;
                } else if (key == "class") {
                    node_->class_name.clear();
                    node_->has_class = true;
                    node_->classes.clear();
                } else {
                    node_->attributes[key] = std::string();
                }
            } else {
                if (key == "id") {
                    node_->id.clear();
                    node_->has_id = false;
                } else if (key == "class") {
                    node_->class_name.clear();
                    node_->has_class = false;
                    node_->classes.clear();
                } else {
                    node_->attributes.erase(key);
                }
            }
            document_->queue_attribute(isolate, context, node_, key, present,
                                       old_value);
        }
        return v8::Boolean::New(isolate, should_exist);
    }
    // M4-A-5 subtree queries: same minimal selector subset / tag rules as the
    // document-level ones, but scoped to THIS element's current subtree (the
    // element itself + its descendants, so the root may match). Plain JS Array
    // for the *All / by-tag forms; null / first-match for querySelector.
    if (name == "querySelector") {
        return document_->wrap_node(
            isolate, context, dfs_find(node_, selector_predicate(arg_string(0))));
    }
    if (name == "querySelectorAll") {
        std::vector<DomNode*> out;
        collect_matching(node_, selector_predicate(arg_string(0)), out);
        return document_->elements_array(isolate, context, out);
    }
    if (name == "getElementsByTagName") {
        return document_->live_collection(isolate, context, node_, "tag", arg_string(0));
    }
    if (name == "getElementsByClassName") {
        return document_->live_collection(isolate, context, node_, "class", arg_string(0));
    }
    if (node_->tag == "select" && (name == "item" || name == "namedItem")) {
        const std::vector<DomNode*> options = collect_options(node_);
        if (name == "item") {
            int index = 0;
            if (args.Length() > 0) {
                v8::Local<v8::Int32> converted;
                if (!args[0]->ToInt32(context).ToLocal(&converted)) {
                    return v8::Null(isolate);
                }
                index = converted->Value();
            }
            if (index < 0 || index >= static_cast<int>(options.size())) {
                return v8::Null(isolate);
            }
            return document_->wrap_node(
                isolate, context, options[static_cast<std::size_t>(index)]);
        }
        const std::string wanted = arg_string(0);
        for (DomNode* option : options) {
            if (option->id == wanted) {
                return document_->wrap_node(isolate, context, option);
            }
            const auto it = option->attributes.find("name");
            if (it != option->attributes.end() && it->second == wanted) {
                return document_->wrap_node(isolate, context, option);
            }
        }
        return v8::Null(isolate);
    }
    // M4-B-2 contains() is handled by the shared Node method before this point.
    // M4-B-3 single-node selector match: apply the SAME minimal selector predicate
    // used by document/element querySelector[All] (#id / .class / tagname) to this
    // node only. A complex/empty/unsupported selector yields a match-nothing
    // predicate -> false (no syntax error). Depends only on this element's own
    // tag/id/class (live), not on its position in the tree, so it works on a
    // detached element too.
    if (name == "matches") {
        return v8::Boolean::New(isolate,
                                selector_predicate(arg_string(0))(node_));
    }
    // M4-B-4 nearest-ancestor selector match: check this node first, then walk the
    // parent chain, returning the first node whose own tag/id/class matches the
    // same minimal selector (shared with matches / querySelector[All]); JS null if
    // none. Complex/empty selector -> match-nothing predicate -> null. Walks the
    // live parent chain, so it follows tree edits and works within a detached
    // subtree (independent of isConnected). Wrapping reuses wrap_element.
    if (name == "closest") {
        const std::function<bool(const DomNode*)> pred =
            selector_predicate(arg_string(0));
        for (DomNode* n = node_; n != nullptr; n = n->parent) {
            if (pred(n)) {
                return document_->wrap_node(isolate, context, n);
            }
        }
        return v8::Null(isolate);
    }
    // M6-1 form.reset() (exposed only on <form>): restore the supported control
    // state (input/textarea/button.value, input.checked, option.selected) of every
    // such control in this form's CURRENT subtree to its parse/create-time initial
    // seed. select.value has no own slot, so it recovers via option.selected.
    // Restoring all three slots on each matched control is harmless (a control only
    // reads the slot it exposes). Reads the live subtree, so a reparented control
    // outside the form is unaffected; works on a detached <form>. Returns undefined;
    // no reset event, no validation, no submission.
    if (name == "reset") {
        std::vector<DomNode*> controls;
        collect_matching(node_,
                         [](const DomNode* n) {
                             return n->tag == "input" || n->tag == "textarea" ||
                                    n->tag == "button" || n->tag == "option";
                         },
                         controls);
        for (DomNode* control : controls) {
            control->value = control->initial_value;
            control->checked = control->initial_checked;
            control->selected = control->initial_selected;
        }
        return v8::Undefined(isolate);
    }
    // M7-1 form.submit() (exposed only on <form>): a minimal "exists and is
    // callable" submission entry point. No arguments; returns undefined. It does
    // NOT navigate, make a network request, dispatch a submit event, validate, or
    // change any control's live value / default baseline — a deliberate no-op this
    // phase. Callable on attached and detached forms.
    if (name == "submit") {
        return v8::Undefined(isolate);
    }
    // M7-2 form.requestSubmit() (exposed only on <form>): like submit(), a minimal
    // "exists and is callable" entry point. This phase supports only the no-argument
    // call; it returns undefined and is a deliberate no-op — no submitter handling,
    // no submit/input/change/reset event, no validation, no navigation / network /
    // FormData, and no change to any control's live value / default baseline.
    // Callable on attached and detached forms; result matches submit().
    if (name == "requestSubmit") {
        return v8::Undefined(isolate);
    }
    return v8::Undefined(isolate);
}

void NodeHost::dispatch_event(v8::Isolate* isolate,
                                 v8::Local<v8::Context> context,
                                 v8::Local<v8::Object> event,
                                 v8::Local<v8::Object> receiver) {
    document_->run_element_bubbling(isolate, context, event, node_, this, receiver);
}

}  // namespace iv8::browser
