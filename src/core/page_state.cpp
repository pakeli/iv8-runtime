#include "iv8/page_state.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "iv8/access_summary.h"
#include "iv8/browser_compatibility.h"
#include "iv8/context_host.h"
#include "iv8/dom_url.h"
#include "iv8/html_entities.h"
#include "iv8/query_parameters.h"
#include "iv8/selector_parser.h"
#include "iv8/url_parser.h"
#include "iv8/web_interface.h"
#include "iv8/web_method.h"

#include "browser/internal/common.h"
#include "browser/host/event_target.h"
#include "browser/host/node_hosts.h"
#include "browser/host/document_host.h"
#include "browser/dom/dom_node.h"
#include "browser/dom/html_parser.h"
#include "browser/dom/html_serializer.h"
#include "browser/dom/dom_query.h"
#include "browser/dom/selector_match.h"
#include "browser/dom/form_controls.h"
#include "browser/internal/trace_hooks.h"
#include "browser/web/base64_globals.h"
#include "browser/web/crypto.h"
#include "browser/web/encoding.h"
#include "browser/bom/host_probe.h"
#include "browser/bom/console.h"
#include "browser/bom/web_storage.h"
#include "browser/bom/navigator_parts.h"
#include "browser/bom/media_query.h"
#include "browser/bom/navigator.h"
#include "browser/bom/visual_viewport.h"
#include "browser/bom/location.h"
#include "browser/web/performance.h"
#include "browser/web/structured_clone.h"
#include "browser/web/capabilities.h"
#include "browser/web/event_constructors.h"
#include "browser/web/url.h"
#include "browser/bootstrap/bootstrap_scripts.h"
#include "browser/internal/event_support.h"


// M9-1: V8 Inspector (CDP) attach base. Included after context_host.h so the
// v8_headers.h COMPILER-macro workaround has already run before any V8 header.
#include "v8-inspector.h"


namespace iv8 {




// Page runtime internals. Formerly an anonymous namespace; named so the planned
// split of this file (docs/page_state_split_plan.md) can share these symbols
// across translation units. The using-directive after the closing brace keeps
// unqualified lookup from the iv8-scope code below unchanged.
namespace browser {



// v8_string() and dom_exception() live in browser/internal/common.h.

// --- M11-7 Web Storage -------------------------------------------------------
//
// A Storage object is a plain JS object whose methods and `length` accessor carry
// an External pointer to one of PageState's two page-lifetime vectors. The
// vectors are insertion-ordered (updating a key keeps its position) and are
// re-used by every load() generation.















// Step 5e: navigator.storage stays a plain object so its method descriptors,
// prototype, and cached identity match the M11-43 surface, but its native
// method callbacks record with this stable native identity. The parent
// navigator.storage getter is recorded by NavigatorHost's normal property
// accessor. kStorageManagerTraceToken is defined in browser/internal/common.h.






















// Step 5h: legacy navigator metadata. The arrays stay plain frozen arrays (read
// through NavigatorHost's traced getter); javaEnabled stays a read-only property
// whose cached native function records the call and returns false.
// kJavaEnabledTraceToken is defined in browser/internal/common.h.




// Fixed internal default base URL for M2-3. The RFC 2606 `.invalid` TLD makes it
// clearly a non-routable placeholder (not a real navigation). A real page.load()
// that sets this per page is a LATER phase, deliberately not done here.
constexpr char kDefaultBaseUrl[] = "https://iv8.invalid/";

// --- M2-6 minimal document --------------------------------------------------
// A JS-global `document` host object + `element` host objects, backed by a
// MINIMAL internal HTML tree (tag / id / class / children only). This is NOT an
// HTML5 parser and NOT a DOM: it serves exactly documentElement / body / title /
// getElementById / querySelector(#id | tagname | .class). No Node/Element layer,
// no mutation, no textContent/attributes/children exposed.
























































class DocumentHost;  // ElementHost holds a back-pointer to its document

// --- M11-5 MutationObserver ---------------------------------------------------
// A record captured at mutation time. Node pointers are stable for the whole page
// generation (DocumentHost's pools are never reallocated); the JS MutationRecord
// object is built at delivery/takeRecords() time.




// Per-observer native state. Owned by the generation's DocumentHost; V8 Global
// handles are released in DocumentHost::release_v8_handles() (isolate-alive
// teardown hook). No JS-GC finalizer unregisters an observer in this phase.


void mutation_observer_delivery(const v8::FunctionCallbackInfo<v8::Value>& info);



// --- M3-3 minimal event model ------------------------------------------------
// EventTargetHost is a mixin for host objects that support events (document,
// element). It stores JS listener functions per event type as isolate-safe
// Global handles and implements addEventListener / removeEventListener /
// dispatchEvent.
//
// This is deliberately NOT the DOM Events spec: a single flat listener list per
// type, fired in registration order. Each target fires via fire_at (snapshot,
// swallow exceptions, `this` = the target). M4-A-7 adds minimal BUBBLING over the
// current tree (element -> ancestors -> document -> window) for events with
// bubbles === true, plus stopPropagation() (skips later targets only). Still NO
// capture phase, no preventDefault / stopImmediatePropagation / defaultPrevented,
// no listener options (once/capture/passive), and no default actions. Listeners
// are JS functions only — there is no JS->Python callback bridge in this phase.
// The retained Global handles are released via release_v8_handles() before the
// owning context's isolate is disposed (PageState wires this into ContextState's
// teardown hook).

class ScreenHost : public EventTargetHost {
public:
    explicit ScreenHost(const HostEnvironment* environment)
        : environment_(environment), orientation_(environment) {}

    std::string global_name() const override { return "screen"; }
    std::string prototype_key() const override { return "Screen"; }
    std::string trace_object_type() const override { return "screen"; }

    std::vector<std::string> property_names() const override {
        return {"width",      "height",     "availWidth", "availHeight",
                "availLeft",  "availTop",   "colorDepth", "pixelDepth",
                "orientation"};
    }

    std::vector<std::string> method_names() const override { return event_method_names(); }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context,
                                      const std::string& name) override {
        if (name == "width") {
            return v8::Integer::New(isolate, environment_->screen_width);
        }
        if (name == "height") {
            return v8::Integer::New(isolate, environment_->screen_height);
        }
        if (name == "availWidth") {
            return v8::Integer::New(isolate, environment_->avail_width);
        }
        if (name == "availHeight") {
            return v8::Integer::New(isolate, environment_->avail_height);
        }
        // M11-35: reported available origin (independent configured values).
        if (name == "availLeft") {
            return v8::Integer::New(isolate, environment_->avail_left);
        }
        if (name == "availTop") {
            return v8::Integer::New(isolate, environment_->avail_top);
        }
        if (name == "colorDepth") {
            return v8::Integer::New(isolate, environment_->color_depth);
        }
        if (name == "pixelDepth") {
            return v8::Integer::New(isolate, environment_->pixel_depth);
        }
        // M11-35: deterministic orientation derived from the reported screen
        // dimensions. Step 5 exposes it as a nested HostObject.
        if (name == "orientation") {
            return make_host_object(isolate, context, &orientation_);
        }
        return v8::Undefined(isolate);
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& args) override {
        v8::Local<v8::Value> result;
        if (handle_event_method(isolate, context, name, args, result)) return result;
        return v8::Undefined(isolate);
    }

private:
    const HostEnvironment* environment_;
    ScreenOrientationHost orientation_;
};

// M3-4 window event target. window / globalThis / self stay the intrinsic global
// object (M2-2, so window === globalThis === self and globals live on it); this
// backing object ONLY holds the global's event listeners and provides the
// add/remove/dispatch semantics (inherited from EventTargetHost). It is NOT
// installed as a named global host object. Shared EventTarget methods resolve
// its backing through the context's private Window host slot.
class WindowEventTarget : public EventTargetHost {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "Window"; }
    std::string trace_object_type() const override { return "window"; }
    std::string trace_label() const override { return "window"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override { return event_method_names(); }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate, v8::Local<v8::Context>,
                                      const std::string&) override {
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& args) override {
        watch_record(isolate, "window", name);
        v8::Local<v8::Value> result;
        if (handle_event_method(isolate, context, name, args, result)) return result;
        return v8::Undefined(isolate);
    }

    // M11-51: release IDL event handler attributes with the ordinary listeners.
    void release_v8_handles() noexcept override {
        EventTargetHost::release_v8_handles();
        for (auto& entry : event_handlers_) {
            entry.second.Reset();
        }
        event_handlers_.clear();
    }

    v8::Local<v8::Value> get_event_handler(v8::Isolate* isolate,
                                           const std::string& name) {
        auto it = event_handlers_.find(event_type_for_handler(name));
        if (it == event_handlers_.end() || it->second.IsEmpty()) {
            return v8::Null(isolate);
        }
        return it->second.Get(isolate);
    }

    void set_event_handler(v8::Isolate* isolate, const std::string& name,
                           v8::Local<v8::Value> value) {
        const std::string type = event_type_for_handler(name);
        auto it = event_handlers_.find(type);
        if (!value->IsFunction()) {
            // WebIDL EventHandler? treats non-callable (including null and
            // undefined) as null.
            if (it != event_handlers_.end()) {
                it->second.Reset();
                event_handlers_.erase(it);
            }
            return;
        }
        v8::Local<v8::Function> callback = value.As<v8::Function>();
        if (it == event_handlers_.end()) {
            event_handlers_.emplace(
                type, v8::Global<v8::Function>(isolate, callback));
        } else {
            it->second.Reset(isolate, callback);
        }
    }

protected:
    // M11-51: IDL event handler attributes are non-capture listeners. They run
    // after the regular listener snapshot at this target, for at-target (2) and
    // bubbling (3) phases; the capture phase (1) never invokes them. A handler's
    // false return marks a cancelable event as defaultPrevented, and thrown
    // exceptions are swallowed like listener errors.
    void invoke_event_handler(v8::Isolate* isolate,
                              v8::Local<v8::Context> context,
                              v8::Local<v8::Object> event,
                              v8::Local<v8::Value> receiver, int phase,
                              const std::string& type) override {
        if (phase == 1) {
            return;
        }
        auto it = event_handlers_.find(type);
        if (it == event_handlers_.end() || it->second.IsEmpty()) {
            return;
        }
        v8::Local<v8::Function> handler = it->second.Get(isolate);
        v8::Local<v8::Value> event_arg = event;
        v8::Local<v8::Value> result;
        v8::TryCatch try_catch(isolate);
        const bool called =
            handler->Call(context, receiver, 1, &event_arg).ToLocal(&result);
        if (!called || try_catch.HasCaught()) {
            state_from_isolate(isolate)->record_async_error(isolate, context, try_catch, "listener");
            try_catch.Reset();
            return;
        }
        if (!result->IsFalse()) {
            return;
        }
        v8::Local<v8::Value> cancelable;
        if (event->Get(context, v8_string(isolate, "cancelable"))
                .ToLocal(&cancelable) &&
            cancelable->BooleanValue(isolate)) {
            (void)event->Set(context, v8_string(isolate, "defaultPrevented"),
                             v8::True(isolate));
        }
    }

private:
    // Handler accessors are named `on<type>`; dispatch looks handlers up by the
    // bare event type (`load`, `message`, ...). Normalize at the get/set boundary
    // so both paths share one key space.
    static std::string event_type_for_handler(const std::string& name) {
        if (name.size() > 2 && name[0] == 'o' && name[1] == 'n') {
            return name.substr(2);
        }
        return name;
    }

    std::unordered_map<std::string, v8::Global<v8::Function>> event_handlers_;
};

// M11-8 FileReader state. The listener store reuses EventTargetHost; the object
// itself is a plain JS instance whose prototype methods dispatch back here via a
// private slot. States are owned by DocumentHost for the generation, so pending
// microtask deliveries and listeners can be released at teardown.




// M11-10: backing for one `element.classList` object. Bindings are owned by the
// generation's DocumentHost; the returned classList is a fresh plain object per
// access (the runtime's usual no-identity-guarantee model).


// M11-48: common base for every JS wrapper that backs a DomNode (element,
// text/comment character data, document fragment). Node arguments are identified
// without RTTI through is_node(); node() is public for DocumentHost helpers.

std::string reflected_element_attribute(const DomNode* node, const std::string& name) {
    if (node->tag == "meta") {
        if (name == "httpEquiv") return "http-equiv";
        if (name == "content" || name == "name" || name == "scheme") return name;
    }
    if (node->tag == "script" && (name == "src" || name == "type")) return name;
    return {};
}



// M2-6/M2-7 element host object. Read-only, minimal node/element surface:
// tagName / nodeName / nodeType / id / className / textContent + parentNode /
// childNodes / children + getAttribute / hasAttribute. No mutation, no full
// Node/Element layer. parent/child accessors wrap related nodes back through the
// owning DocumentHost. Methods are defined out-of-line (below DocumentHost).


// M11-48: Text/Comment host. Both kinds share the character-data surface; the
// backing node's kind selects the DOM nodeType/nodeName and trace labels.

// M11-48: detached DocumentFragment host. It owns children like an element but
// is never attached itself; inserting it moves its children into the target.

// M2-6 document host object (M2-7 hands out node-capable elements). Owns the
// parsed tree + element wrappers for this page generation, so a load()/dispose()
// that tears down the context invalidates everything together.


std::vector<DomNode*> DocumentHost::collection_nodes(const LiveCollectionBinding& binding) const {
    const auto tokens = split_class_tokens(binding.query);
    const auto predicate = binding.kind == "tag" ? tag_predicate(binding.query) :
        std::function<bool(const DomNode*)>([tokens](const DomNode* node) {
            return !tokens.empty() && has_all_class_tokens(node, tokens);
        });
    if (binding.root == nullptr) return collect(predicate);
    std::vector<DomNode*> nodes;
    for (auto child : binding.root->children) collect_matching(child, predicate, nodes);
    return nodes;
}

LiveCollectionBinding* collection_binding(v8::Local<v8::Object> object) {
    if (object->InternalFieldCount() != 2) return nullptr;
    return static_cast<LiveCollectionBinding*>(object->GetAlignedPointerFromInternalField(0, v8::kEmbedderDataTypeTagDefault));
}

v8::Intercepted collection_index(std::uint32_t index,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
    auto binding = collection_binding(info.Holder());
    if (binding == nullptr) return v8::Intercepted::kNo;
    auto nodes = binding->document->collection_nodes(*binding);
    if (index >= nodes.size()) return v8::Intercepted::kNo;
    info.GetReturnValue().Set(binding->document->wrap_node(info.GetIsolate(),
        info.GetIsolate()->GetCurrentContext(), nodes[index]));
    return v8::Intercepted::kYes;
}

v8::Intercepted collection_index_query(std::uint32_t index,
    const v8::PropertyCallbackInfo<v8::Integer>& info) {
    auto binding = collection_binding(info.Holder());
    if (binding == nullptr || index >= binding->document->collection_nodes(*binding).size())
        return v8::Intercepted::kNo;
    info.GetReturnValue().Set(static_cast<int>(v8::ReadOnly));
    return v8::Intercepted::kYes;
}

void collection_indices(const v8::PropertyCallbackInfo<v8::Array>& info) {
    auto binding = collection_binding(info.Holder());
    if (binding == nullptr) return;
    auto nodes = binding->document->collection_nodes(*binding);
    auto array = v8::Array::New(info.GetIsolate(), static_cast<int>(nodes.size()));
    for (std::uint32_t index = 0; index < nodes.size(); ++index)
        (void)array->Set(info.GetIsolate()->GetCurrentContext(), index,
            v8::Integer::NewFromUnsigned(info.GetIsolate(), index));
    info.GetReturnValue().Set(array);
}

DomNode* collection_named(LiveCollectionBinding* binding, const std::string& name) {
    if (name.empty()) return nullptr;
    for (auto node : binding->document->collection_nodes(*binding)) {
        auto found = node->attributes.find("name");
        if (node->id == name || (found != node->attributes.end() && found->second == name))
            return node;
    }
    return nullptr;
}

v8::Intercepted collection_name(v8::Local<v8::Name> name,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (!name->IsString()) return v8::Intercepted::kNo;
    auto binding = collection_binding(info.Holder());
    if (binding == nullptr) return v8::Intercepted::kNo;
    v8::String::Utf8Value text(info.GetIsolate(), name);
    auto node = collection_named(binding, *text != nullptr ? *text : "");
    if (node == nullptr) return v8::Intercepted::kNo;
    info.GetReturnValue().Set(binding->document->wrap_node(info.GetIsolate(),
        info.GetIsolate()->GetCurrentContext(), node));
    return v8::Intercepted::kYes;
}

void collection_length(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto binding = collection_binding(info.This());
    if (binding == nullptr) {
        info.GetIsolate()->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(info.GetIsolate(), "Illegal invocation")));
        return;
    }
    info.GetReturnValue().Set(static_cast<std::uint32_t>(
        binding->document->collection_nodes(*binding).size()));
}

void collection_item(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto binding = collection_binding(info.This());
    if (binding == nullptr) {
        isolate->ThrowException(v8::Exception::TypeError(v8::String::NewFromUtf8Literal(isolate, "Illegal invocation")));
        return;
    }
    v8::Local<v8::Value> argument = info.Length() ? info[0] :
        v8::Local<v8::Value>(v8::Undefined(isolate));
    DomNode* node = nullptr;
    const bool named = info.Data()->IsTrue();
    if (named) {
        v8::Local<v8::String> name;
        if (!argument->ToString(context).ToLocal(&name)) return;
        v8::String::Utf8Value text(isolate, name);
        node = collection_named(binding, *text != nullptr ? *text : "");
    } else {
        auto index = argument->Uint32Value(context);
        if (index.IsNothing()) return;
        auto nodes = binding->document->collection_nodes(*binding);
        if (index.FromJust() < nodes.size()) node = nodes[index.FromJust()];
    }
    info.GetReturnValue().Set(binding->document->wrap_node(isolate, context, node));
}

void all_collection_call(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto binding = static_cast<LiveCollectionBinding*>(info.Data().As<v8::External>()->Value(v8::kExternalPointerTypeTagDefault));
    if (info.Length() == 0) { info.GetReturnValue().Set(v8::Null(isolate)); return; }
    v8::Local<v8::String> value;
    if (!info[0]->ToString(context).ToLocal(&value)) return;
    v8::String::Utf8Value name(isolate, value);
    DomNode* node = collection_named(binding, *name != nullptr ? *name : "");
    info.GetReturnValue().Set(binding->document->wrap_node(isolate, context, node));
}

v8::Local<v8::Object> DocumentHost::live_collection(v8::Isolate* isolate,
    v8::Local<v8::Context> context, DomNode* root, const std::string& kind,
    const std::string& query, bool all) {
    auto binding = std::make_unique<LiveCollectionBinding>(LiveCollectionBinding{this, root, kind, query});
    auto raw = binding.get();
    collections_.push_back(std::move(binding));
    auto object_template = v8::ObjectTemplate::New(isolate);
    object_template->SetInternalFieldCount(2); // Distinct from a HostObject wrapper.
    object_template->SetHandler(v8::IndexedPropertyHandlerConfiguration(
        &collection_index, nullptr, &collection_index_query, nullptr, &collection_indices));
    object_template->SetHandler(v8::NamedPropertyHandlerConfiguration(&collection_name,
        nullptr, nullptr, nullptr, nullptr, v8::Local<v8::Value>(),
        v8::PropertyHandlerFlags::kNonMasking));
    if (all) {
        object_template->MarkAsUndetectable();
        object_template->SetCallAsFunctionHandler(&all_collection_call,
            v8::External::New(isolate, raw, v8::kExternalPointerTypeTagDefault));
    }
    v8::Local<v8::Object> object;
    if (!object_template->NewInstance(context).ToLocal(&object)) return v8::Object::New(isolate);
    object->SetAlignedPointerInInternalField(0, raw, v8::kEmbedderDataTypeTagDefault);
    auto cache_key = v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate,
        "iv8.collection.prototypes"));
    v8::Local<v8::Value> cached;
    if (!context->Global()->GetPrivate(context, cache_key).ToLocal(&cached)) return object;
    v8::Local<v8::Object> prototypes = cached->IsObject() ? cached.As<v8::Object>() : v8::Object::New(isolate);
    (void)context->Global()->SetPrivate(context, cache_key, prototypes);
    const auto class_name = v8_string(isolate, all ? "HTMLAllCollection" : "HTMLCollection").As<v8::String>();
    if (!prototypes->Get(context, class_name).ToLocal(&cached)) return object;
    v8::Local<v8::Object> prototype;
    if (cached->IsObject()) prototype = cached.As<v8::Object>();
    else {
        prototype = v8::Object::New(isolate);
        v8::Local<v8::Function> getter;
        if (!v8::Function::New(context, &collection_length).ToLocal(&getter)) return object;
        v8::PropertyDescriptor length(getter, v8::Undefined(isolate));
        length.set_enumerable(true); length.set_configurable(true);
        (void)prototype->DefineProperty(context, v8::String::NewFromUtf8Literal(isolate, "length"), length);
        for (const auto& name : {"item", "namedItem"}) {
            v8::Local<v8::Function> method;
            if (!v8::Function::New(context, &collection_item,
                    v8::Boolean::New(isolate, std::string(name) == "namedItem")).ToLocal(&method)) return object;
            (void)prototype->Set(context, v8_string(isolate, name), method);
        }
        (void)prototype->DefineOwnProperty(context, v8::Symbol::GetToStringTag(isolate),
            class_name, static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum));
        v8::Local<v8::Value> iterator;
        if (v8::Array::New(isolate)->Get(context, v8::Symbol::GetIterator(isolate)).ToLocal(&iterator))
            (void)prototype->DefineOwnProperty(context, v8::Symbol::GetIterator(isolate), iterator, v8::DontEnum);
        (void)prototypes->Set(context, class_name, prototype);
    }
    (void)object->SetPrototype(context, prototype);
    return object;
}

// --- M11-5 MutationObserver native registry / delivery -----------------------

// Tag for the Externals carrying MutationObserverState* / DocumentHost*. V8 15.0
// requires an ExternalPointerTypeTag on External::New / External::Value; the
// default suffices and MUST match between New and Value call sites (same rule as
// kWindowEventsTag below, see 52ded6e).
constexpr v8::ExternalPointerTypeTag kMutationObserverTag =
    v8::kExternalPointerTypeTagDefault;

MutationObserverState* DocumentHost::create_mutation_observer(
    v8::Isolate* isolate, v8::Local<v8::Function> callback,
    v8::Local<v8::Object> self) {
    auto state = std::make_unique<MutationObserverState>();
    state->owner = this;
    state->callback.Reset(isolate, callback);
    state->self.Reset(isolate, self);
    MutationObserverState* raw = state.get();
    observers_.push_back(std::move(state));
    return raw;
}

void DocumentHost::schedule_mutation_delivery(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    MutationObserverState* state) {
    if (state == nullptr || state->delivery_queued || !state->alive) {
        return;
    }
    state->delivery_queued = true;
    v8::Local<v8::Function> delivery;
    if (!v8::Function::New(context, &mutation_observer_delivery,
                           v8::External::New(isolate, state,
                                             kMutationObserverTag))
             .ToLocal(&delivery)) {
        state->delivery_queued = false;
        return;
    }
    isolate->EnqueueMicrotask(delivery);
}

void DocumentHost::queue_child_list(
    v8::Isolate* isolate, v8::Local<v8::Context> context, DomNode* parent,
    const std::vector<DomNode*>& added, const std::vector<DomNode*>& removed,
    DomNode* previous_sibling, DomNode* next_sibling) {
    if (observers_.empty()) {
        return;
    }
    for (const std::unique_ptr<MutationObserverState>& observer : observers_) {
        if (!observer->alive) {
            continue;
        }
        bool matched = false;
        for (const MutationObserverRegistration& registration :
             observer->registrations) {
            if (!registration.child_list) {
                continue;
            }
            bool target_matches = false;
            if (registration.target_is_document) {
                target_matches = parent == &document_node_ ||
                    (registration.subtree && is_in_tree(parent));
            } else {
                target_matches =
                    registration.target == parent ||
                    (registration.subtree &&
                     is_self_or_ancestor(registration.target, parent));
            }
            if (!target_matches) {
                continue;
            }
            MutationRecordData record;
            record.type = MutationRecordData::Type::ChildList;
            record.target = parent;
            record.added = added;
            record.removed = removed;
            record.previous_sibling = previous_sibling;
            record.next_sibling = next_sibling;
            observer->pending.push_back(std::move(record));
            matched = true;
            break;
        }
        if (matched) {
            schedule_mutation_delivery(isolate, context, observer.get());
        }
    }
}

void DocumentHost::queue_attribute(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   DomNode* target, const std::string& name,
                                   bool has_old_value,
                                   const std::string& old_value) {
    if (observers_.empty()) {
        return;
    }
    for (const std::unique_ptr<MutationObserverState>& observer : observers_) {
        if (!observer->alive) {
            continue;
        }
        bool matched = false;
        for (const MutationObserverRegistration& registration :
             observer->registrations) {
            if (!registration.attributes) {
                continue;
            }
            bool target_matches = false;
            if (registration.target_is_document) {
                target_matches = registration.subtree && is_in_tree(target);
            } else {
                target_matches =
                    registration.target == target ||
                    (registration.subtree &&
                     is_self_or_ancestor(registration.target, target));
            }
            if (!target_matches) {
                continue;
            }
            if (registration.has_attribute_filter &&
                std::find(registration.attribute_filter.begin(),
                          registration.attribute_filter.end(),
                          name) == registration.attribute_filter.end()) {
                continue;
            }
            MutationRecordData record;
            record.type = MutationRecordData::Type::Attributes;
            record.target = target;
            record.attribute_name = name;
            record.has_old_value =
                registration.attribute_old_value && has_old_value;
            if (record.has_old_value) {
                record.old_value = old_value;
            }
            observer->pending.push_back(std::move(record));
            matched = true;
            break;
        }
        if (matched) {
            schedule_mutation_delivery(isolate, context, observer.get());
        }
    }
}

void DocumentHost::queue_character_data(v8::Isolate* isolate,
                                        v8::Local<v8::Context> context,
                                        DomNode* target,
                                        const std::string& old_value) {
    if (observers_.empty()) {
        return;
    }
    for (const std::unique_ptr<MutationObserverState>& observer : observers_) {
        if (!observer->alive) {
            continue;
        }
        bool matched = false;
        for (const MutationObserverRegistration& registration :
             observer->registrations) {
            if (!registration.character_data) {
                continue;
            }
            bool target_matches = false;
            if (registration.target_is_document) {
                target_matches = registration.subtree && is_in_tree(target);
            } else {
                target_matches =
                    registration.target == target ||
                    (registration.subtree &&
                     is_self_or_ancestor(registration.target, target));
            }
            if (!target_matches) {
                continue;
            }
            MutationRecordData record;
            record.type = MutationRecordData::Type::CharacterData;
            record.target = target;
            record.has_old_value = registration.character_data_old_value;
            if (record.has_old_value) {
                record.old_value = old_value;
            }
            observer->pending.push_back(std::move(record));
            matched = true;
            break;
        }
        if (matched) {
            schedule_mutation_delivery(isolate, context, observer.get());
        }
    }
}

v8::Local<v8::Array> DocumentHost::build_records_array(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const std::vector<MutationRecordData>& records) {
    v8::Local<v8::Array> array =
        v8::Array::New(isolate, static_cast<int>(records.size()));
    for (std::size_t i = 0; i < records.size(); ++i) {
        const MutationRecordData& record = records[i];
        const bool child_list =
            record.type == MutationRecordData::Type::ChildList;
        const bool attributes =
            record.type == MutationRecordData::Type::Attributes;
        v8::Local<v8::Object> object = v8::Object::New(isolate);
        (void)object->Set(
            context, v8_string(isolate, "type"),
            v8_string(isolate, child_list
                                  ? "childList"
                                  : (attributes ? "attributes"
                                                : "characterData")));
        (void)object->Set(context, v8_string(isolate, "target"),
                          wrap_node(isolate, context, record.target));
        (void)object->Set(context, v8_string(isolate, "addedNodes"),
                          nodes_array(isolate, context, record.added));
        (void)object->Set(context, v8_string(isolate, "removedNodes"),
                          nodes_array(isolate, context, record.removed));
        (void)object->Set(context, v8_string(isolate, "previousSibling"),
                          wrap_node(isolate, context,
                                       record.previous_sibling));
        (void)object->Set(context, v8_string(isolate, "nextSibling"),
                          wrap_node(isolate, context, record.next_sibling));
        if (attributes) {
            (void)object->Set(context, v8_string(isolate, "attributeName"),
                              v8_string(isolate, record.attribute_name));
        } else {
            (void)object->Set(context, v8_string(isolate, "attributeName"),
                              v8::Null(isolate));
        }
        (void)object->Set(context, v8_string(isolate, "attributeNamespace"),
                          v8::Null(isolate));
        if (record.has_old_value) {
            (void)object->Set(context, v8_string(isolate, "oldValue"),
                              v8_string(isolate, record.old_value));
        } else {
            (void)object->Set(context, v8_string(isolate, "oldValue"),
                              v8::Null(isolate));
        }
        (void)array->Set(context, static_cast<std::uint32_t>(i), object);
    }
    return array;
}

void mutation_observer_delivery(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!info.Data()->IsExternal()) {
        return;
    }
    auto* state = static_cast<MutationObserverState*>(
        info.Data().As<v8::External>()->Value(kMutationObserverTag));
    if (state == nullptr || !state->alive || state->owner == nullptr) {
        return;
    }
    state->delivery_queued = false;
    if (state->pending.empty()) {
        return;
    }
    std::vector<MutationRecordData> records;
    records.swap(state->pending);
    v8::Local<v8::Array> array =
        state->owner->build_records_array(isolate, context, records);
    if (state->callback.IsEmpty() || state->self.IsEmpty()) {
        return;
    }
    v8::Local<v8::Function> callback = state->callback.Get(isolate);
    v8::Local<v8::Object> self = state->self.Get(isolate);
    v8::Local<v8::Value> argv[2] = {array.As<v8::Value>(),
                                    self.As<v8::Value>()};
    v8::TryCatch try_catch(isolate);
    (void)callback->Call(context, self.As<v8::Value>(), 2, argv);
    if (try_catch.HasCaught()) {
        try_catch.Reset();
    }
}

// --- M11-5 MutationObserver JS API -------------------------------------------

v8::Local<v8::Private> mo_private_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate, v8::String::NewFromUtf8Literal(
                     isolate, "iv8::MutationObserver::state"));
}

void throw_mo_type_error(v8::Isolate* isolate, const char* message) {
    isolate->ThrowException(v8::Exception::TypeError(
        v8_string(isolate, std::string(message)).As<v8::String>()));
}

MutationObserverState* mo_state_from_this(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> value;
    if (!info.This()
             ->GetPrivate(context, mo_private_key(isolate))
             .ToLocal(&value)) {
        return nullptr;
    }
    if (!value->IsExternal()) {
        return nullptr;
    }
    return static_cast<MutationObserverState*>(
        value.As<v8::External>()->Value(kMutationObserverTag));
}

void mutation_observer_constructor(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!info.IsConstructCall()) {
        throw_mo_type_error(isolate,
                            "MutationObserver constructor requires 'new'");
        return;
    }
    DocumentHost* document = nullptr;
    if (info.Data()->IsExternal()) {
        document = static_cast<DocumentHost*>(
            info.Data().As<v8::External>()->Value(kMutationObserverTag));
    }
    if (document == nullptr) {
        throw_mo_type_error(isolate, "MutationObserver: invalid constructor");
        return;
    }
    if (info.Length() < 1 || !info[0]->IsFunction()) {
        throw_mo_type_error(isolate,
                            "MutationObserver: callback must be a function");
        return;
    }
    v8::Local<v8::Object> self = info.This();
    MutationObserverState* state = document->create_mutation_observer(
        isolate, info[0].As<v8::Function>(), self);
    (void)self->SetPrivate(
        context, mo_private_key(isolate),
        v8::External::New(isolate, state, kMutationObserverTag));
}

void mo_observe(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    MutationObserverState* state = mo_state_from_this(info);
    if (state == nullptr) {
        throw_mo_type_error(isolate, "Illegal invocation");
        return;
    }
    if (info.Length() < 1) {
        throw_mo_type_error(isolate, "observe: target is required");
        return;
    }
    HostObject* host = host_object_backing(info[0]);
    bool target_is_document = false;
    DomNode* target_node = nullptr;
    if (host == nullptr) {
        throw_mo_type_error(isolate,
                            "observe: target must be an element or document");
        return;
    }
    if (state->owner != nullptr &&
        static_cast<HostObject*>(state->owner) == host) {
        target_is_document = true;
    } else if (host->is_node()) {
        NodeHost* node_host = static_cast<NodeHost*>(host);
        if (node_host->document() != state->owner) {
            throw_mo_type_error(isolate, "observe: target is from another document");
            return;
        }
        target_node = node_host->node();
    } else {
        throw_mo_type_error(isolate,
                            "observe: target must be a node or document");
        return;
    }
    if (info.Length() < 2 || !info[1]->IsObject()) {
        throw_mo_type_error(isolate, "observe: options must be an object");
        return;
    }
    v8::Local<v8::Object> options = info[1].As<v8::Object>();
    const auto get_bool = [&](const char* key, bool* out) -> bool {
        v8::Local<v8::Value> value;
        if (!options->Get(context, v8_string(isolate, key)).ToLocal(&value)) {
            return false;
        }
        *out = value->BooleanValue(isolate);
        return true;
    };
    bool child_list = false;
    bool attributes = false;
    bool character_data = false;
    bool subtree = false;
    bool attribute_old_value = false;
    bool character_data_old_value = false;
    if (!get_bool("childList", &child_list) ||
        !get_bool("attributes", &attributes) ||
        !get_bool("characterData", &character_data) ||
        !get_bool("subtree", &subtree) ||
        !get_bool("attributeOldValue", &attribute_old_value) ||
        !get_bool("characterDataOldValue", &character_data_old_value)) {
        return;  // a getter threw; keep the pending exception
    }
    v8::Maybe<bool> maybe_has =
        options->Has(context, v8_string(isolate, "attributeFilter"));
    if (maybe_has.IsNothing()) {
        return;
    }
    const bool has_filter = maybe_has.FromJust();
    if (!child_list && !attributes && !character_data) {
        throw_mo_type_error(isolate,
                            "observe: at least one of childList, attributes "
                            "or characterData must be true");
        return;
    }
    if (attribute_old_value && !attributes) {
        throw_mo_type_error(isolate,
                            "observe: attributeOldValue requires attributes");
        return;
    }
    if (has_filter && !attributes) {
        throw_mo_type_error(isolate,
                            "observe: attributeFilter requires attributes");
        return;
    }
    if (character_data_old_value && !character_data) {
        throw_mo_type_error(
            isolate,
            "observe: characterDataOldValue requires characterData");
        return;
    }
    std::vector<std::string> filter;
    if (has_filter) {
        v8::Local<v8::Value> filter_value;
        if (!options->Get(context, v8_string(isolate, "attributeFilter"))
                 .ToLocal(&filter_value)) {
            return;
        }
        if (!filter_value->IsArray()) {
            throw_mo_type_error(isolate,
                                "observe: attributeFilter must be an array");
            return;
        }
        v8::Local<v8::Array> filter_array = filter_value.As<v8::Array>();
        const std::uint32_t length = filter_array->Length();
        for (std::uint32_t i = 0; i < length; ++i) {
            v8::Local<v8::Value> item;
            if (!filter_array->Get(context, i).ToLocal(&item)) {
                return;
            }
            v8::Local<v8::String> item_string;
            if (!item->ToString(context).ToLocal(&item_string)) {
                return;
            }
            v8::String::Utf8Value utf8(isolate, item_string);
            if (*utf8 != nullptr) {
                filter.push_back(ascii_lower(std::string(
                    *utf8, static_cast<std::size_t>(utf8.length()))));
            }
        }
    }
    MutationObserverRegistration registration;
    registration.target = target_node;
    registration.target_is_document = target_is_document;
    registration.child_list = child_list;
    registration.attributes = attributes;
    registration.subtree = subtree;
    registration.attribute_old_value = attribute_old_value;
    registration.character_data = character_data;
    registration.character_data_old_value = character_data_old_value;
    registration.has_attribute_filter = has_filter;
    registration.attribute_filter = std::move(filter);
    bool replaced = false;
    for (MutationObserverRegistration& existing : state->registrations) {
        if (existing.target == target_node &&
            existing.target_is_document == target_is_document) {
            existing = std::move(registration);
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        state->registrations.push_back(std::move(registration));
    }
    info.GetReturnValue().SetUndefined();
}

void mo_disconnect(const v8::FunctionCallbackInfo<v8::Value>& info) {
    MutationObserverState* state = mo_state_from_this(info);
    if (state == nullptr) {
        throw_mo_type_error(info.GetIsolate(), "Illegal invocation");
        return;
    }
    state->registrations.clear();
    state->pending.clear();
    info.GetReturnValue().SetUndefined();
}

void mo_take_records(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    MutationObserverState* state = mo_state_from_this(info);
    if (state == nullptr) {
        throw_mo_type_error(isolate, "Illegal invocation");
        return;
    }
    std::vector<MutationRecordData> records;
    records.swap(state->pending);
    info.GetReturnValue().Set(
        state->owner->build_records_array(isolate, context, records)
            .As<v8::Value>());
}

// --- M11-10 classList / toggleAttribute -------------------------------------
// A classList object is a fresh plain object per access; its methods and
// length/value accessors carry an External to a generation-owned binding.

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

// --- M11-49 HTML fragment / variadic tree helpers ---------------------------

std::vector<DomNode*> DocumentHost::parse_html_fragment(const std::string& html) {
    std::vector<std::unique_ptr<DomNode>> pool;
    std::vector<DomNode*> roots;
    std::vector<ScriptRecord> scripts;  // inserted scripts stay inert
    parse_html(html, pool, roots, scripts);
    initialize_parsed_nodes(pool);
    for (std::unique_ptr<DomNode>& node : pool) {
        detached_pool_.push_back(std::move(node));
    }
    return roots;
}

DomNode* DocumentHost::make_detached_fragment() {
    auto fragment = std::make_unique<DomNode>();
    fragment->kind = NodeKind::DocumentFragment;
    DomNode* raw = fragment.get();
    detached_pool_.push_back(std::move(fragment));
    return raw;
}

bool DocumentHost::insert_dom_node(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   DomNode* parent, DomNode* child,
                                   DomNode* ref, std::string* error) {
    if (parent == nullptr || child == nullptr) {
        *error = "invalid node";
        return false;
    }
    if (child->kind == NodeKind::Document) {
        *error = "Cannot insert a Document";
        return false;
    }
    if (const char* invalid = document_insertion_error(parent, {child})) {
        *error = invalid;
        return false;
    }
    if (ref != nullptr) {
        if (ref->parent != parent) {
            *error = "reference is not a child of this node";
            return false;
        }
        if (child == ref) {
            return true;
        }
    }
    if (is_self_or_ancestor(child, parent)) {
        *error = "cannot insert a node into its own subtree";
        return false;
    }
    if (child->kind == NodeKind::DocumentFragment) {
        std::vector<DomNode*> moved = child->children;
        if (moved.empty()) {
            return true;
        }
        std::size_t insert_at = parent->children.size();
        if (ref != nullptr) {
            const auto pos =
                std::find(parent->children.begin(), parent->children.end(), ref);
            insert_at = static_cast<std::size_t>(
                std::distance(parent->children.begin(), pos));
        }
        DomNode* previous =
            insert_at > 0 ? parent->children[insert_at - 1] : nullptr;
        DomNode* next = ref;
        for (DomNode* moved_child : moved) {
            moved_child->parent = nullptr;
        }
        child->children.clear();
        std::vector<DomNode*> none;
        queue_child_list(isolate, context, child, none, moved, nullptr,
                         nullptr);
        for (std::size_t k = 0; k < moved.size(); ++k) {
            moved[k]->parent = parent;
            parent->children.insert(
                parent->children.begin() +
                    static_cast<std::ptrdiff_t>(insert_at + k),
                moved[k]);
        }
        queue_child_list(isolate, context, parent, moved, none, previous, next);
        return true;
    }
    DomNode* old_parent = child->parent;
    DomNode* old_previous = nullptr;
    DomNode* old_next = nullptr;
    if (old_parent != nullptr) {
        siblings_of(child, &old_previous, &old_next);
    }
    detach_from_parent(child);
    child->parent = parent;
    if (ref == nullptr) {
        parent->children.push_back(child);
    } else {
        const auto pos =
            std::find(parent->children.begin(), parent->children.end(), ref);
        parent->children.insert(pos, child);
    }
    DomNode* new_previous = nullptr;
    DomNode* new_next = nullptr;
    siblings_of(child, &new_previous, &new_next);
    if (old_parent != nullptr) {
        std::vector<DomNode*> added;
        std::vector<DomNode*> removed = {child};
        queue_child_list(isolate, context, old_parent, added, removed,
                         old_previous, old_next);
    }
    std::vector<DomNode*> added = {child};
    std::vector<DomNode*> removed;
    queue_child_list(isolate, context, parent, added, removed, new_previous,
                     new_next);
    return true;
}

bool DocumentHost::remove_dom_node(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   DomNode* parent, DomNode* child,
                                   std::string* error) {
    if (parent == nullptr || child == nullptr || child->parent != parent) {
        *error = "node is not a child of this node";
        return false;
    }
    DomNode* previous = nullptr;
    DomNode* next = nullptr;
    siblings_of(child, &previous, &next);
    detach_from_parent(child);
    std::vector<DomNode*> added;
    std::vector<DomNode*> removed = {child};
    queue_child_list(isolate, context, parent, added, removed, previous, next);
    return true;
}

bool DocumentHost::replace_dom_node(v8::Isolate* isolate,
                                    v8::Local<v8::Context> context,
                                    DomNode* parent, DomNode* new_child,
                                    DomNode* old_child, std::string* error) {
    if (parent == nullptr || new_child == nullptr || old_child == nullptr ||
        old_child->parent != parent) {
        *error = "oldChild is not a child of this node";
        return false;
    }
    if (const char* invalid = document_insertion_error(parent, {new_child}, old_child)) {
        *error = invalid;
        return false;
    }
    if (new_child == old_child) {
        return true;
    }
    if (is_self_or_ancestor(new_child, parent)) {
        *error = "cannot insert a node into its own subtree";
        return false;
    }
    if (new_child->kind == NodeKind::DocumentFragment) {
        std::vector<DomNode*> moved = new_child->children;
        std::size_t index = 0;
        const auto old_it =
            std::find(parent->children.begin(), parent->children.end(),
                      old_child);
        if (old_it != parent->children.end()) {
            index = static_cast<std::size_t>(
                std::distance(parent->children.begin(), old_it));
        }
        DomNode* old_previous = nullptr;
        DomNode* old_next = nullptr;
        siblings_of(old_child, &old_previous, &old_next);
        if (!moved.empty()) {
            for (DomNode* moved_child : moved) {
                moved_child->parent = nullptr;
            }
            new_child->children.clear();
            std::vector<DomNode*> none;
            queue_child_list(isolate, context, new_child, none, moved, nullptr,
                             nullptr);
        }
        detach_from_parent(old_child);
        std::vector<DomNode*> none;
        std::vector<DomNode*> old_removed = {old_child};
        queue_child_list(isolate, context, parent, none, old_removed,
                         old_previous, old_next);
        if (!moved.empty()) {
            std::size_t insert_at =
                index > parent->children.size() ? parent->children.size()
                                                : index;
            DomNode* previous =
                insert_at > 0 ? parent->children[insert_at - 1] : nullptr;
            DomNode* next = insert_at < parent->children.size()
                                ? parent->children[insert_at]
                                : nullptr;
            for (std::size_t k = 0; k < moved.size(); ++k) {
                moved[k]->parent = parent;
                parent->children.insert(
                    parent->children.begin() +
                        static_cast<std::ptrdiff_t>(insert_at + k),
                    moved[k]);
            }
            queue_child_list(isolate, context, parent, moved, none, previous,
                             next);
        }
        return true;
    }
    if (new_child->parent != nullptr) {
        DomNode* old_parent = new_child->parent;
        DomNode* previous = nullptr;
        DomNode* next = nullptr;
        siblings_of(new_child, &previous, &next);
        detach_from_parent(new_child);
        std::vector<DomNode*> added;
        std::vector<DomNode*> removed = {new_child};
        queue_child_list(isolate, context, old_parent, added, removed,
                         previous, next);
    }
    std::size_t index = 0;
    const auto old_it =
        std::find(parent->children.begin(), parent->children.end(), old_child);
    if (old_it != parent->children.end()) {
        index = static_cast<std::size_t>(
            std::distance(parent->children.begin(), old_it));
    }
    DomNode* old_previous = nullptr;
    DomNode* old_next = nullptr;
    siblings_of(old_child, &old_previous, &old_next);
    detach_from_parent(old_child);
    std::vector<DomNode*> added;
    std::vector<DomNode*> removed = {old_child};
    queue_child_list(isolate, context, parent, added, removed, old_previous,
                     old_next);
    if (index > parent->children.size()) {
        index = parent->children.size();
    }
    new_child->parent = parent;
    parent->children.insert(
        parent->children.begin() + static_cast<std::ptrdiff_t>(index),
        new_child);
    DomNode* new_previous = nullptr;
    DomNode* new_next = nullptr;
    siblings_of(new_child, &new_previous, &new_next);
    added = {new_child};
    removed.clear();
    queue_child_list(isolate, context, parent, added, removed, new_previous,
                     new_next);
    return true;
}

void DocumentHost::replace_all_children(
    v8::Isolate* isolate, v8::Local<v8::Context> context, DomNode* parent,
    const std::vector<DomNode*>& children) {
    if (parent == nullptr) {
        return;
    }
    std::vector<DomNode*> removed = parent->children;
    for (DomNode* child : removed) {
        child->parent = nullptr;
    }
    parent->children.clear();
    for (DomNode* child : children) {
        child->parent = parent;
        parent->children.push_back(child);
    }
    if (!removed.empty() || !children.empty()) {
        queue_child_list(isolate, context, parent, children, removed, nullptr,
                         nullptr);
    }
}

bool DocumentHost::build_argument_fragment(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const v8::FunctionCallbackInfo<v8::Value>& args, DomNode** fragment,
    DomNode* insertion_parent, DomNode* replaced) {
    std::vector<DomNode*> nodes;
    for (int index = 0; index < args.Length(); ++index) {
        auto* host = host_object_backing(args[index]);
        DomNode* node = nullptr;
        if (host != nullptr && host->is_node()) {
            auto* node_host = static_cast<NodeHost*>(host);
            if (node_host->document() != this) {
                isolate->ThrowException(dom_exception(isolate, "WrongDocumentError", "Node belongs to another document"));
                return false;
            }
            node = node_host->node();
            if (node->kind == NodeKind::Document || is_self_or_ancestor(node, insertion_parent)) {
                isolate->ThrowException(dom_exception(isolate, "HierarchyRequestError", "Invalid insertion hierarchy"));
                return false;
            }
        } else {
            v8::Local<v8::String> text;
            if (!args[index]->ToString(context).ToLocal(&text)) return false;
            v8::String::Utf8Value utf8(isolate, text);
            auto created = std::make_unique<DomNode>();
            created->kind = NodeKind::Text;
            if (*utf8 != nullptr) created->data.assign(*utf8, static_cast<std::size_t>(utf8.length()));
            node = created.get();
            detached_pool_.push_back(std::move(created));
        }
        nodes.push_back(node);
    }
    if (const char* invalid = document_insertion_error(insertion_parent, nodes, replaced)) {
        isolate->ThrowException(dom_exception(isolate, "HierarchyRequestError", invalid));
        return false;
    }
    auto* result = make_detached_fragment();
    for (auto* node : nodes) {
        std::string error;
        if (!insert_dom_node(isolate, context, result, node, nullptr, &error)) {
            isolate->ThrowException(v8::Exception::TypeError(v8_string(isolate, error).As<v8::String>()));
            return false;
        }
    }
    *fragment = result;
    return true;
}

v8::Local<v8::Value> DocumentHost::call_mutation_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, DomNode* target,
    const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& args) {
    const auto throw_type_error = [&](const std::string& message) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8_string(isolate, message).As<v8::String>()));
        return v8::Undefined(isolate);
    };
    if (name == "before" || name == "after" || name == "replaceWith") {
        if (target->parent == nullptr) {
            return v8::Undefined(isolate);  // ChildNode methods: no parent, no-op
        }
    }
    const bool child_operation = name == "before" || name == "after" || name == "replaceWith";
    DomNode* insertion_parent = child_operation ? target->parent : target;
    DomNode* reference = nullptr;
    if (child_operation) {
        std::unordered_set<DomNode*> moving;
        for (int index = 0; index < args.Length(); ++index) {
            auto* node = NodeHost::node_of_arg(args[index]);
            if (node == nullptr) continue;
            if (node->kind == NodeKind::DocumentFragment) moving.insert(node->children.begin(), node->children.end());
            else moving.insert(node);
        }
        reference = name == "before" ? target : sibling_node(target, 1);
        while (reference != nullptr && moving.contains(reference)) reference = sibling_node(reference, 1);
    }
    DomNode* fragment = nullptr;
    if (!build_argument_fragment(isolate, context, args, &fragment, insertion_parent,
            name == "replaceWith" ? target : nullptr)) {
        return v8::Undefined(isolate);
    }
    if (name == "append" || name == "prepend") {
        DomNode* ref = nullptr;
        if (name == "prepend" && !target->children.empty()) {
            ref = target->children.front();
        }
        std::string error;
        if (!insert_dom_node(isolate, context, target, fragment, ref, &error)) {
            return throw_type_error(error);
        }
        return v8::Undefined(isolate);
    }
    if (name == "replaceChildren") {
        std::vector<DomNode*> moved = fragment->children;
        fragment->children.clear();
        for (DomNode* child : moved) {
            child->parent = nullptr;
        }
        replace_all_children(isolate, context, target, moved);
        return v8::Undefined(isolate);
    }
    std::string error;
    const bool replacing = name == "replaceWith" && target->parent == insertion_parent;
    const bool changed = replacing
        ? replace_dom_node(isolate, context, insertion_parent, fragment, target, &error)
        : insert_dom_node(isolate, context, insertion_parent, fragment, reference, &error);
    if (!changed) return throw_type_error(error);

    return v8::Undefined(isolate);
}

v8::Local<v8::Value> DocumentHost::call_insert_adjacent(
    v8::Isolate* isolate, v8::Local<v8::Context> context, DomNode* target,
    const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& args) {
    const auto syntax_error = [&](const char* message) {
        isolate->ThrowException(v8::Exception::SyntaxError(
            v8_string(isolate, message).As<v8::String>()));
        return v8::Undefined(isolate);
    };
    const auto type_error = [&](const std::string& message) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8_string(isolate, message).As<v8::String>()));
        return v8::Undefined(isolate);
    };
    std::string position;
    if (args.Length() > 0) {
        v8::String::Utf8Value utf8(isolate, args[0]);
        if (*utf8 != nullptr) {
            position.assign(*utf8, static_cast<std::size_t>(utf8.length()));
        }
    }
    if (position != "beforebegin" && position != "afterbegin" &&
        position != "beforeend" && position != "afterend") {
        return syntax_error("insertAdjacent: invalid position");
    }
    const bool needs_parent =
        position == "beforebegin" || position == "afterend";
    DomNode* parent = needs_parent ? target->parent : target;
    if (parent == nullptr) {
        return type_error("insertAdjacent: element has no parent");
    }
    if (parent->kind == NodeKind::Document && name == "insertAdjacentHTML") {
        isolate->ThrowException(dom_exception(isolate, "NoModificationAllowedError", "Cannot parse HTML beside the document element"));
        return v8::Undefined(isolate);
    }
    if (name == "insertAdjacentElement" && args.Length() > 1) {
        auto* host = host_object_backing(args[1]);
        if (host != nullptr && host->is_node() && static_cast<NodeHost*>(host)->document() != this) {
            isolate->ThrowException(dom_exception(isolate, "WrongDocumentError", "Node belongs to another document"));
            return v8::Undefined(isolate);
        }
    }
    DomNode* node = nullptr;
    if (name == "insertAdjacentHTML") {
        std::string html;
        if (args.Length() > 1) {
            v8::String::Utf8Value utf8(isolate, args[1]);
            if (*utf8 != nullptr) {
                html.assign(*utf8, static_cast<std::size_t>(utf8.length()));
            }
        }
        std::vector<DomNode*> roots = parse_html_fragment(html);
        DomNode* fragment = make_detached_fragment();
        for (DomNode* root : roots) {
            root->parent = fragment;
            fragment->children.push_back(root);
        }
        node = fragment;
    } else if (name == "insertAdjacentElement") {
        HostObject* host = args.Length() > 1 ? host_object_backing(args[1])
                                             : nullptr;
        if (host == nullptr || !host->is_node() ||
            host->node_kind() != NodeKind::Element) {
            return type_error("insertAdjacentElement: argument is not an element");
        }
        node = static_cast<NodeHost*>(host)->node();
    } else {  // insertAdjacentText
        std::string text;
        if (args.Length() > 1) {
            v8::Local<v8::String> str;
            if (!args[1]->ToString(context).ToLocal(&str)) {
                return v8::Undefined(isolate);
            }
            v8::String::Utf8Value utf8(isolate, str);
            if (*utf8 != nullptr) {
                text.assign(*utf8, static_cast<std::size_t>(utf8.length()));
            }
        }
        auto text_node = std::make_unique<DomNode>();
        text_node->kind = NodeKind::Text;
        text_node->data = text;
        node = text_node.get();
        detached_pool_.push_back(std::move(text_node));
    }
    DomNode* ref = nullptr;
    if (position == "beforebegin") {
        ref = target;
    } else if (position == "afterbegin") {
        ref = target->children.empty() ? nullptr : target->children.front();
    } else if (position == "beforeend") {
        ref = nullptr;
    } else {  // afterend
        ref = sibling_node(target, 1);
    }
    std::string error;
    if (!insert_dom_node(isolate, context, parent, node, ref, &error)) {
        if (parent->kind == NodeKind::Document) {
            isolate->ThrowException(dom_exception(isolate, "HierarchyRequestError", error));
            return v8::Undefined(isolate);
        }
        return type_error(error);
    }
    if (name == "insertAdjacentElement") {
        return args[1];
    }
    return v8::Undefined(isolate);
}

// M11-48: shared Node/tree methods used by every node host. The receiver is a
// plain DomNode*; argument validation accepts any node kind. DocumentFragment
// arguments insert/move their children instead of the fragment itself.
bool DocumentHost::call_node_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context, DomNode* target,
    const std::string& name, const v8::FunctionCallbackInfo<v8::Value>& args,
    v8::Local<v8::Value>& out) {
    const auto type_error = [&](const char* message) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8_string(isolate, message).As<v8::String>()));
        out = v8::Undefined(isolate);
        return true;
    };
    const auto arg = [&](int index) {
        return args.Length() > index ? args[index] : v8::Local<v8::Value>();
    };
    if (name == "hasChildNodes") {
        out = v8::Boolean::New(isolate, !target->children.empty());
        return true;
    }
    if (name == "cloneNode") {
        const bool deep = args.Length() > 0 && args[0]->BooleanValue(isolate);
        out = wrap_node(isolate, context, clone_detached(target, deep));
        return true;
    }
    if (name == "isSameNode" || name == "isEqualNode") {
        DomNode* other = NodeHost::node_of_arg(arg(0));
        if (other == nullptr) {
            out = v8::False(isolate);
            return true;
        }
        out = v8::Boolean::New(
            isolate, name == "isSameNode" ? other == target
                                          : nodes_equal(target, other));
        return true;
    }
    if (name == "contains") {
        DomNode* other = NodeHost::node_of_arg(arg(0));
        bool result = false;
        for (DomNode* n = other; n != nullptr; n = n->parent) {
            if (n == target) {
                result = true;
                break;
            }
        }
        out = v8::Boolean::New(isolate, result);
        return true;
    }
    if (name == "remove") {
        DomNode* parent = target->parent;
        if (parent != nullptr) {
            DomNode* previous = nullptr;
            DomNode* next = nullptr;
            siblings_of(target, &previous, &next);
            detach_from_parent(target);
            std::vector<DomNode*> added;
            std::vector<DomNode*> removed = {target};
            queue_child_list(isolate, context, parent, added, removed, previous,
                             next);
        }
        out = v8::Undefined(isolate);
        return true;
    }
    if (name != "appendChild" && name != "removeChild" &&
        name != "insertBefore" && name != "replaceChild") {
        return false;
    }

    const auto dom_error = [&](const char* error_name, const char* message) {
        auto error = v8::Exception::Error(v8_string(isolate, message).As<v8::String>());
        (void)error.As<v8::Object>()->Set(context, v8_string(isolate, "name"), v8_string(isolate, error_name));
        isolate->ThrowException(error);
        out = v8::Undefined(isolate);
        return true;
    };
    if (target->kind == NodeKind::Text || target->kind == NodeKind::Comment) {
        return dom_error(name == "removeChild" || name == "replaceChild" ? "NotFoundError" : "HierarchyRequestError",
            "CharacterData nodes cannot have children");
    }
    if (auto* child_host = host_object_backing(arg(0)); child_host != nullptr && child_host->is_node() &&
        static_cast<NodeHost*>(child_host)->document() != this) {
        return dom_error("WrongDocumentError", "Node belongs to another document");
    }
    DomNode* child = NodeHost::node_of_arg(arg(0));
    if (child != nullptr && child->kind == NodeKind::Document)
        return dom_error("HierarchyRequestError", "Cannot insert a Document");
    if (name == "replaceChild") {
        DomNode* old_child = NodeHost::node_of_arg(arg(1));
        if (child == nullptr || old_child == nullptr) {
            return type_error("replaceChild: arguments must be nodes");
        }
        if (old_child->parent != target) {
            return type_error(
                "replaceChild: oldChild is not a child of this node");
        }
        if (child == old_child) {
            out = args[1];
            return true;
        }
        if (is_self_or_ancestor(child, target)) {
            return type_error(
                "replaceChild: cannot insert a node into its own subtree");
        }
        if (child->kind == NodeKind::DocumentFragment) {
            const auto old_it =
                std::find(target->children.begin(), target->children.end(),
                          old_child);
            const std::size_t index = static_cast<std::size_t>(
                std::distance(target->children.begin(), old_it));
            std::vector<DomNode*> moved = child->children;
            DomNode* old_previous = nullptr;
            DomNode* old_next = nullptr;
            siblings_of(old_child, &old_previous, &old_next);
            if (!moved.empty()) {
                for (DomNode* moved_child : moved) {
                    moved_child->parent = nullptr;
                }
                child->children.clear();
                std::vector<DomNode*> none;
                queue_child_list(isolate, context, child, none, moved, nullptr,
                                 nullptr);
            }
            detach_from_parent(old_child);
            std::vector<DomNode*> none;
            std::vector<DomNode*> old_removed = {old_child};
            queue_child_list(isolate, context, target, none, old_removed,
                             old_previous, old_next);
            if (!moved.empty()) {
                std::size_t insert_at =
                    index > target->children.size() ? target->children.size()
                                                    : index;
                DomNode* previous =
                    insert_at > 0 ? target->children[insert_at - 1] : nullptr;
                DomNode* next = insert_at < target->children.size()
                                    ? target->children[insert_at]
                                    : nullptr;
                for (std::size_t k = 0; k < moved.size(); ++k) {
                    moved[k]->parent = target;
                    target->children.insert(
                        target->children.begin() +
                            static_cast<std::ptrdiff_t>(insert_at + k),
                        moved[k]);
                }
                queue_child_list(isolate, context, target, moved, none, previous,
                                 next);
            }
            out = args[1];
            return true;
        }
        if (child->parent != nullptr) {
            DomNode* old_parent = child->parent;
            DomNode* previous = nullptr;
            DomNode* next = nullptr;
            siblings_of(child, &previous, &next);
            detach_from_parent(child);
            std::vector<DomNode*> added;
            std::vector<DomNode*> removed = {child};
            queue_child_list(isolate, context, old_parent, added, removed,
                             previous, next);
        }
        DomNode* old_previous = nullptr;
        DomNode* old_next = nullptr;
        siblings_of(old_child, &old_previous, &old_next);
        std::size_t index = 0;
        const auto old_it =
            std::find(target->children.begin(), target->children.end(),
                      old_child);
        if (old_it != target->children.end()) {
            index = static_cast<std::size_t>(
                std::distance(target->children.begin(), old_it));
        }
        detach_from_parent(old_child);
        std::vector<DomNode*> added;
        std::vector<DomNode*> removed = {old_child};
        queue_child_list(isolate, context, target, added, removed, old_previous,
                         old_next);
        if (index > target->children.size()) {
            index = target->children.size();
        }
        child->parent = target;
        target->children.insert(
            target->children.begin() + static_cast<std::ptrdiff_t>(index),
            child);
        DomNode* new_previous = nullptr;
        DomNode* new_next = nullptr;
        siblings_of(child, &new_previous, &new_next);
        added = {child};
        removed.clear();
        queue_child_list(isolate, context, target, added, removed, new_previous,
                         new_next);
        out = args[1];
        return true;
    }

    if (child == nullptr) {
        return type_error("argument is not a node");
    }
    if (name == "removeChild") {
        if (child->parent != target) {
            return type_error("removeChild: not a child of this node");
        }
        DomNode* previous = nullptr;
        DomNode* next = nullptr;
        siblings_of(child, &previous, &next);
        detach_from_parent(child);
        std::vector<DomNode*> added;
        std::vector<DomNode*> removed = {child};
        queue_child_list(isolate, context, target, added, removed, previous,
                         next);
        out = args[0];
        return true;
    }
    // appendChild / insertBefore: resolve ref (insertBefore only; null = end).
    DomNode* ref = nullptr;
    if (name == "insertBefore") {
        const bool ref_is_null =
            args.Length() > 1 && (args[1]->IsNull() || args[1]->IsUndefined());
        if (!ref_is_null) {
            ref = NodeHost::node_of_arg(arg(1));
            if (ref == nullptr) {
                return type_error("insertBefore: ref is not a node");
            }
            if (ref->parent != target) {
                return type_error(
                    "insertBefore: ref is not a child of this node");
            }
            if (child == ref) {
                out = args[0];  // stable no-op
                return true;
            }
        }
    }
    if (is_self_or_ancestor(child, target)) {
        return type_error("cannot insert a node into its own subtree");
    }
    if (child->kind == NodeKind::DocumentFragment) {
        std::vector<DomNode*> moved = child->children;
        std::size_t insert_at = target->children.size();
        if (ref != nullptr) {
            const auto pos =
                std::find(target->children.begin(), target->children.end(),
                          ref);
            insert_at = static_cast<std::size_t>(
                std::distance(target->children.begin(), pos));
        }
        if (moved.empty()) {
            out = args[0];
            return true;
        }
        DomNode* previous =
            insert_at > 0 ? target->children[insert_at - 1] : nullptr;
        DomNode* next = ref;
        for (DomNode* moved_child : moved) {
            moved_child->parent = nullptr;
        }
        child->children.clear();
        std::vector<DomNode*> none;
        queue_child_list(isolate, context, child, none, moved, nullptr,
                         nullptr);
        for (std::size_t k = 0; k < moved.size(); ++k) {
            moved[k]->parent = target;
            target->children.insert(
                target->children.begin() +
                    static_cast<std::ptrdiff_t>(insert_at + k),
                moved[k]);
        }
        queue_child_list(isolate, context, target, moved, none, previous, next);
        out = args[0];
        return true;
    }
    DomNode* old_parent = child->parent;
    DomNode* old_previous = nullptr;
    DomNode* old_next = nullptr;
    if (old_parent != nullptr) {
        siblings_of(child, &old_previous, &old_next);
    }
    detach_from_parent(child);
    child->parent = target;
    if (ref == nullptr) {
        target->children.push_back(child);  // append
    } else {
        const auto pos =
            std::find(target->children.begin(), target->children.end(), ref);
        target->children.insert(pos, child);
    }
    DomNode* new_previous = nullptr;
    DomNode* new_next = nullptr;
    siblings_of(child, &new_previous, &new_next);
    if (old_parent != nullptr) {
        std::vector<DomNode*> added;
        std::vector<DomNode*> removed = {child};
        queue_child_list(isolate, context, old_parent, added, removed,
                         old_previous, old_next);
    }
    std::vector<DomNode*> added = {child};
    std::vector<DomNode*> removed;
    queue_child_list(isolate, context, target, added, removed, new_previous,
                     new_next);
    out = args[0];
    return true;
}

// --- M11-48 character-data host ------------------------------------------------

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

// --- M11-48 DocumentFragment host ----------------------------------------------

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

// M2-8: textContent write. In the minimal (text-node-free) tree, setting
// textContent replaces all children with the given text — i.e. detach children
// and store the text. Live queries (getElementById/querySelector/children) then
// no longer see the removed nodes; no separate index needs updating.
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

// M11-32: fire the minimal `invalid` event at a control. It is a target-only,
// non-bubbling event with the standard read fields; it is a plain object (like
// the M3-4 lifecycle events), not a full Event instance, so preventDefault /
// stopPropagation are not installed on it. Return values are unaffected.
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

// M4-A-7: an element's dispatchEvent bubbles along the current tree. The owning
// document runs the walk (element -> ancestors -> document -> window), because it
// owns the element wrappers, is_in_tree, and the window target. `receiver` is this
// element's JS object and stays event.target throughout.
void NodeHost::dispatch_event(v8::Isolate* isolate,
                                 v8::Local<v8::Context> context,
                                 v8::Local<v8::Object> event,
                                 v8::Local<v8::Object> receiver) {
    document_->run_element_bubbling(isolate, context, event, node_, this, receiver);
}

// --- M2-4 timers: JS-visible setTimeout/clearTimeout/setInterval/clearInterval.
// These are bare global functions (not a host object). Each recovers the owning
// ContextState from the isolate embedder-data slot and delegates to its timer
// registry. They run during eval, i.e. under the context operation guard.

void register_timer_callback(const v8::FunctionCallbackInfo<v8::Value>& info,
                             bool repeating) {
    v8::Isolate* isolate = info.GetIsolate();
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr || info.Length() < 1 || !info[0]->IsFunction()) {
        return;  // invalid call: no timer registered, returns undefined
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Function> callback = info[0].As<v8::Function>();
    std::int32_t delay = 0;
    if (info.Length() > 1) {
        delay = info[1]->Int32Value(context).FromMaybe(0);
    }
    const std::int64_t id = state->register_timer(callback, delay, repeating);
    info.GetReturnValue().Set(static_cast<double>(id));
}

void set_timeout_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "setTimeout");  // M9-2
    register_timer_callback(info, /*repeating=*/false);
}

void set_interval_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "setInterval");  // M9-2
    register_timer_callback(info, /*repeating=*/true);
}

// --- M11-4 microtasks: JS-visible queueMicrotask.
//
// queueMicrotask(callback) appends a callback to the current context's V8
// microtask queue. It deliberately has no cancel form (the standard API has
// none) and no new Python pump: ContextState already installs
// MicrotasksPolicy::kExplicit, so pending microtasks run only when
// Page.run_jobs() performs the checkpoint. The callback must be a function;
// unlike the permissive rAF/idle host functions, a non-callable argument throws
// a JS TypeError before anything is queued.
void queue_microtask_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "queueMicrotask");  // M9-2
    v8::Isolate* isolate = info.GetIsolate();
    if (info.Length() < 1 || !info[0]->IsFunction()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(
                isolate, "queueMicrotask: callback must be a function")));
        return;
    }
    isolate->EnqueueMicrotask(info[0].As<v8::Function>());
}

// --- M11-2 animation frames: JS-visible requestAnimationFrame /
// cancelAnimationFrame. Bare global functions like the M2-4 timers, reaching the
// owning ContextState through the isolate embedder-data slot. They run during eval
// (or during a frame pump), i.e. under the context operation guard.
//
// requestAnimationFrame(callback) -> handle (a Number). The callback queue is FIFO
// and independent of the timer registry. M11-31 passes the current virtual
// timestamp as the callback's only argument; legacy frames do not advance that
// clock. Nothing fires until Page.run_animation_frame() (or the M11-30 facade)
// runs the queue.
void request_animation_frame_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "requestAnimationFrame");  // M9-2
    v8::Isolate* isolate = info.GetIsolate();
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr || info.Length() < 1 || !info[0]->IsFunction()) {
        return;  // invalid call: nothing queued, returns undefined
    }
    const std::int64_t handle =
        state->register_animation_frame(info[0].As<v8::Function>());
    info.GetReturnValue().Set(static_cast<double>(handle));
}

// cancelAnimationFrame(handle): drop a not-yet-run callback. Its handle space is
// separate from the timer ids, so it does NOT cancel a setTimeout/setInterval.
void cancel_animation_frame_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "cancelAnimationFrame");  // M9-2
    v8::Isolate* isolate = info.GetIsolate();
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr || info.Length() < 1) {
        return;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::int64_t handle =
        static_cast<std::int64_t>(info[0]->IntegerValue(context).FromMaybe(0));
    state->cancel_animation_frame(handle);
}

// --- M11-3 idle callbacks: JS-visible requestIdleCallback / cancelIdleCallback.
// Bare global functions like the M2-4 timers and the M11-2 frame globals, reaching
// the owning ContextState through the isolate embedder-data slot. They run during
// eval (or during an idle pump), i.e. under the context operation guard.
//
// requestIdleCallback(callback[, options]) -> handle (a Number). The queue is FIFO
// and independent of both the timer registry and the animation-frame queue. Nothing
// fires until Page.run_idle_callbacks() is called.
//
// options handling: `options` may be absent / undefined / null (no options), or an
// object whose OPTIONAL `timeout` must be a Number. A non-object options, or a
// present-but-non-Number / non-finite / negative `timeout`, throws a TypeError —
// this phase rejects a malformed value loudly rather than silently reinterpreting
// it. A valid `timeout` is RECORDED ONLY: no real timeout firing semantics exist
// this phase (there is no clock). Unknown option keys are ignored.
void request_idle_callback_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "requestIdleCallback");  // M9-2
    v8::Isolate* isolate = info.GetIsolate();
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr || info.Length() < 1 || !info[0]->IsFunction()) {
        return;  // invalid call: nothing queued, returns undefined
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::int32_t timeout = -1;  // absent
    if (info.Length() > 1 && !info[1]->IsUndefined() && !info[1]->IsNull()) {
        if (!info[1]->IsObject()) {
            isolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8Literal(
                    isolate, "requestIdleCallback: options must be an object")));
            return;
        }
        v8::Local<v8::Object> options = info[1].As<v8::Object>();
        v8::Local<v8::Value> value;
        if (!options->Get(context, v8_string(isolate, "timeout")).ToLocal(&value)) {
            return;  // the getter threw
        }
        if (!value->IsUndefined()) {
            if (!value->IsNumber()) {
                isolate->ThrowException(v8::Exception::TypeError(
                    v8::String::NewFromUtf8Literal(
                        isolate,
                        "requestIdleCallback: options.timeout must be a number")));
                return;
            }
            const double ms = value.As<v8::Number>()->Value();
            if (std::isnan(ms) || std::isinf(ms) || ms < 0.0) {
                isolate->ThrowException(v8::Exception::TypeError(
                    v8::String::NewFromUtf8Literal(
                        isolate,
                        "requestIdleCallback: options.timeout must be a "
                        "non-negative finite number")));
                return;
            }
            // Clamp to int32 range; the value is recorded only, never a deadline.
            timeout = static_cast<std::int32_t>(
                ms > 2147483647.0 ? 2147483647.0 : ms);
        }
    }
    const std::int64_t handle =
        state->register_idle_callback(info[0].As<v8::Function>(), timeout);
    info.GetReturnValue().Set(static_cast<double>(handle));
}

// cancelIdleCallback(handle): drop a not-yet-run callback. Its handle space is
// separate from the timer ids and the animation-frame handles, so it cancels
// neither a setTimeout/setInterval nor a requestAnimationFrame.
void cancel_idle_callback_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "cancelIdleCallback");  // M9-2
    v8::Isolate* isolate = info.GetIsolate();
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr || info.Length() < 1) {
        return;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::int64_t handle =
        static_cast<std::int64_t>(info[0]->IntegerValue(context).FromMaybe(0));
    state->cancel_idle_callback(handle);
}

// Shared by clearTimeout and clearInterval (single id space, like browsers).
void clear_timer_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr || info.Length() < 1) {
        return;
    }
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::int64_t id =
        static_cast<std::int64_t>(info[0]->IntegerValue(context).FromMaybe(0));
    state->clear_timer(id);
}

// --- M11-36 deterministic dialogs ------------------------------------------
//
// There is no windowing system or Python callback bridge in this runtime. The
// dialog globals are deterministic stubs: alert/print do nothing, confirm
// returns false, and prompt returns null. They never block and never mutate
// page state.
void alert_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "alert");
}

void confirm_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "confirm");
    info.GetReturnValue().Set(v8::False(info.GetIsolate()));
}

void prompt_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "prompt");
    info.GetReturnValue().Set(v8::Null(info.GetIsolate()));
}

void print_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "print");
}

// --- M11-50 deterministic window scroll / focus / OS-window stubs ------------
//
// The runtime has no layout engine, no focus/navigation machinery, and no OS
// window it could move or resize. These methods are compatibility no-ops: they
// accept any arguments, change no state, and return undefined. scrollX/scrollY
// and the pageXOffset/pageYOffset aliases consequently stay 0 (see
// window_metadata_value). Each call still participates in the M9-2 watch channel.
void window_scroll_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "scroll");
}

void window_scroll_to_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "scrollTo");
}

void window_scroll_by_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "scrollBy");
}

void window_focus_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "focus");
}

void window_blur_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "blur");
}

void window_move_by_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "moveBy");
}

void window_move_to_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "moveTo");
}

void window_resize_by_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "resizeBy");
}

void window_resize_to_callback(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "resizeTo");
}

void window_stop_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "stop");
}


























v8::Local<v8::Private> window_location_slot(v8::Isolate* isolate) {
    return v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.window.location"));
}

void window_location_get(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    if (info.This() != context->Global()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Illegal invocation")));
        return;
    }
    v8::Local<v8::Value> location;
    if (context->Global()->GetPrivate(context, window_location_slot(isolate)).ToLocal(&location))
        info.GetReturnValue().Set(location);
}

void window_location_set(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    if (info.This() != context->Global()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Illegal invocation")));
        return;
    }
    v8::Local<v8::Value> location;
    if (!context->Global()->GetPrivate(context, window_location_slot(isolate)).ToLocal(&location) ||
        !location->IsObject()) return;
    auto value = info.Length() ? info[0] : v8::Local<v8::Value>(v8::Undefined(isolate));
    (void)location.As<v8::Object>()->Set(context, v8_string(isolate, "href"), value);
}

void install_global_function(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             v8::Local<v8::Object> global, const std::string& name,
                             v8::FunctionCallback callback) {
    v8::Local<v8::Function> fn;
    if (!make_web_method(isolate, context, name,
            web_method_length("window", name), callback).ToLocal(&fn)) return;
    (void)global->Set(context, v8_string(isolate, name), fn);
}


// --- M11-6 event subclasses --------------------------------------------------
//
// The four constructors below reuse event_constructor() for the inherited Event
// fields (type / bubbles / target / currentTarget / stopPropagation) and add a
// small set of own read-only fields. They intentionally do NOT change Event
// itself: no capture phase, cancellation, listener options, native event
// generation, or Python surface is added.











// Tag for the External carrying the WindowEventTarget* in the window event
// functions' Data. V8 15.0 requires an ExternalPointerTypeTag on External::New /
// External::Value; the default suffices for this single pointer type and MUST
// match between New (install_page) and Value (below).
constexpr v8::ExternalPointerTypeTag kWindowEventsTag =
    v8::kExternalPointerTypeTagDefault;



// M11-51: Window IDL event handler attributes. The global slots are native
// accessors backed by WindowEventTarget; assigning a function replaces the
// handler, assigning null/undefined/non-callable clears it, and the getter
// reports null when unset. The supported names are a bounded set of common
// Window handlers; arbitrary `onfoo` names remain ordinary global properties.
const char* const kWindowEventHandlerNames[] = {
    "onload",        "onunload",        "onbeforeunload", "onerror",
    "onresize",      "onscroll",        "onfocus",        "onblur",
    "onhashchange",  "onpopstate",      "onmessage",      "onstorage",
    "ononline",      "onoffline",       "onpagehide",     "onpageshow",
    "onunhandledrejection",              "onrejectionhandled",
};

std::string window_handler_name(v8::Isolate* isolate,
                                v8::Local<v8::Name> property) {
    v8::String::Utf8Value utf8(isolate, property);
    return *utf8 != nullptr
               ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
               : std::string();
}

void window_event_handler_getter(
    v8::Local<v8::Name> property,
    const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* target = static_cast<WindowEventTarget*>(
        info.Data().As<v8::External>()->Value(kWindowEventsTag));
    info.GetReturnValue().Set(
        target->get_event_handler(isolate,
                                  window_handler_name(isolate, property)));
}

void window_event_handler_setter(
    v8::Local<v8::Name> property, v8::Local<v8::Value> value,
    const v8::PropertyCallbackInfo<void>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* target = static_cast<WindowEventTarget*>(
        info.Data().As<v8::External>()->Value(kWindowEventsTag));
    target->set_event_handler(isolate, window_handler_name(isolate, property),
                              value);
}

// M11-52: window.postMessage is a JS bootstrap wrapper around these hidden
// natives. The wrapper validates/coerces arguments, clones the message with the
// captured structuredClone, builds a MessageEvent, and schedules the dispatch
// on the captured setTimeout queue. dispatch() itself runs from that timer task
// and uses the ordinary window event-target path (listeners + onmessage).
void window_post_message_dispatch(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (info.Length() < 1 || !info[0]->IsObject()) {
        return;
    }
    auto* target = static_cast<EventTargetHost*>(
        info.Data().As<v8::External>()->Value(kWindowEventsTag));
    target->dispatch_event(isolate, context, info[0].As<v8::Object>(),
                           context->Global());
}

void window_post_message_record(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    watch_record(info.GetIsolate(), "window", "postMessage");
}
















// --- M11-8 Blob / File / FileReader ------------------------------------------
//
// Blob/File are immutable value objects whose bytes live in a private
// ArrayBuffer slot. FileReader is a plain JS instance whose private slot points
// at a generation-owned state; reads complete on the existing run_jobs()
// microtask checkpoint, so no new pump is introduced.

constexpr v8::ExternalPointerTypeTag kBlobTag =
    v8::kExternalPointerTypeTagDefault;
constexpr v8::ExternalPointerTypeTag kFileReaderTag =
    v8::kExternalPointerTypeTagDefault;

v8::Local<v8::Private> blob_bytes_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate, v8::String::NewFromUtf8Literal(isolate, "iv8::Blob::bytes"));
}

v8::Local<v8::Private> blob_type_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate, v8::String::NewFromUtf8Literal(isolate, "iv8::Blob::type"));
}

v8::Local<v8::Private> file_reader_state_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8::FileReader::state"));
}

void throw_blob_type_error(v8::Isolate* isolate, const char* message) {
    isolate->ThrowException(v8::Exception::TypeError(
        v8::String::NewFromUtf8(isolate, message).ToLocalChecked()));
}

bool blob_read_bytes(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Value> value,
                     std::vector<std::uint8_t>* out) {
    out->clear();
    if (!value->IsObject()) {
        return false;
    }
    v8::Local<v8::Value> stored;
    if (!value.As<v8::Object>()
             ->GetPrivate(context, blob_bytes_key(isolate))
             .ToLocal(&stored) ||
        !stored->IsArrayBuffer()) {
        return false;
    }
    const v8::Local<v8::ArrayBuffer> buffer = stored.As<v8::ArrayBuffer>();
    const std::size_t length = buffer->ByteLength();
    if (length > 0) {
        const auto* data = static_cast<const std::uint8_t*>(buffer->Data());
        out->assign(data, data + length);
    }
    return true;
}

void append_bytes(std::vector<std::uint8_t>* out, const std::uint8_t* data,
                  std::size_t length) {
    if (length > 0) {
        out->insert(out->end(), data, data + length);
    }
}

bool blob_append_part(v8::Isolate* isolate, v8::Local<v8::Context> context,
                      v8::Local<v8::Value> value,
                      std::vector<std::uint8_t>* out) {
    std::vector<std::uint8_t> bytes;
    if (blob_read_bytes(isolate, context, value, &bytes)) {
        append_bytes(out, bytes.data(), bytes.size());
        return true;
    }
    if (value->IsArrayBuffer()) {
        const v8::Local<v8::ArrayBuffer> buffer = value.As<v8::ArrayBuffer>();
        append_bytes(out,
                     static_cast<const std::uint8_t*>(buffer->Data()),
                     buffer->ByteLength());
        return true;
    }
    if (value->IsArrayBufferView()) {
        const v8::Local<v8::ArrayBufferView> view =
            value.As<v8::ArrayBufferView>();
        const auto* base =
            static_cast<const std::uint8_t*>(view->Buffer()->Data());
        append_bytes(out, base + view->ByteOffset(), view->ByteLength());
        return true;
    }
    v8::Local<v8::String> str;
    if (!value->ToString(context).ToLocal(&str)) {
        return false;
    }
    v8::String::Utf8Value utf8(isolate, str);
    if (*utf8 != nullptr) {
        append_bytes(out, reinterpret_cast<const std::uint8_t*>(*utf8),
                     static_cast<std::size_t>(utf8.length()));
    }
    return true;
}

bool blob_type_from_options(v8::Isolate* isolate,
                            v8::Local<v8::Context> context,
                            v8::Local<v8::Value> options, std::string* out) {
    out->clear();
    if (!options->IsObject()) {
        return true;
    }
    v8::Local<v8::Value> value;
    if (!options.As<v8::Object>()
             ->Get(context, v8_string(isolate, "type"))
             .ToLocal(&value)) {
        return false;
    }
    if (value->IsUndefined()) {
        return true;
    }
    v8::Local<v8::String> str;
    if (!value->ToString(context).ToLocal(&str)) {
        return false;
    }
    v8::String::Utf8Value utf8(isolate, str);
    if (*utf8 != nullptr) {
        *out = ascii_lower(
            std::string(*utf8, static_cast<std::size_t>(utf8.length())));
    }
    return true;
}

void set_blob_field(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    v8::Local<v8::Object> self, const char* name,
                    v8::Local<v8::Value> value) {
    (void)self->DefineOwnProperty(
        context, v8::String::NewFromUtf8(isolate, name).ToLocalChecked(), value,
        v8::ReadOnly);
}

bool blob_initialize(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Value> parts, v8::Local<v8::Value> options,
                     v8::Local<v8::Object> self) {
    std::vector<std::uint8_t> bytes;
    if (!parts->IsUndefined() && !parts->IsNull()) {
        if (!parts->IsArray()) {
            throw_blob_type_error(isolate, "Blob: parts must be an Array");
            return false;
        }
        const v8::Local<v8::Array> array = parts.As<v8::Array>();
        const std::uint32_t length = array->Length();
        for (std::uint32_t i = 0; i < length; i++) {
            v8::Local<v8::Value> part;
            if (!array->Get(context, i).ToLocal(&part)) {
                return false;
            }
            if (!blob_append_part(isolate, context, part, &bytes)) {
                return false;
            }
        }
    }
    std::string type;
    if (!blob_type_from_options(isolate, context, options, &type)) {
        return false;
    }
    v8::Local<v8::ArrayBuffer> buffer =
        v8::ArrayBuffer::New(isolate, bytes.size());
    if (!bytes.empty()) {
        std::memcpy(buffer->Data(), bytes.data(), bytes.size());
    }
    (void)self->SetPrivate(context, blob_bytes_key(isolate), buffer);
    (void)self->SetPrivate(context, blob_type_key(isolate),
                           v8_string(isolate, type));
    set_blob_field(isolate, context, self, "size",
                   v8::Number::New(isolate, static_cast<double>(bytes.size())));
    set_blob_field(isolate, context, self, "type", v8_string(isolate, type));
    return true;
}

void blob_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!event_construct_check(info, "Blob")) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    (void)blob_initialize(isolate, context, info[0], info[1], info.This());
}

void file_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!event_construct_check(info, "File")) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!blob_initialize(isolate, context, info[0], info[2], info.This())) {
        return;
    }
    std::string name;
    if (info.Length() > 1 && !info[1]->IsUndefined()) {
        v8::Local<v8::String> str;
        if (!info[1]->ToString(context).ToLocal(&str)) {
            return;
        }
        v8::String::Utf8Value utf8(isolate, str);
        if (*utf8 != nullptr) {
            name.assign(*utf8, static_cast<std::size_t>(utf8.length()));
        }
    }
    set_blob_field(isolate, context, info.This(), "name",
                   v8_string(isolate, name));
    double last_modified = 0;
    if (info.Length() > 2 && info[2]->IsObject()) {
        v8::Local<v8::Value> value;
        if (!info[2]
                 .As<v8::Object>()
                 ->Get(context, v8_string(isolate, "lastModified"))
                 .ToLocal(&value)) {
            return;
        }
        if (!value->IsUndefined()) {
            v8::Local<v8::Number> number;
            if (!value->ToNumber(context).ToLocal(&number)) {
                return;
            }
            const double value_number = number->Value();
            if (std::isfinite(value_number)) {
                last_modified = std::trunc(value_number);
            }
        }
    }
    set_blob_field(isolate, context, info.This(), "lastModified",
                   v8::Number::New(isolate, last_modified));
}

DocumentHost* document_from_blob_data(v8::Local<v8::Value> data) {
    if (!data->IsExternal()) {
        return nullptr;
    }
    return static_cast<DocumentHost*>(
        data.As<v8::External>()->Value(kBlobTag));
}

bool blob_bytes_or_throw(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         v8::Local<v8::Object> self,
                         std::vector<std::uint8_t>* out) {
    if (blob_read_bytes(isolate, context, self, out)) {
        return true;
    }
    throw_blob_type_error(isolate, "Illegal invocation");
    return false;
}

bool blob_index_arg(v8::Local<v8::Context> context, v8::Local<v8::Value> value,
                    std::size_t size, std::size_t default_value,
                    std::size_t* out) {
    if (value->IsUndefined()) {
        *out = default_value;
        return true;
    }
    v8::Local<v8::Number> number;
    if (!value->ToNumber(context).ToLocal(&number)) {
        return false;
    }
    double index = number->Value();
    if (std::isnan(index)) {
        index = 0;
    }
    if (index < 0) {
        index = static_cast<double>(size) + index;
    }
    if (index < 0) {
        index = 0;
    }
    if (index > static_cast<double>(size)) {
        index = static_cast<double>(size);
    }
    *out = static_cast<std::size_t>(index);
    return true;
}

void blob_slice(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::vector<std::uint8_t> bytes;
    if (!blob_bytes_or_throw(isolate, context, info.This(), &bytes)) {
        return;
    }
    std::size_t start = 0;
    std::size_t end = bytes.size();
    if (!blob_index_arg(context, info[0], bytes.size(), 0, &start) ||
        !blob_index_arg(context, info[1], bytes.size(), bytes.size(), &end)) {
        return;
    }
    if (end < start) {
        end = start;
    }
    std::string type;
    if (info.Length() > 2 && !info[2]->IsUndefined()) {
        v8::Local<v8::String> str;
        if (!info[2]->ToString(context).ToLocal(&str)) {
            return;
        }
        v8::String::Utf8Value utf8(isolate, str);
        if (*utf8 != nullptr) {
            type = ascii_lower(
                std::string(*utf8, static_cast<std::size_t>(utf8.length())));
        }
    }
    DocumentHost* document = document_from_blob_data(info.Data());
    if (document == nullptr) {
        throw_blob_type_error(isolate, "Blob.slice: invalid receiver");
        return;
    }
    v8::Local<v8::Function> constructor = document->blob_constructor(isolate);
    if (constructor.IsEmpty()) {
        throw_blob_type_error(isolate, "Blob.slice: Blob is unavailable");
        return;
    }
    v8::Local<v8::ArrayBuffer> buffer =
        v8::ArrayBuffer::New(isolate, end - start);
    if (end > start) {
        std::memcpy(buffer->Data(), bytes.data() + start, end - start);
    }
    v8::Local<v8::Array> parts = v8::Array::New(isolate, 1);
    (void)parts->Set(context, 0, buffer);
    v8::Local<v8::Object> options = v8::Object::New(isolate);
    (void)options->Set(context, v8_string(isolate, "type"),
                       v8_string(isolate, type));
    v8::Local<v8::Value> argv[2] = {parts, options};
    v8::Local<v8::Object> result;
    if (constructor->NewInstance(context, 2, argv).ToLocal(&result)) {
        info.GetReturnValue().Set(result);
    }
}

void blob_text(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::vector<std::uint8_t> bytes;
    if (!blob_bytes_or_throw(isolate, context, info.This(), &bytes)) {
        return;
    }
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        return;
    }
    v8::Local<v8::String> text = v8::String::Empty(isolate);
    if (!bytes.empty() &&
        !v8::String::NewFromUtf8(isolate,
                                 reinterpret_cast<const char*>(bytes.data()),
                                 v8::NewStringType::kNormal,
                                 static_cast<int>(bytes.size()))
             .ToLocal(&text)) {
        return;
    }
    (void)resolver->Resolve(context, text);
    info.GetReturnValue().Set(resolver->GetPromise());
}

void blob_array_buffer(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::vector<std::uint8_t> bytes;
    if (!blob_bytes_or_throw(isolate, context, info.This(), &bytes)) {
        return;
    }
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) {
        return;
    }
    v8::Local<v8::ArrayBuffer> buffer =
        v8::ArrayBuffer::New(isolate, bytes.size());
    if (!bytes.empty()) {
        std::memcpy(buffer->Data(), bytes.data(), bytes.size());
    }
    (void)resolver->Resolve(context, buffer);
    info.GetReturnValue().Set(resolver->GetPromise());
}

FileReaderState* file_reader_state_from(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!info.This()->IsObject()) {
        return nullptr;
    }
    v8::Local<v8::Value> value;
    if (!info.This()
             ->GetPrivate(context, file_reader_state_key(isolate))
             .ToLocal(&value) ||
        !value->IsExternal()) {
        return nullptr;
    }
    return static_cast<FileReaderState*>(
        value.As<v8::External>()->Value(kFileReaderTag));
}

FileReaderState* file_reader_state_or_throw(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    FileReaderState* state = file_reader_state_from(info);
    if (state == nullptr) {
        throw_blob_type_error(info.GetIsolate(), "Illegal invocation");
    }
    return state;
}

void file_reader_delivery(const v8::FunctionCallbackInfo<v8::Value>& info);

void file_reader_schedule(v8::Isolate* isolate,
                          v8::Local<v8::Context> context,
                          FileReaderState* state) {
    v8::Local<v8::Function> delivery;
    if (!v8::Function::New(context, &file_reader_delivery,
                           v8::External::New(isolate, state, kFileReaderTag))
             .ToLocal(&delivery)) {
        return;
    }
    isolate->EnqueueMicrotask(delivery);
}

void file_reader_delivery(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!info.Data()->IsExternal()) {
        return;
    }
    auto* state = static_cast<FileReaderState*>(
        info.Data().As<v8::External>()->Value(kFileReaderTag));
    if (state == nullptr || !state->alive || !state->pending ||
        state->self.IsEmpty()) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Object> self = state->self.Get(isolate);
    state->pending = false;
    (void)self->Set(context, v8_string(isolate, "readyState"),
                    v8::Integer::New(isolate, 2));
    if (state->mode == FileReaderState::Mode::Text) {
        v8::Local<v8::Value> result = v8::String::Empty(isolate);
        if (!state->bytes.empty()) {
            v8::Local<v8::String> text;
            if (v8::String::NewFromUtf8(
                    isolate,
                    reinterpret_cast<const char*>(state->bytes.data()),
                    v8::NewStringType::kNormal,
                    static_cast<int>(state->bytes.size()))
                    .ToLocal(&text)) {
                result = text;
            }
        }
        (void)self->Set(context, v8_string(isolate, "result"), result);
    } else {
        v8::Local<v8::ArrayBuffer> buffer =
            v8::ArrayBuffer::New(isolate, state->bytes.size());
        if (!state->bytes.empty()) {
            std::memcpy(buffer->Data(), state->bytes.data(),
                        state->bytes.size());
        }
        (void)self->Set(context, v8_string(isolate, "result"), buffer);
    }
    state->mode = FileReaderState::Mode::None;
    state->bytes.clear();
    state->events.dispatch_native(isolate, context, "load", self);
    state->events.dispatch_native(isolate, context, "loadend", self);
}

void file_reader_begin(const v8::FunctionCallbackInfo<v8::Value>& info,
                       bool as_text) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    FileReaderState* state = file_reader_state_or_throw(info);
    if (state == nullptr) {
        return;
    }
    if (state->pending) {
        throw_blob_type_error(isolate, "FileReader is already reading");
        return;
    }
    std::vector<std::uint8_t> bytes;
    if (info.Length() < 1 ||
        !blob_read_bytes(isolate, context, info[0], &bytes)) {
        throw_blob_type_error(isolate,
                              "FileReader: argument must be a Blob");
        return;
    }
    state->bytes = std::move(bytes);
    state->mode = as_text ? FileReaderState::Mode::Text
                          : FileReaderState::Mode::ArrayBuffer;
    state->pending = true;
    v8::Local<v8::Object> self = info.This();
    (void)self->Set(context, v8_string(isolate, "readyState"),
                    v8::Integer::New(isolate, 1));
    (void)self->Set(context, v8_string(isolate, "result"), v8::Null(isolate));
    state->events.dispatch_native(isolate, context, "loadstart", self);
    file_reader_schedule(isolate, context, state);
}

void file_reader_read_as_text(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    file_reader_begin(info, true);
}

void file_reader_read_as_array_buffer(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    file_reader_begin(info, false);
}

void file_reader_abort(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    FileReaderState* state = file_reader_state_or_throw(info);
    if (state == nullptr || !state->pending) {
        return;
    }
    state->pending = false;
    state->mode = FileReaderState::Mode::None;
    state->bytes.clear();
    v8::Local<v8::Object> self = info.This();
    (void)self->Set(context, v8_string(isolate, "readyState"),
                    v8::Integer::New(isolate, 2));
    (void)self->Set(context, v8_string(isolate, "result"), v8::Null(isolate));
    state->events.dispatch_native(isolate, context, "abort", self);
    state->events.dispatch_native(isolate, context, "loadend", self);
}

void file_reader_event_method(const v8::FunctionCallbackInfo<v8::Value>& info,
                              const char* name) {
    FileReaderState* state = file_reader_state_or_throw(info);
    if (state == nullptr) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> out;
    if (state->events.handle_event_method(isolate, context, name, info, out)) {
        info.GetReturnValue().Set(out);
    }
}

void file_reader_add_event_listener(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    file_reader_event_method(info, "addEventListener");
}

void file_reader_remove_event_listener(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    file_reader_event_method(info, "removeEventListener");
}

void file_reader_dispatch_event(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    file_reader_event_method(info, "dispatchEvent");
}

void file_reader_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    if (!event_construct_check(info, "FileReader")) {
        return;
    }
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    DocumentHost* document = document_from_blob_data(info.Data());
    if (document == nullptr) {
        throw_blob_type_error(isolate, "FileReader: invalid constructor");
        return;
    }
    FileReaderState* state =
        document->create_file_reader(isolate, info.This());
    (void)info.This()->SetPrivate(
        context, file_reader_state_key(isolate),
        v8::External::New(isolate, state, kFileReaderTag));
    (void)info.This()->Set(context, v8_string(isolate, "readyState"),
                           v8::Integer::New(isolate, 0));
    (void)info.This()->Set(context, v8_string(isolate, "result"),
                           v8::Null(isolate));
    (void)info.This()->Set(context, v8_string(isolate, "error"),
                           v8::Null(isolate));
}

// M9-1: convert an Inspector StringView (CDP protocol message) to a UTF-8
// std::string. CDP JSON is normally 8-bit; a 16-bit buffer is transcoded via V8
// (safe here — sendResponse/sendNotification run inside dispatchProtocolMessage,
// which we call with the isolate entered and a HandleScope active).
std::string inspector_sv_to_utf8(v8::Isolate* isolate,
                                 const v8_inspector::StringView& view) {
    if (view.is8Bit()) {
        return std::string(reinterpret_cast<const char*>(view.characters8()),
                           view.length());
    }
    v8::Local<v8::String> str;
    if (v8::String::NewFromTwoByte(isolate, view.characters16(),
                                   v8::NewStringType::kNormal,
                                   static_cast<int>(view.length()))
            .ToLocal(&str)) {
        v8::String::Utf8Value utf8(isolate, str);
        if (*utf8 != nullptr) {
            return std::string(*utf8, static_cast<std::size_t>(utf8.length()));
        }
    }
    return std::string();
}

// Minimal Inspector client. Every V8InspectorClient method has a default
// implementation. M9-3: runMessageLoopOnPause is overridden to COUNT pauses and
// return immediately (no nested loop — never blocks). V8 calls it only when the
// debugger actually pauses, so the count is direct evidence that a break happened.
class DevToolsClient : public v8_inspector::V8InspectorClient {
public:
    void runMessageLoopOnPause(int /*contextGroupId*/) override {
        pause_count_.fetch_add(1, std::memory_order_relaxed);
        // Return immediately: there is no message loop this phase, so the paused
        // state is instantaneous (observable, non-blocking).
    }
    int pause_count() const {
        return pause_count_.load(std::memory_order_relaxed);
    }

private:
    std::atomic<int> pause_count_{0};
};

// Channel that accumulates the session's synchronous outbound CDP messages
// (responses + notifications) into a buffer the dispatch call drains and returns.
class DevToolsChannel : public v8_inspector::V8Inspector::Channel {
public:
    void set_isolate(v8::Isolate* isolate) { isolate_ = isolate; }
    std::vector<std::string> take() {
        std::vector<std::string> out;
        out.swap(outbox_);
        return out;
    }
    void sendResponse(int /*callId*/,
                      std::unique_ptr<v8_inspector::StringBuffer> message) override {
        push(message.get());
    }
    void sendNotification(
        std::unique_ptr<v8_inspector::StringBuffer> message) override {
        push(message.get());
    }
    void flushProtocolNotifications() override {}

private:
    void push(v8_inspector::StringBuffer* message) {
        if (message != nullptr) {
            outbox_.push_back(inspector_sv_to_utf8(isolate_, message->string()));
        }
    }
    v8::Isolate* isolate_ = nullptr;
    std::vector<std::string> outbox_;
};

// M9-4: stringify a Reflect.get propertyKey without ever throwing. A string/number
// key uses ToString ("x" / "5"); a Symbol key uses its description ("Symbol(desc)")
// so a symbol-keyed Reflect.get is not turned into a TypeError.
std::string reflect_property_string(v8::Isolate* isolate,
                                    v8::Local<v8::Context> context,
                                    v8::Local<v8::Value> key) {
    if (key->IsSymbol()) {
        v8::Local<v8::Value> desc = key.As<v8::Symbol>()->Description(isolate);
        std::string inner;
        if (desc->IsString()) {
            v8::String::Utf8Value utf8(isolate, desc);
            if (*utf8 != nullptr) {
                inner.assign(*utf8, static_cast<std::size_t>(utf8.length()));
            }
        }
        return "Symbol(" + inner + ")";
    }
    v8::Local<v8::String> str;
    if (key->ToString(context).ToLocal(&str)) {
        v8::String::Utf8Value utf8(isolate, str);
        if (*utf8 != nullptr) {
            return std::string(*utf8, static_cast<std::size_t>(utf8.length()));
        }
    }
    return std::string();
}

// Step 6d/6f: target association and safe key text for the access-trace query
// wrappers. Unlike reflect_property_string(), this must not invoke user
// conversion hooks while collecting metadata; non-primitive keys are labelled
// generically.
struct AccessTargetInfo {
    HostObject* host = nullptr;
    std::string type = "unknown";
    std::string label;
};

AccessTargetInfo access_target_info(v8::Local<v8::Value> target) {
    if (target.IsEmpty()) {
        return {};
    }
    HostObject* host = host_object_backing(target);
    if (host == nullptr) {
        return {};
    }
    return AccessTargetInfo{host, host->trace_object_type(),
                            host->trace_label()};
}

std::string access_member_name(v8::Isolate* isolate,
                               v8::Local<v8::Context> context,
                               v8::Local<v8::Value> key) {
    if (key.IsEmpty()) {
        return std::string();
    }
    if (key->IsSymbol()) {
        v8::Local<v8::Value> desc = key.As<v8::Symbol>()->Description(isolate);
        std::string inner;
        if (desc->IsString()) {
            v8::String::Utf8Value utf8(isolate, desc);
            if (*utf8 != nullptr) {
                inner.assign(*utf8, static_cast<std::size_t>(utf8.length()));
            }
        }
        return "Symbol(" + inner + ")";
    }
    if (key->IsString()) {
        v8::String::Utf8Value utf8(isolate, key.As<v8::String>());
        return *utf8 != nullptr
                   ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
                   : std::string();
    }
    if (key->IsNumber() || key->IsBoolean() || key->IsBigInt() ||
        key->IsUndefined() || key->IsNull()) {
        v8::Local<v8::String> str;
        if (key->ToString(context).ToLocal(&str)) {
            v8::String::Utf8Value utf8(isolate, str);
            return *utf8 != nullptr
                       ? std::string(*utf8,
                                     static_cast<std::size_t>(utf8.length()))
                       : std::string();
        }
        return std::string();
    }
    return "<non-primitive-key>";
}

std::uint64_t record_access_query(v8::Isolate* isolate,
                                  const AccessTargetInfo& target,
                                  const std::string& member,
                                  AccessOperation operation) {
    if (target.host == nullptr) {
        return 0;
    }
    return access_trace_record(isolate, target.host, target.type.c_str(),
                               target.label.c_str(), member.c_str(), operation);
}

bool call_wrapped_original(const v8::FunctionCallbackInfo<v8::Value>& info,
                           v8::Local<v8::Context> context,
                           v8::Local<v8::Value> receiver) {
    v8::Local<v8::Function> original = info.Data().As<v8::Function>();
    if (original.IsEmpty()) {
        return false;
    }
    std::vector<v8::Local<v8::Value>> args;
    args.reserve(static_cast<std::size_t>(info.Length()));
    for (int i = 0; i < info.Length(); ++i) {
        args.push_back(info[i]);
    }
    v8::Local<v8::Value> result;
    if (original
            ->Call(context, receiver, info.Length(), args.data())
            .ToLocal(&result)) {
        info.GetReturnValue().Set(result);
        return true;
    }
    return false;
}

// M9-4: record a Reflect.get hit if reflect-get watching is enabled (else no-op).
void reflect_record(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    v8::Local<v8::Value> target,
                    v8::Local<v8::Value> key) {
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr) {
        return;
    }
    WatchRegistry* registry = state->watch_registry();
    if (registry == nullptr || !registry->reflect_get_enabled()) {
        return;
    }
    const AccessTargetInfo target_info = access_target_info(target);
    registry->record_reflect_get(reflect_property_string(isolate, context, key),
                                 target_info.type, target_info.label);
}

// M9-4: the Reflect.get hook — records the propertyKey (arg 1), then delegates to
// the original Reflect.get (carried as this function's Data) so semantics are
// exactly preserved. Recording is gated inside reflect_record, so a disabled hook
// is a transparent pass-through.
void reflect_get_wrapper(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (info.Length() > 1) {
        reflect_record(isolate, context, info[0], info[1]);
    }
    v8::Local<v8::Function> original = info.Data().As<v8::Function>();
    std::vector<v8::Local<v8::Value>> args;
    args.reserve(static_cast<std::size_t>(info.Length()));
    for (int i = 0; i < info.Length(); i++) {
        args.push_back(info[i]);
    }
    v8::Local<v8::Value> result;
    if (original
            ->Call(context, v8::Undefined(isolate), info.Length(), args.data())
            .ToLocal(&result)) {
        info.GetReturnValue().Set(result);
    }
    // else: the original threw; the pending exception propagates unchanged.
}

// M9-5: record a Reflect.has hit if reflect-has watching is enabled (mirrors
// reflect_record). The same wrapper also feeds the access-trace `has` operation
// when that channel is enabled; the two channels remain independent.
std::uint64_t reflect_has_record(v8::Isolate* isolate,
                                 v8::Local<v8::Context> context,
                                 v8::Local<v8::Value> target,
                                 v8::Local<v8::Value> key) {
    const AccessTargetInfo target_info = access_target_info(target);
    const std::uint64_t seq = record_access_query(
        isolate, target_info, access_member_name(isolate, context, key),
        AccessOperation::kHas);
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr) {
        return seq;
    }
    WatchRegistry* registry = state->watch_registry();
    if (registry == nullptr || !registry->reflect_has_enabled()) {
        return seq;
    }
    registry->record_reflect_has(reflect_property_string(isolate, context, key),
                                 target_info.type, target_info.label);
    return seq;
}

// M9-5: the Reflect.has hook — records the propertyKey (arg 1), then delegates to
// the original Reflect.has (carried as this function's Data).
void reflect_has_wrapper(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    std::uint64_t seq = 0;
    if (info.Length() > 1) {
        seq = reflect_has_record(isolate, context, info[0], info[1]);
    }
    AccessCallCompletion completion(isolate, seq, info);
    (void)call_wrapped_original(info, context, v8::Undefined(isolate));
}

// M10-1: record a Reflect.ownKeys hit if reflect-ownKeys watching is enabled.
// Reflect.ownKeys(target) has no propertyKey, so the hit carries only the resource
// name (recorded inside the registry).
void reflect_ownkeys_record(v8::Isolate* isolate, v8::Local<v8::Value> target) {
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr) {
        return;
    }
    WatchRegistry* registry = state->watch_registry();
    if (registry == nullptr || !registry->reflect_ownkeys_enabled()) {
        return;
    }
    const AccessTargetInfo target_info = access_target_info(target);
    registry->record_reflect_ownkeys(target_info.type, target_info.label);
}

// M10-1: the Reflect.ownKeys hook — records the call, then delegates to the original
// Reflect.ownKeys (carried as this function's Data).
void reflect_ownkeys_wrapper(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> target = v8::Undefined(isolate);
    if (info.Length() > 0) {
        target = info[0];
    }
    reflect_ownkeys_record(isolate, target);
    v8::Local<v8::Function> original = info.Data().As<v8::Function>();
    std::vector<v8::Local<v8::Value>> args;
    args.reserve(static_cast<std::size_t>(info.Length()));
    for (int i = 0; i < info.Length(); i++) {
        args.push_back(info[i]);
    }
    v8::Local<v8::Value> result;
    if (original
            ->Call(context, v8::Undefined(isolate), info.Length(), args.data())
            .ToLocal(&result)) {
        info.GetReturnValue().Set(result);
    }
    // else: the original threw; the pending exception propagates unchanged.
}

// M10-2: record an Object.keys hit if Object.keys watching is enabled. Object.keys
// (target) has no propertyKey, so the hit carries only the resource name.
void object_keys_record(v8::Isolate* isolate) {
    ContextState* state = state_from_isolate(isolate);
    if (state == nullptr) {
        return;
    }
    WatchRegistry* registry = state->watch_registry();
    if (registry == nullptr || !registry->object_keys_enabled()) {
        return;
    }
    registry->record_object_keys();
}

// M10-2: the Object.keys hook — records the call, then delegates to the original
// Object.keys (carried as this function's Data).
void object_keys_wrapper(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    object_keys_record(isolate);
    v8::Local<v8::Function> original = info.Data().As<v8::Function>();
    std::vector<v8::Local<v8::Value>> args;
    args.reserve(static_cast<std::size_t>(info.Length()));
    for (int i = 0; i < info.Length(); i++) {
        args.push_back(info[i]);
    }
    v8::Local<v8::Value> result;
    if (original
            ->Call(context, v8::Undefined(isolate), info.Length(), args.data())
            .ToLocal(&result)) {
        info.GetReturnValue().Set(result);
    }
    // else: the original threw; the pending exception propagates unchanged.
}

// Step 6d: explicit query wrappers. These are installed only while an access
// trace session exists (or has existed) on this generation. They record a
// request event only for HostObject targets; all other targets are left
// unrecorded. The original function is carried as Data and is always called
// with the original receiver/arguments.
std::uint64_t record_query_call(v8::Isolate* isolate,
                                v8::Local<v8::Context> context,
                                v8::Local<v8::Value> target,
                                v8::Local<v8::Value> key,
                                AccessOperation operation) {
    const AccessTargetInfo target_info = access_target_info(target);
    return record_access_query(
        isolate, target_info, access_member_name(isolate, context, key),
        operation);
}

void object_get_own_property_descriptor_wrapper(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> target = v8::Undefined(isolate);
    v8::Local<v8::Value> key = v8::Undefined(isolate);
    if (info.Length() > 0) target = info[0];
    if (info.Length() > 1) key = info[1];
    const std::uint64_t seq =
        record_query_call(isolate, context, target, key,
                          AccessOperation::kGetOwnPropertyDescriptor);
    AccessCallCompletion completion(isolate, seq, info);
    (void)call_wrapped_original(info, context, v8::Undefined(isolate));
}

void reflect_get_own_property_descriptor_wrapper(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> target = v8::Undefined(isolate);
    v8::Local<v8::Value> key = v8::Undefined(isolate);
    if (info.Length() > 0) target = info[0];
    if (info.Length() > 1) key = info[1];
    const std::uint64_t seq =
        record_query_call(isolate, context, target, key,
                          AccessOperation::kGetOwnPropertyDescriptor);
    AccessCallCompletion completion(isolate, seq, info);
    (void)call_wrapped_original(info, context, v8::Undefined(isolate));
}

void object_has_own_wrapper(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> target = v8::Undefined(isolate);
    v8::Local<v8::Value> key = v8::Undefined(isolate);
    if (info.Length() > 0) target = info[0];
    if (info.Length() > 1) key = info[1];
    const std::uint64_t seq =
        record_query_call(isolate, context, target, key,
                          AccessOperation::kHas);
    AccessCallCompletion completion(isolate, seq, info);
    (void)call_wrapped_original(info, context, v8::Undefined(isolate));
}

void has_own_property_wrapper(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> key = v8::Undefined(isolate);
    if (info.Length() > 0) key = info[0];
    const std::uint64_t seq = record_query_call(
        isolate, context, info.This(), key, AccessOperation::kHas);
    AccessCallCompletion completion(isolate, seq, info);
    (void)call_wrapped_original(info, context, info.This());
}

}  // namespace browser

using namespace browser;

// M9-1: the current generation's V8 Inspector objects (opaque in the header). The
// channel is owned by the embedder and must outlive the session; declaration order
// (channel before session) ensures the session is destroyed first.
struct DevToolsInspector {
    DevToolsClient client;
    DevToolsChannel channel;
    std::unique_ptr<v8_inspector::V8Inspector> inspector;
    std::unique_ptr<v8_inspector::V8InspectorSession> session;
};

PageState::PageState(HostEnvironment environment)
    : environment_(std::move(environment)) {
    // Split-plan stage 0.2: the access-trace identity tokens must resolve to one
    // address program-wide. Fails loudly (every Page construction) if a token
    // ever regresses to a per-translation-unit copy.
    verify_trace_token_identity(IV8_BROWSER_LOCAL_TRACE_TOKEN_ADDRESSES());
    // M9-3: the watch registry requests an Inspector pause through this Page (set
    // once; the registry outlives no generation and is a member of this Page).
    watch_registry_.set_pause_requester([this]() { request_inspector_pause(); });
    // Initial page state uses the fixed default base URL and empty document seed.
    install_page(kDefaultBaseUrl, std::string());
}

std::string PageState::document_cookie() const {
    return cookie_jar_.cookie_string();
}

void PageState::set_document_cookie(const std::string& value) {
    cookie_jar_.set_cookie(value);
}

void PageState::request_inspector_pause() {
    // 口径 1: without an attached Inspector session, break_on_hit records only —
    // no pause, no error.
    if (inspector_ == nullptr || !inspector_->session) {
        return;
    }
    static const char kReason[] = "watch_apis";
    // breakProgram pauses immediately at the current point (we are on the JS stack
    // inside the watched host-method call, like a `debugger` statement). When the
    // Debugger domain is enabled on the session, V8 emits Debugger.paused and calls
    // the client's runMessageLoopOnPause (which counts the pause and returns).
    inspector_->session->breakProgram(
        v8_inspector::StringView(reinterpret_cast<const uint8_t*>(kReason),
                                 sizeof(kReason) - 1),
        v8_inspector::StringView());
}

int PageState::devtools_pause_count() {
    // Internal probe: how many times the current generation's Inspector has paused
    // (0 if devtools was never enabled). Reset per generation with the Inspector.
    return inspector_ != nullptr ? inspector_->client.pause_count() : 0;
}

void PageState::install_inspector(v8::Isolate* isolate,
                                  v8::Local<v8::Context> context) {
    auto inspector = std::make_unique<DevToolsInspector>();
    inspector->inspector =
        v8_inspector::V8Inspector::create(isolate, &inspector->client);
    inspector->channel.set_isolate(isolate);
    static const char kName[] = "iv8 page";
    inspector->inspector->contextCreated(v8_inspector::V8ContextInfo(
        context, /*contextGroupId=*/1,
        v8_inspector::StringView(reinterpret_cast<const uint8_t*>(kName),
                                 sizeof(kName) - 1)));
    inspector_ = std::move(inspector);
}

bool PageState::devtools_enable() {
    // Idempotent: once enabled, install_page keeps a fresh Inspector per
    // generation, so a later call just confirms.
    if (inspector_) {
        devtools_enabled_ = true;
        return true;
    }
    // with_scope runs under the operation guard — a disposed page raises
    // ContextDisposedError here (translated to JSContextDisposedError).
    state_->with_scope([this](v8::Isolate* isolate, v8::Local<v8::Context> context) {
        install_inspector(isolate, context);
    });
    devtools_enabled_ = true;
    return true;
}

py::list PageState::devtools_dispatch(const std::string& message) {
    if (inspector_ == nullptr) {
        // Not enabled (devtools_url() never called): nothing to attach to.
        return py::list();
    }
    state_->with_scope([this, &message](v8::Isolate* isolate,
                                        v8::Local<v8::Context> context) {
        (void)context;
        if (!inspector_->session) {
            inspector_->session = inspector_->inspector->connect(
                /*contextGroupId=*/1, &inspector_->channel,
                v8_inspector::StringView(),
                v8_inspector::V8Inspector::kFullyTrusted);
        }
        inspector_->channel.set_isolate(isolate);
        inspector_->session->dispatchProtocolMessage(v8_inspector::StringView(
            reinterpret_cast<const uint8_t*>(message.data()), message.size()));
    });
    // Drain the synchronously-produced outbound messages (GIL is held — this runs
    // on the caller's thread inside the pybind trampoline).
    py::list out;
    for (const std::string& frame : inspector_->channel.take()) {
        out.append(py::str(frame));
    }
    return out;
}

void PageState::watch_apis(const std::vector<std::string>& paths,
                           bool break_on_hit) {
    // Registration is a plain member operation (no V8), but a disposed page reuses
    // the M1 error path (JSContextDisposedError).
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    // dedup + set break mode + enable (replaces any prior set).
    watch_registry_.set_paths(paths, break_on_hit);
}

py::list PageState::read_watch_api_hits() {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    py::list out;
    for (const WatchHit& hit : watch_registry_.take_hits()) {  // read-and-clear
        py::dict record;
        record["path"] = hit.path;
        record["resource_name"] = hit.resource_name;
        record["type"] = "call";
        out.append(record);
    }
    return out;
}

void PageState::install_reflect_get_hook(v8::Isolate* isolate,
                                         v8::Local<v8::Context> context) {
    v8::Local<v8::Value> reflect_value;
    if (!context->Global()
             ->Get(context, v8_string(isolate, "Reflect"))
             .ToLocal(&reflect_value) ||
        !reflect_value->IsObject()) {
        return;
    }
    v8::Local<v8::Object> reflect = reflect_value.As<v8::Object>();
    v8::Local<v8::Value> get_value;
    if (!reflect->Get(context, v8_string(isolate, "get")).ToLocal(&get_value) ||
        !get_value->IsFunction()) {
        return;
    }
    // Carry the ORIGINAL Reflect.get as the wrapper's Data so it can delegate.
    v8::Local<v8::Function> wrapper;
    if (v8::Function::New(context, &reflect_get_wrapper, get_value)
            .ToLocal(&wrapper)) {
        (void)reflect->Set(context, v8_string(isolate, "get"), wrapper);
        reflect_hook_installed_ = true;
    }
}

void PageState::watch_reflect_get(bool enabled, bool break_on_hit) {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    watch_registry_.set_reflect_get(enabled, break_on_hit);
    // Install the hook on the current generation now (once). When disabled, the
    // existing wrapper stays but is a transparent pass-through (gated on the flag);
    // a later load starts fresh (no hook installed unless still enabled).
    if (enabled && !reflect_hook_installed_) {
        state_->with_scope(
            [this](v8::Isolate* isolate, v8::Local<v8::Context> context) {
                install_reflect_get_hook(isolate, context);
            });
    }
}

py::list PageState::read_reflect_get_hits() {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    py::list out;
    for (const WatchHit& hit : watch_registry_.take_reflect_get_hits()) {
        py::dict record;
        record["property"] = hit.path;  // WatchHit.path carries the propertyKey
        record["resource_name"] = hit.resource_name;
        record["type"] = "Reflect.get";
        record["target_type"] = hit.target_type;
        record["target_label"] = hit.target_label;
        out.append(record);
    }
    return out;
}

void PageState::install_reflect_has_hook(v8::Isolate* isolate,
                                         v8::Local<v8::Context> context) {
    if (reflect_has_hook_installed_) {
        return;
    }
    v8::Local<v8::Value> reflect_value;
    if (!context->Global()
             ->Get(context, v8_string(isolate, "Reflect"))
             .ToLocal(&reflect_value) ||
        !reflect_value->IsObject()) {
        return;
    }
    v8::Local<v8::Object> reflect = reflect_value.As<v8::Object>();
    v8::Local<v8::Value> has_value;
    if (!reflect->Get(context, v8_string(isolate, "has")).ToLocal(&has_value) ||
        !has_value->IsFunction()) {
        return;
    }
    v8::Local<v8::Function> wrapper;
    if (v8::Function::New(context, &reflect_has_wrapper, has_value)
            .ToLocal(&wrapper)) {
        (void)reflect->Set(context, v8_string(isolate, "has"), wrapper);
        reflect_has_hook_installed_ = true;
    }
}

void PageState::watch_reflect_has(bool enabled, bool break_on_hit) {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    watch_registry_.set_reflect_has(enabled, break_on_hit);
    if (enabled && !reflect_has_hook_installed_) {
        state_->with_scope(
            [this](v8::Isolate* isolate, v8::Local<v8::Context> context) {
                install_reflect_has_hook(isolate, context);
            });
    }
}

py::list PageState::read_reflect_has_hits() {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    py::list out;
    for (const WatchHit& hit : watch_registry_.take_reflect_has_hits()) {
        py::dict record;
        record["property"] = hit.path;
        record["resource_name"] = hit.resource_name;
        record["type"] = "Reflect.has";
        record["target_type"] = hit.target_type;
        record["target_label"] = hit.target_label;
        out.append(record);
    }
    return out;
}

void PageState::install_reflect_ownkeys_hook(v8::Isolate* isolate,
                                             v8::Local<v8::Context> context) {
    v8::Local<v8::Value> reflect_value;
    if (!context->Global()
             ->Get(context, v8_string(isolate, "Reflect"))
             .ToLocal(&reflect_value) ||
        !reflect_value->IsObject()) {
        return;
    }
    v8::Local<v8::Object> reflect = reflect_value.As<v8::Object>();
    v8::Local<v8::Value> ownkeys_value;
    if (!reflect->Get(context, v8_string(isolate, "ownKeys"))
             .ToLocal(&ownkeys_value) ||
        !ownkeys_value->IsFunction()) {
        return;
    }
    v8::Local<v8::Function> wrapper;
    if (v8::Function::New(context, &reflect_ownkeys_wrapper, ownkeys_value)
            .ToLocal(&wrapper)) {
        (void)reflect->Set(context, v8_string(isolate, "ownKeys"), wrapper);
        reflect_ownkeys_hook_installed_ = true;
    }
}

void PageState::watch_reflect_ownkeys(bool enabled, bool break_on_hit) {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    watch_registry_.set_reflect_ownkeys(enabled, break_on_hit);
    if (enabled && !reflect_ownkeys_hook_installed_) {
        state_->with_scope(
            [this](v8::Isolate* isolate, v8::Local<v8::Context> context) {
                install_reflect_ownkeys_hook(isolate, context);
            });
    }
}

py::list PageState::read_reflect_ownkeys_hits() {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    py::list out;
    for (const WatchHit& hit : watch_registry_.take_reflect_ownkeys_hits()) {
        py::dict record;  // Reflect.ownKeys has no propertyKey -> resource_name only
        record["resource_name"] = hit.resource_name;
        record["type"] = "Reflect.ownKeys";
        record["target_type"] = hit.target_type;
        record["target_label"] = hit.target_label;
        out.append(record);
    }
    return out;
}

void PageState::install_object_keys_hook(v8::Isolate* isolate,
                                         v8::Local<v8::Context> context) {
    v8::Local<v8::Value> object_value;
    if (!context->Global()
             ->Get(context, v8_string(isolate, "Object"))
             .ToLocal(&object_value) ||
        !object_value->IsObject()) {
        return;
    }
    v8::Local<v8::Object> object_ctor = object_value.As<v8::Object>();
    v8::Local<v8::Value> keys_value;
    if (!object_ctor->Get(context, v8_string(isolate, "keys"))
             .ToLocal(&keys_value) ||
        !keys_value->IsFunction()) {
        return;
    }
    v8::Local<v8::Function> wrapper;
    if (v8::Function::New(context, &object_keys_wrapper, keys_value)
            .ToLocal(&wrapper)) {
        (void)object_ctor->Set(context, v8_string(isolate, "keys"), wrapper);
        object_keys_hook_installed_ = true;
    }
}

void PageState::install_access_trace_query_hooks(
    v8::Isolate* isolate, v8::Local<v8::Context> context) {
    if (access_trace_query_hooks_installed_) {
        return;
    }
    // Reflect.has is shared with the M9-5 channel; install it once.
    if (!reflect_has_hook_installed_) {
        install_reflect_has_hook(isolate, context);
    }
    const auto install_wrapper = [&](v8::Local<v8::Object> holder,
                                     const char* name,
                                     v8::FunctionCallback callback) {
        v8::Local<v8::Value> original;
        if (!holder->Get(context, v8_string(isolate, name)).ToLocal(&original) ||
            !original->IsFunction()) {
            return;
        }
        v8::Local<v8::Function> wrapper;
        if (v8::Function::New(context, callback, original).ToLocal(&wrapper)) {
            (void)holder->Set(context, v8_string(isolate, name), wrapper);
        }
    };
    v8::Local<v8::Value> object_value;
    if (context->Global()
            ->Get(context, v8_string(isolate, "Object"))
            .ToLocal(&object_value) &&
        object_value->IsObject()) {
        v8::Local<v8::Object> object_ctor = object_value.As<v8::Object>();
        install_wrapper(object_ctor, "getOwnPropertyDescriptor",
                        &object_get_own_property_descriptor_wrapper);
        install_wrapper(object_ctor, "hasOwn", &object_has_own_wrapper);
        v8::Local<v8::Value> prototype_value;
        if (object_ctor->Get(context, v8_string(isolate, "prototype"))
                .ToLocal(&prototype_value) &&
            prototype_value->IsObject()) {
            install_wrapper(prototype_value.As<v8::Object>(), "hasOwnProperty",
                            &has_own_property_wrapper);
        }
    }
    v8::Local<v8::Value> reflect_value;
    if (context->Global()
            ->Get(context, v8_string(isolate, "Reflect"))
            .ToLocal(&reflect_value) &&
        reflect_value->IsObject()) {
        install_wrapper(reflect_value.As<v8::Object>(),
                        "getOwnPropertyDescriptor",
                        &reflect_get_own_property_descriptor_wrapper);
    }
    access_trace_query_hooks_installed_ = true;
}

void PageState::watch_object_keys(bool enabled, bool break_on_hit) {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    watch_registry_.set_object_keys(enabled, break_on_hit);
    if (enabled && !object_keys_hook_installed_) {
        state_->with_scope(
            [this](v8::Isolate* isolate, v8::Local<v8::Context> context) {
                install_object_keys_hook(isolate, context);
            });
    }
}

py::list PageState::read_object_keys_hits() {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    py::list out;
    for (const WatchHit& hit : watch_registry_.take_object_keys_hits()) {
        py::dict record;  // Object.keys has no propertyKey -> resource_name only
        record["resource_name"] = hit.resource_name;
        record["type"] = "Object.keys";
        out.append(record);
    }
    return out;
}

void PageState::start_access_trace(
    std::size_t max_events,
    std::optional<std::vector<std::string>> object_types) {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    access_trace_.start(max_events, std::move(object_types));
    // Step 6d: explicit has/descriptor queries need JS wrappers. Install them on
    // the current generation now; install_page re-installs them after a load.
    state_->with_scope(
        [this](v8::Isolate* isolate, v8::Local<v8::Context> context) {
            install_access_trace_query_hooks(isolate, context);
        });
}

void PageState::stop_access_trace() {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    access_trace_.stop();
}

py::dict PageState::read_access_trace(bool clear) {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    const AccessSnapshot snapshot = access_trace_.snapshot(clear);

    py::dict report;
    py::list events;
    for (const AccessEvent& event : snapshot.events) {
        py::dict record;
        record["seq"] = py::cast(event.seq);
        record["generation"] = py::cast(event.generation);
        record["object_id"] = py::cast(event.object_id);
        record["object_type"] = event.object_type;
        record["label"] = event.label;
        record["member"] = event.member;
        record["operation"] = event.operation;
        record["phase"] = event.phase;
        if (!event.resolution.empty()) {
            record["resolution"] = event.resolution;
        }
        record["resource_name"] = event.resource_name;
        record["source_kind"] = event.source_kind;
        if (!event.outcome.empty()) {
            record["outcome"] = event.outcome;
        }
        if (!event.value_summary.empty()) {
            record["value_summary"] = event.value_summary;
        }
        if (!event.return_summary.empty()) {
            record["return_summary"] = event.return_summary;
        }
        if (!event.exception_name.empty()) {
            record["exception_name"] = event.exception_name;
        }
        if (!event.exception_message.empty()) {
            record["exception_message"] = event.exception_message;
        }
        if (!event.exception_stack.empty()) {
            record["exception_stack"] = event.exception_stack;
        }
        if (!event.exception_location.empty()) {
            record["exception_location"] = event.exception_location;
        }
        if (event.operation == "call") {
            record["argument_count"] = py::cast(event.argument_count);
            py::list argument_summaries;
            for (const std::string& summary : event.argument_summaries) {
                argument_summaries.append(summary);
            }
            record["argument_summaries"] = argument_summaries;
        }
        events.append(record);
    }
    report["events"] = events;
    report["enabled"] = snapshot.enabled;
    report["generation"] = py::cast(snapshot.generation);
    report["max_events"] = py::cast(snapshot.max_events);
    report["dropped_events"] = py::cast(snapshot.dropped_events);
    if (snapshot.filter_object_types.has_value()) {
        py::list filter;
        for (const std::string& type : *snapshot.filter_object_types) {
            filter.append(type);
        }
        report["filter_object_types"] = filter;
    } else {
        report["filter_object_types"] = py::none();
    }

    py::dict coverage;
    coverage["scope"] =
        "installed HostObject callbacks, explicit query wrappers, and traced "
        "native BOM callbacks";
    py::list object_types;
    for (const std::string& type : AccessTrace::supported_object_types()) {
        object_types.append(type);
    }
    coverage["object_types"] = object_types;
    py::list operations;
    operations.append("get");
    operations.append("set");
    operations.append("get_method");
    operations.append("call");
    operations.append("get_probe");
    operations.append("has");
    operations.append("delete");
    operations.append("own_keys");
    operations.append("get_own_property_descriptor");
    operations.append("define_property");
    coverage["operations"] = operations;
    coverage["phase"] = "enter";
    coverage["summaries"] =
        "generic HostObject and traced native get/set/call plus has/descriptor "
        "queries: returned/threw outcome, primitive value/argument/return "
        "summaries, first 8 arguments, safe exception name/message/location/"
        "stack; string content capped at 64 bytes with the original byte "
        "length and exception text capped at 4096 bytes";
    py::list not_covered;
    for (const std::string& gap : AccessTrace::not_covered()) {
        not_covered.append(gap);
    }
    coverage["not_covered"] = not_covered;
    report["coverage"] = coverage;
    return report;
}

// M11-35: window.name is a Page-level mutable string. The accessor data carries
// the owning PageState pointer; the PageState outlives every generation, and the
// context is torn down before it, so no callback can outlive it.
constexpr v8::ExternalPointerTypeTag kWindowNameTag =
    v8::kExternalPointerTypeTagDefault;

void window_name_getter(v8::Local<v8::Name>,
                        const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kWindowNameTag));
    const std::uint64_t seq = access_trace_record(
        isolate, page, "window", "window", "name", AccessOperation::kGet);
    AccessPropertyCompletion completion(isolate, seq, info);
    info.GetReturnValue().Set(v8_string(isolate, page->window_name()));
}

void window_name_setter(v8::Local<v8::Name>, v8::Local<v8::Value> value,
                        const v8::PropertyCallbackInfo<void>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kWindowNameTag));
    const std::uint64_t seq = access_trace_record(
        isolate, page, "window", "window", "name", AccessOperation::kSet);
    AccessPropertySetCompletion completion(isolate, seq, info, value);
    v8::Local<v8::String> str;
    if (!value->ToString(context).ToLocal(&str)) {
        return;  // coercion threw; leave the stored name unchanged
    }
    v8::String::Utf8Value utf8(isolate, str);
    page->set_window_name(
        *utf8 != nullptr
            ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
            : std::string());
}

// M11-50: window.status is a writable string that resets with each generation.
// It shares window.name's coercion and trace shape, but is PageState state that
// install_page() clears on every new document.
void window_status_getter(v8::Local<v8::Name>,
                          const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kWindowNameTag));
    const std::uint64_t seq = access_trace_record(
        isolate, page, "window", "window", "status", AccessOperation::kGet);
    AccessPropertyCompletion completion(isolate, seq, info);
    info.GetReturnValue().Set(v8_string(isolate, page->window_status()));
}

void window_status_setter(v8::Local<v8::Name>, v8::Local<v8::Value> value,
                          const v8::PropertyCallbackInfo<void>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kWindowNameTag));
    const std::uint64_t seq = access_trace_record(
        isolate, page, "window", "window", "status", AccessOperation::kSet);
    AccessPropertySetCompletion completion(isolate, seq, info, value);
    v8::Local<v8::String> str;
    if (!value->ToString(context).ToLocal(&str)) {
        return;  // coercion threw; leave the stored status unchanged
    }
    v8::String::Utf8Value utf8(isolate, str);
    page->set_window_status(
        *utf8 != nullptr
            ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
            : std::string());
}

// Step 5i: window.parent / top / frames / opener are traced accessors.
// parent/frames/opener are replaceable (the setter stores the new value per
// generation); top stays read-only. Values and assignment results are unchanged.
constexpr v8::ExternalPointerTypeTag kWindowAliasTag =
    v8::kExternalPointerTypeTagDefault;

void window_alias_getter(v8::Local<v8::Name> property,
                         const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kWindowAliasTag));
    if (page == nullptr) {
        return;
    }
    v8::String::Utf8Value utf8(isolate, property);
    const std::string name(*utf8 != nullptr ? *utf8 : "");
    const std::uint64_t seq = access_trace_record(
        isolate, page, "window", "window", name.c_str(),
        AccessOperation::kGet);
    AccessPropertyCompletion completion(isolate, seq, info);
    info.GetReturnValue().Set(page->window_alias_value(
        isolate, isolate->GetCurrentContext(), name));
}

void window_alias_setter(v8::Local<v8::Name> property,
                         v8::Local<v8::Value> value,
                         const v8::PropertyCallbackInfo<void>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kWindowAliasTag));
    if (page == nullptr) {
        return;
    }
    v8::String::Utf8Value utf8(isolate, property);
    const std::string name(*utf8 != nullptr ? *utf8 : "");
    const std::uint64_t seq = access_trace_record(
        isolate, page, "window", "window", name.c_str(),
        AccessOperation::kSet);
    AccessPropertySetCompletion completion(isolate, seq, info, value);
    page->set_window_alias(isolate, name, value);
}

// M11-35: a deterministic secure-context bit derived from the base URL (no host
// OS or network access): https/wss/file are secure; http/ws are secure only for
// loopback hosts; everything else is insecure.
bool is_secure_context_url(const std::string& base_url) {
    const std::string lower = ascii_lower(base_url);
    if (lower.rfind("https://", 0) == 0 || lower.rfind("wss://", 0) == 0 ||
        lower.rfind("file://", 0) == 0) {
        return true;
    }
    if (lower.rfind("http://", 0) == 0 || lower.rfind("ws://", 0) == 0) {
        const UrlParts parts = decompose_url(base_url);
        const std::string host = ascii_lower(parts.hostname);
        return host == "localhost" || host == "127.0.0.1" ||
               host == "::1" || host == "[::1]";
    }
    return false;
}

// Host-access trace, step 5: the value source for the generic window metadata
// accessors. Values are computed from fixed Page state; no JS object is touched.
v8::Local<v8::Value> PageState::window_metadata_value(
    v8::Isolate* isolate, const std::string& name) const {
    if (name == "devicePixelRatio") {
        return v8::Number::New(isolate, environment_.device_pixel_ratio);
    }
    if (name == "innerWidth") {
        return v8::Integer::New(isolate, environment_.inner_width);
    }
    if (name == "innerHeight") {
        return v8::Integer::New(isolate, environment_.inner_height);
    }
    if (name == "outerWidth") {
        return v8::Integer::New(isolate, environment_.outer_width);
    }
    if (name == "outerHeight") {
        return v8::Integer::New(isolate, environment_.outer_height);
    }
    if (name == "screenX" || name == "screenLeft") {
        return v8::Integer::New(isolate, environment_.screen_x);
    }
    if (name == "screenY" || name == "screenTop") {
        return v8::Integer::New(isolate, environment_.screen_y);
    }
    if (name == "length") {
        return v8::Integer::New(isolate, 0);
    }
    if (name == "isSecureContext") {
        return v8::Boolean::New(isolate,
                               is_secure_context_url(bootstrap_.base_url));
    }
    if (name == "origin") {
        return v8_string(isolate, decompose_url(bootstrap_.base_url).origin);
    }
    if (name == "crossOriginIsolated" || name == "closed") {
        return v8::False(isolate);
    }
    if (name == "frameElement") {
        return v8::Null(isolate);
    }
    // M11-50: there is no layout engine, so the viewport never scrolls; the
    // legacy pageXOffset/pageYOffset names alias scrollX/scrollY.
    if (name == "scrollX" || name == "pageXOffset" ||
        name == "scrollY" || name == "pageYOffset") {
        return v8::Integer::New(isolate, 0);
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> PageState::window_alias_value(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const std::string& name) const {
    if (name == "parent") {
        return window_parent_override_.IsEmpty()
                   ? v8::Local<v8::Value>(context->Global())
                   : window_parent_override_.Get(isolate);
    }
    if (name == "frames") {
        return window_frames_override_.IsEmpty()
                   ? v8::Local<v8::Value>(context->Global())
                   : window_frames_override_.Get(isolate);
    }
    if (name == "top") {
        return context->Global();
    }
    if (name == "opener") {
        return window_opener_override_.IsEmpty()
                   ? v8::Local<v8::Value>(v8::Null(isolate))
                   : window_opener_override_.Get(isolate);
    }
    return v8::Undefined(isolate);
}

void PageState::set_window_alias(v8::Isolate* isolate, const std::string& name,
                                 v8::Local<v8::Value> value) {
    if (name == "parent") {
        window_parent_override_.Reset(isolate, value);
    } else if (name == "frames") {
        window_frames_override_.Reset(isolate, value);
    } else if (name == "opener") {
        window_opener_override_.Reset(isolate, value);
    }
}

void PageState::reset_window_aliases() noexcept {
    window_parent_override_.Reset();
    window_frames_override_.Reset();
    window_opener_override_.Reset();
}

constexpr v8::ExternalPointerTypeTag kWindowTraceTag =
    v8::kExternalPointerTypeTagDefault;

void window_metadata_getter(v8::Local<v8::Name> property,
                            const v8::PropertyCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    auto* page = static_cast<PageState*>(
        info.Data().As<v8::External>()->Value(kWindowTraceTag));
    v8::String::Utf8Value utf8(isolate, property);
    if (*utf8 == nullptr) {
        return;
    }
    const std::string name(*utf8, static_cast<std::size_t>(utf8.length()));
    const std::uint64_t seq = access_trace_record(
        isolate, page, "window", "window", name.c_str(),
        AccessOperation::kGet);
    AccessPropertyCompletion completion(isolate, seq, info);
    info.GetReturnValue().Set(page->window_metadata_value(isolate, name));
}

HostObject* PageState::media_query_list_host(const std::u16string& media,
                                              bool matches) {
    if (!media_query_lists_) {
        media_query_lists_ = std::make_unique<MediaQueryListStore>();
    }
    std::u16string key = media;
    key.push_back(u'\x01');
    key.push_back(matches ? u'1' : u'0');
    auto& by_key = media_query_lists_->by_key;
    auto it = by_key.find(key);
    if (it == by_key.end()) {
        it = by_key
                 .emplace(key, std::make_unique<MediaQueryListHost>(media,
                                                                    matches))
                 .first;
    }
    return it->second.get();
}

v8::Local<v8::Value> PageState::create_document(v8::Isolate* isolate,
    v8::Local<v8::Context> context, bool html_document) {
    auto document = std::make_unique<DocumentHost>("", "about:blank", this, false, html_document);
    auto object = make_host_object(isolate, context, document.get());
    document->set_wrapper(isolate, object);
    host_objects_.push_back(std::move(document));
    return object;
}

v8::Local<v8::Value> PageState::create_web_interface(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name, v8::Local<v8::Array> arguments) {
    if (name == "XMLHttpRequest" || name == "Worker") {
        isolate->ThrowException(unsupported_capability(isolate, name));
        return v8::Undefined(isolate);
    }
    if (name == "Document") return create_document(isolate, context);
    if (name == "EventTarget") {
        auto target = std::make_unique<StandaloneEventTarget>();
        auto object = make_host_object(isolate, context, target.get());
        host_objects_.push_back(std::move(target));
        return object;
    }
    auto* document = static_cast<DocumentHost*>(document_host_);
    auto node = std::make_unique<DomNode>();
    node->kind = name == "Text" ? NodeKind::Text : name == "Comment" ? NodeKind::Comment : NodeKind::DocumentFragment;
    if (node->kind != NodeKind::DocumentFragment && arguments->Length() > 0) {
        v8::Local<v8::Value> value;
        if (!arguments->Get(context, 0).ToLocal(&value)) return v8::Undefined(isolate);
        if (!value->IsUndefined()) {
            v8::Local<v8::String> text;
            if (!value->ToString(context).ToLocal(&text)) return v8::Undefined(isolate);
            v8::String::Utf8Value utf8(isolate, text);
            if (*utf8 != nullptr) node->data.assign(*utf8, static_cast<std::size_t>(utf8.length()));
        }
    }
    return document->wrap_created_node(isolate, context, std::move(node));
}

void PageState::install_page(const std::string& base_url,
                             const std::string& html) {
    bootstrap_ = PageBootstrap{html, base_url};
    // M11-50: window.status is per-document state; every generation starts "".
    window_status_.clear();

    state_ = std::make_shared<ContextState>();
    // Host-access tracing is Page-level. Each generation starts with fresh
    // native identity bindings; the trace configuration/events themselves persist.
    ++generation_;
    access_trace_.begin_generation(generation_);
    state_->set_access_trace(&access_trace_);
    // M11-37: the performance timeline is per-generation.
    performance_store_ = std::make_unique<PerformanceStore>();
    // M9-4/M9-5/M10-1: a fresh generation has pristine (unwrapped) Reflect.get /
    // .has / .ownKeys.
    reflect_hook_installed_ = false;
    reflect_has_hook_installed_ = false;
    reflect_ownkeys_hook_installed_ = false;
    object_keys_hook_installed_ = false;  // M10-2
    access_trace_query_hooks_installed_ = false;  // step 6d
    // M9-2: point the new generation at the Page-level watch registry so its
    // host-method dispatch points can record hits and eval() can stamp the current
    // script's resource name. The registry itself outlives generations.
    state_->set_watch_registry(&watch_registry_);
    state_->set_async_error_capture(async_error_capture_);
    // M3-3/M3-4: before this context's isolate is disposed, release the event-
    // listener Global handles (teardown ordering, mirroring timers). The hook
    // reads host_objects_ + window_events_ lazily at teardown time, so it sees
    // whatever this generation installed below. DocumentHost forwards to its
    // wrapped elements; window_events_ holds window's listeners.
    state_->set_on_teardown([this]() {
        // M9-1: tear down this generation's Inspector (session before inspector)
        // while the isolate is still alive, before releasing host handles.
        if (inspector_) {
            inspector_->session.reset();
            inspector_->inspector.reset();
            inspector_.reset();
        }
        for (const std::unique_ptr<HostObject>& host : host_objects_) {
            host->release_v8_handles();
        }
        // Step 5i: drop replaced parent/frames/opener values while the isolate is
        // still alive.
        reset_window_aliases();
        if (window_events_) {
            window_events_->release_v8_handles();
        }
    });
    host_objects_.clear();
    // Step 5g: matchMedia backings are per-generation; their JS wrappers died
    // with the previous context above.
    media_query_lists_.reset();
    host_objects_.push_back(std::make_unique<HostProbe>());    // M2-1 probe
    host_objects_.push_back(std::make_unique<ConsoleHost>());  // M2-2 console
    // M2-3 navigator + M11-1 screen, both sourced from the Page-level environment_
    // (which outlives generations), so every load() reports identical values.
    host_objects_.push_back(std::make_unique<NavigatorHost>(&environment_));
    host_objects_.push_back(std::make_unique<ScreenHost>(&environment_));  // M11-1
    host_objects_.push_back(  // M2-3 location, sourced from this page's base URL
        std::make_unique<LocationHost>(decompose_url(base_url), this));
    auto document = std::make_unique<DocumentHost>(html, base_url, this);  // M2-6
    document_host_ = document.get();  // M3-4 observing ptr for lifecycle dispatch
    host_objects_.push_back(std::move(document));
    // M3-4 window event target: NOT installed as a named global (window stays the
    // intrinsic global object); it only backs window's listener store.
    window_events_ = std::make_unique<WindowEventTarget>();
    // M4-A-7: give the document the window target so element/document bubbling can
    // reach window as the final hop. document_host_ is the DocumentHost just added.
    static_cast<DocumentHost*>(document_host_)
        ->set_window_target(static_cast<EventTargetHost*>(window_events_.get()));

    // Install the host objects and the browser-like global roots into the
    // context. window / self are aliases of the global object; globalThis is the
    // intrinsic global — so window === globalThis and self === window.
    {
        // Bootstrap accesses are internal, not target-script accesses.
        AccessTrace::SourceScope trace_scope(&access_trace_, std::string(),
                                             "internal");
        state_->with_scope(
            [this, &base_url](v8::Isolate* isolate,
                              v8::Local<v8::Context> context) {
            install_web_interfaces(isolate, context, &web_interface_factory,
                v8::External::New(isolate, this, v8::kExternalPointerTypeTagDefault));
            set_window_host(isolate, context, window_events_.get());
            install_host_interface(isolate, context, window_events_.get());
            for (const auto& host : host_objects_) {
                install_host_object(isolate, context, host.get());
            }
            v8::Local<v8::Object> global = context->Global();
            auto* document = static_cast<DocumentHost*>(document_host_);
            v8::Local<v8::Value> document_object;
            if (!global->Get(context, v8_string(isolate, "document")).ToLocal(&document_object)) return;
            document->set_wrapper(isolate, document_object.As<v8::Object>());
            // Register tag-specific member sets without creating JS instances.
            for (const char* tag : {"html", "head", "body", "title", "div", "span", "p",
                    "form", "script", "meta", "a", "area", "input", "button", "select",
                    "textarea", "option", "canvas", "img", "iframe", "link", "style"}) {
                DomNode node;
                node.tag = tag;
                ElementHost host(&node, document);
                install_host_interface(isolate, context, &host);
            }
            for (auto kind : {NodeKind::Text, NodeKind::Comment, NodeKind::DocumentFragment}) {
                DomNode node;
                node.kind = kind;
                if (kind == NodeKind::DocumentFragment) {
                    DocumentFragmentHost host(&node, document);
                    install_host_interface(isolate, context, &host);
                } else {
                    CharacterDataHost host(&node, document);
                    install_host_interface(isolate, context, &host);
                }
            }
            auto window_prototype = web_interface_prototype(isolate, context, "Window");
            (void)global->SetPrototype(context, window_prototype);
            v8::Local<v8::Value> location;
            if (!global->Get(context, v8_string(isolate, "location")).ToLocal(&location)) return;
            (void)global->SetPrivate(context, window_location_slot(isolate), location);
            v8::Local<v8::Function> location_getter;
            v8::Local<v8::Function> location_setter;
            if (!make_web_method(isolate, context, "get location", 0, &window_location_get).ToLocal(&location_getter) ||
                !make_web_method(isolate, context, "set location", 1, &window_location_set).ToLocal(&location_setter)) return;
            v8::PropertyDescriptor location_descriptor(location_getter, location_setter);
            location_descriptor.set_enumerable(true);
            location_descriptor.set_configurable(false);
            (void)global->DefineProperty(context, v8_string(isolate, "location").As<v8::String>(), location_descriptor);
            install_global_function(isolate, context, global, "fetch", &offline_fetch);

            (void)global->Set(
                context, v8::String::NewFromUtf8Literal(isolate, "window"), global);
            (void)global->Set(
                context, v8::String::NewFromUtf8Literal(isolate, "self"), global);
            // M11-35: mutable window.name, persisted by PageState across load().
            (void)global->SetNativeDataProperty(
                context, v8::String::NewFromUtf8Literal(isolate, "name"),
                &window_name_getter, &window_name_setter,
                v8::External::New(isolate, this, kWindowNameTag));
            // M11-50: writable window.status, reset per generation (see
            // install_page). It follows the same ToString + trace shape as name.
            (void)global->SetNativeDataProperty(
                context, v8::String::NewFromUtf8Literal(isolate, "status"),
                &window_status_getter, &window_status_setter,
                v8::External::New(isolate, this, kWindowNameTag));
            // M2-4 JS-visible timers (manual-pump; see PageState::run_timers).
            install_global_function(isolate, context, global, "setTimeout",
                                    &set_timeout_callback);
            install_global_function(isolate, context, global, "clearTimeout",
                                    &clear_timer_callback);
            install_global_function(isolate, context, global, "setInterval",
                                    &set_interval_callback);
            install_global_function(isolate, context, global, "clearInterval",
                                    &clear_timer_callback);
            // M11-4 JS-visible microtasks. This feeds the existing V8 microtask
            // queue; Page.run_jobs() is the only trigger (no new pump).
            install_global_function(isolate, context, global, "queueMicrotask",
                                    &queue_microtask_callback);
            // M11-2 JS-visible animation frames (manual-pump; see
            // PageState::run_animation_frame). No clock, no background thread — a
            // queued callback runs only when Page.run_animation_frame() is called.
            install_global_function(isolate, context, global,
                                    "requestAnimationFrame",
                                    &request_animation_frame_callback);
            install_global_function(isolate, context, global,
                                    "cancelAnimationFrame",
                                    &cancel_animation_frame_callback);
            // M11-3 JS-visible idle callbacks (manual-pump; see
            // PageState::run_idle_callbacks). Independent of the timer and
            // animation-frame queues; nothing fires until the pump is called.
            install_global_function(isolate, context, global,
                                    "requestIdleCallback",
                                    &request_idle_callback_callback);
            install_global_function(isolate, context, global,
                                    "cancelIdleCallback",
                                    &cancel_idle_callback_callback);
            // M11-36 deterministic dialog stubs (no UI, no blocking).
            install_global_function(isolate, context, global, "alert",
                                    &alert_callback);
            install_global_function(isolate, context, global, "confirm",
                                    &confirm_callback);
            install_global_function(isolate, context, global, "prompt",
                                    &prompt_callback);
            install_global_function(isolate, context, global, "print",
                                    &print_callback);
            // M11-50 deterministic scroll / focus / OS-window stubs. No layout,
            // focus, or window-manager integration; all return undefined.
            install_global_function(isolate, context, global, "scroll",
                                    &window_scroll_callback);
            install_global_function(isolate, context, global, "scrollTo",
                                    &window_scroll_to_callback);
            install_global_function(isolate, context, global, "scrollBy",
                                    &window_scroll_by_callback);
            install_global_function(isolate, context, global, "focus",
                                    &window_focus_callback);
            install_global_function(isolate, context, global, "blur",
                                    &window_blur_callback);
            install_global_function(isolate, context, global, "moveBy",
                                    &window_move_by_callback);
            install_global_function(isolate, context, global, "moveTo",
                                    &window_move_to_callback);
            install_global_function(isolate, context, global, "resizeBy",
                                    &window_resize_by_callback);
            install_global_function(isolate, context, global, "resizeTo",
                                    &window_resize_to_callback);
            install_global_function(isolate, context, global, "stop",
                                    &window_stop_callback);
            // M11-38 bounded structured clone.
            install_global_function(isolate, context, global, "structuredClone",
                                    &structured_clone_callback);
            // M11-31/M11-37 virtual performance clock and entry timeline:
            // performance.now() reads the facade clock; timeOrigin is the virtual
            // epoch (0); mark/measure/getEntries*/clear* use virtual timestamps.
            // There is no PerformanceObserver, resource/navigation timing, or
            // real clock.
            {
                v8::Local<v8::Object> performance = v8::Object::New(isolate);
                const auto define_performance_readonly =
                    [&](const char* name, v8::Local<v8::Value> value) {
                        (void)performance->DefineOwnProperty(
                            context,
                            v8::String::NewFromUtf8(isolate, name)
                                .ToLocalChecked(),
                            value,
                            static_cast<v8::PropertyAttribute>(
                                v8::ReadOnly | v8::DontDelete));
                    };
                v8::Local<v8::Function> now_fn =
                    v8::FunctionTemplate::New(isolate, &performance_now_callback)
                        ->GetFunction(context)
                        .ToLocalChecked();
                define_performance_readonly("now", now_fn);
                (void)performance->SetNativeDataProperty(
                    context,
                    v8::String::NewFromUtf8Literal(isolate, "timeOrigin"),
                    &performance_time_origin_getter, nullptr,
                    v8::Local<v8::Value>(),
                    static_cast<v8::PropertyAttribute>(v8::ReadOnly |
                                                       v8::DontDelete));
                v8::Local<v8::External> performance_data =
                    v8::External::New(isolate, performance_store_.get(),
                                      kPerformanceTag);
                const auto add_performance_method =
                    [&](const char* name, v8::FunctionCallback callback) {
                        v8::Local<v8::Function> fn =
                            v8::FunctionTemplate::New(isolate, callback,
                                                      performance_data)
                                ->GetFunction(context)
                                .ToLocalChecked();
                        define_performance_readonly(name, fn);
                    };
                add_performance_method("mark", &performance_mark_callback);
                add_performance_method("measure", &performance_measure_callback);
                add_performance_method("getEntries",
                                       &performance_get_entries_callback);
                add_performance_method(
                    "getEntriesByName",
                    &performance_get_entries_by_name_callback);
                add_performance_method(
                    "getEntriesByType",
                    &performance_get_entries_by_type_callback);
                add_performance_method("clearMarks",
                                       &performance_clear_marks_callback);
                add_performance_method("clearMeasures",
                                       &performance_clear_measures_callback);
                (void)global->DefineOwnProperty(
                    context, v8::String::NewFromUtf8Literal(isolate, "performance"),
                    performance,
                    static_cast<v8::PropertyAttribute>(v8::ReadOnly |
                                                       v8::DontDelete));
            }
            // M11-33 OS-backed Crypto slice: getRandomValues + randomUUID.
            // crypto.subtle is intentionally absent in this milestone.
            {
                v8::Local<v8::Object> crypto = v8::Object::New(isolate);
                const auto add_crypto_method = [&](const char* name,
                                                   v8::FunctionCallback callback) {
                    v8::Local<v8::Function> fn =
                        v8::FunctionTemplate::New(isolate, callback)
                            ->GetFunction(context)
                            .ToLocalChecked();
                    (void)crypto->DefineOwnProperty(
                        context,
                        v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
                        fn,
                        static_cast<v8::PropertyAttribute>(v8::ReadOnly |
                                                           v8::DontDelete));
                };
                add_crypto_method("getRandomValues",
                                  &crypto_get_random_values_callback);
                add_crypto_method("randomUUID", &crypto_random_uuid_callback);
                (void)global->DefineOwnProperty(
                    context, v8::String::NewFromUtf8Literal(isolate, "crypto"),
                    crypto,
                    static_cast<v8::PropertyAttribute>(v8::ReadOnly |
                                                       v8::DontDelete));
            }
            // M11-1/M11-26 window geometry. window is the intrinsic global object;
            // these are read-only native accessors on it. Values come from the
            // Page-level environment_ and are re-applied on every generation. M11-35 adds window.length,
            // screenLeft/screenTop aliases, and isSecureContext. Geometry and
            // capabilities are read-only (ReadOnly|DontDelete); no scroll /
            // resize / matchMedia surface.
            {
                // Host-access trace step 5: these were read-only data properties;
                // they are now read-only native accessors so their reads can be
                // observed. Values and assignment behavior are unchanged.
                const auto define_traced_readonly = [&](const char* name) {
                    (void)global->SetNativeDataProperty(
                        context,
                        v8::String::NewFromUtf8(isolate, name)
                            .ToLocalChecked(),
                        &window_metadata_getter, nullptr,
                        v8::External::New(isolate, this, kWindowTraceTag),
                        static_cast<v8::PropertyAttribute>(
                            v8::ReadOnly | v8::DontDelete));
                };
                define_traced_readonly("devicePixelRatio");
                define_traced_readonly("innerWidth");
                define_traced_readonly("innerHeight");
                // M11-26: reported window geometry (read-only; no layout engine).
                define_traced_readonly("outerWidth");
                define_traced_readonly("outerHeight");
                define_traced_readonly("screenX");
                define_traced_readonly("screenY");
                // M11-35: no child browsing contexts -> length 0; screenLeft/Top
                // alias the reported screen coordinates.
                define_traced_readonly("length");
                define_traced_readonly("screenLeft");
                define_traced_readonly("screenTop");
                define_traced_readonly("isSecureContext");
                // M11-41 window flags, now sourced through the same accessor.
                define_traced_readonly("origin");
                define_traced_readonly("crossOriginIsolated");
                define_traced_readonly("closed");
                define_traced_readonly("frameElement");
                // M11-50: no layout engine, so the viewport never scrolls.
                // scrollX/scrollY are always 0 and the legacy pageXOffset /
                // pageYOffset names alias them; scroll()/scrollTo()/scrollBy()
                // are no-ops (installed above with the other global stubs).
                define_traced_readonly("scrollX");
                define_traced_readonly("scrollY");
                define_traced_readonly("pageXOffset");
                define_traced_readonly("pageYOffset");
                // M11-39: this runtime has exactly one top-level browsing
                // context. parent/top/frames alias the window itself; there is
                // no opener and no child frame collection. parent and frames are
                // replaceable; top is the [LegacyUnforgeable] read-only alias.
                // Step 5i: these are traced accessors now. parent/frames/opener
                // keep their writable+configurable behavior (the setter stores a
                // per-generation replacement, and `delete` / top-level `let`
                // declarations still work); top stays read-only. Descriptor reads
                // evaluate the getter.
                const auto define_traced_alias = [&](const char* name,
                                                     bool writable) {
                    v8::Local<v8::Name> key =
                        v8::String::NewFromUtf8(isolate, name).ToLocalChecked();
                    v8::Local<v8::Value> data =
                        v8::External::New(isolate, this, kWindowAliasTag);
                    if (writable) {
                        (void)global->SetNativeDataProperty(
                            context, key, &window_alias_getter,
                            &window_alias_setter, data,
                            v8::PropertyAttribute::None);
                    } else {
                        (void)global->SetNativeDataProperty(
                            context, key, &window_alias_getter, nullptr, data,
                            static_cast<v8::PropertyAttribute>(
                                v8::ReadOnly | v8::DontDelete));
                    }
                };
                define_traced_alias("parent", true);
                define_traced_alias("top", false);
                define_traced_alias("frames", true);
                define_traced_alias("opener", true);
                // M11-50: browser-chrome BarProp metadata. There is no browser
                // UI, but the reported desktop window is complete: every bar is
                // a fresh read-only {visible: true} object. These are plain
                // metadata, not traced HostObjects.
                const auto define_bar_prop = [&](const char* name) {
                    v8::Local<v8::Object> bar = v8::Object::New(isolate);
                    (void)bar->DefineOwnProperty(
                        context,
                        v8::String::NewFromUtf8Literal(isolate, "visible"),
                        v8::True(isolate),
                        static_cast<v8::PropertyAttribute>(v8::ReadOnly |
                                                           v8::DontDelete));
                    (void)global->DefineOwnProperty(
                        context,
                        v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
                        bar,
                        static_cast<v8::PropertyAttribute>(v8::ReadOnly |
                                                           v8::DontDelete));
                };
                define_bar_prop("locationbar");
                define_bar_prop("menubar");
                define_bar_prop("personalbar");
                define_bar_prop("scrollbars");
                define_bar_prop("statusbar");
                define_bar_prop("toolbar");
                // M11-50: legacy alias required by older feature detection.
                v8::Local<v8::Value> navigator_value;
                if (global->Get(context, v8_string(isolate, "navigator"))
                        .ToLocal(&navigator_value)) {
                    (void)global->DefineOwnProperty(
                        context,
                        v8::String::NewFromUtf8Literal(isolate,
                                                       "clientInformation"),
                        navigator_value,
                        static_cast<v8::PropertyAttribute>(v8::ReadOnly |
                                                           v8::DontDelete));
                }
            }
            // M11-40 scheduler.postTask bootstrap. Compiled after setTimeout and
            // Promise are available; scheduler tasks therefore use the existing
            // timer queue and explicit pumps.
            {
                v8::TryCatch try_catch(isolate);
                v8::Local<v8::String> source = v8::String::NewFromUtf8(
                    isolate, kSchedulerBootstrap,
                    v8::NewStringType::kNormal,
                    static_cast<int>(sizeof(kSchedulerBootstrap) - 1))
                    .ToLocalChecked();
                v8::Local<v8::Script> script;
                if (v8::Script::Compile(context, source).ToLocal(&script)) {
                    (void)script->Run(context);
                }
                if (try_catch.HasCaught()) {
                    try_catch.Reset();  // fixed bootstrap; never poison install
                }
            }
            // Step 5g: temporary native factory the bootstrap captures in a
            // closure and deletes immediately, so matchMedia stays a JS function
            // while each result object gets a traceable HostObject backing.
            {
                v8::Local<v8::Function> factory =
                    v8::FunctionTemplate::New(
                        isolate, &make_media_query_list_callback,
                        v8::External::New(isolate, this, kMediaQueryFactoryTag))
                        ->GetFunction(context)
                        .ToLocalChecked();
                (void)global->Set(
                    context,
                    v8::String::NewFromUtf8Literal(
                        isolate, "__iv8MakeMediaQueryList"),
                    factory);
            }
            // M11-41..M11-45 BOM metadata/device stubs. Installed after the
            // window dimensions and navigator so the bootstrap can derive the
            // static visual viewport and media-query results from them.
            {
                v8::TryCatch try_catch(isolate);
                v8::Local<v8::String> source = v8::String::NewFromUtf8(
                    isolate, kBomExtrasBootstrap,
                    v8::NewStringType::kNormal,
                    static_cast<int>(sizeof(kBomExtrasBootstrap) - 1))
                    .ToLocalChecked();
                v8::Local<v8::Script> script;
                if (v8::Script::Compile(context, source).ToLocal(&script)) {
                    (void)script->Run(context);
                }
                if (try_catch.HasCaught()) {
                    try_catch.Reset();  // fixed bootstrap; never poison install
                }
            }
            // Never leave the temporary factory visible to page scripts.
            (void)global->Delete(
                context,
                v8::String::NewFromUtf8Literal(isolate,
                                               "__iv8MakeMediaQueryList"));
            // Step 5d: the static visual viewport is a HostObject (the bootstrap
            // above no longer creates it), so get/set/call/probe entries are
            // observable. Width/height still come from environment_; the global
            // property stays read-only. Internal descriptor change: inner
            // metadata fields are now native accessors instead of data
            // properties; values and write behavior are unchanged.
            {
                auto viewport =
                    std::make_unique<VisualViewportHost>(&environment_);
                v8::Local<v8::Object> viewport_object =
                    make_host_object(isolate, context, viewport.get());
                host_objects_.push_back(std::move(viewport));
                (void)global->DefineOwnProperty(
                    context,
                    v8::String::NewFromUtf8Literal(isolate, "visualViewport"),
                    viewport_object,
                    static_cast<v8::PropertyAttribute>(v8::ReadOnly |
                                                       v8::DontDelete));
            }
            // M11-7 Web Storage: two independent, per-Page in-memory stores.
            // The backings live in PageState (so they survive load()); each
            // generation installs fresh branded Storage instances over them.
            // Shared prototype members resolve the actual receiver's store.
            {
                const auto install_storage = [&](const char* name,
                                                 StorageStore* store) {
                    v8::Local<v8::Object> storage = v8::Object::New(isolate);
                    v8::Local<v8::Value> data =
                        v8::External::New(isolate, store, kStorageTag);
                    (void)storage->SetPrivate(context, storage_brand(isolate), data);
                    auto prototype = web_interface_prototype(isolate, context, "Storage");
                    (void)storage->SetPrototype(context, prototype);
                    const auto add_method = [&](const char* method,
                                                v8::FunctionCallback callback) {
                        if (prototype->HasOwnProperty(context, v8_string(isolate, method).As<v8::String>()).FromMaybe(true)) return;
                        v8::Local<v8::Function> fn;
                        if (!make_web_method(isolate, context, method,
                                web_method_length("storage", method), callback)
                                .ToLocal(&fn)) return;
                        (void)prototype->DefineOwnProperty(context, v8_string(isolate, method).As<v8::String>(), fn);
                    };
                    add_method("getItem", &storage_get_item);
                    add_method("setItem", &storage_set_item);
                    add_method("removeItem", &storage_remove_item);
                    add_method("clear", &storage_clear);
                    add_method("key", &storage_key);
                    if (!prototype->HasOwnProperty(context, v8_string(isolate, "length").As<v8::String>()).FromMaybe(true)) {
                        v8::Local<v8::Function> getter;
                        if (!make_web_method(isolate, context, "get length", 0,
                                &storage_length_getter).ToLocal(&getter)) return;
                        v8::PropertyDescriptor descriptor(getter, v8::Undefined(isolate));
                        descriptor.set_enumerable(true);
                        descriptor.set_configurable(true);
                        (void)prototype->DefineProperty(context, v8_string(isolate, "length").As<v8::String>(), descriptor);
                    }
                    (void)global->DefineOwnProperty(
                        context,
                        v8::String::NewFromUtf8(isolate, name)
                            .ToLocalChecked(),
                        storage,
                        static_cast<v8::PropertyAttribute>(v8::ReadOnly |
                                                           v8::DontDelete));
                };
                install_storage("localStorage", &local_storage_);
                install_storage("sessionStorage", &session_storage_);
            }
            // M3-3 minimal Event constructor (global `Event`). document/element
            // expose addEventListener/removeEventListener/dispatchEvent via their
            // host-object methods; this is the event value they dispatch.
            v8::Local<v8::FunctionTemplate> event_template =
                v8::FunctionTemplate::New(isolate, &event_constructor);
            // SetClassName requires a Local<String> (v8_string yields
            // Local<Value>), so build the name string directly.
            event_template->SetClassName(
                v8::String::NewFromUtf8Literal(isolate, "Event"));
            // M11-6: the subclass templates inherit Event and must be created
            // before either template is instantiated. Each subclass reuses
            // event_constructor for type / bubbles / target / currentTarget /
            // stopPropagation.
            auto make_event_subclass = [&](const char* name,
                                           v8::FunctionCallback callback) {
                v8::Local<v8::FunctionTemplate> tpl =
                    v8::FunctionTemplate::New(isolate, callback);
                tpl->SetClassName(
                    v8::String::NewFromUtf8(isolate, name).ToLocalChecked());
                tpl->Inherit(event_template);
                return tpl;
            };
            const auto custom_event_template =
                make_event_subclass("CustomEvent", &custom_event_constructor);
            const auto mouse_event_template =
                make_event_subclass("MouseEvent", &mouse_event_constructor);
            const auto keyboard_event_template = make_event_subclass(
                "KeyboardEvent", &keyboard_event_constructor);
            const auto focus_event_template =
                make_event_subclass("FocusEvent", &focus_event_constructor);
            const auto message_event_template =
                make_event_subclass("MessageEvent", &message_event_constructor);
            (void)global->Set(
                context, v8_string(isolate, "Event"),
                event_template->GetFunction(context).ToLocalChecked());
            const auto install_event_subclass =
                [&](const char* name,
                    v8::Local<v8::FunctionTemplate> tpl) {
                    (void)global->Set(
                        context, v8_string(isolate, name),
                        tpl->GetFunction(context).ToLocalChecked());
                };
            install_event_subclass("CustomEvent", custom_event_template);
            install_event_subclass("MouseEvent", mouse_event_template);
            install_event_subclass("KeyboardEvent", keyboard_event_template);
            install_event_subclass("FocusEvent", focus_event_template);
            install_event_subclass("MessageEvent", message_event_template);
            // Window uses the shared EventTarget methods. Event handlers and
            // postMessage delivery still carry the native listener backing.
            v8::Local<v8::External> window_data = v8::External::New(
                isolate, static_cast<EventTargetHost*>(window_events_.get()),
                kWindowEventsTag);
            auto event_target_prototype = web_interface_prototype(isolate, context, "EventTarget");
            for (const char* name : {"addEventListener", "removeEventListener", "dispatchEvent"}) {
                v8::Local<v8::Value> function;
                if (!event_target_prototype->Get(context, v8_string(isolate, name)).ToLocal(&function)) return;
                (void)global->Set(context, v8_string(isolate, name), function);
            }
            // M11-51: bounded Window event handler attributes. They share the
            // same WindowEventTarget backing as addEventListener, run after the
            // regular listeners at this target, and expose null when unset.
            for (const char* handler_name : kWindowEventHandlerNames) {
                (void)global->SetNativeDataProperty(
                    context,
                    v8::String::NewFromUtf8(isolate, handler_name)
                        .ToLocalChecked(),
                    &window_event_handler_getter,
                    &window_event_handler_setter, window_data,
                    v8::PropertyAttribute::None);
            }
            // M11-52: hidden natives used by the postMessage bootstrap. The
            // wrapper captures and removes them, so page scripts only see the
            // normal window.postMessage function.
            {
                v8::Local<v8::Function> dispatch_fn =
                    v8::FunctionTemplate::New(isolate,
                                              &window_post_message_dispatch,
                                              window_data)
                        ->GetFunction(context)
                        .ToLocalChecked();
                (void)global->Set(
                    context,
                    v8::String::NewFromUtf8Literal(
                        isolate, "__iv8PostMessageDispatch"),
                    dispatch_fn);
                install_global_function(isolate, context, global,
                                        "__iv8PostMessageRecord",
                                        &window_post_message_record);
                v8::TryCatch try_catch(isolate);
                v8::Local<v8::String> source = v8::String::NewFromUtf8(
                    isolate, kWindowPostMessageBootstrap,
                    v8::NewStringType::kNormal,
                    static_cast<int>(sizeof(kWindowPostMessageBootstrap) - 1))
                    .ToLocalChecked();
                v8::Local<v8::Script> script;
                if (v8::Script::Compile(context, source).ToLocal(&script)) {
                    (void)script->Run(context);
                }
                if (try_catch.HasCaught()) {
                    try_catch.Reset();  // fixed bootstrap; never poison install
                }
                // Defensive cleanup if the bootstrap failed before its deletes.
                (void)global->Delete(
                    context,
                    v8::String::NewFromUtf8Literal(
                        isolate, "__iv8PostMessageDispatch"));
                (void)global->Delete(
                    context,
                    v8::String::NewFromUtf8Literal(
                        isolate, "__iv8PostMessageRecord"));
            }

            // M8-1 TextEncoder / TextDecoder (UTF-8 only): constructor functions with
            // a prototype method + a read-only `encoding` prototype data property.
            auto install_encoding_ctor =
                [&](const char* class_name, v8::FunctionCallback ctor,
                    const char* method_name, v8::FunctionCallback method) {
                    v8::Local<v8::FunctionTemplate> tpl =
                        v8::FunctionTemplate::New(isolate, ctor);
                    tpl->SetClassName(
                        v8::String::NewFromUtf8(isolate, class_name)
                            .ToLocalChecked());
                    v8::Local<v8::ObjectTemplate> proto = tpl->PrototypeTemplate();
                    proto->Set(isolate, method_name,
                               v8::FunctionTemplate::New(isolate, method));
                    proto->Set(isolate, "encoding",
                               v8::String::NewFromUtf8Literal(isolate, "utf-8"),
                               v8::ReadOnly);
                    (void)global->Set(
                        context, v8_string(isolate, class_name),
                        tpl->GetFunction(context).ToLocalChecked());
                };
            install_encoding_ctor("TextEncoder", &text_encoder_constructor,
                                  "encode", &text_encoder_encode);
            install_encoding_ctor("TextDecoder", &text_decoder_constructor,
                                  "decode", &text_decoder_decode);

            // M8-2 global URL: constructor with read-only per-instance fields (set in
            // the constructor) + toString / toJSON prototype methods returning href.
            {
                v8::Local<v8::FunctionTemplate> url_tpl =
                    v8::FunctionTemplate::New(isolate, &url_constructor);
                url_tpl->SetClassName(
                    v8::String::NewFromUtf8Literal(isolate, "URL"));
                v8::Local<v8::ObjectTemplate> url_proto =
                    url_tpl->PrototypeTemplate();
                url_proto->Set(isolate, "toString",
                               v8::FunctionTemplate::New(isolate, &url_to_href));
                url_proto->Set(isolate, "toJSON",
                               v8::FunctionTemplate::New(isolate, &url_to_href));
                (void)global->Set(
                    context, v8_string(isolate, "URL"),
                    url_tpl->GetFunction(context).ToLocalChecked());
            }

            // M8-3 global URLSearchParams: constructor + the seven query-parameter
            // methods on the prototype. Per-instance state lives in a private slot.
            {
                v8::Local<v8::FunctionTemplate> usp_tpl =
                    v8::FunctionTemplate::New(isolate,
                                              &url_search_params_constructor);
                usp_tpl->SetClassName(
                    v8::String::NewFromUtf8Literal(isolate, "URLSearchParams"));
                v8::Local<v8::ObjectTemplate> usp_proto =
                    usp_tpl->PrototypeTemplate();
                const auto add_method = [&](const char* name,
                                            v8::FunctionCallback cb) {
                    usp_proto->Set(isolate, name,
                                   v8::FunctionTemplate::New(isolate, cb));
                };
                add_method("get", &usp_get);
                add_method("getAll", &usp_get_all);
                add_method("has", &usp_has);
                add_method("set", &usp_set);
                add_method("append", &usp_append);
                add_method("delete", &usp_delete);
                add_method("toString", &usp_to_string);
                (void)global->Set(
                    context, v8_string(isolate, "URLSearchParams"),
                    usp_tpl->GetFunction(context).ToLocalChecked());
            }

            // M11-8 Blob / File / FileReader. Blob/File expose immutable fields
            // and Promise-returning readers; FileReader is a plain JS instance
            // backed by a generation-owned state and completes on run_jobs().
            {
                DocumentHost* document =
                    static_cast<DocumentHost*>(document_host_);
                v8::Local<v8::Value> blob_data =
                    v8::External::New(isolate, document, kBlobTag);
                v8::Local<v8::FunctionTemplate> blob_tpl =
                    v8::FunctionTemplate::New(isolate, &blob_constructor);
                blob_tpl->SetClassName(
                    v8::String::NewFromUtf8Literal(isolate, "Blob"));
                v8::Local<v8::ObjectTemplate> blob_proto =
                    blob_tpl->PrototypeTemplate();
                blob_proto->Set(isolate, "slice",
                                v8::FunctionTemplate::New(
                                    isolate, &blob_slice, blob_data));
                blob_proto->Set(isolate, "text",
                                v8::FunctionTemplate::New(
                                    isolate, &blob_text, blob_data));
                blob_proto->Set(isolate, "arrayBuffer",
                                v8::FunctionTemplate::New(
                                    isolate, &blob_array_buffer, blob_data));
                v8::Local<v8::FunctionTemplate> file_tpl =
                    v8::FunctionTemplate::New(isolate, &file_constructor);
                file_tpl->SetClassName(
                    v8::String::NewFromUtf8Literal(isolate, "File"));
                file_tpl->Inherit(blob_tpl);
                v8::Local<v8::Function> blob_fn =
                    blob_tpl->GetFunction(context).ToLocalChecked();
                document->set_blob_constructor(isolate, blob_fn);
                (void)global->Set(context, v8_string(isolate, "Blob"), blob_fn);
                (void)global->Set(
                    context, v8_string(isolate, "File"),
                    file_tpl->GetFunction(context).ToLocalChecked());
                v8::Local<v8::FunctionTemplate> reader_tpl =
                    v8::FunctionTemplate::New(
                        isolate, &file_reader_constructor, blob_data);
                reader_tpl->SetClassName(
                    v8::String::NewFromUtf8Literal(isolate, "FileReader"));
                v8::Local<v8::ObjectTemplate> reader_proto =
                    reader_tpl->PrototypeTemplate();
                const auto add_reader_method = [&](const char* name,
                                                   v8::FunctionCallback cb) {
                    reader_proto->Set(
                        isolate, name, v8::FunctionTemplate::New(isolate, cb));
                };
                add_reader_method("addEventListener",
                                  &file_reader_add_event_listener);
                add_reader_method("removeEventListener",
                                  &file_reader_remove_event_listener);
                add_reader_method("dispatchEvent",
                                  &file_reader_dispatch_event);
                add_reader_method("readAsText", &file_reader_read_as_text);
                add_reader_method("readAsArrayBuffer",
                                  &file_reader_read_as_array_buffer);
                add_reader_method("abort", &file_reader_abort);
                (void)global->Set(
                    context, v8_string(isolate, "FileReader"),
                    reader_tpl->GetFunction(context).ToLocalChecked());
            }

            // M11-5 global MutationObserver: constructor + prototype methods.
            // Per-instance state lives in a private external slot; the constructor
            // carries the owning DocumentHost as its FunctionTemplate data.
            {
                v8::Local<v8::FunctionTemplate> mo_tpl =
                    v8::FunctionTemplate::New(
                        isolate, &mutation_observer_constructor,
                        v8::External::New(isolate, document_host_,
                                          kMutationObserverTag));
                mo_tpl->SetClassName(v8::String::NewFromUtf8Literal(
                    isolate, "MutationObserver"));
                v8::Local<v8::ObjectTemplate> mo_proto =
                    mo_tpl->PrototypeTemplate();
                mo_proto->Set(
                    isolate, "observe",
                    v8::FunctionTemplate::New(isolate, &mo_observe));
                mo_proto->Set(
                    isolate, "disconnect",
                    v8::FunctionTemplate::New(isolate, &mo_disconnect));
                mo_proto->Set(
                    isolate, "takeRecords",
                    v8::FunctionTemplate::New(isolate, &mo_take_records));
                (void)global->Set(
                    context, v8_string(isolate, "MutationObserver"),
                    mo_tpl->GetFunction(context).ToLocalChecked());
            }

            // M8-4 global atob / btoa (minimal standard Base64).
            install_global_function(isolate, context, global, "btoa",
                                    &btoa_callback);
            install_global_function(isolate, context, global, "atob",
                                    &atob_callback);

            install_browser_compatibility(isolate, context, base_url);

            // M9-1: if DevTools was enabled on a prior generation, give this fresh
            // generation its own Inspector + registered context (the WS URL stays
            // stable Python-side). When never enabled, this is skipped entirely.
            if (devtools_enabled_) {
                install_inspector(isolate, context);
            }
            // M9-4: re-wrap Reflect.get for this fresh generation if reflect-get
            // watching is enabled (config persists across load). install_page reset
            // reflect_hook_installed_ to false above, so this wraps the pristine
            // native Reflect.get exactly once.
            if (watch_registry_.reflect_get_enabled()) {
                install_reflect_get_hook(isolate, context);
            }
            // M9-5: same for Reflect.has.
            if (watch_registry_.reflect_has_enabled()) {
                install_reflect_has_hook(isolate, context);
            }
            // M10-1: same for Reflect.ownKeys.
            if (watch_registry_.reflect_ownkeys_enabled()) {
                install_reflect_ownkeys_hook(isolate, context);
            }
            // M10-2: same for Object.keys.
            if (watch_registry_.object_keys_enabled()) {
                install_object_keys_hook(isolate, context);
            }
            // Step 6d: tracing config persists across load, so reinstall the
            // explicit has/descriptor query wrappers for this generation.
            if (access_trace_.enabled()) {
                install_access_trace_query_hooks(isolate, context);
            }
            });
    }
}

void PageState::dispatch_lifecycle_events() {
    // On a SUCCESSFUL load, auto-run the load-completion lifecycle. Called by
    // Page.load's success path AFTER the scripts run; a load that failed (a script
    // raised) never reaches this. Runs under the operation guard, GIL released
    // around the JS listener calls (like the M2-4 timer pump).
    //
    // M3-6 fixed sequence on `document` (readyState migration + readystatechange)
    // then `window`:
    //   1) document.readyState = "interactive"
    //   2) readystatechange on document   (listeners observe "interactive")
    //   3) DOMContentLoaded on document
    //   4) document.readyState = "complete"
    //   5) readystatechange on document   (listeners observe "complete")
    //   6) load on window
    // readyState is set (a plain C++ member write, GIL-free) BEFORE each dispatch
    // so a listener reading document.readyState sees the value for that step.
    state_->with_scope(
        [this](v8::Isolate* isolate, v8::Local<v8::Context> context) {
            py::gil_scoped_release release_gil;
            v8::Local<v8::Object> global = context->Global();

            // Resolve the document host + its JS object once (event.target).
            auto* document = static_cast<DocumentHost*>(document_host_);
            v8::Local<v8::Object> document_object;
            bool have_document = false;
            if (document != nullptr) {
                v8::Local<v8::Value> document_value;
                if (global->Get(context, v8_string(isolate, "document"))
                        .ToLocal(&document_value) &&
                    document_value->IsObject()) {
                    document_object = document_value.As<v8::Object>();
                    have_document = true;
                }
            }

            if (have_document) {
                document->set_ready_state("interactive");
                document->dispatch_native(isolate, context, "readystatechange",
                                          document_object);
                document->dispatch_native(isolate, context, "DOMContentLoaded",
                                          document_object);
                document->set_ready_state("complete");
                document->dispatch_native(isolate, context, "readystatechange",
                                          document_object);
            }
            // load on window (event.target = the global object).
            if (window_events_) {
                static_cast<EventTargetHost*>(window_events_.get())
                    ->dispatch_native(isolate, context, "load", global);
            }
        });
}

py::list PageState::html_scripts() {
    // M3-5: the scripts the current document parsed from its HTML, in document
    // order. Each entry is a dict {"src": str|None, "code": str}: an inline
    // script has src None and its JS in code; a `<script src>` has its raw src
    // string and an empty code (the host resolves it via resources in Page.load).
    // Pure data read (no V8/isolate access, no operation guard) — the GIL is held
    // by the Python caller.
    py::list result;
    if (document_host_ != nullptr) {
        auto* document = static_cast<DocumentHost*>(document_host_);
        for (const ScriptRecord& script : document->scripts()) {
            py::dict entry;
            if (script.has_src) {
                entry["src"] = script.src;  // pybind casts std::string -> str
            } else {
                entry["src"] = py::none();
            }
            entry["code"] = script.code;
            // M3-10: only executable classic scripts run; Page.load skips the rest
            // (they stay in the DOM / document.scripts but never execute).
            entry["executable"] =
                script.node == nullptr || is_executable_classic_script(script.node);
            result.append(entry);
        }
    }
    return result;
}

void PageState::run_html_script(std::size_t index, const std::string& code,
                                const std::string& name) {
    // M3-7: run one HTML script (Page.load resolves its code/name — inline source
    // or a <script src> looked up in resources) with document.currentScript set to
    // this script's element for the duration, then cleared to null. The guard
    // clears even if eval throws, so the JSError propagates with currentScript
    // already back to null. Host scripts=[...] use plain eval() (no currentScript).
    auto* document = document_host_ != nullptr
                         ? static_cast<DocumentHost*>(document_host_)
                         : nullptr;
    if (document != nullptr) {
        document->set_current_script(index);
    }
    struct CurrentScriptGuard {
        DocumentHost* document;
        ~CurrentScriptGuard() {
            if (document != nullptr) {
                document->clear_current_script();
            }
        }
    } guard{document};
    state_->eval(code, false, name, "html_script");
}

void PageState::load(const std::string& html, const std::string& base_url) {
    // dispose() is terminal for a Page: a load after it uses the M1 error path.
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    // Replace the current page state. dispose() enforces the busy rule
    // (JSContextBusyError if an operation is active) and tears down the current
    // context, which invalidates any retained page-bound JSValues per the M1
    // rules. Then install a fresh context whose location reflects base_url.
    state_->dispose();
    install_page(base_url, html);
    // M3-6: an explicit load enters the load lifecycle — this generation is
    // "loading" (scripts observe it) until dispatch_lifecycle_events() migrates it
    // to "interactive" then "complete" on success. (The constructor installs the
    // default generation via install_page directly, so a fresh Page() stays
    // "complete" and never enters "loading".) A plain member write — no V8 access.
    if (document_host_ != nullptr) {
        static_cast<DocumentHost*>(document_host_)->set_ready_state("loading");
    }
}

PageState::~PageState() {
    // Release V8 resources immediately on page GC (api_contract §8), even if
    // JSValue wrappers still observe the state. The context is reset here; the
    // host objects are freed afterwards (member destruction order), so no JS
    // object with a dangling internal-field pointer can survive.
    state_->teardown();
}

bool PageState::disposed() const { return state_->disposed(); }

void PageState::capture_async_errors(bool enabled) {
    state_->set_async_error_capture(enabled);
    async_error_capture_ = enabled;
}

py::dict PageState::read_async_errors(bool clear) { return state_->read_async_errors(clear); }

void PageState::record_navigation(v8::Isolate* isolate, const std::string& operation,
                                   const std::string& url) {
    if (navigation_requests_.size() >= 1000) { ++dropped_navigation_requests_; return; }
    std::string resource;
    auto stack = v8::StackTrace::CurrentStackTrace(isolate, 1, v8::StackTrace::kScriptNameOrSourceURL);
    if (stack->GetFrameCount()) {
        auto name = stack->GetFrame(isolate, 0)->GetScriptNameOrSourceURL();
        if (!name.IsEmpty()) {
            v8::String::Utf8Value utf8(isolate, name);
            if (*utf8 != nullptr) resource.assign(*utf8, utf8.length());
        }
    }
    navigation_requests_.push_back({operation, url, resource, generation_});
}

py::dict PageState::read_navigation_requests(bool clear) {
    py::dict result;
    py::list requests;
    state_->with_scope([&](v8::Isolate*, v8::Local<v8::Context>) {
        for (const auto& request : navigation_requests_) {
            py::dict entry;
            entry["operation"] = request.operation;
            entry["url"] = request.url;
            entry["resource_name"] = request.resource_name;
            entry["generation"] = request.generation;
            requests.append(entry);
        }
        result["requests"] = requests;
        result["dropped_requests"] = dropped_navigation_requests_;
        result["max_requests"] = 1000;
        if (clear) { navigation_requests_.clear(); dropped_navigation_requests_ = 0; }
    });
    return result;
}

py::object PageState::eval(const std::string& source, bool to_py,
                           const std::string& name) {
    return state_->eval(source, to_py, name);
}

void PageState::dispose() { state_->dispose(); }

void PageState::run_timers() { state_->run_timers(); }

// M11-29: deterministic event-loop facade; delegates to the owned context so
// the M1 operation guard / disposed / busy rules apply unchanged.
bool PageState::tick() { return state_->tick(); }

bool PageState::advance(std::int64_t ms, std::uint32_t max_steps) {
    return state_->advance(ms, max_steps);
}

bool PageState::run_until_idle(std::uint32_t max_steps) {
    return state_->run_until_idle(max_steps);
}

std::int64_t PageState::event_loop_time_ms() const {
    if (state_->disposed()) {
        throw ContextDisposedError();
    }
    return state_->event_loop_time_ms();
}

// M11-2: pump exactly one animation frame (delegates to the owned context, like
// run_timers / run_jobs — so the disposed/busy rules are the M1 ones).
void PageState::run_animation_frame() { state_->run_animation_frame(); }

// M11-3: pump one idle round (delegates to the owned context, like
// run_animation_frame — so the disposed/busy rules are the M1 ones).
void PageState::run_idle_callbacks() { state_->run_idle_callbacks(); }

void PageState::run_jobs() { state_->run_jobs(); }

}  // namespace iv8
