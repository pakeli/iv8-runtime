#include "file_system.h"

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "iv8/access_origin.h"
#include "iv8/access_summary.h"
#include "iv8/host_object.h"
#include "iv8/page_state.h"
#include "iv8/web_interface.h"
#include "iv8/web_method.h"
#include "../bom/legacy_quota.h"
#include "../internal/common.h"
#include "../internal/trace_hooks.h"
#include "../internal/web_idl_primitive.h"
#include "../window/window_trace_support.h"
#include "blob_file_reader.h"
#include "dom_exception.h"

namespace iv8::browser {
namespace {

using Node = std::shared_ptr<FileSystemNode>;
using Path = std::vector<std::u16string>;

constexpr char kNotFound[] = "A requested file or directory could not be found at the time an operation was processed.";
constexpr char kTypeMismatch[] = "The path supplied exists, but was not an entry of requested type.";
constexpr char kInvalidModification[] = "The object can not be modified in this way.";

v8::Local<v8::String> string16(v8::Isolate* isolate, const std::u16string& value) {
    v8::Local<v8::String> result;
    if (!v8::String::NewFromTwoByte(isolate,
            reinterpret_cast<const std::uint16_t*>(value.data()),
            v8::NewStringType::kNormal, static_cast<int>(value.size())).ToLocal(&result)) {
        return v8::String::Empty(isolate);
    }
    return result;
}

bool type_error(v8::Isolate* isolate, const std::string& message) {
    isolate->ThrowException(v8::Exception::TypeError(v8_string(isolate, message).As<v8::String>()));
    return false;
}

v8::Local<v8::Value> resolved(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> value) {
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver)) return v8::Undefined(isolate);
    if (!access_user_operation(isolate, [&] { return resolver->Resolve(context, value); }).FromMaybe(false))
        return v8::Undefined(isolate);
    return resolver->GetPromise();
}

v8::Local<v8::Value> rejected(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::TryCatch& caught) {
    if (!caught.CanContinue()) { caught.ReThrow(); return v8::Undefined(isolate); }
    auto exception = caught.Exception();
    caught.Reset();
    v8::Local<v8::Promise::Resolver> resolver;
    if (!v8::Promise::Resolver::New(context).ToLocal(&resolver) ||
        !resolver->Reject(context, exception).FromMaybe(false)) return v8::Undefined(isolate);
    return resolver->GetPromise();
}

void dom_error(v8::Isolate* isolate, v8::Local<v8::Context> context,
    PageState* owner, const char* name, const std::string& message) {
    isolate->ThrowException(make_dom_exception(isolate, context, owner, name, message));
}

bool boolean_option(v8::Isolate* isolate, v8::Local<v8::Context> context,
    v8::Local<v8::Value> options, const char* dictionary, const char* member,
    const std::string& prefix, bool* output) {
    *output = false;
    if (options->IsNullOrUndefined()) return true;
    if (!options->IsObject()) return type_error(isolate,
        prefix + "The provided value is not of type '" + dictionary + "'.");
    v8::Local<v8::Value> value;
    if (!access_user_operation(isolate, [&] {
            return options.As<v8::Object>()->Get(context, v8_string(isolate, member));
        }).ToLocal(&value)) return false;
    *output = value->BooleanValue(isolate);
    return true;
}

bool valid_name(v8::Isolate* isolate, const std::u16string& name,
    const std::string& prefix) {
    if (name.empty() || name == u"." || name == u".." ||
        name.find(u'/') != std::u16string::npos || name.find(u'\\') != std::u16string::npos)
        return type_error(isolate, prefix + "Name is not allowed.");
    return true;
}

void normalize_usv(std::u16string* name) {
    for (std::size_t index = 0; index < name->size(); ++index) {
        auto& unit = (*name)[index];
        if (unit >= 0xd800 && unit <= 0xdbff) {
            if (index + 1 < name->size() && (*name)[index + 1] >= 0xdc00 &&
                (*name)[index + 1] <= 0xdfff) ++index;
            else unit = 0xfffd;
        } else if (unit >= 0xdc00 && unit <= 0xdfff) unit = 0xfffd;
    }
}

Node lookup(const Node& root, const Path& path) {
    Node current = root;
    for (const auto& component : path) {
        if (!current || !current->directory) return {};
        const auto found = current->children.find(component);
        if (found == current->children.end()) return {};
        current = found->second;
    }
    return current;
}

std::string offline_mime(const std::u16string& name) {
    const auto point = name.rfind(u'.');
    if (point == std::u16string::npos) return {};
    auto extension = name.substr(point + 1);
    for (char16_t& character : extension) {
        if (character >= u'A' && character <= u'Z') character += u'a' - u'A';
    }
    if (extension == u"txt") return "text/plain";
    return {};
}

class FileSystemHandleHost;
v8::Local<v8::Object> make_handle(v8::Isolate*, v8::Local<v8::Context>,
    PageState*, const Node&, const Path&, bool, const std::u16string* = nullptr);
v8::Local<v8::Object> make_iterator(v8::Isolate*, v8::Local<v8::Context>,
    PageState*, const Node&, const Path&, int);

class FileSystemHandleHost final : public HostObject {
public:
    FileSystemHandleHost(PageState* owner, Node root, Path path, bool directory,
        bool base_declaration = false, const std::u16string* display_name = nullptr)
        : owner_(owner), root_(std::move(root)), path_(std::move(path)),
          directory_(directory), base_declaration_(base_declaration),
          display_name_(display_name ? *display_name : path_.empty() ? std::u16string() : path_.back()) {}

    std::string global_name() const override { return {}; }
    std::string prototype_key() const override {
        return base_declaration_ ? "FileSystemHandle" :
            (directory_ ? "FileSystemDirectoryHandle" : "FileSystemFileHandle");
    }
    std::string trace_label() const override { return prototype_key(); }
    std::vector<std::string> property_names() const override { return {"kind", "name"}; }
    std::vector<std::string> method_names() const override {
        std::vector<std::string> names = {"isSameEntry", "queryPermission", "remove", "requestPermission"};
        if (!base_declaration_) {
            const std::vector<std::string> extra = directory_
                ? std::vector<std::string>{"getDirectoryHandle", "getFileHandle", "removeEntry", "resolve", "entries", "keys", "values"}
                : std::vector<std::string>{"createWritable", "getFile", "move"};
            names.insert(names.end(), extra.begin(), extra.end());
        }
        return names;
    }
    void release_v8_handles() noexcept override { root_.reset(); path_.clear(); display_name_.clear(); }

    const Node& root() const { return root_; }
    const Path& path() const { return path_; }
    bool directory() const { return directory_; }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate,
        v8::Local<v8::Context>, const std::string& name) override {
        if (name == "kind") return v8_string(isolate, directory_ ? "directory" : "file");
        if (name == "name") return string16(isolate, display_name_);
        return v8::Undefined(isolate);
    }
    v8::Local<v8::Value> call_method(v8::Isolate* isolate,
        v8::Local<v8::Context> context, const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& args) override;

private:
    PageState* owner_;
    Node root_;
    Path path_;
    bool directory_;
    bool base_declaration_;
    std::u16string display_name_;
};

FileSystemHandleHost* handle_from(v8::Local<v8::Value> value) {
    auto* host = host_object_backing(value);
    if (host == nullptr || !web_interface_inherits(host->prototype_key(), "FileSystemHandle")) return nullptr;
    return static_cast<FileSystemHandleHost*>(host);
}

v8::Local<v8::Object> make_handle(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    const Node& root, const Path& path, bool directory, const std::u16string* display_name) {
    return owner->adopt_ephemeral_host_object(isolate, context,
        std::make_unique<FileSystemHandleHost>(owner, root, path, directory, false, display_name));
}

v8::Local<v8::Value> FileSystemHandleHost::call_method(v8::Isolate* isolate,
    v8::Local<v8::Context> context, const std::string& name,
    const v8::FunctionCallbackInfo<v8::Value>& args) {
    watch_record(isolate, prototype_key(), name);
    if (name == "entries" || name == "keys" || name == "values") {
        return make_iterator(isolate, context, owner_, root_, path_,
            name == "keys" ? 0 : name == "values" ? 1 : 2);
    }
    const bool base = name == "isSameEntry" || name == "queryPermission" ||
        name == "requestPermission" || name == "remove";
    const std::string prefix = "Failed to execute '" + name + "' on '" +
        (base ? "FileSystemHandle" : prototype_key()) + "': ";
    v8::TryCatch caught(isolate);
    const int required = (name == "isSameEntry" || name == "getFileHandle" ||
        name == "getDirectoryHandle" || name == "removeEntry" || name == "resolve" || name == "move") ? 1 : 0;
    if (args.Length() < required) {
        type_error(isolate, prefix + "1 argument required, but only 0 present.");
        return rejected(isolate, context, caught);
    }
    if (name == "isSameEntry" || name == "resolve") {
        auto* other = handle_from(args[0]);
        if (other == nullptr) {
            type_error(isolate, prefix + "parameter 1 is not of type 'FileSystemHandle'.");
            return rejected(isolate, context, caught);
        }
        const bool same_root = root_ == other->root();
        if (name == "isSameEntry") return resolved(isolate, context,
            v8::Boolean::New(isolate, same_root && directory_ == other->directory() && path_ == other->path()));
        if (!same_root || path_.size() > other->path().size() ||
            !std::equal(path_.begin(), path_.end(), other->path().begin()))
            return resolved(isolate, context, v8::Null(isolate));
        auto out = v8::Array::New(isolate, static_cast<int>(other->path().size() - path_.size()));
        for (std::size_t index = path_.size(); index < other->path().size(); ++index) {
            if (!out->CreateDataProperty(context, static_cast<std::uint32_t>(index - path_.size()),
                    string16(isolate, other->path()[index])).FromMaybe(false)) return rejected(isolate, context, caught);
        }
        return resolved(isolate, context, out);
    }
    if (name == "queryPermission" || name == "requestPermission") {
        const auto options = args.Length() ? args[0] : v8::Local<v8::Value>(v8::Undefined(isolate));
        if (!options->IsNullOrUndefined()) {
            if (!options->IsObject()) {
                type_error(isolate, prefix + "The provided value is not of type 'FileSystemHandlePermissionDescriptor'.");
                return rejected(isolate, context, caught);
            }
            v8::Local<v8::Value> mode_value;
            if (!access_user_operation(isolate, [&] {
                    return options.As<v8::Object>()->Get(context, v8_string(isolate, "mode"));
                }).ToLocal(&mode_value)) return rejected(isolate, context, caught);
            if (!mode_value->IsUndefined()) {
                std::string mode;
                if (!web_idl_dom_string(isolate, context, mode_value, prefix, &mode)) return rejected(isolate, context, caught);
                if (mode != "read" && mode != "readwrite") {
                    type_error(isolate, prefix + "Failed to read the 'mode' property from 'FileSystemHandlePermissionDescriptor': "
                        "The provided value '" + mode + "' is not a valid enum value of type FileSystemPermissionMode.");
                    return rejected(isolate, context, caught);
                }
            }
        }
        return resolved(isolate, context, v8_string(isolate, "granted"));
    }
    if (name == "createWritable" || name == "move") {
        dom_error(isolate, context, owner_, "NotSupportedError", prefix +
            "This operation is not supported by the offline directory profile.");
        return rejected(isolate, context, caught);
    }

    std::u16string child_name;
    std::u16string child_display_name;
    bool option = false;
    if (name == "getDirectoryHandle" || name == "getFileHandle" || name == "removeEntry") {
        if (!web_idl_dom_string_u16(isolate, context, args[0], prefix, &child_display_name))
            return rejected(isolate, context, caught);
        normalize_usv(&child_display_name);
        const auto options = args.Length() > 1 ? args[1] : v8::Local<v8::Value>(v8::Undefined(isolate));
        const char* dictionary = name == "getDirectoryHandle" ? "FileSystemGetDirectoryOptions" :
            name == "getFileHandle" ? "FileSystemGetFileOptions" : "FileSystemRemoveOptions";
        if (!boolean_option(isolate, context, options, dictionary,
                name == "removeEntry" ? "recursive" : "create", prefix, &option) ||
            !valid_name(isolate, child_display_name, prefix)) return rejected(isolate, context, caught);
        child_name = child_display_name.substr(0, child_display_name.find(u'\0'));
        if (!valid_name(isolate, child_name, prefix)) return rejected(isolate, context, caught);
    } else if (name == "remove") {
        const auto options = args.Length() ? args[0] : v8::Local<v8::Value>(v8::Undefined(isolate));
        if (!boolean_option(isolate, context, options, "FileSystemRemoveOptions", "recursive", prefix, &option))
            return rejected(isolate, context, caught);
    }
    auto node = lookup(root_, path_);
    if (!node) {
        dom_error(isolate, context, owner_, "NotFoundError", kNotFound);
        return rejected(isolate, context, caught);
    }
    if (node->directory != directory_ && name != "getFile") {
        dom_error(isolate, context, owner_, "TypeMismatchError", kTypeMismatch);
        return rejected(isolate, context, caught);
    }
    if (name == "getFile") {
        v8::Local<v8::Object> file;
        if (!create_runtime_file(isolate, context, {}, display_name_,
                offline_mime(display_name_), 0).ToLocal(&file))
            return rejected(isolate, context, caught);
        return resolved(isolate, context, file);
    }
    if (name == "getDirectoryHandle" || name == "getFileHandle") {
        const bool directory = name == "getDirectoryHandle";
        auto found = node->children.find(child_name);
        if (found == node->children.end()) {
            if (!option) {
                dom_error(isolate, context, owner_, "NotFoundError", kNotFound);
                return rejected(isolate, context, caught);
            }
            auto child = std::make_shared<FileSystemNode>();
            child->name = child_name;
            child->directory = directory;
            child->parent = node;
            found = node->children.emplace(child_name, std::move(child)).first;
        } else if (found->second->directory != directory) {
            dom_error(isolate, context, owner_, "TypeMismatchError", kTypeMismatch);
            return rejected(isolate, context, caught);
        }
        auto path = path_;
        path.push_back(child_name);
        return resolved(isolate, context, make_handle(isolate, context, owner_, root_, path, directory, &child_display_name));
    }
    if (name == "remove" || name == "removeEntry") {
        Node parent;
        std::u16string remove_name;
        if (name == "removeEntry") {
            parent = node;
            remove_name = child_name;
        } else {
            if (path_.empty()) {
                if (!node->children.empty() && !option) {
                    dom_error(isolate, context, owner_, "InvalidModificationError", kInvalidModification);
                    return rejected(isolate, context, caught);
                }
                node->children.clear();
                return resolved(isolate, context, v8::Undefined(isolate));
            }
            Path parent_path(path_.begin(), path_.end() - 1);
            parent = lookup(root_, parent_path);
            remove_name = path_.back();
        }
        const auto found = parent->children.find(remove_name);
        if (found == parent->children.end()) {
            dom_error(isolate, context, owner_, "NotFoundError", kNotFound);
            return rejected(isolate, context, caught);
        }
        if (found->second->directory && !found->second->children.empty() && !option) {
            dom_error(isolate, context, owner_, "InvalidModificationError", kInvalidModification);
            return rejected(isolate, context, caught);
        }
        parent->children.erase(found);
        return resolved(isolate, context, v8::Undefined(isolate));
    }
    return resolved(isolate, context, v8::Undefined(isolate));
}

class DirectoryIteratorHost final : public HostObject {
public:
    DirectoryIteratorHost(PageState* owner, Node root, Path path, int kind)
        : owner_(owner), root_(std::move(root)), path_(std::move(path)), kind_(kind) {
        const auto directory = lookup(root_, path_);
        missing_ = !directory || !directory->directory;
        if (!missing_) {
            for (const auto& [name, child] : directory->children)
                entries_.emplace_back(name, child->directory);
        }
    }
    std::string global_name() const override { return {}; }
    std::string trace_label() const override { return "FileSystemDirectoryHandle AsyncIterator"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override { return {}; }
    void release_v8_handles() noexcept override { root_.reset(); path_.clear(); entries_.clear(); }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&) override { return v8::Undefined(isolate); }
    v8::Local<v8::Value> call_method(v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&,
        const v8::FunctionCallbackInfo<v8::Value>&) override { return v8::Undefined(isolate); }

    v8::Local<v8::Value> next(v8::Isolate* isolate, v8::Local<v8::Context> context) {
        v8::TryCatch caught(isolate);
        if (missing_) {
            missing_ = false;
            dom_error(isolate, context, owner_, "NotFoundError", kNotFound);
            return rejected(isolate, context, caught);
        }
        auto step = v8::Object::New(isolate);
        const bool done = index_ >= entries_.size();
        v8::Local<v8::Value> value = v8::Undefined(isolate);
        if (!done) {
            const auto& [name, directory] = entries_[index_++];
            if (kind_ == 0) value = string16(isolate, name);
            else {
                auto path = path_;
                path.push_back(name);
                auto handle = make_handle(isolate, context, owner_, root_, path, directory);
                if (kind_ == 1) value = handle;
                else {
                    auto pair = v8::Array::New(isolate, 2);
                    if (!pair->CreateDataProperty(context, 0, string16(isolate, name)).FromMaybe(false) ||
                        !pair->CreateDataProperty(context, 1, handle).FromMaybe(false)) return rejected(isolate, context, caught);
                    value = pair;
                }
            }
        }
        if (!step->CreateDataProperty(context, v8::String::NewFromUtf8Literal(isolate, "value"), value).FromMaybe(false) ||
            !step->CreateDataProperty(context, v8::String::NewFromUtf8Literal(isolate, "done"), v8::Boolean::New(isolate, done)).FromMaybe(false))
            return rejected(isolate, context, caught);
        return resolved(isolate, context, step);
    }

private:
    PageState* owner_;
    Node root_;
    Path path_;
    int kind_;
    bool missing_ = false;
    std::size_t index_ = 0;
    std::vector<std::pair<std::u16string, bool>> entries_;
};

void directory_next_callback(const v8::FunctionCallbackInfo<v8::Value>& info) {
    auto* isolate = info.GetIsolate();
    auto context = isolate->GetCurrentContext();
    auto* host = host_object_backing(info.This());
    const bool valid = host && host->prototype_key().empty() &&
        host->trace_label() == "FileSystemDirectoryHandle AsyncIterator";
    const auto sequence = access_trace_record(isolate,
        valid ? static_cast<const void*>(host) : native_api_identity(isolate, context->Global()),
        "host_object", "FileSystemDirectoryHandle AsyncIterator", "next", AccessOperation::kCall);
    AccessCallCompletion completion(isolate, sequence, info);
    watch_record(isolate, "FileSystemDirectoryHandle AsyncIterator", "next");
    if (!valid) {
        v8::TryCatch caught(isolate);
        type_error(isolate, "Failed to execute 'next' on 'AsyncIterator_FileSystemDirectoryHandle': Illegal invocation");
        info.GetReturnValue().Set(rejected(isolate, context, caught));
        return;
    }
    info.GetReturnValue().Set(static_cast<DirectoryIteratorHost*>(host)->next(isolate, context));
}

v8::Local<v8::Private> iterator_slot(v8::Isolate* isolate) {
    return v8::Private::ForApi(isolate, v8::String::NewFromUtf8Literal(isolate, "iv8.opfs.iterator.prototype"));
}

bool install_iterator_prototype(v8::Isolate* isolate, v8::Local<v8::Context> context) {
    auto runtime_origin = access_runtime_scope(isolate);
    auto prototype = v8::Object::New(isolate);
    // Capture the engine's pristine %AsyncIteratorPrototype% during bootstrap.
    v8::Local<v8::Script> script;
    v8::Local<v8::Value> generator;
    {
        pybind11::gil_scoped_release release_gil;
        if (!v8::Script::Compile(context, v8::String::NewFromUtf8Literal(isolate, "(async function*(){})()" )).ToLocal(&script) ||
            !script->Run(context).ToLocal(&generator) || !generator->IsObject()) return false;
    }
    auto parent = generator.As<v8::Object>()->GetPrototype();
    for (int level = 0; level < 2; ++level) {
        if (!parent->IsObject()) return false;
        parent = parent.As<v8::Object>()->GetPrototype();
    }
    if (!prototype->SetPrototype(context, parent).FromMaybe(false)) return false;
    v8::Local<v8::Function> next;
    if (!make_web_method(isolate, context, "next", 0, &directory_next_callback).ToLocal(&next) ||
        !install_traced_api_data_property(isolate, context, prototype, "next", next,
            native_api_identity(isolate, prototype), "host_object", "FileSystemDirectoryHandle AsyncIterator.prototype") ||
        !install_traced_api_named_data_property(isolate, context, prototype,
            v8::Symbol::GetToStringTag(isolate), v8_string(isolate, "FileSystemDirectoryHandle AsyncIterator"),
            native_api_identity(isolate, prototype), "host_object", "FileSystemDirectoryHandle AsyncIterator.prototype",
            static_cast<v8::PropertyAttribute>(v8::ReadOnly | v8::DontEnum))) return false;
    return context->Global()->SetPrivate(context, iterator_slot(isolate), prototype).FromMaybe(false);
}

v8::Local<v8::Object> make_iterator(v8::Isolate* isolate, v8::Local<v8::Context> context,
    PageState* owner, const Node& root, const Path& path, int kind) {
    auto object = owner->adopt_ephemeral_host_object(isolate, context,
        std::make_unique<DirectoryIteratorHost>(owner, root, path, kind));
    v8::Local<v8::Value> prototype;
    if (context->Global()->GetPrivate(context, iterator_slot(isolate)).ToLocal(&prototype) && prototype->IsObject())
        (void)object->SetPrototype(context, prototype);
    return object;
}

class StorageManagerHost final : public HostObject {
public:
    StorageManagerHost(PageState* owner, std::string origin, FileSystemStore* store)
        : owner_(owner), origin_(std::move(origin)), store_(store) {}
    std::string global_name() const override { return {}; }
    std::string prototype_key() const override { return "StorageManager"; }
    std::string trace_object_type() const override { return "storage_manager"; }
    std::string trace_label() const override { return "navigator.storage"; }
    std::vector<std::string> property_names() const override { return {}; }
    std::vector<std::string> method_names() const override { return {"estimate", "persisted", "getDirectory", "persist"}; }
    v8::Local<v8::Value> get_property(v8::Isolate* isolate, v8::Local<v8::Context>, const std::string&) override { return v8::Undefined(isolate); }
    v8::Local<v8::Value> call_method(v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name, const v8::FunctionCallbackInfo<v8::Value>&) override {
        watch_record(isolate, "navigator.storage", name);
        if (name == "persist" || name == "persisted") return resolved(isolate, context, v8::False(isolate));
        if (name == "estimate") {
            auto estimate = v8::Object::New(isolate);
            (void)estimate->CreateDataProperty(context, v8::String::NewFromUtf8Literal(isolate, "usage"), v8::Integer::New(isolate, 0));
            (void)estimate->CreateDataProperty(context, v8::String::NewFromUtf8Literal(isolate, "quota"), v8::Number::New(isolate, kOfflineStorageQuota));
            return resolved(isolate, context, estimate);
        }
        if (name == "getDirectory") {
            if (origin_.empty() || store_ == nullptr) {
                v8::TryCatch caught(isolate);
                dom_error(isolate, context, owner_, "SecurityError", "Storage directory access is not supported for this origin.");
                return rejected(isolate, context, caught);
            }
            auto& root = store_->roots[origin_];
            if (!root) root = std::make_shared<FileSystemNode>();
            return resolved(isolate, context, make_handle(isolate, context, owner_, root, {}, true));
        }
        return v8::Undefined(isolate);
    }
private:
    PageState* owner_;
    std::string origin_;
    FileSystemStore* store_;
};

bool reorder_members(v8::Isolate* isolate, v8::Local<v8::Context> context,
    const char* interface_name, const std::vector<const char*>& names) {
    auto prototype = web_interface_prototype(isolate, context, interface_name);
    if (prototype.IsEmpty()) return false;
    std::vector<std::pair<const char*, v8::Local<v8::Value>>> saved;
    for (const char* name : names) {
        v8::Local<v8::Value> value;
        if (!prototype->Get(context, v8_string(isolate, name)).ToLocal(&value) ||
            !prototype->Delete(context, v8_string(isolate, name)).FromMaybe(false)) return false;
        saved.emplace_back(name, value);
    }
    for (const auto& [name, value] : saved) {
        if (!install_traced_api_data_property(isolate, context, prototype, name, value,
                native_api_identity(isolate, prototype), "host_object", std::string(interface_name) + ".prototype",
                std::string(name) == "constructor" ? v8::DontEnum : v8::None)) return false;
    }
    return true;
}

}  // namespace

bool install_file_system_interfaces(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner, bool secure) {
    if (!secure) return true;
    StorageManagerHost storage(owner, {}, nullptr);
    FileSystemHandleHost base(owner, {}, {}, true, true);
    FileSystemHandleHost directory(owner, {}, {}, true);
    FileSystemHandleHost file(owner, {}, {}, false);
    for (HostObject* declaration : {static_cast<HostObject*>(&storage), static_cast<HostObject*>(&base),
                                    static_cast<HostObject*>(&directory), static_cast<HostObject*>(&file)})
        install_host_interface(isolate, context, declaration);
    if (!reorder_members(isolate, context, "StorageManager", {"estimate", "persisted", "constructor", "getDirectory", "persist"}) ||
        !reorder_members(isolate, context, "FileSystemHandle", {"isSameEntry", "queryPermission", "remove", "requestPermission", "constructor"}) ||
        !reorder_members(isolate, context, "FileSystemDirectoryHandle", {"getDirectoryHandle", "getFileHandle", "removeEntry", "resolve", "entries", "keys", "values", "constructor"}) ||
        !reorder_members(isolate, context, "FileSystemFileHandle", {"createWritable", "getFile", "move", "constructor"})) return false;
    auto prototype = web_interface_prototype(isolate, context, "FileSystemDirectoryHandle");
    v8::Local<v8::Value> entries;
    if (!prototype->Get(context, v8::String::NewFromUtf8Literal(isolate, "entries")).ToLocal(&entries) ||
        !install_traced_api_named_data_property(isolate, context, prototype, v8::Symbol::GetAsyncIterator(isolate), entries,
            native_api_identity(isolate, prototype), "host_object", "FileSystemDirectoryHandle.prototype", v8::DontEnum)) return false;
    return install_iterator_prototype(isolate, context);
}

v8::Local<v8::Object> make_native_storage_manager(v8::Isolate* isolate,
    v8::Local<v8::Context> context, PageState* owner,
    const std::string& canonical_origin, FileSystemStore* store) {
    return owner->adopt_host_object(isolate, context,
        std::make_unique<StorageManagerHost>(owner, canonical_origin, store));
}

}  // namespace iv8::browser
