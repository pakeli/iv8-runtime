#include "document_host.h"

#include <algorithm>
#include <iterator>
#include <unordered_set>
#include <utility>

#include "iv8/dom_url.h"
#include "iv8/url_parser.h"
#include "../dom/dom_query.h"
#include "../dom/form_controls.h"
#include "../dom/html_serializer.h"
#include "../dom/selector_match.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {

DocumentHost::DocumentHost(const std::string& html, std::string url, PageState* page, bool active, bool html_document) : NodeHost(& document_node_, this), page_(page), url_(std::move(url)), active_(active), html_document_(html_document), roots_(document_node_ . children) {
        document_node_.kind = NodeKind::Document;
        parse_html(html, pool_, roots_, scripts_);
        // Full-document repair only: fragment parsing must keep its own roots.
        DomNode* html_node = find_tag("html");
        if (html_node != nullptr) {
            DomNode* body_node = find_tag("body");
            const auto html_position = std::find(roots_.begin(), roots_.end(), html_node);
            const bool has_trailing = html_position != roots_.end() &&
                std::next(html_position) != roots_.end();
            if (body_node == nullptr && has_trailing) {
                auto body = std::make_unique<DomNode>();
                body->tag = "body";
                body->parent = html_node;
                body_node = body.get();
                html_node->children.push_back(body_node);
                pool_.push_back(std::move(body));
            }
            if (body_node != nullptr && body_node->parent != html_node) {
                if (body_node->parent != nullptr) {
                    auto& siblings = body_node->parent->children;
                    siblings.erase(std::remove(siblings.begin(), siblings.end(), body_node), siblings.end());
                }
                roots_.erase(std::remove(roots_.begin(), roots_.end(), body_node), roots_.end());
                body_node->parent = html_node;
                html_node->children.push_back(body_node);
            }
            const auto append_to_body = [body_node](DomNode* node) {
                if (node->kind == NodeKind::Text && !body_node->children.empty() &&
                    body_node->children.back()->kind == NodeKind::Text) {
                    body_node->children.back()->data += node->data;
                    node->parent = nullptr;
                } else {
                    node->parent = body_node;
                    body_node->children.push_back(node);
                }
            };
            if (body_node != nullptr && body_node->parent == html_node) {
                bool after_body = false;
                for (auto child = html_node->children.begin(); child != html_node->children.end();) {
                    if (*child == body_node) after_body = true;
                    if (after_body && *child != body_node && (*child)->kind != NodeKind::Comment) {
                        append_to_body(*child);
                        child = html_node->children.erase(child);
                    } else ++child;
                }
            }
            bool after_html = false;
            for (auto root = roots_.begin(); root != roots_.end();) {
                if (*root == html_node) after_html = true;
                if (body_node != nullptr && after_html && *root != html_node &&
                    ((*root)->kind == NodeKind::Element ||
                     (*root)->kind == NodeKind::Text)) {
                    append_to_body(*root);
                    root = roots_.erase(root);
                } else ++root;
            }
        }
        // M11-47/M11-48: title reflects the first parsed <title> element live;
        // title_ remains the fallback for documents without a title element.
        title_node_ = find_tag("title");
        title_ = title_node_ != nullptr ? text_content_of(title_node_)
                                        : std::string();
        // M5-5/M7/M6-1: seed select/form runtime slots and the reset baseline
        // after parsing (shared with HTML-fragment insertion).
        initialize_parsed_nodes(pool_);
        for (auto* root : roots_) root->parent = &document_node_;
    }

v8::Local<v8::Value> DocumentHost::document_global(v8::Isolate* isolate, v8::Local<v8::Context> context) {
        return document_object_.IsEmpty() ? v8::Local<v8::Value>(v8::Null(isolate))
                                         : v8::Local<v8::Value>(document_object_.Get(isolate));
    }

v8::Local<v8::Value> DocumentHost::wrap_created_node(v8::Isolate* isolate, v8::Local<v8::Context> context, std::unique_ptr<DomNode> node) {
        auto* raw = node.get();
        detached_pool_.push_back(std::move(node));
        return wrap_node(isolate, context, raw);
    }

void DocumentHost::set_wrapper(v8::Isolate* isolate, v8::Local<v8::Object> object) {
        document_object_.Reset(isolate, object);
    }

std::string DocumentHost::prototype_key() const { return html_document_ ? "HTMLDocument" : "Document"; }

const std::vector<ScriptRecord>& DocumentHost::scripts() const { return scripts_; }

void DocumentHost::set_ready_state(const std::string& state) { ready_state_ = state; }

void DocumentHost::set_current_script(std::size_t index) {
        current_script_ = index < scripts_.size() ? scripts_[index].node : nullptr;
    }

void DocumentHost::clear_current_script() { current_script_ = nullptr; }

std::string DocumentHost::global_name() const { return "document"; }

std::string DocumentHost::trace_object_type() const { return "document"; }

std::vector<std::string> DocumentHost::property_names() const {
        return {"URL",  "title",           "documentURI", "baseURI",
                "defaultView", "location", "readyState",  "documentElement",
                "head", "body",            "currentScript", "scripts",
                // M4-B-6 document's direct element children.
                "children",
                // M4-B-7 all <form> elements in the current tree.
                "forms",
                // M4-B-8 all <img> elements in the current tree.
                "images",
                // M4-B-9 <a>/<area> elements that carry an href attribute.
                "links",
                // M4-B-10 <a> elements that carry a name attribute.
                "anchors",
                // M4-B-11 all <embed> elements in the current tree.
                "embeds",
                // M4-B-12 all <applet> elements in the current tree.
                "applets",
                // M11-23 deterministic document metadata.
                "characterSet", "contentType", "compatMode", "referrer",
                "domain", "visibilityState", "hidden", "activeElement",
                // M11-46 deterministic Page-level cookie jar.
                "cookie", "all", "nodeType", "nodeName", "nodeValue", "textContent",
                "ownerDocument", "parentNode", "parentElement", "isConnected",
                "childNodes", "firstChild", "lastChild", "previousSibling", "nextSibling",
                "firstElementChild", "lastElementChild", "childElementCount"};
    }

std::vector<std::string> DocumentHost::writable_property_names() const {
        // M11-46 cookie jar; M11-47 writable document.title.
        return {"cookie", "title", "location", "nodeValue", "textContent"};
    }

std::vector<std::string> DocumentHost::method_names() const {
        // M2-6 document methods + M4-A-1 static queries + M4-A-2 createElement +
        // the M3-3 event methods.
        std::vector<std::string> names = {"getElementById", "querySelector",
                                          "querySelectorAll", "getElementsByTagName",
                                          "createElement",
                                          // M11-48 node creation.
                                          "createTextNode", "createComment",
                                          "createDocumentFragment",
                                          // M4-B-13 / M11-19 class query.
                                          "getElementsByClassName",
                                          // M11-20 name query.
                                          "getElementsByName",
                                          // M11-23 deterministic focus report.
                                          "hasFocus", "appendChild", "removeChild",
                                          "insertBefore", "replaceChild", "hasChildNodes", "contains",
                                          "isSameNode", "isEqualNode", "cloneNode"};
        const std::vector<std::string>& events = event_method_names();
        names.insert(names.end(), events.begin(), events.end());
        return names;
    }

void DocumentHost::release_v8_handles() noexcept {
        EventTargetHost::release_v8_handles();
        document_object_.Reset();
        node_objects_.clear();
        all_collection_.Reset();
        for (const std::unique_ptr<NodeHost>& host : node_hosts_) {
            host->release_v8_handles();
        }
        // M11-5: reset observer-backed persistent handles before isolate disposal.
        for (const std::unique_ptr<MutationObserverState>& observer : observers_) {
            observer->alive = false;
            observer->callback.Reset();
            observer->self.Reset();
            observer->registrations.clear();
            observer->pending.clear();
        }
        // M11-8: drop the generation's Blob constructor and any pending
        // FileReader deliveries/listeners before isolate disposal.
        blob_constructor_.Reset();
        for (const std::unique_ptr<FileReaderState>& reader : file_readers_) {
            reader->alive = false;
            reader->pending = false;
            reader->bytes.clear();
            reader->self.Reset();
            reader->events.release_v8_handles();
        }
    }

v8::Local<v8::Value> DocumentHost::get_property(v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name) {
        if (name == "nodeType") return v8::Integer::New(isolate, 9);
        if (name == "nodeName") return v8_string(isolate, "#document");
        if (name == "nodeValue" || name == "textContent" || name == "ownerDocument" ||
            name == "parentNode" || name == "parentElement" ||
            name == "previousSibling" || name == "nextSibling") return v8::Null(isolate);
        if (name == "isConnected") return v8::True(isolate);
        if (name == "childNodes") return nodes_array(isolate, context, roots_);
        if (name == "firstChild" || name == "lastChild") return wrap_node(isolate, context,
            roots_.empty() ? nullptr : name == "firstChild" ? roots_.front() : roots_.back());
        if (name == "firstElementChild" || name == "lastElementChild") {
            const auto children = element_children_of_roots();
            return wrap_node(isolate, context, children.empty() ? nullptr :
                name == "firstElementChild" ? children.front() : children.back());
        }
        if (name == "childElementCount") return v8::Integer::New(isolate,
            static_cast<int>(element_children_of_roots().size()));
        if (name == "all") {
            if (all_collection_.IsEmpty()) all_collection_.Reset(isolate,
                live_collection(isolate, context, nullptr, "tag", "*", true));
            return all_collection_.Get(isolate);
        }
        if (name == "URL") return v8_string(isolate, url_);
        if (name == "title") {
            return v8_string(isolate, title_node_ != nullptr
                                          ? text_content_of(title_node_)
                                          : title_);
        }
        // M11-47 document identity aliases. documentURI/baseURI are the current
        // generation's base URL (the runtime has no <base href> resolution).
        // defaultView is the single global window; location is the same
        // LocationHost wrapper installed as the global `location`.
        if (name == "documentURI" || name == "baseURI") {
            return v8_string(isolate, url_);
        }
        if (name == "defaultView") return active_ ? v8::Local<v8::Value>(context->Global())
                                                   : v8::Local<v8::Value>(v8::Null(isolate));
        if (name == "location") {
            if (!active_) return v8::Null(isolate);
            v8::Local<v8::Value> location;
            if (context->Global()
                    ->Get(context, v8_string(isolate, "location"))
                    .ToLocal(&location)) {
                return location;
            }
            return v8::Undefined(isolate);
        }
        if (name == "readyState") return v8_string(isolate, ready_state_);
        // M11-23: deterministic document metadata (read-only).
        if (name == "characterSet") return v8_string(isolate, "UTF-8");
        if (name == "contentType") return v8_string(isolate, html_document_ ? "text/html" : "application/xml");
        if (name == "compatMode") return v8_string(isolate, "CSS1Compat");
        if (name == "referrer") return v8_string(isolate, std::string());
        if (name == "domain") {
            return v8_string(isolate, decompose_url(url_).hostname);
        }
        if (name == "visibilityState") return v8_string(isolate, "visible");
        if (name == "hidden") return v8::False(isolate);
        if (name == "cookie") {
            return v8_string(isolate, active_ && page_ != nullptr ? page_->document_cookie()
                                                       : std::string());
        }
        if (name == "activeElement") {
            return wrap_node(isolate, context, find_tag("body"));
        }
        if (name == "currentScript") {  // M3-7: element while a HTML script runs, else null
            return wrap_node(isolate, context, current_script_);
        }
        if (name == "scripts") {  // M3-9: current tree's <script> elements, document order
            std::vector<DomNode*> nodes;
            for (DomNode* root : roots_) {
                collect_scripts(root, nodes);
            }
            v8::Local<v8::Array> array =
                v8::Array::New(isolate, static_cast<int>(nodes.size()));
            for (std::size_t k = 0; k < nodes.size(); ++k) {
                (void)array->Set(context, static_cast<std::uint32_t>(k),
                                 wrap_node(isolate, context, nodes[k]));
            }
            return array;
        }
        if (name == "documentElement") {
            const auto children = element_children_of_roots();
            return wrap_node(isolate, context, children.empty() ? nullptr : children.front());
        }
        if (name == "head") {  // M4-A-1: first <head> in the current tree, or null
            return wrap_node(isolate, context, find_tag("head"));
        }
        if (name == "body") {
            return wrap_node(isolate, context, find_tag("body"));
        }
        if (name == "children") {  // M4-B-6: document's direct element children
            // M11-48: roots_ may include top-level Text/Comment nodes; document
            // children stay element-only, in document order. A blank generation
            // has none -> []. Same plain-Array wrapping as other collections.
            return elements_array(isolate, context,
                                  element_children_of_roots());
        }
        if (name == "forms") {  // M4-B-7: all <form> elements in the current tree
            // Same live-tree collection + plain-Array wrapping as
            // getElementsByTagName('form') (shared elements_by_tag): whole tree,
            // document order, detached <form>s excluded. <form> is treated as a
            // plain element — no HTMLFormElement / submit / elements association.
            return elements_array(isolate, context, elements_by_tag("form"));
        }
        if (name == "images") {  // M4-B-8: all <img> elements in the current tree
            // Same live-tree collector as getElementsByTagName('img'): whole tree,
            // document order, detached <img>s excluded. <img> is a plain element —
            // no HTMLImageElement / .src / loading / decoding / events / network.
            return elements_array(isolate, context, elements_by_tag("img"));
        }
        if (name == "links") {  // M4-B-9: <a>/<area> elements carrying an href attr
            // Same live-tree walk (collect over roots_, document order, detached
            // excluded), filtered to <a>/<area> that have an "href" attribute
            // (presence only — value irrelevant; a valueless href counts). Reuses
            // the M3-8/M4-A-4 attribute model (href lives in node.attributes; names
            // are lowercased). <a>/<area> stay plain elements — no navigation, no
            // .href reflection, no target/rel/download semantics.
            std::vector<DomNode*> nodes = collect([](const DomNode* n) {
                return (n->tag == "a" || n->tag == "area") &&
                       n->attributes.find("href") != n->attributes.end();
            });
            return elements_array(isolate, context, nodes);
        }
        if (name == "anchors") {  // M4-B-10: <a> elements carrying a name attribute
            // Same live-tree walk (collect over roots_, document order, detached
            // excluded), filtered to <a> that have a "name" attribute (presence
            // only — value irrelevant; a valueless name counts). <area> and other
            // tags never match. Reuses the M3-8/M4-A-4 attribute model; <a> stays a
            // plain element — no navigation / .href / fragment jump / HTMLAnchorElement.
            std::vector<DomNode*> nodes = collect([](const DomNode* n) {
                return n->tag == "a" &&
                       n->attributes.find("name") != n->attributes.end();
            });
            return elements_array(isolate, context, nodes);
        }
        if (name == "embeds") {  // M4-B-11: all <embed> elements in the current tree
            // Same live-tree collector as getElementsByTagName('embed'): whole tree,
            // document order, detached <embed>s excluded. <embed> is a plain (void)
            // element — no plugin/media loading, network, .src/.type reflection,
            // events, or HTMLEmbedElement; no document.plugins.
            return elements_array(isolate, context, elements_by_tag("embed"));
        }
        if (name == "applets") {  // M4-B-12: all <applet> elements in the current tree
            // Same live-tree collector as getElementsByTagName('applet'): whole
            // tree, document order, detached <applet>s excluded. <applet> is a plain
            // element — no plugin/Java/media/network, no .code/.archive/.object
            // reflection, no events/playback/sizing, no HTMLAppletElement.
            return elements_array(isolate, context, elements_by_tag("applet"));
        }
        return v8::Undefined(isolate);
    }

void DocumentHost::set_property(v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name, v8::Local<v8::Value> value) {
        if (name != "cookie" && name != "title" && name != "location" &&
            name != "nodeValue" && name != "textContent") {
            return;
        }
        if (name == "nodeValue" || name == "textContent") return;
        if (name == "location") {
            if (!active_) return;
            v8::Local<v8::Value> location;
            if (context->Global()->Get(context, v8_string(isolate, "location")).ToLocal(&location) &&
                location->IsObject()) (void)location.As<v8::Object>()->Set(context,
                    v8_string(isolate, "href"), value);
            return;
        }
        if (name == "cookie" && (!active_ || page_ == nullptr)) {
            return;
        }
        v8::Local<v8::String> str;
        if (!value->ToString(context).ToLocal(&str)) {
            return;
        }
        v8::String::Utf8Value utf8(isolate, str);
        const std::string text =
            *utf8 != nullptr
                ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
                : std::string();
        if (name == "cookie") {
            page_->set_document_cookie(text);
            return;
        }
        // M11-47/M11-48 title = ...: update the document string and reflect it
        // into the first parsed <title> element through the shared textContent
        // setter (which creates/removes Text children and queues the record).
        title_ = text;
        if (title_node_ != nullptr) {
            set_text_content(isolate, context, title_node_, text);
        }
    }

v8::Local<v8::Value> DocumentHost::call_method(v8::Isolate* isolate, v8::Local<v8::Context> context, const std::string& name, const v8::FunctionCallbackInfo<v8::Value>& args) {
        // M9-2: record the "document.<method>" call before dispatch.
        watch_record(isolate, "document", name);
        // M3-3: addEventListener / removeEventListener / dispatchEvent first.
        v8::Local<v8::Value> event_result;
        if (handle_event_method(isolate, context, name, args, event_result)) {
            return event_result;
        }
        if (name == "appendChild" || name == "insertBefore" ||
            name == "removeChild" || name == "replaceChild") {
            const auto fail = [&](const char* error_name, const char* message) {
                auto error = v8::Exception::Error(v8::String::NewFromUtf8Literal(isolate, "Document tree edit failed"));
                (void)error.As<v8::Object>()->Set(context, v8::String::NewFromUtf8Literal(isolate, "name"),
                    v8_string(isolate, error_name));
                (void)error.As<v8::Object>()->Set(context, v8::String::NewFromUtf8Literal(isolate, "message"),
                    v8_string(isolate, message));
                isolate->ThrowException(error);
                return v8::Local<v8::Value>(v8::Undefined(isolate));
            };
            DomNode* child = args.Length() ? NodeHost::node_of_arg(args[0]) : nullptr;
            if (child == nullptr) return fail("TypeError", "A Node argument is required");
            if (child->kind == NodeKind::Document) return fail("HierarchyRequestError", "Cannot insert a Document");
            auto host = static_cast<NodeHost*>(host_object_backing(args[0]));
            if (host->document() != this) return fail("WrongDocumentError", "Node belongs to another document");
            if (name == "removeChild") {
                std::string error;
                if (!remove_dom_node(isolate, context, &document_node_, child, &error))
                    return fail("NotFoundError", "Node is not a child of document");
                return wrap_node(isolate, context, child);
            }
            DomNode* reference = nullptr;
            if (name == "insertBefore" || name == "replaceChild") {
                if (args.Length() < 2) return fail("TypeError", "A reference Node is required");
                if (!args[1]->IsNull()) {
                    reference = NodeHost::node_of_arg(args[1]);
                    if (reference == nullptr) return fail("TypeError", "Reference is not a Node");
                    if (std::find(roots_.begin(), roots_.end(), reference) == roots_.end())
                        return fail("NotFoundError", "Reference is not a child of document");
                } else if (name == "replaceChild") return fail("TypeError", "oldChild must be a Node");
            }
            if (child == reference) return wrap_node(isolate, context, child);
            std::string error;
            const bool changed = name == "replaceChild"
                ? replace_dom_node(isolate, context, &document_node_, child, reference, &error)
                : insert_dom_node(isolate, context, &document_node_, child, reference, &error);
            if (!changed) return fail("HierarchyRequestError", error.c_str());
            return wrap_node(isolate, context, name == "replaceChild" ? reference : child);
        }
        if (name == "cloneNode") {
            auto result = page_->create_document(isolate, context, html_document_);
            if (!result->IsObject()) return result;
            auto* clone = static_cast<DocumentHost*>(host_object_backing(result));
            if (args.Length() && args[0]->BooleanValue(isolate)) {
                for (auto* root : roots_) {
                    auto* child = clone->clone_detached(root, true);
                    child->parent = &clone->document_node_;
                    clone->roots_.push_back(child);
                }
            }
            clone->url_ = url_;
            clone->title_node_ = clone->find_tag("title");
            clone->title_ = title_;
            return result;
        }
        v8::Local<v8::Value> node_result;
        if (call_node_method(isolate, context, &document_node_, name, args, node_result)) return node_result;
        std::string arg;
        if (args.Length() > 0) {
            v8::String::Utf8Value utf8(isolate, args[0]);
            if (*utf8 != nullptr) {
                arg.assign(*utf8, static_cast<std::size_t>(utf8.length()));
            }
        }
        if (name == "getElementById") {
            return wrap_node(isolate, context, find_id(arg));
        }
        if (name == "querySelector") {
            return wrap_node(isolate, context, query(arg));
        }
        if (name == "querySelectorAll") {  // M4-A-1: all matches, document order
            return elements_array(isolate, context, query_all(arg));
        }
        if (name == "getElementsByTagName") {  // M4-A-1
            return live_collection(isolate, context, nullptr, "tag", arg);
        }
        if (name == "getElementsByClassName") {
            return live_collection(isolate, context, nullptr, "class", arg);
        }
        if (name == "getElementsByName") {  // M11-20: exact name-attribute query
            std::vector<DomNode*> nodes = collect([&arg](const DomNode* n) {
                const auto it = n->attributes.find("name");
                return it != n->attributes.end() && it->second == arg;
            });
            return elements_array(isolate, context, nodes);
        }
        if (name == "hasFocus") {  // M11-23: no focus machinery -> always false
            return v8::False(isolate);
        }
        if (name == "createElement") {  // M4-A-2: detached element (not in the tree)
            auto node = std::make_unique<DomNode>();
            node->tag = html_document_ ? ascii_lower(arg) : arg;  // arg == String(tag); tagName is UPPER
            DomNode* raw = node.get();
            detached_pool_.push_back(std::move(node));
            return wrap_node(isolate, context, raw);
        }
        // M11-48: detached Text/Comment nodes and DocumentFragments. The backing
        // node is owned by the generation's detached pool until inserted (or for
        // the fragment itself, until the generation ends).
        if (name == "createTextNode" || name == "createComment") {
            auto node = std::make_unique<DomNode>();
            node->kind = name == "createTextNode" ? NodeKind::Text
                                                  : NodeKind::Comment;
            node->data = arg;
            DomNode* raw = node.get();
            detached_pool_.push_back(std::move(node));
            return wrap_node(isolate, context, raw);
        }
        if (name == "createDocumentFragment") {
            auto node = std::make_unique<DomNode>();
            node->kind = NodeKind::DocumentFragment;
            DomNode* raw = node.get();
            detached_pool_.push_back(std::move(node));
            return wrap_node(isolate, context, raw);
        }
        return v8::Undefined(isolate);
    }

const std::string& DocumentHost::base_url() const { return url_; }

DomNode* DocumentHost::clone_detached(DomNode* source, bool deep) {
        auto clone = std::make_unique<DomNode>();
        DomNode* raw = clone.get();
        raw->kind = source->kind;
        raw->data = source->data;
        raw->tag = source->tag;
        raw->id = source->id;
        raw->has_id = source->has_id;
        raw->class_name = source->class_name;
        raw->has_class = source->has_class;
        raw->classes = source->classes;
        raw->src = source->src;
        raw->has_src = source->has_src;
        raw->attributes = source->attributes;
        raw->value = source->value;
        raw->selected = source->selected;
        raw->checked = source->checked;
        raw->method = source->method;
        raw->action = source->action;
        raw->enctype = source->enctype;
        raw->target = source->target;
        raw->no_validate = source->no_validate;
        raw->initial_value = source->initial_value;
        raw->initial_selected = source->initial_selected;
        raw->initial_checked = source->initial_checked;
        if (deep) {
            for (DomNode* child : source->children) {
                DomNode* child_clone = clone_detached(child, true);
                child_clone->parent = raw;
                raw->children.push_back(child_clone);
            }
        }
        detached_pool_.push_back(std::move(clone));
        return raw;
    }

bool DocumentHost::nodes_equal(const DomNode* a, const DomNode* b) const {
        if (a == b) {
            return true;
        }
        if (a == nullptr || b == nullptr) {
            return false;
        }
        if (a->kind != b->kind || a->data != b->data || a->tag != b->tag ||
            a->id != b->id || a->has_id != b->has_id ||
            a->class_name != b->class_name ||
            a->has_class != b->has_class || a->classes != b->classes ||
            a->src != b->src || a->has_src != b->has_src ||
            a->attributes != b->attributes || a->value != b->value ||
            a->selected != b->selected || a->checked != b->checked ||
            a->method != b->method || a->action != b->action ||
            a->enctype != b->enctype || a->target != b->target ||
            a->no_validate != b->no_validate ||
            a->initial_value != b->initial_value ||
            a->initial_selected != b->initial_selected ||
            a->initial_checked != b->initial_checked) {
            return false;
        }
        if (a->children.size() != b->children.size()) {
            return false;
        }
        for (std::size_t i = 0; i < a->children.size(); i++) {
            if (!nodes_equal(a->children[i], b->children[i])) {
                return false;
            }
        }
        return true;
    }

NodeHost* DocumentHost::ensure_node_host(DomNode* node) {
        if (node == &document_node_) return this;
        const auto it = wrappers_.find(node);
        if (it != wrappers_.end()) {
            return it->second;
        }
        std::unique_ptr<NodeHost> created;
        switch (node->kind) {
            case NodeKind::Text:
            case NodeKind::Comment:
                created = std::make_unique<CharacterDataHost>(node, this);
                break;
            case NodeKind::DocumentFragment:
                created = std::make_unique<DocumentFragmentHost>(node, this);
                break;
            case NodeKind::Element:
            default:
                created = std::make_unique<ElementHost>(node, this, html_document_);
                break;
        }
        NodeHost* host = created.get();
        node_hosts_.push_back(std::move(created));
        wrappers_.emplace(node, host);
        return host;
    }

ElementHost* DocumentHost::ensure_element_host(DomNode* node) {
        NodeHost* host = ensure_node_host(node);
        return static_cast<ElementHost*>(host);
    }

v8::Local<v8::Value> DocumentHost::wrap_node(v8::Isolate* isolate, v8::Local<v8::Context> context, DomNode* node) {
        if (node == nullptr) return v8::Null(isolate);
        if (node == &document_node_) return document_global(isolate, context);
        auto& wrapper = node_objects_[node];
        if (wrapper.IsEmpty()) {
            wrapper.Reset(isolate, make_host_object(isolate, context, ensure_node_host(node)));
        }
        return wrapper.Get(isolate);
    }

v8::Local<v8::Array> DocumentHost::nodes_array(v8::Isolate* isolate, v8::Local<v8::Context> context, const std::vector<DomNode*>& nodes) {
        v8::Local<v8::Array> array =
            v8::Array::New(isolate, static_cast<int>(nodes.size()));
        for (std::size_t k = 0; k < nodes.size(); ++k) {
            (void)array->Set(context, static_cast<std::uint32_t>(k),
                             wrap_node(isolate, context, nodes[k]));
        }
        return array;
    }

v8::Local<v8::Array> DocumentHost::elements_array(v8::Isolate* isolate, v8::Local<v8::Context> context, const std::vector<DomNode*>& nodes) {
        return nodes_array(isolate, context, nodes);
    }

void DocumentHost::set_text_content(v8::Isolate* isolate, v8::Local<v8::Context> context, DomNode* node, const std::string& text) {
        if (node == nullptr) {
            return;
        }
        std::vector<DomNode*> removed;
        if (has_mutation_observers()) {
            removed = node->children;
        }
        for (DomNode* child : node->children) {
            child->parent = nullptr;
        }
        node->children.clear();
        DomNode* added_text = nullptr;
        if (!text.empty()) {
            auto text_node = std::make_unique<DomNode>();
            text_node->kind = NodeKind::Text;
            text_node->data = text;
            text_node->parent = node;
            added_text = text_node.get();
            node->children.push_back(added_text);
            detached_pool_.push_back(std::move(text_node));
        }
        if (!removed.empty() || added_text != nullptr) {
            std::vector<DomNode*> added;
            if (added_text != nullptr) {
                added.push_back(added_text);
            }
            queue_child_list(isolate, context, node, added, removed, nullptr,
                             nullptr);
        }
    }

bool DocumentHost::has_mutation_observers() const { return !observers_.empty(); }

bool DocumentHost::is_in_tree(const DomNode* node) const {
        const DomNode* top = node;
        while (top != nullptr && top->parent != nullptr) top = top->parent;
        return top == &document_node_;
    }

void DocumentHost::set_window_target(EventTargetHost* window_target) {
        window_target_ = window_target;
    }

void DocumentHost::set_blob_constructor(v8::Isolate* isolate, v8::Local<v8::Function> ctor) {
        blob_constructor_.Reset(isolate, ctor);
    }

v8::Local<v8::Function> DocumentHost::blob_constructor(v8::Isolate* isolate) {
        return blob_constructor_.Get(isolate);
    }

FileReaderState* DocumentHost::create_file_reader(v8::Isolate* isolate, v8::Local<v8::Object> self) {
        auto state = std::make_unique<FileReaderState>();
        state->owner = this;
        state->self.Reset(isolate, self);
        FileReaderState* raw = state.get();
        file_readers_.push_back(std::move(state));
        return raw;
    }

ClassListBinding* DocumentHost::create_class_list_binding(DomNode* node) {
        auto binding = std::make_unique<ClassListBinding>();
        binding->document = this;
        binding->node = node;
        ClassListBinding* raw = binding.get();
        class_list_bindings_.push_back(std::move(binding));
        return raw;
    }

void DocumentHost::run_element_bubbling(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> event, DomNode* node, EventTargetHost* element_host, v8::Local<v8::Object> receiver) {
        (void)event->Set(context, v8_string(isolate, "target"), receiver);
        reset_event_stopped(isolate, context, event);
        const bool bubbles = event_bubbles(isolate, context, event);
        std::vector<DomNode*> ancestors;
        for (DomNode* ancestor = node->parent; ancestor != nullptr && ancestor != &document_node_;
             ancestor = ancestor->parent) {
            ancestors.push_back(ancestor);
        }
        const bool connected = is_in_tree(node);
        const auto stopped = [&]() {
            return event_is_stopped(isolate, context, event);
        };

        // Capture: window -> document -> topmost ancestor -> ... -> parent.
        if (connected) {
            if (window_target_ != nullptr) {
                window_target_->fire_capture_at(isolate, context, event,
                                                context->Global());
                if (stopped()) {
                    finish_dispatch(isolate, context, event);
                    return;
                }
            }
            fire_capture_at(isolate, context, event,
                            document_global(isolate, context));
            if (stopped()) {
                finish_dispatch(isolate, context, event);
                return;
            }
        }
        for (auto it = ancestors.rbegin(); it != ancestors.rend(); ++it) {
            fire_element_hop(isolate, context, event, *it, true);
            if (stopped()) {
                finish_dispatch(isolate, context, event);
                return;
            }
        }

        // AT_TARGET: capture and bubble listeners in registration order.
        element_host->fire_target_at(isolate, context, event, receiver);
        if (!bubbles || stopped()) {
            finish_dispatch(isolate, context, event);
            return;
        }

        // Bubble: parent -> ... -> topmost ancestor (connected only), then
        // document -> window.
        for (DomNode* ancestor : ancestors) {
            fire_element_hop(isolate, context, event, ancestor, false);
            if (stopped()) {
                finish_dispatch(isolate, context, event);
                return;
            }
        }
        if (connected) {
            fire_bubble_at(isolate, context, event,
                           document_global(isolate, context));
            if (stopped() || window_target_ == nullptr) {
                finish_dispatch(isolate, context, event);
                return;
            }
            window_target_->fire_bubble_at(isolate, context, event,
                                           context->Global());
        }
        finish_dispatch(isolate, context, event);
    }

void DocumentHost::dispatch_event(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> event, v8::Local<v8::Object> receiver) {
        (void)event->Set(context, v8_string(isolate, "target"), receiver);
        reset_event_stopped(isolate, context, event);
        if (window_target_ != nullptr) {
            window_target_->fire_capture_at(isolate, context, event,
                                            context->Global());
            if (event_is_stopped(isolate, context, event)) {
                finish_dispatch(isolate, context, event);
                return;
            }
        }
        fire_target_at(isolate, context, event, receiver);
        if (!event_bubbles(isolate, context, event) ||
            event_is_stopped(isolate, context, event) ||
            window_target_ == nullptr) {
            finish_dispatch(isolate, context, event);
            return;
        }
        window_target_->fire_bubble_at(isolate, context, event,
                                       context->Global());
        finish_dispatch(isolate, context, event);
    }

void DocumentHost::fire_element_hop(v8::Isolate* isolate, v8::Local<v8::Context> context, v8::Local<v8::Object> event, DomNode* node, bool capture) {
        v8::Local<v8::Value> js = wrap_node(isolate, context, node);
        if (!js->IsObject()) {
            return;
        }
        const auto it = wrappers_.find(node);
        if (it == wrappers_.end()) {
            return;
        }
        if (capture) {
            it->second->fire_capture_at(isolate, context, event,
                                        js.As<v8::Object>());
        } else {
            it->second->fire_bubble_at(isolate, context, event,
                                       js.As<v8::Object>());
        }
    }

DomNode* DocumentHost::find_tag(const std::string& tag) const {
        return find_first(roots_,
                          [&](const DomNode* n) { return n->tag == tag; });
    }

std::vector<DomNode*> DocumentHost::element_children_of_roots() const {
        std::vector<DomNode*> out;
        for (DomNode* root : roots_) {
            if (root->kind == NodeKind::Element) {
                out.push_back(root);
            }
        }
        return out;
    }

DomNode* DocumentHost::find_id(const std::string& id) const {
        if (id.empty()) return nullptr;
        return find_first(roots_, [&](const DomNode* n) { return n->id == id; });
    }

DomNode* DocumentHost::find_class(const std::string& cls) const {
        if (cls.empty()) return nullptr;
        return find_first(roots_, [&](const DomNode* n) {
            return std::find(n->classes.begin(), n->classes.end(), cls) !=
                   n->classes.end();
        });
    }

DomNode* DocumentHost::query(const std::string& raw) const {
        return find_first(roots_, selector_predicate(raw));
    }

std::vector<DomNode*> DocumentHost::collect(const std::function<bool(const DomNode*)>& pred) const {
        std::vector<DomNode*> out;
        for (DomNode* root : roots_) {
            collect_matching(root, pred, out);
        }
        return out;
    }

std::vector<DomNode*> DocumentHost::query_all(const std::string& raw) const {
        return collect(selector_predicate(raw));
    }

std::vector<DomNode*> DocumentHost::elements_by_tag(const std::string& raw) const {
        return collect(tag_predicate(raw));
    }

}  // namespace iv8::browser
