#include "iv8/web_interface.h"

#include <algorithm>
#include <initializer_list>
#include <unordered_map>
#include <unordered_set>
#include <utility>

#include "iv8/web_method.h"

namespace iv8 {
namespace {

v8::Local<v8::String> string(v8::Isolate* isolate, std::string_view text) {
    v8::Local<v8::String> result;
    if (!v8::String::NewFromUtf8(isolate, text.data(), v8::NewStringType::kNormal,
            static_cast<int>(text.size())).ToLocal(&result)) return {};
    return result;
}

std::string_view parent_interface(std::string_view name) {
    static const std::unordered_map<std::string_view, std::string_view> parents = {
        {"Window", "WindowProperties"}, {"WindowProperties", "EventTarget"},
        {"HTMLDocument", "Document"}, {"XMLDocument", "Document"},
        {"Document", "Node"}, {"Element", "Node"}, {"HTMLElement", "Element"},
        {"HTMLAudioElement", "HTMLMediaElement"}, {"HTMLVideoElement", "HTMLMediaElement"},
        {"HTMLMediaElement", "HTMLElement"},
        {"Node", "EventTarget"}, {"CharacterData", "Node"},
        {"Text", "CharacterData"}, {"Comment", "CharacterData"},
        {"DocumentFragment", "Node"}, {"Screen", "EventTarget"},
        {"XMLHttpRequestEventTarget", "EventTarget"},
        {"XMLHttpRequest", "XMLHttpRequestEventTarget"}, {"Worker", "EventTarget"},
    };
    const auto found = parents.find(name);
    if (found != parents.end()) return found->second;
    if (name.starts_with("HTML") && name.ends_with("Element") && name != "HTMLElement")
        return "HTMLElement";
    return {};
}

v8::Local<v8::Private> registry_key(v8::Isolate* isolate, std::string_view name) {
    return v8::Private::ForApi(isolate, string(isolate, std::string("iv8.webidl.") + std::string(name)));
}

void interface_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    v8::String::Utf8Value text(isolate, info.Data());
    const std::string name(*text != nullptr ? *text : "");
    const bool constructible = name == "Document" || name == "EventTarget" ||
        name == "Text" || name == "Comment" || name == "DocumentFragment" ||
        name == "XMLHttpRequest" || name == "Worker";
    if (!info.IsConstructCall()) {
        const auto message = constructible
            ? "Failed to construct '" + name + "': Please use the 'new' operator, this DOM object constructor cannot be called as a function."
            : "Illegal constructor";
        isolate->ThrowException(v8::Exception::TypeError(string(isolate, message)));
        return;
    }
    if (!constructible) {
        isolate->ThrowException(v8::Exception::TypeError(string(isolate,
            "Failed to construct '" + name + "': Illegal constructor")));
        return;
    }
    v8::Local<v8::Value> factory;
    if (!context->Global()->GetPrivate(context, registry_key(isolate, "factory"))
            .ToLocal(&factory) || !factory->IsFunction()) return;
    auto arguments = v8::Array::New(isolate, info.Length());
    for (int index = 0; index < info.Length(); ++index) {
        if (!arguments->Set(context, index, info[index]).FromMaybe(false)) return;
    }
    v8::Local<v8::Value> factory_arguments[] = {info.Data(), arguments};
    v8::Local<v8::Value> result;
    if (!factory.As<v8::Function>()->Call(context, info.This(), 2, factory_arguments)
            .ToLocal(&result)) return;
    // Respect a subclass's prototype while retaining the native receiver brand.
    v8::Local<v8::Value> prototype;
    if (result->IsObject() && info.NewTarget().As<v8::Object>()
            ->Get(context, string(isolate, "prototype")).ToLocal(&prototype) &&
        prototype->IsObject()) {
        if (!result.As<v8::Object>()->SetPrototype(context, prototype).FromMaybe(false)) return;
    }
    info.GetReturnValue().Set(result);
}

bool member_in(std::string_view member, std::initializer_list<std::string_view> names) {
    return std::find(names.begin(), names.end(), member) != names.end();
}

const std::unordered_map<std::string_view, std::string_view>& html_interfaces() {
    static const std::unordered_map<std::string_view, std::string_view> names = {
        {"html", "HTMLHtmlElement"}, {"head", "HTMLHeadElement"},
        {"body", "HTMLBodyElement"}, {"title", "HTMLTitleElement"},
        {"div", "HTMLDivElement"}, {"span", "HTMLSpanElement"},
        {"p", "HTMLParagraphElement"}, {"form", "HTMLFormElement"},
        {"script", "HTMLScriptElement"}, {"meta", "HTMLMetaElement"},
        {"a", "HTMLAnchorElement"}, {"area", "HTMLAreaElement"},
        {"input", "HTMLInputElement"}, {"button", "HTMLButtonElement"},
        {"select", "HTMLSelectElement"}, {"textarea", "HTMLTextAreaElement"},
        {"option", "HTMLOptionElement"}, {"canvas", "HTMLCanvasElement"},
        {"img", "HTMLImageElement"}, {"iframe", "HTMLIFrameElement"},
        {"link", "HTMLLinkElement"}, {"style", "HTMLStyleElement"},
        {"ul", "HTMLUListElement"}, {"ol", "HTMLOListElement"},
        {"li", "HTMLLIElement"}, {"table", "HTMLTableElement"},
        {"tr", "HTMLTableRowElement"}, {"td", "HTMLTableCellElement"},
        {"th", "HTMLTableCellElement"}, {"label", "HTMLLabelElement"},
        {"tbody", "HTMLTableSectionElement"}, {"thead", "HTMLTableSectionElement"},
        {"tfoot", "HTMLTableSectionElement"}, {"caption", "HTMLTableCaptionElement"},
        {"col", "HTMLTableColElement"}, {"colgroup", "HTMLTableColElement"},
        {"embed", "HTMLEmbedElement"}, {"object", "HTMLObjectElement"},
        {"br", "HTMLBRElement"}, {"hr", "HTMLHRElement"}, {"pre", "HTMLPreElement"},
        {"dl", "HTMLDListElement"}, {"menu", "HTMLMenuElement"},
        {"fieldset", "HTMLFieldSetElement"}, {"legend", "HTMLLegendElement"},
        {"optgroup", "HTMLOptGroupElement"}, {"output", "HTMLOutputElement"},
        {"progress", "HTMLProgressElement"}, {"meter", "HTMLMeterElement"},
        {"audio", "HTMLAudioElement"}, {"video", "HTMLVideoElement"},
        {"source", "HTMLSourceElement"}, {"track", "HTMLTrackElement"},
        {"template", "HTMLTemplateElement"}, {"slot", "HTMLSlotElement"},
        {"dialog", "HTMLDialogElement"}, {"details", "HTMLDetailsElement"},
        {"time", "HTMLTimeElement"}, {"data", "HTMLDataElement"},
        {"q", "HTMLQuoteElement"}, {"blockquote", "HTMLQuoteElement"},
        {"del", "HTMLModElement"}, {"ins", "HTMLModElement"},
        {"map", "HTMLMapElement"}, {"picture", "HTMLPictureElement"},
        {"base", "HTMLBaseElement"},
        {"h1", "HTMLHeadingElement"}, {"h2", "HTMLHeadingElement"},
        {"h3", "HTMLHeadingElement"}, {"h4", "HTMLHeadingElement"},
        {"h5", "HTMLHeadingElement"}, {"h6", "HTMLHeadingElement"},
    };
    return names;
}

}  // namespace

bool web_interface_inherits(std::string_view name, std::string_view base) {
    for (; !name.empty(); name = parent_interface(name)) {
        if (name == base) return true;
    }
    return false;
}

std::string html_element_interface(std::string_view tag) {
    const auto& names = html_interfaces();
    const auto found = names.find(tag);
    if (found != names.end()) return std::string(found->second);
    static const std::unordered_set<std::string_view> generic_tags = {
        "abbr", "address", "article", "aside", "b", "bdi", "bdo", "cite", "code",
        "dd", "dfn", "dt", "em", "figcaption", "figure", "footer", "header", "hgroup",
        "i", "kbd", "main", "mark", "nav", "noscript", "rp", "rt", "ruby", "s", "samp",
        "search", "section", "small", "strong", "sub", "summary", "sup", "u", "var", "wbr"};
    return tag.find('-') != std::string_view::npos || generic_tags.contains(tag)
        ? "HTMLElement" : "HTMLUnknownElement";
}

std::string web_member_interface(const std::string& name,
                                  std::string_view member, bool method) {
    if (web_interface_inherits(name, "EventTarget") && method &&
        member_in(member, {"addEventListener", "removeEventListener", "dispatchEvent"}))
        return "EventTarget";
    if (web_interface_inherits(name, "Node")) {
        if ((method && member_in(member, {"appendChild", "removeChild", "insertBefore",
                "replaceChild", "cloneNode", "isEqualNode", "isSameNode", "contains", "hasChildNodes"})) ||
            (!method && member_in(member, {"nodeName", "nodeType", "nodeValue", "textContent",
                "parentNode", "parentElement", "childNodes", "ownerDocument", "isConnected",
                "firstChild", "lastChild", "previousSibling", "nextSibling", "baseURI"})))
            return "Node";
    }
    if (web_interface_inherits(name, "Document")) return "Document";
    if (web_interface_inherits(name, "CharacterData")) return "CharacterData";
    if (web_interface_inherits(name, "Element")) {
        if ((method && member_in(member, {"getAttribute", "hasAttribute", "setAttribute",
                "removeAttribute", "toggleAttribute", "hasAttributes", "getAttributeNames", "getElementsByTagName", "getElementsByClassName",
                "querySelector", "querySelectorAll", "matches", "closest", "remove", "append",
                "prepend", "replaceChildren", "before", "after", "replaceWith", "insertAdjacentElement",
                "insertAdjacentHTML", "insertAdjacentText"})) ||
            (!method && member_in(member, {"id", "className", "tagName", "innerHTML", "outerHTML",
                "children", "previousElementSibling", "nextElementSibling", "firstElementChild",
                "lastElementChild", "childElementCount", "classList"}))) return "Element";
        if (!method && member_in(member, {"innerText", "style"})) return "HTMLElement";
    }
    return name;
}

v8::Local<v8::Object> web_interface_prototype(
    v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name) {
    auto slot = registry_key(isolate, "prototype." + name);
    v8::Local<v8::Value> cached;
    if (!context->Global()->GetPrivate(context, slot).ToLocal(&cached)) return {};
    if (cached->IsObject()) return cached.As<v8::Object>();
    auto prototype = v8::Object::New(isolate);
    if (!context->Global()->SetPrivate(context, slot, prototype).FromMaybe(false)) return {};
    const auto parent = parent_interface(name);
    if (!parent.empty()) {
        auto parent_prototype = web_interface_prototype(isolate, context, std::string(parent));
        if (parent_prototype.IsEmpty() ||
            !prototype->SetPrototype(context, parent_prototype).FromMaybe(false)) return {};
    }
    if (!prototype->DefineOwnProperty(context, v8::Symbol::GetToStringTag(isolate),
            string(isolate, name), static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum))
            .FromMaybe(false)) return {};
    if (name == "WindowProperties") return prototype;
    v8::Local<v8::Function> constructor;
    const int length = name == "Worker" ? 1 : 0;
    if (!v8::Function::New(context, &interface_constructor, string(isolate, name), length)
            .ToLocal(&constructor)) return {};
    constructor->SetName(string(isolate, name));
    (void)prototype->SetPrivate(context, registry_key(isolate, "constructor"), constructor);
    if (!constructor->DefineOwnProperty(context, string(isolate, "prototype"), prototype,
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum | v8::DontDelete))
            .FromMaybe(false) ||
        !prototype->DefineOwnProperty(context, string(isolate, "constructor"), constructor,
            v8::DontEnum).FromMaybe(false) ||
        !context->Global()->DefineOwnProperty(context, string(isolate, name), constructor,
            v8::DontEnum).FromMaybe(false)) return {};
    if (name == "Node") {
        static const std::pair<const char*, int> constants[] = {
            {"ELEMENT_NODE", 1}, {"ATTRIBUTE_NODE", 2}, {"TEXT_NODE", 3},
            {"CDATA_SECTION_NODE", 4}, {"ENTITY_REFERENCE_NODE", 5}, {"ENTITY_NODE", 6},
            {"PROCESSING_INSTRUCTION_NODE", 7}, {"COMMENT_NODE", 8}, {"DOCUMENT_NODE", 9},
            {"DOCUMENT_TYPE_NODE", 10}, {"DOCUMENT_FRAGMENT_NODE", 11}, {"NOTATION_NODE", 12},
            {"DOCUMENT_POSITION_DISCONNECTED", 1}, {"DOCUMENT_POSITION_PRECEDING", 2},
            {"DOCUMENT_POSITION_FOLLOWING", 4}, {"DOCUMENT_POSITION_CONTAINS", 8},
            {"DOCUMENT_POSITION_CONTAINED_BY", 16}, {"DOCUMENT_POSITION_IMPLEMENTATION_SPECIFIC", 32}};
        for (const auto& [member, value] : constants) {
            for (auto target : {v8::Local<v8::Object>(constructor), prototype}) {
                if (!target->DefineOwnProperty(context, string(isolate, member),
                        v8::Integer::New(isolate, value),
                        static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontDelete))
                        .FromMaybe(false)) return {};
            }
        }
    }
    // Interface object inheritance follows the same parent interface, except
    // WindowProperties which is an instance-prototype-only link.
    auto constructor_parent = parent;
    if (constructor_parent == "WindowProperties") constructor_parent = "EventTarget";
    if (!constructor_parent.empty()) {
        v8::Local<v8::Value> parent_constructor;
        auto parent_prototype = web_interface_prototype(isolate, context, std::string(constructor_parent));
        if (parent_prototype.IsEmpty() || !parent_prototype->GetPrivate(context, registry_key(isolate, "constructor"))
                .ToLocal(&parent_constructor) ||
            !constructor->SetPrototype(context, parent_constructor).FromMaybe(false)) return {};
    }
    return prototype;
}

bool replace_window_properties_prototype(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> prototype) {
    auto parent = web_interface_prototype(isolate, context, "EventTarget");
    auto window = web_interface_prototype(isolate, context, "Window");
    if (prototype.IsEmpty() || parent.IsEmpty() || window.IsEmpty()) return false;
    const auto registry_slot = v8::Private::ForApi(isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8.webidl.prototype.WindowProperties"));
    return prototype->SetPrototype(context, parent).FromMaybe(false) &&
        prototype->DefineOwnProperty(context, v8::Symbol::GetToStringTag(isolate),
            v8::String::NewFromUtf8Literal(isolate, "WindowProperties"),
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum)).FromMaybe(false) &&
        window->SetPrototype(context, prototype).FromMaybe(false) &&
        context->Global()->SetPrivate(context, registry_slot, prototype).FromMaybe(false);
}

void install_web_interfaces(v8::Isolate* isolate, v8::Local<v8::Context> context,
                             v8::FunctionCallback factory, v8::Local<v8::Value> data) {
    v8::Local<v8::Function> function;
    if (!make_web_method(isolate, context, "", 2, factory, data).ToLocal(&function)) return;
    if (!context->Global()->SetPrivate(context, registry_key(isolate, "factory"), function)
            .FromMaybe(false)) return;
    for (const char* name : {"Window", "Document", "HTMLDocument", "XMLDocument",
            "Navigator", "Screen", "Location", "EventTarget", "Node", "Element",
            "HTMLElement", "HTMLDivElement", "HTMLFormElement", "HTMLScriptElement",
            "HTMLMetaElement", "Text", "Comment", "DocumentFragment", "Storage",
            "XMLHttpRequest", "Worker"}) {
        if (web_interface_prototype(isolate, context, name).IsEmpty()) return;
    }
    for (const auto& [tag, name] : html_interfaces()) {
        if (web_interface_prototype(isolate, context, std::string(name)).IsEmpty()) return;
    }
    (void)web_interface_prototype(isolate, context, "HTMLUnknownElement");
}

}  // namespace iv8
