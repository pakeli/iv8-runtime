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

}  // namespace iv8::browser
