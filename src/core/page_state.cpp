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
#include "browser/bom/screen.h"
#include "browser/window/window_event_target.h"
#include "browser/window/window_functions.h"
#include "browser/window/window_event_handlers.h"
#include "browser/window/post_message.h"
#include "browser/web/blob_file_reader.h"
#include "browser/window/window_metadata.h"
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



// M3-4 window event target. window / globalThis / self stay the intrinsic global
// object (M2-2, so window === globalThis === self and globals live on it); this
// backing object ONLY holds the global's event listeners and provides the
// add/remove/dispatch semantics (inherited from EventTargetHost). It is NOT
// installed as a named global host object. Shared EventTarget methods resolve
// its backing through the context's private Window host slot.


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
























// --- M11-5 MutationObserver native registry / delivery -----------------------

// Tag for the Externals carrying MutationObserverState* / DocumentHost*. V8 15.0
// requires an ExternalPointerTypeTag on External::New / External::Value; the
// default suffices and MUST match between New and Value call sites (same rule as
// kWindowEventsTag below, see 52ded6e).
















// --- M11-5 MutationObserver JS API -------------------------------------------















// --- M11-10 classList / toggleAttribute -------------------------------------
// A classList object is a fresh plain object per access; its methods and
// length/value accessors carry an External to a generation-owned binding.































// --- M11-49 HTML fragment / variadic tree helpers ---------------------------



















// M11-48: shared Node/tree methods used by every node host. The receiver is a
// plain DomNode*; argument validation accepts any node kind. DocumentFragment
// arguments insert/move their children instead of the fragment itself.


// --- M11-48 character-data host ------------------------------------------------







// --- M11-48 DocumentFragment host ----------------------------------------------









// M2-8: textContent write. In the minimal (text-node-free) tree, setting
// textContent replaces all children with the given text — i.e. detach children
// and store the text. Live queries (getElementById/querySelector/children) then
// no longer see the removed nodes; no separate index needs updating.


// M11-32: fire the minimal `invalid` event at a control. It is a target-only,
// non-bubbling event with the standard read fields; it is a plain object (like
// the M3-4 lifecycle events), not a full Event instance, so preventDefault /
// stopPropagation are not installed on it. Return values are unaffected.








// M4-A-7: an element's dispatchEvent bubbles along the current tree. The owning
// document runs the walk (element -> ancestors -> document -> window), because it
// owns the element wrappers, is_in_tree, and the window target. `receiver` is this
// element's JS object and stays event.target throughout.


// --- M2-4 timers: JS-visible setTimeout/clearTimeout/setInterval/clearInterval.
// These are bare global functions (not a host object). Each recovers the owning
// ContextState from the isolate embedder-data slot and delegates to its timer
// registry. They run during eval, i.e. under the context operation guard.







// --- M11-4 microtasks: JS-visible queueMicrotask.
//
// queueMicrotask(callback) appends a callback to the current context's V8
// microtask queue. It deliberately has no cancel form (the standard API has
// none) and no new Python pump: ContextState already installs
// MicrotasksPolicy::kExplicit, so pending microtasks run only when
// Page.run_jobs() performs the checkpoint. The callback must be a function;
// unlike the permissive rAF/idle host functions, a non-callable argument throws
// a JS TypeError before anything is queued.


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


// cancelAnimationFrame(handle): drop a not-yet-run callback. Its handle space is
// separate from the timer ids, so it does NOT cancel a setTimeout/setInterval.


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


// cancelIdleCallback(handle): drop a not-yet-run callback. Its handle space is
// separate from the timer ids and the animation-frame handles, so it cancels
// neither a setTimeout/setInterval nor a requestAnimationFrame.


// Shared by clearTimeout and clearInterval (single id space, like browsers).


// --- M11-36 deterministic dialogs ------------------------------------------
//
// There is no windowing system or Python callback bridge in this runtime. The
// dialog globals are deterministic stubs: alert/print do nothing, confirm
// returns false, and prompt returns null. They never block and never mutate
// page state.








// --- M11-50 deterministic window scroll / focus / OS-window stubs ------------
//
// The runtime has no layout engine, no focus/navigation machinery, and no OS
// window it could move or resize. These methods are compatibility no-ops: they
// accept any arguments, change no state, and return undefined. scrollX/scrollY
// and the pageXOffset/pageYOffset aliases consequently stay 0 (see
// window_metadata_value). Each call still participates in the M9-2 watch channel.






















































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




// M11-51: Window IDL event handler attributes. The global slots are native
// accessors backed by WindowEventTarget; assigning a function replaces the
// handler, assigning null/undefined/non-callable clears it, and the getter
// reports null when unset. The supported names are a bounded set of common
// Window handlers; arbitrary `onfoo` names remain ordinary global properties.








// M11-52: window.postMessage is a JS bootstrap wrapper around these hidden
// natives. The wrapper validates/coerces arguments, clones the message with the
// captured structuredClone, builds a MessageEvent, and schedules the dispatch
// on the captured setTimeout queue. dispatch() itself runs from that timer task
// and uses the ordinary window event-target path (listeners + onmessage).



















// --- M11-8 Blob / File / FileReader ------------------------------------------
//
// Blob/File are immutable value objects whose bytes live in a private
// ArrayBuffer slot. FileReader is a plain JS instance whose private slot points
// at a generation-owned state; reads complete on the existing run_jobs()
// microtask checkpoint, so no new pump is introduced.




































































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






// M11-50: window.status is a writable string that resets with each generation.
// It shares window.name's coercion and trace shape, but is PageState state that
// install_page() clears on every new document.




// Step 5i: window.parent / top / frames / opener are traced accessors.
// parent/frames/opener are replaceable (the setter stores the new value per
// generation); top stays read-only. Values and assignment results are unchanged.






// M11-35: a deterministic secure-context bit derived from the base URL (no host
// OS or network access): https/wss/file are secure; http/ws are secure only for
// loopback hosts; everything else is insecure.


// Host-access trace, step 5: the value source for the generic window metadata
// accessors. Values are computed from fixed Page state; no JS object is touched.












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
