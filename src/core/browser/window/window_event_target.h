#pragma once

#include "../internal/trace_hooks.h"
#include "../host/event_target.h"

namespace iv8::browser {

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

}  // namespace iv8::browser
