#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "../dom/dom_node.h"
#include "event_target.h"

namespace iv8::browser {

class DocumentHost;

struct LiveCollectionBinding {
    DocumentHost* document;
    DomNode* root;
    std::string kind;
    std::string query;
};

}  // namespace iv8::browser
