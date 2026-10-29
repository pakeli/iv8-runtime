#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <vector>

#include <pybind11/pybind11.h>

#include "iv8/v8_headers.h"

#include "iv8/isolate_host.h"

namespace iv8 {

// M9-2 forward declaration; the full definition (header-only) lives in
// iv8/watch_registry.h. ContextState only holds an observing pointer.
class WatchRegistry;

// Isolate embedder-data slot holding the owning ContextState* so host callbacks
// (e.g. setTimeout) can reach it via isolate->GetData(). Each ContextState owns
// its own isolate, so this slot is private to that context.
inline constexpr std::uint32_t kIsolateStateSlot = 0;

// Structured lifecycle errors, translated by the binding layer into the public
// Python exceptions iv8.JSContextDisposedError / JSContextBusyError.
class ContextDisposedError : public std::runtime_error {
public:
    ContextDisposedError()
        : std::runtime_error("operation on a disposed JSContext") {}
};

class ContextBusyError : public std::runtime_error {
public:
    ContextBusyError()
        : std::runtime_error("JSContext already has an active operation") {}
};

// Shared native state for one JSContext. Owns the isolate (+ allocator), the
// persistent context, the operation guard, and a handle table of outstanding
// JSValue persistent handles. Held by a shared_ptr so JSValue wrappers can
// safely observe the disposed flag after teardown WITHOUT keeping the isolate
// alive: V8 resource lifetime is controlled by teardown(), not by shared_ptr
// refcount.
class ContextState : public std::enable_shared_from_this<ContextState> {
public:
    ContextState();
    ~ContextState();

    ContextState(const ContextState&) = delete;
    ContextState& operator=(const ContextState&) = delete;

    bool disposed() const {
        return disposed_.load(std::memory_order_acquire);
    }
    bool alive() const { return !disposed(); }

    std::string version();
    pybind11::object eval(const std::string& source, bool to_py,
                          const std::string& name);

    // Run `fn` within this context's isolate / Locker / HandleScope /
    // Context::Scope, under the operation guard (so it observes disposal and
    // busy state like any other operation). Used by the M2 host-object
    // framework to install host objects into the global at page setup. `fn`
    // must touch only V8 (it runs with whatever GIL state the caller holds).
    void with_scope(
        const std::function<void(v8::Isolate*, v8::Local<v8::Context>)>& fn);

    // --- M2-4 timers / jobs ---------------------------------------------------
    // Register a timer callback and return its id. Called from the setTimeout /
    // setInterval host functions WHILE the operation guard is already held (they
    // run during eval), so these take no guard themselves.
    std::int64_t register_timer(v8::Local<v8::Function> callback,
                                std::int32_t delay, bool repeating);
    // Cancel a timer by id (clearTimeout / clearInterval); also guard-free.
    void clear_timer(std::int64_t id);
    // Manual pump: fire every currently-scheduled timer once, ordered by
    // (delay, registration seq). One-shots are removed; intervals remain for the
    // next pump; timers scheduled during the pump fire on the NEXT pump. Runs
    // under the operation guard (serial/busy rules) with the GIL released around
    // each callback; a throwing callback is swallowed so the context stays usable.
    void run_timers();
    // Manual pump: drain the pending microtask (job) queue.
    void run_jobs();

    // --- M11-29 deterministic event-loop facade (timers + microtasks) ----------
    // Run one event-loop turn: drain microtasks, fire at most one timer whose
    // virtual due time is <= the current virtual clock, then drain microtasks.
    // Returns true if a timer ran. Never advances the virtual clock.
    bool tick();
    // Advance the virtual clock by `ms`, firing due timers in (due, seq) order
    // and draining microtasks after each callback. Returns true when the full
    // window was processed; false if max_steps stopped it first.
    bool advance(std::int64_t ms, std::uint32_t max_steps);
    // Repeatedly run the earliest timer (advancing virtual time to its due time)
    // and drain microtasks until no timers remain. Returns true only when no
    // timers remain; intervals normally return false at max_steps.
    bool run_until_idle(std::uint32_t max_steps);
    // Current generation's virtual clock (ms). Reset with each generation.
    std::int64_t event_loop_time_ms() const { return virtual_now_ms_; }

    // --- M11-2 animation frames -----------------------------------------------
    // Register an animation-frame callback and return its handle. Called from the
    // requestAnimationFrame host function WHILE the operation guard is already
    // held (it runs during eval / during a frame pump), so it takes no guard.
    // The RAF queue is deliberately SEPARATE from the timer registry above: it is
    // strict FIFO (registration order only — there is no delay parameter) and has
    // its own handle space, matching the browser split between the timer task
    // queue and the animation-frame callback list.
    std::int64_t register_animation_frame(v8::Local<v8::Function> callback);
    // Cancel a pending animation-frame callback by handle (cancelAnimationFrame).
    // Guard-free like clear_timer. Cancelling an already-run, unknown, or
    // already-cancelled handle is a no-op. Cancelling a callback that belongs to
    // the frame currently being pumped still prevents it from running (the pump
    // re-checks each handle right before invoking it).
    void cancel_animation_frame(std::int64_t handle);
    // Manual pump: run exactly ONE frame — the callbacks queued when this call
    // began, in registration order. The queue is detached up front, so a callback
    // that calls requestAnimationFrame() again schedules into the NEXT frame and
    // can never be run by this pump (bounded, loop-safe: an unconditionally
    // self-rescheduling callback advances one frame per call instead of spinning).
    // Runs under the operation guard with the GIL released around each callback; a
    // throwing callback is swallowed so the context stays usable. There is no real
    // clock and no background thread — only this call executes RAF work.
    void run_animation_frame();

    // --- M11-3 idle callbacks -------------------------------------------------
    // Register an idle callback and return its handle. Mirrors
    // register_animation_frame (guard-free, strict FIFO, own handle space).
    // `timeout` is the validated options.timeout in ms, or -1 when absent: it is
    // STORED ONLY and never acts as a deadline — this phase implements no real
    // timeout semantics (see docs/m11_3_idle_callbacks.md non-goals).
    std::int64_t register_idle_callback(v8::Local<v8::Function> callback,
                                       std::int32_t timeout);
    // Cancel a pending idle callback by handle (cancelIdleCallback). Guard-free.
    // An unknown / already-run / already-cancelled handle is a no-op. Cancelling a
    // callback belonging to the round being pumped still prevents it from running
    // (the pump re-checks each handle right before invoking it).
    void cancel_idle_callback(std::int64_t handle);
    // Manual pump: run exactly ONE idle round — the callbacks queued when this call
    // began, in registration order. A callback that calls requestIdleCallback()
    // again schedules into the NEXT round and can never run in this pump (bounded,
    // loop-safe, same guarantee as run_animation_frame). Each callback receives a
    // minimal IdleDeadline-style object: read-only `didTimeout` (always false this
    // phase) and `timeRemaining()` (a fixed budget — there is no clock). Runs under
    // the operation guard with the GIL released around each callback; a throwing
    // callback is swallowed so the context stays usable.
    void run_idle_callbacks();

    // M3-3: install an optional hook that teardown() runs while the isolate is
    // still alive, right after the timer handles are reset and before the context
    // /isolate are torn down. PageState uses it to release host-object V8 handles
    // (event listeners) in the correct order. The hook must not throw (teardown
    // is noexcept); teardown swallows any exception defensively.
    void set_on_teardown(std::function<void()> fn) {
        on_teardown_ = std::move(fn);
    }

    // M9-2: point this generation at the Page-level watch registry (survives
    // load). Host-method dispatch points reach it via state_from_isolate(); eval()
    // stamps the current script's resource name onto it. Null on a bare JSContext.
    void set_watch_registry(WatchRegistry* registry) { watch_ = registry; }
    WatchRegistry* watch_registry() const { return watch_; }

    // Explicit dispose: rejects if an operation is active (ContextBusyError),
    // otherwise runs the ordered teardown.
    void dispose();

    // Idempotent, non-throwing ordered teardown: mark disposed -> reset all
    // JSValue handles -> reset persistent context -> dispose isolate -> release
    // allocator. Safe to call from destructors and on GC.
    void teardown() noexcept;

    // --- JSValue handle table -------------------------------------------------
    // type_name / to_py for a retained value; both run under the operation guard
    // and raise ContextDisposedError after teardown.
    std::string value_type_name(std::uint64_t id);
    pybind11::object value_to_py(std::uint64_t id);
    // Drop a retained handle. noexcept: safe to call from JSValue destruction
    // (Python GC), including after teardown (then a no-op).
    void release_value(std::uint64_t id) noexcept;

private:
    friend class OperationScope;

    // Store a value in the handle table and return its opaque id. Called only
    // from eval() while the operation guard is held.
    std::uint64_t retain_value(v8::Local<v8::Value> value);
    // Materialize a retained handle as a Local. Caller must hold the operation
    // guard and an active HandleScope.
    v8::Local<v8::Value> local_value(std::uint64_t id);

    std::unique_ptr<IsolateHost> isolate_host_;
    v8::Global<v8::Context> context_;

    std::mutex op_mutex_;
    std::atomic<bool> disposed_{false};

    // Separate mutex for the handle table so JSValue release (GC) is serialized
    // against retain/lookup/clear without touching the operation mutex.
    std::mutex table_mutex_;
    std::unordered_map<std::uint64_t, v8::Global<v8::Value>> values_;
    std::uint64_t next_id_ = 1;

    // Timer registry (M2-4). Only mutated under the operation guard (during eval
    // for register/clear, and inside run_timers), and reset in teardown() before
    // isolate disposal — so no separate mutex is needed.
    struct TimerEntry {
        v8::Global<v8::Function> callback;
        std::int32_t delay = 0;
        bool repeating = false;
        std::uint64_t seq = 0;
        // M11-29: virtual due time; unused by the legacy run_timers() path.
        std::int64_t due_ms = 0;
    };
    std::unordered_map<std::int64_t, TimerEntry> timers_;
    std::int64_t next_timer_id_ = 1;
    std::uint64_t next_timer_seq_ = 0;
    // M11-29 virtual clock, reset with the generation.
    std::int64_t virtual_now_ms_ = 0;

    // M11-30 virtual frame clock. The interval is deliberately fixed: this
    // runtime has no display refresh or vsync.
    static constexpr std::int64_t kFrameIntervalMs = 16;
    std::int64_t next_frame_due_ms_ = kFrameIntervalMs;

    // M11-29/M11-30 helpers. `bounded` selects timers with due_ms <= limit; the
    // unbounded form returns the globally earliest timer. Returns -1 when none.
    std::int64_t next_timer_due(std::int64_t limit, bool bounded) const;
    // Effective next frame boundary, or -1 when no frame callbacks are queued.
    std::int64_t next_frame_due() const;
    // Fire one timer under the already-held operation guard/scopes; repeating
    // timers are rescheduled before the callback, one-shots are removed first.
    void fire_timer_locked(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           std::int64_t id);
    // Fire one animation-frame snapshot / idle round under the already-held
    // guard/scopes. Each callback is followed by a microtask checkpoint by the
    // caller.
    void fire_animation_frame_locked(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     bool drain_between);
    void fire_idle_round_locked(v8::Isolate* isolate,
                                v8::Local<v8::Context> context,
                                bool drain_between);
    // Perform a microtask checkpoint under the already-held guard/scopes.
    void drain_microtasks_locked(v8::Isolate* isolate);

    // Animation-frame queue (M11-2). A vector, not a map: RAF is strict FIFO with
    // no delay key, so registration order IS the run order and a vector preserves
    // it without a sort. Same concurrency story as timers_: only mutated under the
    // operation guard (during eval for request/cancel, and inside
    // run_animation_frame), and reset in teardown() before isolate disposal — so it
    // needs no separate mutex. The handle space is independent of the timer ids.
    struct AnimationFrameEntry {
        std::int64_t handle = 0;
        v8::Global<v8::Function> callback;
    };
    std::vector<AnimationFrameEntry> animation_frames_;
    std::int64_t next_animation_frame_handle_ = 1;

    // Idle-callback queue (M11-3). Same shape and concurrency story as
    // animation_frames_ above (vector = FIFO, guard-only mutation, reset in
    // teardown before isolate disposal), with its own independent handle space.
    // `timeout` holds the validated options.timeout in ms (-1 = absent) and is
    // recorded ONLY — this phase implements no real timeout firing semantics.
    struct IdleCallbackEntry {
        std::int64_t handle = 0;
        std::int32_t timeout = -1;
        v8::Global<v8::Function> callback;
    };
    std::vector<IdleCallbackEntry> idle_callbacks_;
    std::int64_t next_idle_callback_handle_ = 1;

    // M3-3 teardown hook (see set_on_teardown). Empty unless PageState installs
    // it; a bare JSContext leaves it unset.
    std::function<void()> on_teardown_;

    // M9-2 observing pointer to the Page-level watch registry (owned by PageState,
    // outlives each generation). Null unless PageState set it.
    WatchRegistry* watch_ = nullptr;
};

// Thin per-JSContext owner: holds the shared state and delegates. This is the
// type bound to Python as _core.Context.
class ContextHost {
public:
    ContextHost() : state_(std::make_shared<ContextState>()) {}
    // GC of the Python context releases native V8 resources immediately
    // (api_contract §8), even if JSValue wrappers still reference the state.
    ~ContextHost() { state_->teardown(); }

    ContextHost(const ContextHost&) = delete;
    ContextHost& operator=(const ContextHost&) = delete;

    bool disposed() const { return state_->disposed(); }
    std::string version() { return state_->version(); }
    pybind11::object eval(const std::string& source, bool to_py,
                          const std::string& name) {
        return state_->eval(source, to_py, name);
    }
    void dispose() { state_->dispose(); }

private:
    std::shared_ptr<ContextState> state_;
};

// RAII operation guard: non-blocking try-lock of the context's operation mutex
// (overlap -> ContextBusyError; never serializes), plus a use-after-dispose
// check (ContextDisposedError). Holds the lock for the operation's lifetime.
class OperationScope {
public:
    explicit OperationScope(ContextState& state);

    OperationScope(const OperationScope&) = delete;
    OperationScope& operator=(const OperationScope&) = delete;

private:
    std::unique_lock<std::mutex> lock_;
};

}  // namespace iv8
