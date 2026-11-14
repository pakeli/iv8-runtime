#pragma once

#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "iv8/host_object.h"
#include "../internal/common.h"
#include "../internal/event_support.h"

namespace iv8::browser {

class EventTargetHost : public HostObject {
public:
    // Reset every retained listener handle. Runs (via PageState's teardown hook)
    // while the isolate is still alive, mirroring the timer-handle reset.
    void release_v8_handles() noexcept override {
        for (auto& entry : listeners_) {
            for (Listener& listener : entry.second) {
                listener.callback.Reset();
            }
        }
        listeners_.clear();
    }

    // If `name` is an event-target method, handle it, write the JS result to
    // `out`, and return true; otherwise return false so the concrete host handles
    // its own methods. Runs inside the isolate/context scopes (during eval), under
    // the operation guard, with the Python GIL already released. Public so the
    // M3-4 window event functions (bare globals, not a host-object trampoline) can
    // share the exact same semantics as document/element.
    bool handle_event_method(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             const std::string& name,
                             const v8::FunctionCallbackInfo<v8::Value>& args,
                             v8::Local<v8::Value>& out) {
        if (name == "addEventListener") {
            add_listener(isolate, type_arg(isolate, context, args, 0),
                         listener_arg(args, 1),
                         listener_options(isolate, context, args, 2));
            out = v8::Undefined(isolate);
            return true;
        }
        if (name == "removeEventListener") {
            remove_listener(
                isolate, type_arg(isolate, context, args, 0),
                listener_arg(args, 1),
                listener_options(isolate, context, args, 2).capture);
            out = v8::Undefined(isolate);
            return true;
        }
        if (name == "dispatchEvent") {
            // A non-object event dispatches to nothing (returns true). A
            // cancelable event that called preventDefault returns false.
            bool canceled = false;
            if (args.Length() >= 1 && args[0]->IsObject()) {
                v8::Local<v8::Object> event = args[0].As<v8::Object>();
                dispatch_event(isolate, context, event, args.This());
                canceled = event_default_prevented(isolate, context, event);
            }
            out = v8::Boolean::New(isolate, !canceled);
            return true;
        }
        return false;
    }

    // M11-11: dispatch `event` starting at this target (`receiver` is the JS
    // object dispatchEvent was called on). The base does a single-target dispatch
    // (window / FileReader use this); ElementHost / DocumentHost override to walk
    // the current tree. Capture and bubble listeners both run at target, in
    // registration order, with eventPhase === 2.
    virtual void dispatch_event(v8::Isolate* isolate,
                                v8::Local<v8::Context> context,
                                v8::Local<v8::Object> event,
                                v8::Local<v8::Object> receiver) {
        (void)event->Set(context, v8_string(isolate, "target"), receiver);
        reset_event_stopped(isolate, context, event);
        fire_target_at(isolate, context, event, receiver);
        finish_dispatch(isolate, context, event);
    }

    // Phase-specific firing. Capture phase filters capture listeners, bubble phase
    // filters bubble listeners, and AT_TARGET runs both in registration order.
    void fire_capture_at(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         v8::Local<v8::Object> event,
                         v8::Local<v8::Value> receiver) {
        fire_phase(isolate, context, event, receiver, 1, true, false);
    }
    void fire_bubble_at(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> event,
                        v8::Local<v8::Value> receiver) {
        fire_phase(isolate, context, event, receiver, 3, false, true);
    }
    void fire_target_at(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Object> event,
                        v8::Local<v8::Value> receiver) {
        fire_phase(isolate, context, event, receiver, 2, true, true);
    }

    // Dispatch is over: eventPhase returns to NONE.
    void finish_dispatch(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         v8::Local<v8::Object> event) {
        (void)event->Set(context, v8_string(isolate, "eventPhase"),
                         v8::Integer::New(isolate, 0));
    }

    // M3-4 / M11-11: dispatch a synthetic lifecycle / FileReader event of `type`
    // from C++ (no JS caller). Single target, at-target phase, no bubbling.
    void dispatch_native(v8::Isolate* isolate, v8::Local<v8::Context> context,
                         const std::string& type, v8::Local<v8::Object> receiver) {
        v8::Local<v8::Object> event = v8::Object::New(isolate);
        (void)event->Set(context, v8_string(isolate, "type"),
                         v8_string(isolate, type));
        (void)event->Set(context, v8_string(isolate, "target"), receiver);
        fire_target_at(isolate, context, event, receiver);
        finish_dispatch(isolate, context, event);
    }

protected:
    // The event-target method names every EventTargetHost exposes; concrete hosts
    // append these to their own method_names().
    static const std::vector<std::string>& event_method_names() {
        static const std::vector<std::string> names = {
            "addEventListener", "removeEventListener", "dispatchEvent"};
        return names;
    }

    // M11-51: Window-only hook for IDL event handler attributes (`onload`, ...).
    // It runs after the ordinary listener snapshot for one target/phase, unless a
    // listener called stopImmediatePropagation(). The base is a no-op, so
    // document/element/FileReader dispatch behavior is unchanged.
    virtual void invoke_event_handler(
        v8::Isolate*, v8::Local<v8::Context>, v8::Local<v8::Object>,
        v8::Local<v8::Value>, int, const std::string&) {}

private:
    struct ListenerOptions {
        bool capture = false;
        bool once = false;
        bool passive = false;
    };
    struct Listener {
        v8::Global<v8::Function> callback;
        bool capture = false;
        bool once = false;
        bool passive = false;
    };
    struct ListenerSnapshot {
        v8::Local<v8::Function> callback;
        bool capture = false;
        bool once = false;
        bool passive = false;
    };

    // args[index] coerced to a UTF-8 string (the event `type`), or "".
    static std::string type_arg(v8::Isolate* isolate,
                                v8::Local<v8::Context> context,
                                const v8::FunctionCallbackInfo<v8::Value>& args,
                                int index) {
        if (index >= args.Length()) {
            return std::string();
        }
        v8::Local<v8::String> str;
        if (!args[index]->ToString(context).ToLocal(&str)) {
            return std::string();
        }
        v8::String::Utf8Value utf8(isolate, str);
        return *utf8 != nullptr
                   ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
                   : std::string();
    }

    // args[index] if it is a JS function, else an empty handle (non-callable
    // listeners are ignored, keeping add/removeEventListener non-throwing).
    static v8::Local<v8::Function> listener_arg(
        const v8::FunctionCallbackInfo<v8::Value>& args, int index) {
        if (index < args.Length() && args[index]->IsFunction()) {
            return args[index].As<v8::Function>();
        }
        return v8::Local<v8::Function>();
    }

    static bool listener_option_bool(v8::Isolate* isolate,
                                     v8::Local<v8::Context> context,
                                     v8::Local<v8::Object> options,
                                     const char* name) {
        v8::Local<v8::Value> value;
        if (!options->Get(context, v8_string(isolate, name)).ToLocal(&value)) {
            return false;
        }
        return value->BooleanValue(isolate);
    }

    // options may be a boolean (capture) or an object with capture / once /
    // passive. Missing / null / other values use the defaults.
    static ListenerOptions listener_options(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const v8::FunctionCallbackInfo<v8::Value>& args, int index) {
        ListenerOptions out;
        if (index >= args.Length()) {
            return out;
        }
        v8::Local<v8::Value> value = args[index];
        if (value->IsObject()) {
            v8::Local<v8::Object> options = value.As<v8::Object>();
            out.capture =
                listener_option_bool(isolate, context, options, "capture");
            out.once = listener_option_bool(isolate, context, options, "once");
            out.passive =
                listener_option_bool(isolate, context, options, "passive");
        } else if (!value->IsUndefined() && !value->IsNull()) {
            out.capture = value->BooleanValue(isolate);
        }
        return out;
    }

    void add_listener(v8::Isolate* isolate, const std::string& type,
                      v8::Local<v8::Function> callback,
                      ListenerOptions options) {
        if (callback.IsEmpty()) {
            return;  // non-callable listener: ignored
        }
        std::vector<Listener>& bucket = listeners_[type];
        for (Listener& existing : bucket) {
            if (existing.capture == options.capture &&
                existing.callback.Get(isolate)->StrictEquals(callback)) {
                return;  // dedupe by (type, callback, capture)
            }
        }
        bucket.emplace_back();
        Listener& listener = bucket.back();
        listener.callback.Reset(isolate, callback);
        listener.capture = options.capture;
        listener.once = options.once;
        listener.passive = options.passive;
    }

    void remove_listener_handle(v8::Isolate* isolate, const std::string& type,
                                v8::Local<v8::Function> callback,
                                bool capture) {
        if (callback.IsEmpty()) {
            return;
        }
        auto it = listeners_.find(type);
        if (it == listeners_.end()) {
            return;
        }
        std::vector<Listener>& bucket = it->second;
        for (auto entry = bucket.begin(); entry != bucket.end(); ++entry) {
            if (entry->capture == capture &&
                entry->callback.Get(isolate)->StrictEquals(callback)) {
                entry->callback.Reset();
                bucket.erase(entry);
                return;  // remove only the first exact identity
            }
        }
    }

    void remove_listener(v8::Isolate* isolate, const std::string& type,
                         v8::Local<v8::Function> callback, bool capture) {
        remove_listener_handle(isolate, type, callback, capture);
    }

    // Fire the matching listeners for one target/phase. Capture listeners are
    // included when include_capture is true, bubble listeners when
    // include_bubble is true (AT_TARGET passes both, in registration order).
    void fire_phase(v8::Isolate* isolate, v8::Local<v8::Context> context,
                    v8::Local<v8::Object> event,
                    v8::Local<v8::Value> receiver, int phase,
                    bool include_capture, bool include_bubble) {
        (void)event->Set(context, v8_string(isolate, "currentTarget"),
                         receiver);
        (void)event->Set(context, v8_string(isolate, "eventPhase"),
                         v8::Integer::New(isolate, phase));

        std::string type;
        v8::Local<v8::Value> type_value;
        if (event->Get(context, v8_string(isolate, "type")).ToLocal(&type_value)) {
            v8::Local<v8::String> str;
            if (type_value->ToString(context).ToLocal(&str)) {
                v8::String::Utf8Value utf8(isolate, str);
                if (*utf8 != nullptr) {
                    type.assign(*utf8, static_cast<std::size_t>(utf8.length()));
                }
            }
        }

        auto it = listeners_.find(type);
        if (it != listeners_.end()) {
            std::vector<ListenerSnapshot> snapshot;
            snapshot.reserve(it->second.size());
            for (Listener& listener : it->second) {
                if ((listener.capture && include_capture) ||
                    (!listener.capture && include_bubble)) {
                    snapshot.push_back(ListenerSnapshot{
                        listener.callback.Get(isolate), listener.capture,
                        listener.once, listener.passive});
                }
            }
            v8::Local<v8::Value> event_arg = event;
            for (ListenerSnapshot& listener : snapshot) {
                if (listener.once) {
                    // Remove before the call so a reentrant dispatch cannot call it
                    // again.
                    remove_listener_handle(isolate, type, listener.callback,
                                           listener.capture);
                }
                const bool previous_passive =
                    event_in_passive_listener(isolate, context, event);
                // Always set (false for active listeners) so a nested dispatch from
                // a passive listener lets its own active listeners call
                // preventDefault; restore the outer state afterwards.
                set_event_in_passive_listener(isolate, context, event,
                                              listener.passive);
                v8::TryCatch try_catch(isolate);
                (void)listener.callback->Call(context, receiver, 1, &event_arg);
                if (try_catch.HasCaught()) {
                    state_from_isolate(isolate)->record_async_error(isolate, context, try_catch, "listener");
                    try_catch.Reset();  // one listener's error never stops the rest
                }
                set_event_in_passive_listener(isolate, context, event,
                                              previous_passive);
                if (event_is_immediately_stopped(isolate, context, event)) {
                    break;  // stopImmediatePropagation: skip later same-target listeners
                }
            }
        }
        if (!event_is_immediately_stopped(isolate, context, event)) {
            invoke_event_handler(isolate, context, event, receiver, phase, type);
        }
    }

    // Per-type listener lists (registration order). Global handles are released in
    // release_v8_handles() before isolate disposal.
    std::unordered_map<std::string, std::vector<Listener>> listeners_;
};


class StandaloneEventTarget : public EventTargetHost {
public:
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "EventTarget"; }
    std::string trace_object_type() const override { return "event_target"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override { return event_method_names(); }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate, v8::Local<v8::Context>,
                                      const std::string&) override { return v8::Undefined(isolate); }
    v8::Local<v8::Value> call_method(v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name, const v8::FunctionCallbackInfo<v8::Value>& args) override {
        v8::Local<v8::Value> result;
        if (handle_event_method(isolate, context, name, args, result)) return result;
        return v8::Undefined(isolate);
    }
};

}  // namespace iv8::browser
