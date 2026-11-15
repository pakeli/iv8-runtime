#include "event_handlers.h"

#include "../internal/common.h"

namespace iv8::browser {

namespace {

std::string event_type_for_handler(const std::string& name) {
    if (name.size() > 2 && name[0] == 'o' && name[1] == 'n') {
        return name.substr(2);
    }
    return name;
}

}  // namespace

v8::Local<v8::Value> EventHandlerStore::get(v8::Isolate* isolate,
                                           const std::string& name) {
    const auto handler = handlers_.find(event_type_for_handler(name));
    if (handler == handlers_.end() || handler->second.IsEmpty()) {
        return v8::Null(isolate);
    }
    return handler->second.Get(isolate);
}

void EventHandlerStore::set(v8::Isolate* isolate, const std::string& name,
                             v8::Local<v8::Value> value) {
    const std::string type = event_type_for_handler(name);
    const auto handler = handlers_.find(type);
    if (!value->IsFunction()) {
        if (handler != handlers_.end()) {
            handler->second.Reset();
            handlers_.erase(handler);
        }
        return;
    }
    if (handler == handlers_.end()) {
        handlers_.emplace(type, v8::Global<v8::Function>(
                                    isolate, value.As<v8::Function>()));
    } else {
        handler->second.Reset(isolate, value.As<v8::Function>());
    }
}

void EventHandlerStore::invoke(v8::Isolate* isolate,
                                v8::Local<v8::Context> context,
                                v8::Local<v8::Object> event,
                                v8::Local<v8::Value> receiver, int phase,
                                const std::string& type) {
    // IDL handlers remain non-capture listeners, following the ordinary listener
    // snapshot at the target or during bubbling, as in the M11-51 contract.
    if (phase == 1) {
        return;
    }
    const auto entry = handlers_.find(type);
    if (entry == handlers_.end() || entry->second.IsEmpty()) {
        return;
    }
    v8::Local<v8::Function> handler = entry->second.Get(isolate);
    v8::Local<v8::Value> event_arg = event;
    v8::Local<v8::Value> result;
    v8::TryCatch try_catch(isolate);
    if (!handler->Call(context, receiver, 1, &event_arg).ToLocal(&result) ||
        try_catch.HasCaught()) {
        ContextState* state = state_from_isolate(isolate);
        if (state != nullptr) {
            state->record_async_error(isolate, context, try_catch, "listener");
        }
        try_catch.Reset();
        return;
    }
    if (!result->IsFalse()) {
        return;
    }
    v8::Local<v8::Value> cancelable;
    if (event->Get(context, v8_string(isolate, "cancelable"))
            .ToLocal(&cancelable) && cancelable->BooleanValue(isolate)) {
        (void)event->Set(context, v8_string(isolate, "defaultPrevented"),
                         v8::True(isolate));
    }
}

void EventHandlerStore::release_v8_handles() noexcept {
    for (auto& entry : handlers_) {
        entry.second.Reset();
    }
    handlers_.clear();
}

}  // namespace iv8::browser
