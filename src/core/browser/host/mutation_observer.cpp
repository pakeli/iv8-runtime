#include "mutation_observer.h"

#include <algorithm>
#include <cstdint>
#include <string>
#include <utility>
#include <vector>

#include "iv8/web_method.h"
#include "../dom/dom_query.h"
#include "../dom/selector_match.h"
#include "../internal/trace_hooks.h"
#include "document_host.h"

namespace iv8::browser {

constexpr v8::ExternalPointerTypeTag kMutationObserverTag =
    v8::kExternalPointerTypeTagDefault;

MutationObserverState* DocumentHost::create_mutation_observer(
    v8::Isolate* isolate, v8::Local<v8::Function> callback,
    v8::Local<v8::Object> self) {
    auto state = std::make_unique<MutationObserverState>();
    state->owner = this;
    state->callback.Reset(isolate, callback);
    state->self.Reset(isolate, self);
    MutationObserverState* raw = state.get();
    observers_.push_back(std::move(state));
    return raw;
}

void DocumentHost::schedule_mutation_delivery(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    MutationObserverState* state) {
    if (state == nullptr || state->delivery_queued || !state->alive) {
        return;
    }
    state->delivery_queued = true;
    v8::Local<v8::Function> delivery;
    if (!v8::Function::New(context, &mutation_observer_delivery,
                           v8::External::New(isolate, state,
                                             kMutationObserverTag))
             .ToLocal(&delivery)) {
        state->delivery_queued = false;
        return;
    }
    isolate->EnqueueMicrotask(delivery);
}

void DocumentHost::queue_child_list(
    v8::Isolate* isolate, v8::Local<v8::Context> context, DomNode* parent,
    const std::vector<DomNode*>& added, const std::vector<DomNode*>& removed,
    DomNode* previous_sibling, DomNode* next_sibling) {
    if (observers_.empty()) {
        return;
    }
    for (const std::unique_ptr<MutationObserverState>& observer : observers_) {
        if (!observer->alive) {
            continue;
        }
        bool matched = false;
        for (const MutationObserverRegistration& registration :
             observer->registrations) {
            if (!registration.child_list) {
                continue;
            }
            bool target_matches = false;
            if (registration.target_is_document) {
                target_matches = parent == &document_node_ ||
                    (registration.subtree && is_in_tree(parent));
            } else {
                target_matches =
                    registration.target == parent ||
                    (registration.subtree &&
                     is_self_or_ancestor(registration.target, parent));
            }
            if (!target_matches) {
                continue;
            }
            MutationRecordData record;
            record.type = MutationRecordData::Type::ChildList;
            record.target = parent;
            record.added = added;
            record.removed = removed;
            record.previous_sibling = previous_sibling;
            record.next_sibling = next_sibling;
            observer->pending.push_back(std::move(record));
            matched = true;
            break;
        }
        if (matched) {
            schedule_mutation_delivery(isolate, context, observer.get());
        }
    }
}

void DocumentHost::queue_attribute(v8::Isolate* isolate,
                                   v8::Local<v8::Context> context,
                                   DomNode* target, const std::string& name,
                                   bool has_old_value,
                                   const std::string& old_value) {
    if (observers_.empty()) {
        return;
    }
    for (const std::unique_ptr<MutationObserverState>& observer : observers_) {
        if (!observer->alive) {
            continue;
        }
        bool matched = false;
        for (const MutationObserverRegistration& registration :
             observer->registrations) {
            if (!registration.attributes) {
                continue;
            }
            bool target_matches = false;
            if (registration.target_is_document) {
                target_matches = registration.subtree && is_in_tree(target);
            } else {
                target_matches =
                    registration.target == target ||
                    (registration.subtree &&
                     is_self_or_ancestor(registration.target, target));
            }
            if (!target_matches) {
                continue;
            }
            if (registration.has_attribute_filter &&
                std::find(registration.attribute_filter.begin(),
                          registration.attribute_filter.end(),
                          name) == registration.attribute_filter.end()) {
                continue;
            }
            MutationRecordData record;
            record.type = MutationRecordData::Type::Attributes;
            record.target = target;
            record.attribute_name = name;
            record.has_old_value =
                registration.attribute_old_value && has_old_value;
            if (record.has_old_value) {
                record.old_value = old_value;
            }
            observer->pending.push_back(std::move(record));
            matched = true;
            break;
        }
        if (matched) {
            schedule_mutation_delivery(isolate, context, observer.get());
        }
    }
}

void DocumentHost::queue_character_data(v8::Isolate* isolate,
                                        v8::Local<v8::Context> context,
                                        DomNode* target,
                                        const std::string& old_value) {
    if (observers_.empty()) {
        return;
    }
    for (const std::unique_ptr<MutationObserverState>& observer : observers_) {
        if (!observer->alive) {
            continue;
        }
        bool matched = false;
        for (const MutationObserverRegistration& registration :
             observer->registrations) {
            if (!registration.character_data) {
                continue;
            }
            bool target_matches = false;
            if (registration.target_is_document) {
                target_matches = registration.subtree && is_in_tree(target);
            } else {
                target_matches =
                    registration.target == target ||
                    (registration.subtree &&
                     is_self_or_ancestor(registration.target, target));
            }
            if (!target_matches) {
                continue;
            }
            MutationRecordData record;
            record.type = MutationRecordData::Type::CharacterData;
            record.target = target;
            record.has_old_value = registration.character_data_old_value;
            if (record.has_old_value) {
                record.old_value = old_value;
            }
            observer->pending.push_back(std::move(record));
            matched = true;
            break;
        }
        if (matched) {
            schedule_mutation_delivery(isolate, context, observer.get());
        }
    }
}

v8::Local<v8::Array> DocumentHost::build_records_array(
    v8::Isolate* isolate, v8::Local<v8::Context> context,
    const std::vector<MutationRecordData>& records) {
    v8::Local<v8::Array> array =
        v8::Array::New(isolate, static_cast<int>(records.size()));
    for (std::size_t i = 0; i < records.size(); ++i) {
        const MutationRecordData& record = records[i];
        const bool child_list =
            record.type == MutationRecordData::Type::ChildList;
        const bool attributes =
            record.type == MutationRecordData::Type::Attributes;
        v8::Local<v8::Object> object = v8::Object::New(isolate);
        (void)object->Set(
            context, v8_string(isolate, "type"),
            v8_string(isolate, child_list
                                  ? "childList"
                                  : (attributes ? "attributes"
                                                : "characterData")));
        (void)object->Set(context, v8_string(isolate, "target"),
                          wrap_node(isolate, context, record.target));
        (void)object->Set(context, v8_string(isolate, "addedNodes"),
                          nodes_array(isolate, context, record.added));
        (void)object->Set(context, v8_string(isolate, "removedNodes"),
                          nodes_array(isolate, context, record.removed));
        (void)object->Set(context, v8_string(isolate, "previousSibling"),
                          wrap_node(isolate, context,
                                       record.previous_sibling));
        (void)object->Set(context, v8_string(isolate, "nextSibling"),
                          wrap_node(isolate, context, record.next_sibling));
        if (attributes) {
            (void)object->Set(context, v8_string(isolate, "attributeName"),
                              v8_string(isolate, record.attribute_name));
        } else {
            (void)object->Set(context, v8_string(isolate, "attributeName"),
                              v8::Null(isolate));
        }
        (void)object->Set(context, v8_string(isolate, "attributeNamespace"),
                          v8::Null(isolate));
        if (record.has_old_value) {
            (void)object->Set(context, v8_string(isolate, "oldValue"),
                              v8_string(isolate, record.old_value));
        } else {
            (void)object->Set(context, v8_string(isolate, "oldValue"),
                              v8::Null(isolate));
        }
        (void)array->Set(context, static_cast<std::uint32_t>(i), object);
    }
    return array;
}

void mutation_observer_delivery(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!info.Data()->IsExternal()) {
        return;
    }
    auto* state = static_cast<MutationObserverState*>(
        info.Data().As<v8::External>()->Value(kMutationObserverTag));
    if (state == nullptr || !state->alive || state->owner == nullptr) {
        return;
    }
    state->delivery_queued = false;
    if (state->pending.empty()) {
        return;
    }
    std::vector<MutationRecordData> records;
    records.swap(state->pending);
    v8::Local<v8::Array> array =
        state->owner->build_records_array(isolate, context, records);
    if (state->callback.IsEmpty() || state->self.IsEmpty()) {
        return;
    }
    v8::Local<v8::Function> callback = state->callback.Get(isolate);
    v8::Local<v8::Object> self = state->self.Get(isolate);
    v8::Local<v8::Value> argv[2] = {array.As<v8::Value>(),
                                    self.As<v8::Value>()};
    v8::TryCatch try_catch(isolate);
    (void)callback->Call(context, self.As<v8::Value>(), 2, argv);
    if (try_catch.HasCaught()) {
        try_catch.Reset();
    }
}

v8::Local<v8::Private> mo_private_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate, v8::String::NewFromUtf8Literal(
                     isolate, "iv8::MutationObserver::state"));
}

void throw_mo_type_error(v8::Isolate* isolate, const char* message) {
    isolate->ThrowException(v8::Exception::TypeError(
        v8_string(isolate, std::string(message)).As<v8::String>()));
}

MutationObserverState* mo_state_from_this(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> value;
    if (!info.This()
             ->GetPrivate(context, mo_private_key(isolate))
             .ToLocal(&value)) {
        return nullptr;
    }
    if (!value->IsExternal()) {
        return nullptr;
    }
    return static_cast<MutationObserverState*>(
        value.As<v8::External>()->Value(kMutationObserverTag));
}

void mutation_observer_constructor(
    const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!info.IsConstructCall()) {
        throw_mo_type_error(isolate,
                            "MutationObserver constructor requires 'new'");
        return;
    }
    DocumentHost* document = nullptr;
    if (info.Data()->IsExternal()) {
        document = static_cast<DocumentHost*>(
            info.Data().As<v8::External>()->Value(kMutationObserverTag));
    }
    if (document == nullptr) {
        throw_mo_type_error(isolate, "MutationObserver: invalid constructor");
        return;
    }
    if (info.Length() < 1 || !info[0]->IsFunction()) {
        throw_mo_type_error(isolate,
                            "MutationObserver: callback must be a function");
        return;
    }
    v8::Local<v8::Object> self = info.This();
    MutationObserverState* state = document->create_mutation_observer(
        isolate, info[0].As<v8::Function>(), self);
    (void)self->SetPrivate(
        context, mo_private_key(isolate),
        v8::External::New(isolate, state, kMutationObserverTag));
}

void mo_observe(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    MutationObserverState* state = mo_state_from_this(info);
    if (state == nullptr) {
        throw_mo_type_error(isolate, "Illegal invocation");
        return;
    }
    if (info.Length() < 1) {
        throw_mo_type_error(isolate, "observe: target is required");
        return;
    }
    HostObject* host = host_object_backing(info[0]);
    bool target_is_document = false;
    DomNode* target_node = nullptr;
    if (host == nullptr) {
        throw_mo_type_error(isolate,
                            "observe: target must be an element or document");
        return;
    }
    if (state->owner != nullptr &&
        static_cast<HostObject*>(state->owner) == host) {
        target_is_document = true;
    } else if (host->is_node()) {
        NodeHost* node_host = static_cast<NodeHost*>(host);
        if (node_host->document() != state->owner) {
            throw_mo_type_error(isolate, "observe: target is from another document");
            return;
        }
        target_node = node_host->node();
    } else {
        throw_mo_type_error(isolate,
                            "observe: target must be a node or document");
        return;
    }
    if (info.Length() < 2 || !info[1]->IsObject()) {
        throw_mo_type_error(isolate, "observe: options must be an object");
        return;
    }
    v8::Local<v8::Object> options = info[1].As<v8::Object>();
    const auto get_bool = [&](const char* key, bool* out) -> bool {
        v8::Local<v8::Value> value;
        if (!options->Get(context, v8_string(isolate, key)).ToLocal(&value)) {
            return false;
        }
        *out = value->BooleanValue(isolate);
        return true;
    };
    bool child_list = false;
    bool attributes = false;
    bool character_data = false;
    bool subtree = false;
    bool attribute_old_value = false;
    bool character_data_old_value = false;
    if (!get_bool("childList", &child_list) ||
        !get_bool("attributes", &attributes) ||
        !get_bool("characterData", &character_data) ||
        !get_bool("subtree", &subtree) ||
        !get_bool("attributeOldValue", &attribute_old_value) ||
        !get_bool("characterDataOldValue", &character_data_old_value)) {
        return;  // a getter threw; keep the pending exception
    }
    v8::Maybe<bool> maybe_has =
        options->Has(context, v8_string(isolate, "attributeFilter"));
    if (maybe_has.IsNothing()) {
        return;
    }
    const bool has_filter = maybe_has.FromJust();
    if (!child_list && !attributes && !character_data) {
        throw_mo_type_error(isolate,
                            "observe: at least one of childList, attributes "
                            "or characterData must be true");
        return;
    }
    if (attribute_old_value && !attributes) {
        throw_mo_type_error(isolate,
                            "observe: attributeOldValue requires attributes");
        return;
    }
    if (has_filter && !attributes) {
        throw_mo_type_error(isolate,
                            "observe: attributeFilter requires attributes");
        return;
    }
    if (character_data_old_value && !character_data) {
        throw_mo_type_error(
            isolate,
            "observe: characterDataOldValue requires characterData");
        return;
    }
    std::vector<std::string> filter;
    if (has_filter) {
        v8::Local<v8::Value> filter_value;
        if (!options->Get(context, v8_string(isolate, "attributeFilter"))
                 .ToLocal(&filter_value)) {
            return;
        }
        if (!filter_value->IsArray()) {
            throw_mo_type_error(isolate,
                                "observe: attributeFilter must be an array");
            return;
        }
        v8::Local<v8::Array> filter_array = filter_value.As<v8::Array>();
        const std::uint32_t length = filter_array->Length();
        for (std::uint32_t i = 0; i < length; ++i) {
            v8::Local<v8::Value> item;
            if (!filter_array->Get(context, i).ToLocal(&item)) {
                return;
            }
            v8::Local<v8::String> item_string;
            if (!item->ToString(context).ToLocal(&item_string)) {
                return;
            }
            v8::String::Utf8Value utf8(isolate, item_string);
            if (*utf8 != nullptr) {
                filter.push_back(ascii_lower(std::string(
                    *utf8, static_cast<std::size_t>(utf8.length()))));
            }
        }
    }
    MutationObserverRegistration registration;
    registration.target = target_node;
    registration.target_is_document = target_is_document;
    registration.child_list = child_list;
    registration.attributes = attributes;
    registration.subtree = subtree;
    registration.attribute_old_value = attribute_old_value;
    registration.character_data = character_data;
    registration.character_data_old_value = character_data_old_value;
    registration.has_attribute_filter = has_filter;
    registration.attribute_filter = std::move(filter);
    bool replaced = false;
    for (MutationObserverRegistration& existing : state->registrations) {
        if (existing.target == target_node &&
            existing.target_is_document == target_is_document) {
            existing = std::move(registration);
            replaced = true;
            break;
        }
    }
    if (!replaced) {
        state->registrations.push_back(std::move(registration));
    }
    info.GetReturnValue().SetUndefined();
}

void mo_disconnect(const v8::FunctionCallbackInfo<v8::Value>& info) {
    MutationObserverState* state = mo_state_from_this(info);
    if (state == nullptr) {
        throw_mo_type_error(info.GetIsolate(), "Illegal invocation");
        return;
    }
    state->registrations.clear();
    state->pending.clear();
    info.GetReturnValue().SetUndefined();
}

void mo_take_records(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    MutationObserverState* state = mo_state_from_this(info);
    if (state == nullptr) {
        throw_mo_type_error(isolate, "Illegal invocation");
        return;
    }
    std::vector<MutationRecordData> records;
    records.swap(state->pending);
    info.GetReturnValue().Set(
        state->owner->build_records_array(isolate, context, records)
            .As<v8::Value>());
}

}  // namespace iv8::browser
