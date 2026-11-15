#include "attribute_hosts.h"

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

#include "iv8/web_interface.h"
#include "../dom/html_parser.h"
#include "../internal/common.h"
#include "document_host.h"

namespace iv8::browser {
namespace {

constexpr const char* kHtmlNamespace = "http://www.w3.org/1999/xhtml";
constexpr const char* kXmlNamespace = "http://www.w3.org/XML/1998/namespace";
constexpr const char* kXmlnsNamespace = "http://www.w3.org/2000/xmlns/";

void dom_error(v8::Isolate* isolate, const char* name, const char* message) {
    auto error = v8::Exception::Error(v8_string(isolate, message).As<v8::String>());
    (void)error.As<v8::Object>()->Set(isolate->GetCurrentContext(),
        v8_string(isolate, "name"), v8_string(isolate, name));
    isolate->ThrowException(error);
}

bool string_value(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> value, std::string* output, bool nullable = false) {
    if (nullable && value->IsNullOrUndefined()) { output->clear(); return true; }
    v8::Local<v8::String> text;
    if (!value->ToString(context).ToLocal(&text)) return false;
    v8::String::Utf8Value utf8(isolate, text);
    if (*utf8 == nullptr) return false;
    output->assign(*utf8, static_cast<std::size_t>(utf8.length()));
    return true;
}

bool xml_name(const std::string& name, bool allow_colon = true) {
    if (name.empty()) return false;
    const auto first = [allow_colon](unsigned char character) {
        return std::isalpha(character) || character == '_' || character >= 0x80 ||
            (allow_colon && character == ':');
    };
    if (!first(static_cast<unsigned char>(name.front()))) return false;
    return std::all_of(name.begin() + 1, name.end(), [&](unsigned char character) {
        return first(character) || std::isdigit(character) || character == '-' || character == '.';
    });
}

std::string canonical_name(DocumentHost* document, DomNode* node, std::string name) {
    return document->is_html_document() && node->namespace_uri == kHtmlNamespace
        ? ascii_lower(std::move(name)) : name;
}

std::optional<std::string> null_value(const DomNode* node, const std::string& name) {
    if (name == "id") return node->has_id ? std::optional(node->id) : std::nullopt;
    if (name == "class") return node->has_class ? std::optional(node->class_name) : std::nullopt;
    const auto found = node->attributes.find(name);
    return found == node->attributes.end() ? std::nullopt : std::optional(found->second);
}

void store_null_value(DomNode* node, const std::string& name,
    const std::optional<std::string>& value) {
    if (name == "id") {
        node->id = value.value_or(""); node->has_id = value.has_value();
    } else if (name == "class") {
        node->class_name = value.value_or(""); node->has_class = value.has_value();
        node->classes = split_class_tokens(node->class_name);
    } else if (value) {
        node->attributes[name] = *value;
    } else {
        node->attributes.erase(name);
    }
    if (name == "src") { node->src = value.value_or(""); node->has_src = value.has_value(); }
}

std::string attribute_value(const std::shared_ptr<DomAttribute>& attribute) {
    return attribute->owner_element != nullptr && attribute->namespace_uri.empty()
        ? null_value(attribute->owner_element, attribute->qualified_name).value_or(attribute->value)
        : attribute->value;
}

void ensure_metadata(DomNode* node) {
    std::vector<std::string> names;
    if (node->has_id) names.push_back("id");
    if (node->has_class) names.push_back("class");
    std::vector<std::string> raw;
    for (const auto& [name, value] : node->attributes) raw.push_back(name);
    std::sort(raw.begin(), raw.end());
    names.insert(names.end(), raw.begin(), raw.end());
    for (const auto& name : names) {
        const auto found = std::find_if(node->attribute_nodes.begin(), node->attribute_nodes.end(),
            [&name](const auto& item) { return item->namespace_uri.empty() && item->qualified_name == name; });
        if (found == node->attribute_nodes.end()) {
            auto attribute = std::make_shared<DomAttribute>();
            attribute->qualified_name = name; attribute->local_name = name; attribute->owner_element = node;
            node->attribute_nodes.push_back(std::move(attribute));
        }
    }
}

std::shared_ptr<DomAttribute> find_attribute(DomNode* node,
    const std::string& name, const std::string& namespace_uri = "", bool namespace_aware = false) {
    ensure_metadata(node);
    for (const auto& attribute : node->attribute_nodes) {
        if (namespace_aware
                ? attribute->namespace_uri == namespace_uri && attribute->local_name == name
                : attribute->qualified_name == name) return attribute;
    }
    return {};
}

void write_attribute(v8::Isolate* isolate, v8::Local<v8::Context> context,
    DocumentHost* document, const std::shared_ptr<DomAttribute>& attribute, const std::string& value) {
    auto* element = attribute->owner_element;
    if (element == nullptr) { attribute->value = value; return; }
    const auto previous = attribute_value(attribute);
    if (attribute->namespace_uri.empty()) store_null_value(element, attribute->qualified_name, value);
    else attribute->value = value;
    document->queue_attribute(isolate, context, element, attribute->local_name, true, previous, attribute->namespace_uri);
}

void remove_attribute(v8::Isolate* isolate, v8::Local<v8::Context> context,
    DocumentHost* document, const std::shared_ptr<DomAttribute>& attribute) {
    auto* element = attribute->owner_element;
    if (element == nullptr) return;
    const auto previous = attribute_value(attribute);
    if (attribute->namespace_uri.empty()) store_null_value(element, attribute->qualified_name, std::nullopt);
    attribute->value = previous; attribute->owner_element = nullptr;
    auto& attributes = element->attribute_nodes;
    attributes.erase(std::remove(attributes.begin(), attributes.end(), attribute), attributes.end());
    document->queue_attribute(isolate, context, element, attribute->local_name, true, previous, attribute->namespace_uri);
}

void add_null_attribute(v8::Isolate* isolate, v8::Local<v8::Context> context,
    DocumentHost* document, DomNode* element, const std::string& name, const std::string& value) {
    store_null_value(element, name, value);
    ensure_metadata(element);
    document->queue_attribute(isolate, context, element, name, false, "");
}

class AttributeObjectHost : public HostObject {
public:
    enum class Kind { Attribute, Map, Dataset };
    AttributeObjectHost(DocumentHost* owner, Kind type, DomNode* element = nullptr)
        : document(owner), kind(type), element(element) {}
    std::string global_name() const override { return {}; }
    DocumentHost* document;
    Kind kind;
    DomNode* element;
};

class AttrHost : public AttributeObjectHost {
public:
    AttrHost(DocumentHost* owner, std::shared_ptr<DomAttribute> value)
        : AttributeObjectHost(owner, Kind::Attribute), attribute(std::move(value)) {}
    std::string prototype_key() const override { return "Attr"; }
    std::string trace_object_type() const override { return "attr"; }
    std::vector<std::string> property_names() const override {
        return {"name", "value", "specified", "ownerElement", "namespaceURI", "prefix", "localName",
            "nodeType", "nodeName", "nodeValue", "textContent", "ownerDocument", "parentNode", "parentElement",
            "previousSibling", "nextSibling", "firstChild", "lastChild", "childNodes", "isConnected"};
    }
    std::vector<std::string> writable_property_names() const override { return {"value", "nodeValue", "textContent"}; }
    std::vector<std::string> method_names() const override {
        return {"cloneNode", "getRootNode", "normalize", "hasChildNodes", "isSameNode", "isEqualNode", "contains", "compareDocumentPosition"};
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name) override;
    void set_property(v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name, v8::Local<v8::Value> value) override;
    v8::Local<v8::Value> call_method(v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;
    std::shared_ptr<DomAttribute> attribute;
};

class NamedNodeMapHost : public AttributeObjectHost {
public:
    NamedNodeMapHost(DocumentHost* owner, DomNode* element) : AttributeObjectHost(owner, Kind::Map, element) {}
    std::string prototype_key() const override { return "NamedNodeMap"; }
    std::string trace_object_type() const override { return "named_node_map"; }
    std::vector<std::string> property_names() const override { return {"length"}; }
    std::vector<std::string> method_names() const override {
        return {"item", "getNamedItem", "getNamedItemNS", "setNamedItem", "setNamedItemNS", "removeNamedItem", "removeNamedItemNS"};
    }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate, v8::Local<v8::Context>, const std::string& name) override {
        ensure_metadata(element);
        return name == "length" ? v8::Local<v8::Value>(v8::Number::New(isolate, static_cast<double>(element->attribute_nodes.size())))
            : v8::Local<v8::Value>(v8::Undefined(isolate));
    }
    v8::Local<v8::Value> call_method(v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& arguments) override;
};

class DatasetHost : public AttributeObjectHost {
public:
    DatasetHost(DocumentHost* owner, DomNode* element) : AttributeObjectHost(owner, Kind::Dataset, element) {}
    std::string prototype_key() const override { return "DOMStringMap"; }
    std::string trace_object_type() const override { return "dom_string_map"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override { return {}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&) override { return v8::Undefined(isolate); }
    v8::Local<v8::Value> call_method(v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override { return v8::Undefined(isolate); }
};

}  // namespace

struct AttributeRegistryData {
    explicit AttributeRegistryData(DocumentHost* owner) : document(owner) {}
    DocumentHost* document;
    std::vector<std::unique_ptr<AttributeObjectHost>> hosts;
    std::unordered_map<DomNode*, NamedNodeMapHost*> maps;
    std::unordered_map<DomNode*, DatasetHost*> datasets;
    std::unordered_map<DomAttribute*, AttrHost*> attributes;
    std::unordered_map<AttributeObjectHost*, v8::Global<v8::Object>> wrappers;
};

AttributeRegistry::AttributeRegistry(DocumentHost* document) : data(std::make_unique<AttributeRegistryData>(document)) {}
AttributeRegistry::~AttributeRegistry() = default;
void AttributeRegistry::release_v8_handles() noexcept { for (auto& [host, object] : data->wrappers) object.Reset(); }
AttributeRegistry& DocumentHost::attribute_registry() {
    if (!attribute_registry_) attribute_registry_ = std::make_unique<AttributeRegistry>(this);
    return *attribute_registry_;
}

bool validate_namespace_name(v8::Isolate* isolate, const std::string& namespace_uri,
    const std::string& qualified_name, DOMNamespaceName* output) {
    const auto colon = qualified_name.find(':');
    const bool prefixed = colon != std::string::npos;
    const std::string prefix = prefixed ? qualified_name.substr(0, colon) : "";
    const std::string local = prefixed ? qualified_name.substr(colon + 1) : qualified_name;
    if (!xml_name(local, false) || (prefixed && (!xml_name(prefix, false) || qualified_name.find(':', colon + 1) != std::string::npos))) {
        dom_error(isolate, "InvalidCharacterError", "The qualified name is not a valid XML name"); return false;
    }
    if ((prefixed && namespace_uri.empty()) || (prefix == "xml" && namespace_uri != kXmlNamespace) ||
        ((qualified_name == "xmlns" || prefix == "xmlns") && namespace_uri != kXmlnsNamespace) ||
        (namespace_uri == kXmlnsNamespace && qualified_name != "xmlns" && prefix != "xmlns")) {
        dom_error(isolate, "NamespaceError", "The namespace and qualified name are incompatible"); return false;
    }
    *output = {namespace_uri, prefix, local, qualified_name};
    return true;
}

void notify_attribute_mutation(DomNode* node, const std::string& name, bool had_old_value, const std::string& old_value) {
    const auto current = null_value(node, name);
    auto found = std::find_if(node->attribute_nodes.begin(), node->attribute_nodes.end(),
        [&name](const auto& attribute) { return attribute->namespace_uri.empty() && attribute->qualified_name == name; });
    if (!current && found != node->attribute_nodes.end()) {
        (*found)->value = had_old_value ? old_value : attribute_value(*found);
        (*found)->owner_element = nullptr;
        node->attribute_nodes.erase(found);
    } else if (current && found == node->attribute_nodes.end()) {
        auto attribute = std::make_shared<DomAttribute>();
        attribute->qualified_name = name; attribute->local_name = name; attribute->owner_element = node;
        node->attribute_nodes.push_back(std::move(attribute));
    }
}

void copy_attribute_metadata(const DomNode* source, DomNode* clone) {
    clone->namespace_uri = source->namespace_uri; clone->prefix = source->prefix; clone->local_name = source->local_name;
    clone->attribute_nodes.clear();
    for (const auto& original : source->attribute_nodes) {
        auto attribute = std::make_shared<DomAttribute>(*original);
        attribute->owner_element = clone;
        clone->attribute_nodes.push_back(std::move(attribute));
    }
}

namespace {

AttrHost* as_attr(v8::Local<v8::Value> value) {
    auto* host = host_object_backing(value);
    return host != nullptr && host->prototype_key() == "Attr" ? static_cast<AttrHost*>(host) : nullptr;
}

v8::Local<v8::Value> wrap_attr(v8::Isolate* isolate, v8::Local<v8::Context> context,
    DocumentHost* document, const std::shared_ptr<DomAttribute>& attribute) {
    if (!attribute) return v8::Null(isolate);
    auto& registry = *document->attribute_registry().data;
    AttrHost* host;
    const auto found = registry.attributes.find(attribute.get());
    if (found == registry.attributes.end()) {
        auto created = std::make_unique<AttrHost>(document, attribute);
        host = created.get(); registry.attributes[attribute.get()] = host;
        registry.hosts.push_back(std::move(created));
    } else host = found->second;
    auto& object = registry.wrappers[host];
    if (object.IsEmpty()) object.Reset(isolate, make_host_object(isolate, context, host));
    return object.Get(isolate);
}

void transfer_host(AttributeObjectHost* host, DocumentHost* target) {
    auto& source = *host->document->attribute_registry().data;
    if (host->document == target) return;
    auto& destination = *target->attribute_registry().data;
    if (host->kind == AttributeObjectHost::Kind::Attribute) {
        auto* attribute = static_cast<AttrHost*>(host);
        source.attributes.erase(attribute->attribute.get());
        destination.attributes[attribute->attribute.get()] = attribute;
    } else if (host->kind == AttributeObjectHost::Kind::Map) {
        source.maps.erase(host->element); destination.maps[host->element] = static_cast<NamedNodeMapHost*>(host);
    } else {
        source.datasets.erase(host->element); destination.datasets[host->element] = static_cast<DatasetHost*>(host);
    }
    const auto object = source.wrappers.find(host);
    if (object != source.wrappers.end()) {
        destination.wrappers[host] = std::move(object->second); source.wrappers.erase(object);
    }
    auto owned = std::find_if(source.hosts.begin(), source.hosts.end(), [host](const auto& value) { return value.get() == host; });
    if (owned != source.hosts.end()) { destination.hosts.push_back(std::move(*owned)); source.hosts.erase(owned); }
    host->document = target;
}

v8::Local<v8::Value> attach_attr(v8::Isolate* isolate, v8::Local<v8::Context> context,
    DocumentHost* document, DomNode* element, AttrHost* host) {
    auto attribute = host->attribute;
    if (attribute->owner_element != nullptr && attribute->owner_element != element) {
        dom_error(isolate, "InUseAttributeError", "The attribute is already owned by another element"); return v8::Undefined(isolate);
    }
    auto replaced = find_attribute(element, attribute->local_name, attribute->namespace_uri, true);
    if (replaced == attribute) return wrap_attr(isolate, context, document, attribute);
    const auto previous = replaced ? std::optional(attribute_value(replaced)) : std::nullopt;
    if (replaced) {
        replaced->value = *previous; replaced->owner_element = nullptr;
        const auto position = std::find(element->attribute_nodes.begin(), element->attribute_nodes.end(), replaced);
        *position = attribute;
    } else element->attribute_nodes.push_back(attribute);
    const std::string value = attribute_value(attribute);
    if (attribute->namespace_uri.empty()) store_null_value(element, attribute->qualified_name, value);
    attribute->owner_element = element;
    transfer_host(host, document);
    document->queue_attribute(isolate, context, element, attribute->local_name, previous.has_value(), previous.value_or(""), attribute->namespace_uri);
    return replaced ? wrap_attr(isolate, context, document, replaced) : v8::Local<v8::Value>(v8::Null(isolate));
}

}  // namespace

void transfer_attribute_wrappers(DocumentHost* source, DocumentHost* target, const std::vector<DomNode*>& subtree) {
    const std::unordered_set<DomNode*> nodes(subtree.begin(), subtree.end());
    auto& registry = *source->attribute_registry().data;
    std::vector<AttributeObjectHost*> moved;
    for (const auto& host : registry.hosts) {
        DomNode* element = host->kind == AttributeObjectHost::Kind::Attribute
            ? static_cast<AttrHost*>(host.get())->attribute->owner_element : host->element;
        if (nodes.contains(element)) moved.push_back(host.get());
    }
    for (auto* host : moved) transfer_host(host, target);
}

namespace {

v8::Local<v8::Value> AttrHost::get_property(v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name) {
    if (name == "name" || name == "nodeName") return v8_string(isolate, attribute->qualified_name);
    if (name == "localName") return v8_string(isolate, attribute->local_name);
    if (name == "namespaceURI" || name == "prefix") {
        const auto& value = name == "prefix" ? attribute->prefix : attribute->namespace_uri;
        return value.empty() ? v8::Local<v8::Value>(v8::Null(isolate)) : v8::Local<v8::Value>(v8_string(isolate, value));
    }
    if (name == "value" || name == "nodeValue" || name == "textContent") return v8_string(isolate, attribute_value(attribute));
    if (name == "specified") return v8::True(isolate);
    if (name == "nodeType") return v8::Integer::New(isolate, 2);
    if (name == "isConnected") return v8::False(isolate);
    if (name == "ownerElement") return document->wrap_node(isolate, context, attribute->owner_element);
    if (name == "ownerDocument") return document->document_global(isolate, context);
    if (name == "childNodes") return v8::Array::New(isolate, 0);
    return v8::Null(isolate);
}

void AttrHost::set_property(v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name, v8::Local<v8::Value> value) {
    std::string text;
    if (string_value(isolate, context, value, &text, name != "value")) write_attribute(isolate, context, document, attribute, text);
}

v8::Local<v8::Value> AttrHost::call_method(v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    if (name == "appendChild" || name == "insertBefore" || name == "replaceChild" || name == "removeChild") {
        const int required = name == "insertBefore" || name == "replaceChild" ? 2 : 1;
        const auto is_node_value = [](v8::Local<v8::Value> value) {
            return NodeHost::node_of_arg(value) != nullptr || as_attr(value) != nullptr;
        };
        if (arguments.Length() < required || !is_node_value(arguments[0]) ||
            (required == 2 && !(name == "insertBefore" && arguments[1]->IsNull()) && !is_node_value(arguments[1]))) {
            isolate->ThrowException(v8::Exception::TypeError(v8_string(isolate, "A Node argument is required").As<v8::String>()));
        } else if (name == "removeChild") {
            dom_error(isolate, "NotFoundError", "An Attr has no child nodes to remove");
        } else {
            dom_error(isolate, "HierarchyRequestError", "An Attr cannot contain child nodes");
        }
        return v8::Undefined(isolate);
    }
    if (name == "cloneNode") {
        auto clone = std::make_shared<DomAttribute>(*attribute);
        clone->value = attribute_value(attribute); clone->owner_element = nullptr;
        return wrap_attr(isolate, context, document, clone);
    }
    if (name == "getRootNode") return wrap_attr(isolate, context, document, attribute);
    if (name == "normalize") return v8::Undefined(isolate);
    if (name == "hasChildNodes") return v8::False(isolate);
    auto* other = arguments.Length() ? as_attr(arguments[0]) : nullptr;
    if (name == "isSameNode" || name == "contains") return v8::Boolean::New(isolate, other != nullptr && other->attribute == attribute);
    if (name == "isEqualNode") return v8::Boolean::New(isolate, other != nullptr &&
        other->attribute->namespace_uri == attribute->namespace_uri && other->attribute->qualified_name == attribute->qualified_name &&
        other->attribute->local_name == attribute->local_name && attribute_value(other->attribute) == attribute_value(attribute));
    if (name == "compareDocumentPosition") {
        if (other != nullptr && other->attribute == attribute) return v8::Integer::New(isolate, 0);
        if (other != nullptr && attribute->owner_element != nullptr && attribute->owner_element == other->attribute->owner_element) {
            const auto& values = attribute->owner_element->attribute_nodes;
            const auto first = std::find(values.begin(), values.end(), attribute);
            const auto second = std::find(values.begin(), values.end(), other->attribute);
            return v8::Integer::New(isolate, 32 | (first < second ? 4 : 2));
        }
        return v8::Integer::New(isolate, 1 | 32 | 4);
    }
    return v8::Undefined(isolate);
}

v8::Local<v8::Value> NamedNodeMapHost::call_method(v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments) {
    const bool namespace_aware = name.ends_with("NS");
    const bool setting = name == "setNamedItem" || name == "setNamedItemNS";
    if (arguments.Length() < (namespace_aware && !setting ? 2 : 1)) {
        isolate->ThrowException(v8::Exception::TypeError(v8_string(isolate, "An attribute argument is required").As<v8::String>()));
        return v8::Undefined(isolate);
    }
    if (setting) {
        auto* attribute = as_attr(arguments[0]);
        if (attribute == nullptr) {
            isolate->ThrowException(v8::Exception::TypeError(v8_string(isolate, "An Attr is required").As<v8::String>()));
            return v8::Undefined(isolate);
        }
        return attach_attr(isolate, context, document, element, attribute);
    }
    if (name == "item") {
        const auto index = arguments[0]->Uint32Value(context);
        if (index.IsNothing()) return v8::Undefined(isolate);
        ensure_metadata(element);
        return index.FromJust() < element->attribute_nodes.size()
            ? wrap_attr(isolate, context, document, element->attribute_nodes[index.FromJust()])
            : v8::Local<v8::Value>(v8::Null(isolate));
    }
    std::string key, namespace_uri;
    if (namespace_aware && !string_value(isolate, context, arguments[0], &namespace_uri, true)) return v8::Undefined(isolate);
    if (!string_value(isolate, context, arguments[namespace_aware ? 1 : 0], &key)) return v8::Undefined(isolate);
    if (!namespace_aware) key = canonical_name(document, element, std::move(key));
    auto attribute = find_attribute(element, key, namespace_uri, namespace_aware);
    if (name == "removeNamedItem" || name == "removeNamedItemNS") {
        if (!attribute) { dom_error(isolate, "NotFoundError", "The requested attribute is not present"); return v8::Undefined(isolate); }
        remove_attribute(isolate, context, document, attribute);
    }
    return wrap_attr(isolate, context, document, attribute);
}

AttributeObjectHost* callback_host(v8::Local<v8::Object> holder) {
    return static_cast<AttributeObjectHost*>(host_object_backing(holder));
}

std::optional<std::string> dataset_key(const std::shared_ptr<DomAttribute>& attribute) {
    if (!attribute->namespace_uri.empty() || !attribute->qualified_name.starts_with("data-")) return std::nullopt;
    const auto raw = attribute->qualified_name.substr(5);
    if (std::any_of(raw.begin(), raw.end(), [](char character) { return character >= 'A' && character <= 'Z'; })) return std::nullopt;
    std::string key;
    for (std::size_t index = 0; index < raw.size(); ++index) {
        if (raw[index] == '-' && index + 1 < raw.size() && raw[index + 1] >= 'a' && raw[index + 1] <= 'z') {
            key += static_cast<char>(raw[++index] - 'a' + 'A');
        } else key += raw[index];
    }
    return key;
}

std::optional<std::string> data_attribute_name(v8::Isolate* isolate, const std::string& key) {
    std::string name = "data-";
    for (std::size_t index = 0; index < key.size(); ++index) {
        const auto character = key[index];
        if (character == '-' && index + 1 < key.size() && key[index + 1] >= 'a' && key[index + 1] <= 'z') {
            dom_error(isolate, "SyntaxError", "Dataset property names cannot contain a dash followed by a lowercase letter"); return std::nullopt;
        }
        if (character >= 'A' && character <= 'Z') { name += '-'; name += static_cast<char>(character - 'A' + 'a'); }
        else name += character;
    }
    if (!xml_name(name)) { dom_error(isolate, "InvalidCharacterError", "Invalid data attribute name"); return std::nullopt; }
    return name;
}

std::shared_ptr<DomAttribute> dynamic_attribute(AttributeObjectHost* host, const std::string& name) {
    ensure_metadata(host->element);
    if (host->kind == AttributeObjectHost::Kind::Map) return find_attribute(host->element, name);
    for (const auto& attribute : host->element->attribute_nodes) {
        if (dataset_key(attribute) == std::optional(name)) return attribute;
    }
    return {};
}

std::string property_name(v8::Isolate* isolate, v8::Local<v8::Name> property) {
    v8::String::Utf8Value text(isolate, property);
    return *text == nullptr ? "" : std::string(*text, static_cast<std::size_t>(text.length()));
}

v8::Intercepted attribute_named_get(v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Value>& info) {
    if (!property->IsString()) return v8::Intercepted::kNo;
    auto* host = callback_host(info.Holder());
    const auto attribute = dynamic_attribute(host, property_name(info.GetIsolate(), property));
    if (!attribute) return v8::Intercepted::kNo;
    info.GetReturnValue().Set(host->kind == AttributeObjectHost::Kind::Dataset
        ? v8_string(info.GetIsolate(), attribute_value(attribute))
        : wrap_attr(info.GetIsolate(), info.GetIsolate()->GetCurrentContext(), host->document, attribute));
    return v8::Intercepted::kYes;
}

v8::Intercepted attribute_named_query(v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Integer>& info) {
    if (!property->IsString()) return v8::Intercepted::kNo;
    auto* host = callback_host(info.Holder());
    if (!dynamic_attribute(host, property_name(info.GetIsolate(), property))) return v8::Intercepted::kNo;
    info.GetReturnValue().Set(host->kind == AttributeObjectHost::Kind::Dataset ? 0 : static_cast<int>(v8::ReadOnly | v8::DontEnum));
    return v8::Intercepted::kYes;
}

bool set_dataset(v8::Isolate* isolate, v8::Local<v8::Context> context,
    AttributeObjectHost* host, const std::string& key, v8::Local<v8::Value> value) {
    std::string text;
    if (!string_value(isolate, context, value, &text)) return false;
    const auto name = data_attribute_name(isolate, key);
    if (!name) return false;
    auto attribute = find_attribute(host->element, *name, "", true);
    if (attribute) write_attribute(isolate, context, host->document, attribute, text);
    else add_null_attribute(isolate, context, host->document, host->element, *name, text);
    return true;
}

v8::Intercepted attribute_named_set(v8::Local<v8::Name> property, v8::Local<v8::Value> value,
    const v8::PropertyCallbackInfo<v8::Boolean>& info) {
    if (!property->IsString()) return v8::Intercepted::kNo;
    auto* host = callback_host(info.Holder());
    if (host->kind == AttributeObjectHost::Kind::Dataset) {
        info.GetReturnValue().Set(set_dataset(info.GetIsolate(), info.GetIsolate()->GetCurrentContext(), host,
            property_name(info.GetIsolate(), property), value));
        return v8::Intercepted::kYes;
    }
    if (!dynamic_attribute(host, property_name(info.GetIsolate(), property))) return v8::Intercepted::kNo;
    info.GetReturnValue().Set(false); return v8::Intercepted::kYes;
}

v8::Intercepted attribute_named_delete(v8::Local<v8::Name> property, const v8::PropertyCallbackInfo<v8::Boolean>& info) {
    if (!property->IsString()) return v8::Intercepted::kNo;
    auto* host = callback_host(info.Holder());
    const auto attribute = dynamic_attribute(host, property_name(info.GetIsolate(), property));
    if (!attribute) return v8::Intercepted::kNo;
    if (host->kind == AttributeObjectHost::Kind::Dataset)
        remove_attribute(info.GetIsolate(), info.GetIsolate()->GetCurrentContext(), host->document, attribute);
    info.GetReturnValue().Set(true); return v8::Intercepted::kYes;
}

v8::Intercepted attribute_named_define(v8::Local<v8::Name> property, const v8::PropertyDescriptor& descriptor,
    const v8::PropertyCallbackInfo<v8::Boolean>& info) {
    if (!property->IsString()) return v8::Intercepted::kNo;
    auto* host = callback_host(info.Holder());
    if (host->kind == AttributeObjectHost::Kind::Dataset) {
        if (descriptor.has_get() || descriptor.has_set()) { info.GetReturnValue().Set(false); return v8::Intercepted::kYes; }
        info.GetReturnValue().Set(set_dataset(info.GetIsolate(), info.GetIsolate()->GetCurrentContext(), host,
            property_name(info.GetIsolate(), property), descriptor.has_value() ? descriptor.value() : v8::Local<v8::Value>(v8::Undefined(info.GetIsolate()))));
        return v8::Intercepted::kYes;
    }
    if (!dynamic_attribute(host, property_name(info.GetIsolate(), property))) return v8::Intercepted::kNo;
    info.GetReturnValue().Set(false); return v8::Intercepted::kYes;
}

void attribute_named_enumerate(const v8::PropertyCallbackInfo<v8::Array>& info) {
    auto* host = callback_host(info.Holder()); ensure_metadata(host->element);
    std::vector<std::string> names;
    for (const auto& attribute : host->element->attribute_nodes) {
        const auto name = host->kind == AttributeObjectHost::Kind::Dataset ? dataset_key(attribute) : std::optional(attribute->qualified_name);
        if (name && std::find(names.begin(), names.end(), *name) == names.end()) names.push_back(*name);
    }
    auto result = v8::Array::New(info.GetIsolate(), static_cast<int>(names.size()));
    for (std::size_t index = 0; index < names.size(); ++index)
        (void)result->Set(info.GetIsolate()->GetCurrentContext(), static_cast<std::uint32_t>(index), v8_string(info.GetIsolate(), names[index]));
    info.GetReturnValue().Set(result);
}

v8::Intercepted attribute_index_get(std::uint32_t index, const v8::PropertyCallbackInfo<v8::Value>& info) {
    auto* host = callback_host(info.Holder());
    if (host->kind == AttributeObjectHost::Kind::Dataset)
        return attribute_named_get(v8_string(info.GetIsolate(), std::to_string(index)).As<v8::Name>(), info);
    ensure_metadata(host->element);
    if (index >= host->element->attribute_nodes.size()) return v8::Intercepted::kNo;
    info.GetReturnValue().Set(wrap_attr(info.GetIsolate(), info.GetIsolate()->GetCurrentContext(), host->document, host->element->attribute_nodes[index]));
    return v8::Intercepted::kYes;
}

v8::Intercepted attribute_index_query(std::uint32_t index, const v8::PropertyCallbackInfo<v8::Integer>& info) {
    auto* host = callback_host(info.Holder());
    if (host->kind == AttributeObjectHost::Kind::Dataset)
        return attribute_named_query(v8_string(info.GetIsolate(), std::to_string(index)).As<v8::Name>(), info);
    ensure_metadata(host->element);
    if (index >= host->element->attribute_nodes.size()) return v8::Intercepted::kNo;
    info.GetReturnValue().Set(static_cast<int>(v8::ReadOnly)); return v8::Intercepted::kYes;
}

v8::Intercepted attribute_index_set(std::uint32_t index, v8::Local<v8::Value> value, const v8::PropertyCallbackInfo<v8::Boolean>& info) {
    return attribute_named_set(v8_string(info.GetIsolate(), std::to_string(index)).As<v8::Name>(), value, info);
}

v8::Intercepted attribute_index_delete(std::uint32_t index, const v8::PropertyCallbackInfo<v8::Boolean>& info) {
    return attribute_named_delete(v8_string(info.GetIsolate(), std::to_string(index)).As<v8::Name>(), info);
}

void attribute_index_enumerate(const v8::PropertyCallbackInfo<v8::Array>& info) {
    auto* host = callback_host(info.Holder());
    if (host->kind == AttributeObjectHost::Kind::Dataset) { info.GetReturnValue().Set(v8::Array::New(info.GetIsolate(), 0)); return; }
    ensure_metadata(host->element);
    auto result = v8::Array::New(info.GetIsolate(), static_cast<int>(host->element->attribute_nodes.size()));
    for (std::uint32_t index = 0; index < host->element->attribute_nodes.size(); ++index)
        (void)result->Set(info.GetIsolate()->GetCurrentContext(), index, v8::Integer::NewFromUnsigned(info.GetIsolate(), index));
    info.GetReturnValue().Set(result);
}

}  // namespace

v8::Local<v8::Value> element_attributes(v8::Isolate* isolate, v8::Local<v8::Context> context,
    DocumentHost* document, DomNode* element, bool dataset) {
    auto& registry = *document->attribute_registry().data;
    AttributeObjectHost* host = nullptr;
    if (dataset) {
        if (const auto found = registry.datasets.find(element); found != registry.datasets.end()) host = found->second;
        else {
            auto created = std::make_unique<DatasetHost>(document, element);
            host = created.get(); registry.datasets[element] = created.get(); registry.hosts.push_back(std::move(created));
        }
    } else {
        if (const auto found = registry.maps.find(element); found != registry.maps.end()) host = found->second;
        else {
            auto created = std::make_unique<NamedNodeMapHost>(document, element);
            host = created.get(); registry.maps[element] = created.get(); registry.hosts.push_back(std::move(created));
        }
    }
    auto& cached = registry.wrappers[host];
    if (cached.IsEmpty()) {
        install_host_interface(isolate, context, host);
        auto object_template = v8::ObjectTemplate::New(isolate);
        object_template->SetInternalFieldCount(1);
        const auto flags = static_cast<v8::PropertyHandlerFlags>(static_cast<int>(v8::PropertyHandlerFlags::kOnlyInterceptStrings) |
            (dataset ? 0 : static_cast<int>(v8::PropertyHandlerFlags::kNonMasking)));
        object_template->SetHandler(v8::NamedPropertyHandlerConfiguration(&attribute_named_get, &attribute_named_set,
            &attribute_named_query, &attribute_named_delete, &attribute_named_enumerate, &attribute_named_define, nullptr, {}, flags));
        object_template->SetHandler(v8::IndexedPropertyHandlerConfiguration(&attribute_index_get, &attribute_index_set,
            &attribute_index_query, &attribute_index_delete, &attribute_index_enumerate));
        v8::Local<v8::Object> object;
        if (!object_template->NewInstance(context).ToLocal(&object)) return v8::Undefined(isolate);
        object->SetAlignedPointerInInternalField(0, host, v8::kEmbedderDataTypeTagDefault);
        if (!object->SetPrototype(context, web_interface_prototype(isolate, context, host->prototype_key())).FromMaybe(false)) return v8::Undefined(isolate);
        cached.Reset(isolate, object);
    }
    return cached.Get(isolate);
}

void install_attribute_interfaces(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    AttrHost attribute(nullptr, std::make_shared<DomAttribute>());
    NamedNodeMapHost map(nullptr, nullptr); DatasetHost dataset(nullptr, nullptr);
    install_host_interface(isolate, context, &attribute);
    install_host_interface(isolate, context, &map);
    install_host_interface(isolate, context, &dataset);
}

bool handle_document_attribute_method(v8::Isolate* isolate, v8::Local<v8::Context> context,
    DocumentHost* document, const std::string& name, const v8::FunctionCallbackInfo<v8::Value>& arguments,
    v8::Local<v8::Value>& output) {
    if (name != "createAttribute" && name != "createAttributeNS") return false;
    output = v8::Undefined(isolate);
    const bool namespace_aware = name == "createAttributeNS";
    if (arguments.Length() < (namespace_aware ? 2 : 1)) {
        isolate->ThrowException(v8::Exception::TypeError(v8_string(isolate, "An attribute name is required").As<v8::String>())); return true;
    }
    std::string qualified_name, namespace_uri;
    if (namespace_aware && !string_value(isolate, context, arguments[0], &namespace_uri, true)) return true;
    if (!string_value(isolate, context, arguments[namespace_aware ? 1 : 0], &qualified_name)) return true;
    DOMNamespaceName metadata;
    if (namespace_aware) {
        if (!validate_namespace_name(isolate, namespace_uri, qualified_name, &metadata)) return true;
    } else {
        if (!xml_name(qualified_name)) { dom_error(isolate, "InvalidCharacterError", "Invalid attribute name"); return true; }
        if (document->is_html_document()) qualified_name = ascii_lower(std::move(qualified_name));
        metadata = {"", "", qualified_name, qualified_name};
    }
    auto attribute = std::make_shared<DomAttribute>();
    attribute->qualified_name = metadata.qualified_name; attribute->namespace_uri = metadata.namespace_uri;
    attribute->prefix = metadata.prefix; attribute->local_name = metadata.local_name;
    output = wrap_attr(isolate, context, document, attribute);
    return true;
}

bool handle_element_attribute_method(v8::Isolate* isolate, v8::Local<v8::Context> context,
    DocumentHost* document, DomNode* element, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments, v8::Local<v8::Value>& output) {
    static const std::unordered_set<std::string> methods = {"getAttribute", "hasAttribute", "setAttribute", "removeAttribute",
        "toggleAttribute", "hasAttributes", "getAttributeNames", "getAttributeNS", "hasAttributeNS", "setAttributeNS", "removeAttributeNS",
        "getAttributeNode", "getAttributeNodeNS", "setAttributeNode", "setAttributeNodeNS", "removeAttributeNode"};
    if (!methods.contains(name)) return false;
    output = v8::Undefined(isolate);
    ensure_metadata(element);
    if (name == "hasAttributes") { output = v8::Boolean::New(isolate, !element->attribute_nodes.empty()); return true; }
    if (name == "getAttributeNames") {
        auto names = v8::Array::New(isolate, static_cast<int>(element->attribute_nodes.size()));
        for (std::size_t index = 0; index < element->attribute_nodes.size(); ++index)
            (void)names->Set(context, static_cast<std::uint32_t>(index), v8_string(isolate, element->attribute_nodes[index]->qualified_name));
        output = names; return true;
    }
    const bool node_setting = name == "setAttributeNode" || name == "setAttributeNodeNS";
    if (node_setting || name == "removeAttributeNode") {
        auto* attribute = arguments.Length() ? as_attr(arguments[0]) : nullptr;
        if (attribute == nullptr) {
            isolate->ThrowException(v8::Exception::TypeError(v8_string(isolate, "An Attr is required").As<v8::String>())); return true;
        }
        if (node_setting) output = attach_attr(isolate, context, document, element, attribute);
        else {
            if (attribute->attribute->owner_element != element) { dom_error(isolate, "NotFoundError", "Attribute is not owned by this element"); return true; }
            remove_attribute(isolate, context, document, attribute->attribute);
            output = arguments[0];
        }
        return true;
    }
    const bool namespace_aware = name.ends_with("NS");
    const bool setting = name == "setAttribute" || name == "setAttributeNS";
    const int required = (namespace_aware ? 2 : 1) + (setting ? 1 : 0);
    if (arguments.Length() < required) {
        isolate->ThrowException(v8::Exception::TypeError(v8_string(isolate, "Attribute arguments are required").As<v8::String>())); return true;
    }
    std::string key, namespace_uri, value;
    if (namespace_aware && !string_value(isolate, context, arguments[0], &namespace_uri, true)) return true;
    if (!string_value(isolate, context, arguments[namespace_aware ? 1 : 0], &key)) return true;
    if (setting && !string_value(isolate, context, arguments[namespace_aware ? 2 : 1], &value)) return true;
    DOMNamespaceName metadata;
    if (setting && namespace_aware) {
        if (!validate_namespace_name(isolate, namespace_uri, key, &metadata)) return true;
        key = metadata.local_name;
    } else if (!namespace_aware) {
        key = canonical_name(document, element, std::move(key));
        if ((setting || name == "toggleAttribute") && !xml_name(key)) {
            dom_error(isolate, "InvalidCharacterError", "Invalid attribute name"); return true;
        }
    }
    auto attribute = find_attribute(element, key, namespace_uri, namespace_aware);
    if (setting) {
        if (attribute) write_attribute(isolate, context, document, attribute, value);
        else if (namespace_aware && !namespace_uri.empty()) {
            attribute = std::make_shared<DomAttribute>();
            attribute->namespace_uri = namespace_uri; attribute->qualified_name = metadata.qualified_name;
            attribute->prefix = metadata.prefix; attribute->local_name = metadata.local_name;
            attribute->value = value; attribute->owner_element = element;
            element->attribute_nodes.push_back(attribute);
            document->queue_attribute(isolate, context, element, attribute->local_name, false, "", namespace_uri);
        } else {
            const auto stored_name = namespace_aware ? metadata.qualified_name : key;
            add_null_attribute(isolate, context, document, element, stored_name, value);
        }
    } else if (name == "removeAttribute" || name == "removeAttributeNS") {
        if (attribute) remove_attribute(isolate, context, document, attribute);
    } else if (name == "hasAttribute" || name == "hasAttributeNS") output = v8::Boolean::New(isolate, attribute != nullptr);
    else if (name == "getAttributeNode" || name == "getAttributeNodeNS") output = wrap_attr(isolate, context, document, attribute);
    else if (name == "getAttribute" || name == "getAttributeNS") output = attribute
        ? v8_string(isolate, attribute_value(attribute)) : v8::Local<v8::Value>(v8::Null(isolate));
    else if (name == "toggleAttribute") {
        const bool explicit_force = arguments.Length() > 1 && !arguments[1]->IsUndefined();
        const bool present = explicit_force ? arguments[1]->BooleanValue(isolate) : !attribute;
        if (present && !attribute) add_null_attribute(isolate, context, document, element, key, "");
        else if (!present && attribute) remove_attribute(isolate, context, document, attribute);
        output = v8::Boolean::New(isolate, present);
    }
    return true;
}

bool handle_attribute_node_operation(v8::Isolate* isolate, v8::Local<v8::Context> context,
    DocumentHost* target, const std::string& operation,
    const v8::FunctionCallbackInfo<v8::Value>& arguments, v8::Local<v8::Value>& output) {
    auto* host = arguments.Length() ? as_attr(arguments[0]) : nullptr;
    if (host == nullptr) return false;
    if (operation == "adoptNode") {
        if (host->attribute->owner_element != nullptr) remove_attribute(isolate, context, host->document, host->attribute);
        transfer_host(host, target); output = arguments[0];
    } else if (operation == "importNode" || operation == "cloneNode") {
        auto clone = std::make_shared<DomAttribute>(*host->attribute);
        clone->value = attribute_value(host->attribute); clone->owner_element = nullptr;
        output = wrap_attr(isolate, context, target, clone);
    } else return false;
    return true;
}

}  // namespace iv8::browser
