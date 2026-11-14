#pragma once

#include <functional>
#include <string>
#include <vector>

#include "dom_node.h"

namespace iv8::browser {

extern DomNode* dfs_find(DomNode* node, const std::function<bool(const DomNode*)>& pred);

extern DomNode* find_first(const std::vector<DomNode*>& roots, const std::function<bool(const DomNode*)>& pred);

extern void collect_scripts(DomNode* node, std::vector<DomNode*>& out);

extern void collect_matching(DomNode* node, const std::function<bool(const DomNode*)>& pred, std::vector<DomNode*>& out);

extern bool has_all_class_tokens(const DomNode* node, const std::vector<std::string>& tokens);

}  // namespace iv8::browser
