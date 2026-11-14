#pragma once

#include <string>
#include <string_view>

#include "iv8/v8_headers.h"

namespace iv8 {

// The context's private registry owns prototypes, constructors and native
// function identities. It has no process-wide or cross-isolate handles.
v8::Local<v8::Object> web_interface_prototype(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const std::string& interface_name);
// Register this generation's native WindowProperties resolver. The caller owns
// its native state and must stop installation if a V8 operation fails.
bool replace_window_properties_prototype(v8::Isolate* isolate,
    v8::Local<v8::Context> context, v8::Local<v8::Object> prototype);
bool web_interface_inherits(std::string_view interface_name,
                            std::string_view base);
std::string html_element_interface(std::string_view tag);
std::string web_member_interface(const std::string& interface_name,
                                  std::string_view member, bool method);

// The factory is a hidden native callback for interfaces with native state.
// Its arguments are the interface name and the constructor argument array.
void install_web_interfaces(v8::Isolate* isolate,
                             v8::Local<v8::Context> context,
                             v8::FunctionCallback factory,
                             v8::Local<v8::Value> data);

}  // namespace iv8
