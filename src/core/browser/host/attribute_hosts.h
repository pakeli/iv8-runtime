#pragma once

#include <memory>
#include <string>
#include <vector>

#include "iv8/page_state.h"
#include "iv8/host_object.h"
#include "../dom/dom_node.h"

namespace iv8::browser {

class DocumentHost;
struct AttributeRegistryData;

class AttributeRegistry {
public:
    explicit AttributeRegistry(DocumentHost* document);
    ~AttributeRegistry();
    void release_v8_handles() noexcept;
    std::unique_ptr<AttributeRegistryData> data;
};

struct DOMNamespaceName {
    std::string namespace_uri;
    std::string prefix;
    std::string local_name;
    std::string qualified_name;
};

bool validate_namespace_name(v8::Isolate* isolate,
    const std::string& namespace_uri, const std::string& qualified_name,
    DOMNamespaceName* output);
void install_attribute_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context);
void copy_attribute_metadata(const DomNode* source, DomNode* clone);
void transfer_attribute_wrappers(DocumentHost* source, DocumentHost* target,
    const std::vector<DomNode*>& subtree);
void notify_attribute_mutation(DomNode* node, const std::string& name,
    bool had_old_value, const std::string& old_value);

v8::Local<v8::Value> element_attributes(v8::Isolate* isolate,
    v8::Local<v8::Context> context, DocumentHost* document, DomNode* element,
    bool dataset);
bool handle_element_attribute_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, DocumentHost* document, DomNode* element,
    const std::string& name, const v8::FunctionCallbackInfo<v8::Value>& arguments,
    v8::Local<v8::Value>& output);
bool handle_document_attribute_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, DocumentHost* document, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& arguments,
    v8::Local<v8::Value>& output);
bool handle_attribute_node_operation(v8::Isolate* isolate,
    v8::Local<v8::Context> context, DocumentHost* target, const std::string& operation,
    const v8::FunctionCallbackInfo<v8::Value>& arguments,
    v8::Local<v8::Value>& output);

}  // namespace iv8::browser
