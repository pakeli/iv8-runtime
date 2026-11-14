#include "dom_node.h"

#include <algorithm>
#include <cstddef>

namespace iv8::browser {

bool is_element_node(const DomNode* node) {
    return node != nullptr && node->kind == NodeKind::Element;
}

bool is_character_data_node(const DomNode* node) {
    return node != nullptr && (node->kind == NodeKind::Text ||
                               node->kind == NodeKind::Comment);
}

std::string text_content_of(const DomNode* node) {
    if (node == nullptr) {
        return std::string();
    }
    if (node->kind == NodeKind::Text || node->kind == NodeKind::Comment) {
        return node->data;
    }
    std::string text;
    for (const DomNode* child : node->children) {
        if (child->kind == NodeKind::Text) {
            text += child->data;
        } else if (child->kind == NodeKind::Element ||
                   child->kind == NodeKind::DocumentFragment) {
            text += text_content_of(child);
        }
    }
    return text;
}

std::vector<DomNode*> element_children_of(const DomNode* node) {
    std::vector<DomNode*> out;
    if (node == nullptr) {
        return out;
    }
    for (DomNode* child : node->children) {
        if (child->kind == NodeKind::Element) {
            out.push_back(child);
        }
    }
    return out;
}

DomNode* sibling_node(const DomNode* node, int direction) {
    if (node == nullptr || node->parent == nullptr) {
        return nullptr;
    }
    const std::vector<DomNode*>& siblings = node->parent->children;
    for (std::size_t i = 0; i < siblings.size(); ++i) {
        if (siblings[i] != node) {
            continue;
        }
        if (direction < 0 && i > 0) {
            return siblings[i - 1];
        }
        if (direction > 0 && i + 1 < siblings.size()) {
            return siblings[i + 1];
        }
        return nullptr;
    }
    return nullptr;
}

DomNode* element_sibling_node(const DomNode* node, int direction) {
    if (node == nullptr || node->parent == nullptr) {
        return nullptr;
    }
    const std::vector<DomNode*>& siblings = node->parent->children;
    for (std::size_t i = 0; i < siblings.size(); ++i) {
        if (siblings[i] != node) {
            continue;
        }
        if (direction < 0) {
            for (std::size_t j = i; j > 0; --j) {
                if (siblings[j - 1]->kind == NodeKind::Element) {
                    return siblings[j - 1];
                }
            }
            return nullptr;
        }
        for (std::size_t j = i + 1; j < siblings.size(); ++j) {
            if (siblings[j]->kind == NodeKind::Element) {
                return siblings[j];
            }
        }
        return nullptr;
    }
    return nullptr;
}

// M4-A-3: unlink `child` from its current parent's children (if any) and clear
// its parent pointer. `child`'s own subtree is untouched.
void detach_from_parent(DomNode* child) {
    DomNode* parent = child->parent;
    if (parent == nullptr) {
        return;
    }
    std::vector<DomNode*>& siblings = parent->children;
    siblings.erase(std::remove(siblings.begin(), siblings.end(), child),
                   siblings.end());
    child->parent = nullptr;
}

// M11-5: capture the immediate element siblings of `child` in its current
// parent's child vector (either or both null). Used before a detach and after an
// insertion to fill a MutationRecord's previousSibling / nextSibling.
void siblings_of(const DomNode* child, DomNode** previous, DomNode** next) {
    *previous = nullptr;
    *next = nullptr;
    const DomNode* parent = child->parent;
    if (parent == nullptr) {
        return;
    }
    const std::vector<DomNode*>& siblings = parent->children;
    const auto it = std::find(siblings.begin(), siblings.end(), child);
    if (it == siblings.end()) {
        return;
    }
    if (it != siblings.begin()) {
        *previous = *(it - 1);
    }
    const auto next_it = it + 1;
    if (next_it != siblings.end()) {
        *next = *next_it;
    }
}

// M4-A-3: true if `candidate` is `of` itself or an ancestor of `of`. Used to
// reject inserting a node into its own subtree (would create a cycle).
bool is_self_or_ancestor(const DomNode* candidate, const DomNode* of) {
    for (const DomNode* node = of; node != nullptr; node = node->parent) {
        if (node == candidate) {
            return true;
        }
    }
    return false;
}

}  // namespace iv8::browser
