#pragma once

#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

#include "iv8/page_state.h"
#include "../dom/html_parser.h"
#include "node_hosts.h"
#include "class_list.h"
#include "file_reader_state.h"
#include "live_collection.h"
#include "mutation_observer.h"

namespace iv8::browser {

class DocumentHost : public NodeHost {
public:
    DocumentHost(const std::string& html, std::string url, PageState* page,
                 bool active = true, bool html_document = true);

    v8::Local<v8::Value> document_global(v8::Isolate* isolate,
                                         v8::Local<v8::Context> context);

    v8::Local<v8::Value> wrap_created_node(v8::Isolate* isolate,
        v8::Local<v8::Context> context, std::unique_ptr<DomNode> node);
    void set_wrapper(v8::Isolate* isolate, v8::Local<v8::Object> object);
    std::string prototype_key() const override;

    // M3-5: the scripts found in the HTML, in document order (inline + external).
    const std::vector<ScriptRecord>& scripts() const;

    // M3-6: this generation's JS document.readyState. Defaults to "complete" (so
    // a fresh Page()'s blank generation reads "complete"); Page.load drives the
    // "loading" -> "interactive" -> "complete" migration for an explicit load.
    void set_ready_state(const std::string& state);

    // M3-7: the <script> node currently executing (document.currentScript). Set to
    // scripts_[index]'s node while that HTML script runs, cleared to null after.
    void set_current_script(std::size_t index);
    void clear_current_script();

    std::string global_name() const override;
    std::string trace_object_type() const override;
    std::vector<std::string> property_names() const override;
    std::vector<std::string> writable_property_names() const override;
    std::vector<std::string> method_names() const override;

    // Release the document's own listener handles AND every wrapped node host's
    // (node hosts are owned here, not in PageState::host_objects_, so the
    // teardown hook only reaches them through the document).
    void release_v8_handles() noexcept override;

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

    const std::string& base_url() const;
    void set_current_url(const std::string& url) { url_ = url; }
    PageState* page_state() const { return page_; }

    v8::MaybeLocal<v8::Value> window_named_property(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name);
    // Run on every child-list mutation even when nobody observes mutations, so
    // moving/removing a control cannot resurrect a form's remembered old name.
    void prune_past_named_controls();

    v8::Local<v8::Object> live_collection(v8::Isolate* isolate,
        v8::Local<v8::Context> context, DomNode* root, const std::string& kind,
        const std::string& query, bool all = false);
    std::vector<DomNode*> collection_nodes(const LiveCollectionBinding& binding) const;
    std::vector<DomNode*> named_collection_nodes(
        const LiveCollectionBinding& binding) const;

    // M11-14: deep/shallow detached clone. The clone is inserted into the
    // detached pool (never into roots_), so document queries ignore it.
    DomNode* clone_detached(DomNode* source, bool deep);

    // M11-14: structural equality used by Element.isEqualNode.
    bool nodes_equal(const DomNode* a, const DomNode* b) const;

    // M11-48: one cached host per backing DomNode, dispatched by node kind.
    NodeHost* ensure_node_host(DomNode* node);

    // M11-32: ensure the ElementHost for an element (the same cached host used by
    // node wrapping), so form-level validation can dispatch invalid events through
    // that control's listeners even if JS never wrapped it.
    ElementHost* ensure_element_host(DomNode* node);

    // Wrap a DOM node as its JS host object (JS null for nullptr). Public so node
    // hosts can wrap parents/children; wrappers are cached + owned here.
    v8::Local<v8::Value> wrap_node(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   DomNode* node);

    // Build a plain JS Array of host objects for arbitrary `nodes` (same wrapping
    // as document.scripts — no NodeList/HTMLCollection, no identity guarantee).
    v8::Local<v8::Array> nodes_array(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     const std::vector<DomNode*>& nodes);

    // Element-only collection array. Kept as a separate name so callers that
    // expect element results stay explicit; the inputs are already element-filtered.
    v8::Local<v8::Array> elements_array(v8::Isolate* isolate,
                                        v8::Local<v8::Context> context,
                                        const std::vector<DomNode*>& nodes);

    // M11-48: DOM textContent assignment. Replaces all children with zero or one
    // Text node and queues one childList record (removed old children + the new
    // Text). Shared by ElementHost, DocumentFragmentHost, and document.title.
    void set_text_content(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          DomNode* node, const std::string& text);

    // M11-5 MutationObserver: per-generation registry. The constructor stores a
    // pointer to the state in the observer's private slot; mutation hooks below
    // queue native records and schedule one microtask delivery per observer.
    MutationObserverState* create_mutation_observer(
        v8::Isolate* isolate, v8::Local<v8::Function> callback,
        v8::Local<v8::Object> self);
    void queue_child_list(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          DomNode* parent,
                          const std::vector<DomNode*>& added,
                          const std::vector<DomNode*>& removed,
                          DomNode* previous_sibling, DomNode* next_sibling);
    void queue_attribute(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         DomNode* target, const std::string& name,
                         bool has_old_value, const std::string& old_value);
    void queue_character_data(v8::Isolate* isolate,
                              v8::Local<v8::Context> context, DomNode* target,
                              const std::string& old_value);
    v8::Local<v8::Array> build_records_array(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::vector<MutationRecordData>& records);
    // Fast-path guard: mutation hooks stay cheap when nobody is observing.
    bool has_mutation_observers() const;

    // M4-A-6: whether `node` is connected to this document's tree — i.e. its
    // topmost ancestor (following parent) is one of roots_. Detached nodes (and
    // whole removed subtrees) are not. Public for ElementHost.isConnected.
    bool is_in_tree(const DomNode* node) const;

    // M4-A-7: remember window's event target so element/document bubbling can
    // reach it as the final hop. Set once at install time.
    void set_window_target(EventTargetHost* window_target);

    // M11-8: Blob constructor used by Blob.slice() (which builds a fresh Blob),
    // and FileReader states owned for this generation.
    void set_blob_constructor(v8::Isolate* isolate,
                              v8::Local<v8::Function> ctor);
    v8::Local<v8::Function> blob_constructor(v8::Isolate* isolate);
    FileReaderState* create_file_reader(v8::Isolate* isolate,
                                        v8::Local<v8::Object> self);

    // M11-10: own a classList binding for this generation.
    ClassListBinding* create_class_list_binding(DomNode* node);

    // M4-A-7: dispatch `event` from an element and bubble it along the CURRENT
    // tree. `node` is the element's backing node, `element_host` its host (whose
    // listeners fire first), `receiver` its JS object (the fixed event.target).
    // Path: element -> ancestor elements (via parentNode) -> document -> window.
    // Bubbling past the element only happens when event.bubbles === true; the
    // document/window hops happen only when the element is connected to the tree
    // (a detached subtree bubbles internally but never escapes to document/window).
    // stopPropagation() blocks only LATER hops (checked between hops); the current
    // target always finishes its own listeners.
    void run_element_bubbling(v8::Isolate* isolate, v8::Local<v8::Context> context,
                              v8::Local<v8::Object> event, DomNode* node,
                              EventTargetHost* element_host,
                              v8::Local<v8::Object> receiver);

    // M11-11: dispatchEvent called on `document` directly. Window capture runs
    // first; document is the target; window bubble runs last for bubbling events.
    void dispatch_event(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> event,
                        v8::Local<v8::Object> receiver) override;

    // M11-48: shared Node methods (remove / hasChildNodes / cloneNode /
    // isSameNode / isEqualNode / contains and the tree-edit methods). Returns
    // true when `name` was handled and writes the JS result to `out`.
    bool call_node_method(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          DomNode* target, const std::string& name,
                          const v8::FunctionCallbackInfo<v8::Value>& args,
                          v8::Local<v8::Value>& out);

    // M11-49: HTML fragment parsing/serialization and variadic tree methods.
    std::vector<DomNode*> parse_html_fragment(const std::string& html);
    DomNode* make_detached_fragment();
    bool build_argument_fragment(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const v8::FunctionCallbackInfo<v8::Value>& args, DomNode** fragment,
        DomNode* insertion_parent, DomNode* replaced);
    bool insert_dom_node(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         DomNode* parent, DomNode* child, DomNode* ref,
                         std::string* error);
    bool replace_dom_node(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          DomNode* parent, DomNode* new_child,
                          DomNode* old_child, std::string* error);
    bool remove_dom_node(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         DomNode* parent, DomNode* child, std::string* error);
    void replace_all_children(v8::Isolate* isolate,
                              v8::Local<v8::Context> context, DomNode* parent,
                              const std::vector<DomNode*>& children);
    v8::Local<v8::Value> call_mutation_method(
        v8::Isolate* isolate, v8::Local<v8::Context> context, DomNode* target,
        const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& args);
    v8::Local<v8::Value> call_insert_adjacent(
        v8::Isolate* isolate, v8::Local<v8::Context> context, DomNode* target,
        const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& args);

private:
    // M11-5: create/enqueue the one pending microtask delivery for `state`.
    void schedule_mutation_delivery(v8::Isolate* isolate,
                                    v8::Local<v8::Context> context,
                                    MutationObserverState* state);

    // Fire one element node's listeners as a single capture/bubble hop: wrap the
    // node and run its matching listeners with itself as currentTarget. A node
    // with no wrapper/listeners is a no-op.
    void fire_element_hop(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Object> event, DomNode* node,
                          bool capture);

    // The global `document` object (event.currentTarget for the document hop), or
    // the global object as a harmless fallback if it cannot be read.

    DomNode* find_tag(const std::string& tag) const;
    // M11-48: roots_ may contain top-level Text/Comment nodes; document.children
    // stays element-only.
    std::vector<DomNode*> element_children_of_roots() const;
    DomNode* find_id(const std::string& id) const;
    DomNode* find_class(const std::string& cls) const;
    // document.querySelector: first match of the minimal selector subset over the
    // whole tree (document order), or nullptr. (M4-A-5 shares selector_predicate.)
    DomNode* query(const std::string& raw) const;

    // M4-A-1: collect all current-tree nodes matching `pred`, document order.
    std::vector<DomNode*> collect(
        const std::function<bool(const DomNode*)>& pred) const;

    // document.querySelectorAll: all matches over the whole tree, document order.
    std::vector<DomNode*> query_all(const std::string& raw) const;

    // document.getElementsByTagName: case-insensitive tag / "*", over the tree.
    std::vector<DomNode*> elements_by_tag(const std::string& raw) const;

    PageState* page_ = nullptr;  // M11-46 cookie jar owner (outlives generations)
    std::string url_;
    std::string title_;
    DomNode* title_node_ = nullptr;  // M11-47 first parsed <title> (may be null)
    std::string ready_state_ = "complete";  // M3-6 JS document.readyState
    DomNode* current_script_ = nullptr;      // M3-7 document.currentScript backing
    std::vector<std::unique_ptr<DomNode>> pool_;
    bool active_ = true;
    bool html_document_ = true;
    DomNode document_node_;
    std::vector<DomNode*>& roots_;
    v8::Global<v8::Object> document_object_;
    // M4-A-2: createElement() nodes. Owned here for the page generation (stable,
    // no dangling) but NEVER linked into roots_/children, so they are detached —
    // invisible to find_*/collect_* (queries, document.scripts) and to the tree.
    std::vector<std::unique_ptr<DomNode>> detached_pool_;
    std::vector<ScriptRecord> scripts_;  // M3-5 scripts in document order
    std::vector<std::unique_ptr<NodeHost>> node_hosts_;
    std::unordered_map<DomNode*, NodeHost*> wrappers_;
    std::unordered_map<DomNode*, v8::Global<v8::Object>> node_objects_;
    v8::Global<v8::Object> all_collection_;
    std::vector<std::unique_ptr<LiveCollectionBinding>> collections_;
    // M11-5: per-generation MutationObserver state (Globals released through the
    // isolate-alive teardown hook in release_v8_handles()).
    std::vector<std::unique_ptr<MutationObserverState>> observers_;
    // M11-8: per-generation FileReader states and the Blob constructor backing
    // slice(). Both are released in release_v8_handles().
    std::vector<std::unique_ptr<FileReaderState>> file_readers_;
    v8::Global<v8::Function> blob_constructor_;
    // M11-10: classList bindings owned for the generation.
    std::vector<std::unique_ptr<ClassListBinding>> class_list_bindings_;
    // M4-A-7: window's event target (final bubbling hop). Observed, not owned;
    // set at install time. Null before install / in the both-modes shell.
    EventTargetHost* window_target_ = nullptr;
};

}  // namespace iv8::browser
