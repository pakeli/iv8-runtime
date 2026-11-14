#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "iv8/node_kind.h"

namespace iv8::browser {

// A minimal HTML node. Only what M2-6/M2-7 queries need — no full DOM.
struct DomNode {
    // M11-48: node kind. Elements are the default; Text/Comment use `data` as
    // their character data; DocumentFragment is a detached container.
    NodeKind kind = NodeKind::Element;
    std::string tag;                   // lowercased tag name (element only)
    std::string data;                  // M11-48 Text/Comment character data
    std::string id;                    // id attribute value, or ""
    bool has_id = false;               // whether an id attribute was present
    std::string class_name;            // raw class attribute value, or ""
    bool has_class = false;            // whether a class attribute was present
    std::vector<std::string> classes;  // class tokens (for .class matching)
    std::string src;                   // M3-5 src attribute value (script), or ""
    bool has_src = false;              // whether a src attribute was present
    // M3-8 raw read-only attribute table: canonical lowercase name -> raw value
    // (boolean/valueless attrs map to ""). Holds every parsed attribute EXCEPT
    // id/class, which keep their dedicated fields (getAttribute routes those to
    // the fields, everything else to this table). Duplicate names: last wins.
    std::unordered_map<std::string, std::string> attributes;
    // M5-3/M5-4 <input>/<textarea>.value runtime slot. Initialized ONCE at
    // parse/create time (<input> from the `value` attribute, <textarea> from its
    // text content), then decoupled: value = ... writes here only. Only meaningful
    // for <input>/<textarea> (the property is exposed only there).
    std::string value;
    // M5-5 <option>.selected runtime bool. Seeded ONCE from the `selected`
    // attribute at parse/create time, then decoupled from the attribute. Only
    // meaningful for <option>.
    bool selected = false;
    // M5-7 <input>.checked runtime bool. Seeded ONCE from the `checked` attribute
    // at parse/create time, then decoupled. Only meaningful for <input> (minimal:
    // no type distinction, no radio-group exclusivity, no defaultChecked).
    bool checked = false;
    // M7-3 <form>.method runtime slot: the normalized submission method ("get" or
    // "post"). Seeded ONCE at parse/create from the `method` attribute (normalized),
    // defaulting to "get" (no attribute / createElement('form')), then decoupled from
    // the attribute. Only meaningful for <form> (the property is exposed only there);
    // form.reset() does NOT touch it. Minimal metadata: not "dialog", no other verbs.
    std::string method = "get";
    // M7-4 <form>.action runtime slot: the raw submission-address string. Seeded ONCE
    // at parse/create from the `action` attribute verbatim (NO URL parsing / relative-
    // to-absolute resolution / normalization), defaulting to "" (no attribute /
    // createElement('form')), then decoupled from the attribute. Only meaningful for
    // <form> (the property is exposed only there); form.reset() does NOT touch it.
    std::string action;
    // M7-5 <form>.enctype runtime slot: the normalized form-submission encoding, one
    // of the three HTML enctypes. Seeded ONCE at parse/create from the `enctype`
    // attribute (normalized), defaulting to "application/x-www-form-urlencoded" (no
    // attribute / createElement('form')), then decoupled from the attribute. Only
    // meaningful for <form>; form.reset() does NOT touch it. Metadata only — no body
    // encoding is implemented.
    std::string enctype = "application/x-www-form-urlencoded";
    // M7-6 <form>.target runtime slot: the raw submission-target string. Seeded ONCE
    // at parse/create from the `target` attribute verbatim (NO browsing-context lookup
    // / window resolution / _self,_blank,... special semantics), defaulting to "" (no
    // attribute / createElement('form')), then decoupled from the attribute. Only
    // meaningful for <form>; form.reset() does NOT touch it.
    std::string target;
    // M7-7 <form>.noValidate runtime bool: the form's validation-skip switch state.
    // Seeded ONCE at parse/create from the PRESENCE of the `novalidate` boolean
    // attribute (present -> true; absent / createElement('form') -> false), then
    // decoupled from the attribute. Only meaningful for <form>; form.reset() does NOT
    // touch it. M11-32: explicit checkValidity()/reportValidity() still validate even
    // when this switch is true.
    bool no_validate = false;
    // M11-32 constraint-validation runtime slot for input/button/select/textarea:
    // the message installed by setCustomValidity(). Empty means no custom error.
    // Decoupled from attributes; form.reset() does NOT clear it.
    std::string custom_validity;
    // M6-1 form.reset() baseline: the value/selected/checked snapshot captured at
    // parse/create (after the M5-5 select normalization), FIXED thereafter (it does
    // not follow later attribute/textContent edits). reset() restores value /
    // selected / checked from these. createElement nodes default to "" / false /
    // false, matching their created state.
    std::string initial_value;
    bool initial_selected = false;
    bool initial_checked = false;
    DomNode* parent = nullptr;         // owning parent node, or null (root)
    std::vector<DomNode*> children;    // owned by the DocumentHost node pool
};

// M11-48: node-kind helpers shared by the parser, hosts, selectors, and mutation
// records. `text_content_of` is the live DOM textContent: Text nodes contribute
// their data, Element/Fragment nodes concatenate descendant Text data in tree
// order, and Comment data is excluded from a parent's aggregate (but returned
// for the comment node itself).
inline const char* document_insertion_error(DomNode* parent, const std::vector<DomNode*>& nodes,
                                      DomNode* replaced = nullptr) {
    if (parent->kind != NodeKind::Document) return nullptr;
    std::unordered_set<DomNode*> incoming;
    for (auto* node : nodes) {
        if (node->kind == NodeKind::DocumentFragment) incoming.insert(node->children.begin(), node->children.end());
        else incoming.insert(node);
    }
    int elements = 0;
    for (auto* child : parent->children) {
        if (child != replaced && !incoming.contains(child) && child->kind == NodeKind::Element) ++elements;
    }
    for (auto* child : incoming) {
        if (child->kind != NodeKind::Element && child->kind != NodeKind::Comment)
            return "Document children must be elements or comments";
        if (child->kind == NodeKind::Element) ++elements;
    }
    return elements > 1 ? "Document can contain only one element child" : nullptr;
}

extern bool is_element_node(const DomNode* node);

extern bool is_character_data_node(const DomNode* node);

extern std::string text_content_of(const DomNode* node);

extern std::vector<DomNode*> element_children_of(const DomNode* node);

extern DomNode* sibling_node(const DomNode* node, int direction);

extern DomNode* element_sibling_node(const DomNode* node, int direction);

extern void detach_from_parent(DomNode* child);

extern void siblings_of(const DomNode* child, DomNode** previous, DomNode** next);

extern bool is_self_or_ancestor(const DomNode* candidate, const DomNode* of);

}  // namespace iv8::browser
