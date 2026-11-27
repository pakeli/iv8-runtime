#pragma once

#include <map>
#include <memory>
#include <string>
#include <vector>

#include "iv8/v8_headers.h"

namespace iv8 { class PageState; }

namespace iv8::browser {

// Pure native data outlives individual Page generations. No host paths or V8
// handles are stored here. Handles identify a root/path, so a replacement at
// the same path can be observed by an older wrapper (Chrome OPFS behavior).
struct FileSystemNode {
    std::u16string name;
    bool directory = true;
    std::weak_ptr<FileSystemNode> parent;
    std::map<std::u16string, std::shared_ptr<FileSystemNode>> children;
};

struct FileSystemStore {
    std::map<std::string, std::shared_ptr<FileSystemNode>> roots;
    void clear() noexcept { roots.clear(); }
};

bool install_file_system_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner, bool secure);

v8::Local<v8::Object> make_native_storage_manager(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    const std::string& canonical_origin, FileSystemStore* store);

}  // namespace iv8::browser
