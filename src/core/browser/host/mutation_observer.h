#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../dom/dom_node.h"
#include "event_target.h"

namespace iv8::browser {

class DocumentHost;

struct MutationRecordData {
    enum class Type { ChildList, Attributes, CharacterData };
    Type type = Type::ChildList;
    DomNode* target = nullptr;
    std::vector<DomNode*> added;
    std::vector<DomNode*> removed;
    DomNode* previous_sibling = nullptr;
    DomNode* next_sibling = nullptr;
    std::string attribute_name;
    bool has_old_value = false;
    std::string old_value;
};

struct MutationObserverRegistration {
    DomNode* target = nullptr;  // null for the document target
    bool target_is_document = false;
    bool child_list = false;
    bool attributes = false;
    bool subtree = false;
    bool attribute_old_value = false;
    bool character_data = false;
    bool character_data_old_value = false;
    bool has_attribute_filter = false;
    std::vector<std::string> attribute_filter;
};

struct MutationObserverState {
    DocumentHost* owner = nullptr;
    v8::Global<v8::Function> callback;
    v8::Global<v8::Object> self;
    std::vector<MutationObserverRegistration> registrations;
    std::vector<MutationRecordData> pending;
    bool delivery_queued = false;
    bool alive = true;
};

}  // namespace iv8::browser
