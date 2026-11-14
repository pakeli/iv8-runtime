#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../dom/dom_node.h"
#include "event_target.h"

namespace iv8::browser {

class DocumentHost;

struct ClassListBinding {
    DocumentHost* document = nullptr;
    DomNode* node = nullptr;
};

extern v8::Local<v8::Value> make_class_list(v8::Isolate* isolate, v8::Local<v8::Context> context, DocumentHost* doc, DomNode* node);

}  // namespace iv8::browser
