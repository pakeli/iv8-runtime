#pragma once

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <pybind11/pybind11.h>

#include "iv8/host_object.h"
#include "iv8/watch_registry.h"

namespace iv8 {

// M11-1 configurable host-environment baseline. These are the values the page's
// read-only environment surface reports: navigator (userAgent / platform /
// language / webdriver), screen (width / height / availWidth / availHeight), and
// window (devicePixelRatio / innerWidth / innerHeight).
//
// This struct carries NO defaults. The Python facade (python/iv8/page.py) owns the
// single default template and always passes a fully-resolved, already-validated set
// of values, so the native layer never has to merge or guess. Every value is a fixed
// constant chosen by the caller — nothing here is derived from the real host OS,
// display, or locale, which keeps a page deterministic and identical across
// platforms.
//
// It is Page-level state: owned by PageState, it outlives each generation, so a
// load() reinstalls the environment surface with the SAME values.
struct HostEnvironment {
    std::string user_agent;
    std::string platform;
    std::string language;
    bool webdriver = false;
    int screen_width = 0;
    int screen_height = 0;
    int avail_width = 0;
    int avail_height = 0;
    double device_pixel_ratio = 0.0;
    int inner_width = 0;
    int inner_height = 0;
};

class ContextState;
// M9-1 opaque holder for the V8 Inspector objects (defined in page_state.cpp so
// v8-inspector.h stays out of this header). Destroyed via the out-of-line
// ~PageState / the context teardown hook while the isolate is still alive.
struct DevToolsInspector;

// M2-1 native page state. A Page is the M2 container that owns one execution
// context (the M1 ContextState) plus the host objects installed into it. This
// round keeps it minimal: lifecycle (eval / dispose / disposed) simply delegates
// to the owned context, so disposed-page access reuses the M1 error paths
// (JSContextDisposedError / JSContextBusyError) unchanged. Host objects are
// bound to this context's lifetime — they are torn down with it and can never be
// accessed once the context is disposed (no JS objects remain to dispatch from).
//
// NOT a full page object: no load/navigation/timers/browser globals (deferred to
// later M2 phases). Bound to Python as _core.Page.
class PageState {
public:
    // M11-1: `environment` is the page's fixed host-environment baseline, already
    // validated and fully resolved by the Python facade (every field is set). It is
    // stored for the page's lifetime and re-applied by every install_page, so a
    // load() generation reports identical values.
    explicit PageState(HostEnvironment environment);
    ~PageState();

    PageState(const PageState&) = delete;
    PageState& operator=(const PageState&) = delete;

    bool disposed() const;
    pybind11::object eval(const std::string& source, bool to_py,
                          const std::string& name);
    void dispose();

    // M2-4 manual pumps (delegate to the owned context). run_timers fires
    // currently-scheduled timer callbacks once; run_jobs drains the microtask
    // queue. Both reuse the M1 operation guard (disposed -> JSContextDisposedError).
    void run_timers();
    void run_jobs();

    // M11-2 manual animation-frame pump: run the callbacks queued for the current
    // frame, in registration order. A callback that re-requests a frame lands in the
    // NEXT frame (so one call = one frame). Delegates to the owned context, reusing
    // the M1 operation guard (disposed -> JSContextDisposedError, busy ->
    // JSContextBusyError). No clock, no background thread, no automatic loop.
    void run_animation_frame();

    // M11-3 manual idle pump: run the callbacks queued for the current idle round,
    // in registration order, each receiving a minimal IdleDeadline-style object. A
    // callback that re-requests lands in the NEXT round (one call = one round).
    // Delegates to the owned context, reusing the M1 operation guard (disposed ->
    // JSContextDisposedError, busy -> JSContextBusyError). No clock, no background
    // thread, no automatic loop, and no real options.timeout semantics.
    void run_idle_callbacks();

    // M2-5 page load: refresh the page state from static input. Tears down the
    // current context (invalidating any old page-bound JSValues per the M1 rules)
    // and installs a fresh one whose `location` derives from `base_url`; `html`
    // is captured as internal document-bootstrap state (no public document
    // surface). NOT a real navigation/loader. On the current context being busy
    // -> JSContextBusyError; after dispose() -> JSContextDisposedError.
    void load(const std::string& html, const std::string& base_url);

    // M3-4: dispatch the load-lifecycle events for a just-completed load —
    // DOMContentLoaded on document, then load on window (fixed order). Called by
    // Page.load's success path AFTER the host-provided scripts run; a failed load
    // (a script raised) never calls it. Runs under the operation guard.
    void dispatch_lifecycle_events();

    // M3-5: the scripts parsed from the current document's HTML, in document
    // order, as a list of {"src": str|None, "code": str, "executable": bool} dicts
    // (inline -> src None + JS in code; `<script src>` -> raw src + empty code;
    // M3-10 `executable` = a minimal classic script that Page.load should run —
    // non-classic types stay in the DOM but are skipped). Page.load reads this
    // after installing the generation to run the HTML scripts. Pure data read
    // (no V8 access).
    pybind11::list html_scripts();

    // M3-7: run the HTML script at `index` (in html_scripts() order) with
    // document.currentScript set to that <script> element for the duration, then
    // cleared to null (even if it throws). `code`/`name` are resolved by Page.load
    // (inline source, or a `<script src>` looked up in resources). Host
    // scripts=[...] instead use eval() and never set currentScript.
    void run_html_script(std::size_t index, const std::string& code,
                         const std::string& name);

    // M9-1: DevTools/Inspector attach base (native side of Page.devtools_url()).
    // Idempotent lazy enable: on first call, create a V8 Inspector for the current
    // generation and register its context; subsequent generations (load()) get a
    // fresh Inspector automatically while enabled. Zero effect until called.
    // Reuses the operation guard, so a disposed page raises JSContextDisposedError.
    bool devtools_enable();
    // Dispatch one CDP protocol message into the current generation's Inspector
    // session (lazily connected) and return the synchronously-produced outbound
    // messages (responses/notifications) as a list of str. Runs under the
    // operation guard (busy -> JSContextBusyError, disposed ->
    // JSContextDisposedError). Best-effort: without a message loop, only
    // synchronous CDP responses are returned this phase.
    pybind11::list devtools_dispatch(const std::string& message);

    // M9-2: register the set of watched host-method paths ("receiver.method",
    // e.g. "document.querySelector") and enable recording. Deduped; an empty list
    // is a valid "enabled, watches nothing" set. Replaces any prior set. The
    // registration persists across load() (same Page's observation intent). Raises
    // JSContextDisposedError after dispose(). Type validation is done in the
    // Python facade before this is called. M9-3: `break_on_hit` additionally
    // schedules a V8 Inspector pause on each matched hit — but only when an
    // Inspector session is attached (else it is recorded only; no error, no
    // pause). Type validation is done in the Python facade.
    void watch_apis(const std::vector<std::string>& paths, bool break_on_hit);
    // M9-2: return the accumulated call-hit records as a list of dicts
    // ({"path", "resource_name", "type": "call"}) and clear the log (read-and-
    // clear). Empty list if nothing was hit. Raises JSContextDisposedError after
    // dispose().
    pybind11::list read_watch_api_hits();

    // M9-4: enable/disable Reflect.get monitoring. When enabled, globalThis's
    // Reflect.get is wrapped (per generation) with a delegating hook that records
    // each call and, when break_on_hit is set and an Inspector session is attached,
    // pauses (reusing M9-3). Disabling stops recording (gate). Config persists
    // across load; disposed page raises JSContextDisposedError. Validation done in
    // the Python facade.
    void watch_reflect_get(bool enabled, bool break_on_hit);
    // M9-4: return the Reflect.get hit records ({"property", "resource_name",
    // "type": "Reflect.get"}) and clear the log (read-and-clear). Empty if none.
    // Raises JSContextDisposedError after dispose().
    pybind11::list read_reflect_get_hits();

    // M9-5: enable/disable Reflect.has monitoring (mirrors watch_reflect_get). When
    // enabled, globalThis's Reflect.has is wrapped (per generation) with a
    // delegating hook that records each call and, with break_on_hit + an attached
    // session, pauses. Config persists across load; disposed page raises. Validation
    // in the Python facade.
    void watch_reflect_has(bool enabled, bool break_on_hit);
    // M9-5: return the Reflect.has hit records ({"property", "resource_name",
    // "type": "Reflect.has"}) and clear the log (read-and-clear).
    pybind11::list read_reflect_has_hits();

    // M10-1: enable/disable Reflect.ownKeys monitoring (mirrors watch_reflect_get/
    // has). Reflect.ownKeys(target) has no propertyKey, so a hit records only the
    // resource name. Config persists across load; disposed page raises. Validation
    // in the Python facade.
    void watch_reflect_ownkeys(bool enabled, bool break_on_hit);
    // M10-1: return the Reflect.ownKeys hit records ({"resource_name", "type":
    // "Reflect.ownKeys"}) and clear the log (read-and-clear).
    pybind11::list read_reflect_ownkeys_hits();

    // M10-2: enable/disable Object.keys monitoring (mirrors watch_reflect_ownkeys).
    // Object.keys(target) has no propertyKey, so a hit records only the resource
    // name. Config persists across load; disposed page raises. Validation in the
    // Python facade.
    void watch_object_keys(bool enabled, bool break_on_hit);
    // M10-2: return the Object.keys hit records ({"resource_name", "type":
    // "Object.keys"}) and clear the log (read-and-clear).
    pybind11::list read_object_keys_hits();

private:
    // Build a fresh ContextState for `base_url` (location source) and install the
    // page's host objects + global roots + timers into it. Used by the ctor and
    // by load().
    void install_page(const std::string& base_url, const std::string& html);

    // M9-1: (re)create the V8 Inspector for the just-installed generation and
    // register `context`. Called from install_page (when devtools is enabled) and
    // from devtools_enable(); both already hold the isolate scope.
    void install_inspector(v8::Isolate* isolate, v8::Local<v8::Context> context);

    // M9-4: wrap globalThis.Reflect.get with the recording hook for this
    // generation (once). Called from install_page (when reflect-get watching is on)
    // and from watch_reflect_get(); both already hold the isolate scope.
    void install_reflect_get_hook(v8::Isolate* isolate,
                                  v8::Local<v8::Context> context);
    // M9-5: same, for globalThis.Reflect.has.
    void install_reflect_has_hook(v8::Isolate* isolate,
                                  v8::Local<v8::Context> context);
    // M10-1: same, for globalThis.Reflect.ownKeys.
    void install_reflect_ownkeys_hook(v8::Isolate* isolate,
                                      v8::Local<v8::Context> context);
    // M10-2: same, for globalThis.Object.keys.
    void install_object_keys_hook(v8::Isolate* isolate,
                                  v8::Local<v8::Context> context);

    // M9-3: request the current Inspector session to pause (the watch-registry
    // pause callback). A no-op when no session is attached, so break_on_hit
    // records-only until an external DevTools client has connected.
    void request_inspector_pause();

public:
    // M9-3 internal probe (bound as _core.Page.devtools_pause_count; NOT public
    // iv8.Page surface): number of times the current generation's Inspector has
    // paused. Used by tests to confirm break_on_hit actually stopped the Inspector.
    int devtools_pause_count();

private:

    // Minimal internal page root state captured by load() — the seed for a later
    // document bootstrap. Intentionally NOT exposed (no public document surface
    // in M2-5). Written on each install; not read yet.
    struct PageBootstrap {
        std::string html;
        std::string base_url;
    };

    // Declared before host_objects_ so that, in ~PageState, the context is torn
    // down (in the body) before the host objects are destroyed (members run in
    // reverse declaration order) — no dangling internal-field pointer is ever
    // dereferenced.
    // M11-1 Page-level host-environment baseline. Declared BEFORE host_objects_ so
    // that in ~PageState it is destroyed AFTER them (members are destroyed in
    // reverse declaration order) — NavigatorHost / ScreenHost hold a raw pointer to
    // it and must never outlive it. Fixed after construction (no setter).
    HostEnvironment environment_;
    std::shared_ptr<ContextState> state_;
    std::vector<std::unique_ptr<HostObject>> host_objects_;
    // M3-4 window event target: holds window's event listeners. window itself
    // stays the intrinsic global object; this is only its listener store and is
    // NOT installed as a named global. Its listener handles are released by the
    // teardown hook alongside host_objects_ (isolate-alive ordering).
    std::unique_ptr<HostObject> window_events_;
    // M3-4 observing pointer to the current DocumentHost (owned by host_objects_),
    // used to dispatch DOMContentLoaded. Reset on each install_page.
    HostObject* document_host_ = nullptr;
    [[maybe_unused]] PageBootstrap bootstrap_;
    // M9-1 DevTools: enabled once Page.devtools_url() is called; then every
    // generation installs a fresh Inspector. inspector_ holds the current
    // generation's Inspector objects and is reset (session then inspector) by the
    // context teardown hook BEFORE the isolate is disposed.
    bool devtools_enabled_ = false;
    std::unique_ptr<DevToolsInspector> inspector_;
    // M9-2 Page-level watch registry: outlives each generation (survives load), so
    // watch_apis() registration persists. Each install_page points the current
    // ContextState at it.
    WatchRegistry watch_registry_;
    // M9-4/M9-5/M10-1: whether the current generation's Reflect.get / .has /
    // .ownKeys are already wrapped (reset per install_page; prevents double-wrap).
    bool reflect_hook_installed_ = false;
    bool reflect_has_hook_installed_ = false;
    bool reflect_ownkeys_hook_installed_ = false;
    bool object_keys_hook_installed_ = false;  // M10-2
};

}  // namespace iv8
