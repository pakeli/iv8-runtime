#include "dom_query.h"

#include <algorithm>

namespace iv8::browser {

DomNode* dfs_find(DomNode* node, const std::function<bool(const DomNode*)>& pred) {
    if (pred(node)) return node;
    for (DomNode* child : node->children) {
        if (DomNode* found = dfs_find(child, pred)) return found;
    }
    return nullptr;
}

// First node (document order / pre-order) matching `pred`, or nullptr. Returns a
// mutable node so M2-8 mutations (setAttribute / textContent) can update it.
DomNode* find_first(const std::vector<DomNode*>& roots,
                    const std::function<bool(const DomNode*)>& pred) {
    for (DomNode* root : roots) {
        if (DomNode* found = dfs_find(root, pred)) return found;
    }
    return nullptr;
}

// M3-9: append every <script> node reachable in the CURRENT tree, in document
// (pre-)order, to `out`. Walks the live parent/child tree (not the M3-5 parse-time
// script records), so a mutation that detached a script subtree (e.g. an M2-8
// textContent write) is reflected — such scripts are simply no longer reachable.
void collect_scripts(DomNode* node, std::vector<DomNode*>& out) {
    if (node->tag == "script") {
        out.push_back(node);
    }
    for (DomNode* child : node->children) {
        collect_scripts(child, out);
    }
}

// M4-A-1: append every node in the CURRENT tree matching `pred`, in document
// (pre-)order, to `out`. Live tree walk (like collect_scripts), used by
// document.querySelectorAll / getElementsByTagName so mutation-detached subtrees
// drop out on re-query.
void collect_matching(DomNode* node,
                      const std::function<bool(const DomNode*)>& pred,
                      std::vector<DomNode*>& out) {
    if (pred(node)) {
        out.push_back(node);
    }
    for (DomNode* child : node->children) {
        collect_matching(child, pred, out);
    }
}

// M11-19: an element matches a multi-token class query when its class token set
// contains every requested token. The caller guarantees tokens is non-empty.
bool has_all_class_tokens(const DomNode* node,
                          const std::vector<std::string>& tokens) {
    for (const std::string& token : tokens) {
        if (std::find(node->classes.begin(), node->classes.end(), token) ==
            node->classes.end()) {
            return false;
        }
    }
    return true;
}

}  // namespace iv8::browser
