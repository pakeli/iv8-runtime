#include "iv8/web_method.h"

#include <unordered_map>

namespace iv8 {

int web_method_length(std::string_view receiver, std::string_view name) {
    if (receiver == "history" && (name == "pushState" || name == "replaceState")) return 2;
    if (name == "add" && receiver == "host_probe") return 2;
    if (name == "replace" && receiver == "location") return 1;
    static const std::unordered_map<std::string_view, int> lengths = {
        {"fetch", 1}, {"echo", 1}, {"getElementById", 1}, {"querySelector", 1},
        {"querySelectorAll", 1}, {"getElementsByTagName", 1},
        {"getElementsByClassName", 1}, {"getElementsByName", 1},
        {"createElement", 1}, {"createTextNode", 1}, {"createComment", 1},
        {"appendChild", 1}, {"removeChild", 1}, {"insertBefore", 2},
        {"replaceChild", 2}, {"isEqualNode", 1}, {"isSameNode", 1},
        {"contains", 1}, {"getAttribute", 1}, {"hasAttribute", 1},
        {"setAttribute", 2}, {"removeAttribute", 1}, {"toggleAttribute", 1},
        {"matches", 1}, {"closest", 1}, {"insertAdjacentElement", 2},
        {"insertAdjacentHTML", 2}, {"insertAdjacentText", 2},
        {"addEventListener", 2}, {"removeEventListener", 2},
        {"dispatchEvent", 1}, {"addListener", 1}, {"removeListener", 1},
        {"assign", 1}, {"replace", 2}, {"getContext", 1},
        {"getItem", 1}, {"setItem", 2}, {"removeItem", 1}, {"key", 1},
        {"setTimeout", 1}, {"setInterval", 1}, {"requestAnimationFrame", 1},
        {"cancelAnimationFrame", 1}, {"requestIdleCallback", 1},
        {"cancelIdleCallback", 1}, {"queueMicrotask", 1}, {"postMessage", 1},
        {"atob", 1}, {"btoa", 1}, {"matchMedia", 1},
        {"setCustomValidity", 1}, {"item", 1}, {"namedItem", 1},
        {"toggle", 1}, {"observe", 2}, {"readAsText", 1},
        {"readAsArrayBuffer", 1}, {"query", 1}, {"getRandomValues", 1},
        {"mark", 1}, {"measure", 1}, {"getEntriesByName", 1},
        {"getEntriesByType", 1}, {"moveTo", 2}, {"moveBy", 2},
        {"resizeTo", 2}, {"resizeBy", 2},
    };
    const auto found = lengths.find(name);
    return found == lengths.end() ? 0 : found->second;
}

v8::MaybeLocal<v8::Function> make_web_method(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    std::string_view name, int length, v8::FunctionCallback callback,
    v8::Local<v8::Value> data) {
    v8::Local<v8::String> function_name;
    if (!v8::String::NewFromUtf8(isolate, name.data(),
            v8::NewStringType::kNormal, static_cast<int>(name.size()))
             .ToLocal(&function_name)) {
        return {};
    }
    v8::Local<v8::Function> function;
    if (!v8::Function::New(context, callback, data, length,
            v8::ConstructorBehavior::kThrow).ToLocal(&function)) {
        return {};
    }
    function->SetName(function_name);
    return function;
}

}  // namespace iv8
