#include "url.h"

#include <cstring>
#include <unordered_set>

#include "iv8/query_parameters.h"
#include "iv8/url_parser.h"
#include "../internal/trace_hooks.h"

namespace iv8::browser {

// `new URL(input, base?)`. input / base go through String(...); resolution failure
// (relative input with no usable base) is a TypeError. Fields are read-only own data
// properties matching location's decomposition. Requires a construct call.
void url_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!info.IsConstructCall()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate,
                                           "URL constructor requires 'new'")));
        return;
    }
    const auto to_std = [&](v8::Local<v8::Value> v, std::string& out) -> bool {
        v8::Local<v8::String> str;
        if (!v->ToString(context).ToLocal(&str)) {
            return false;
        }
        v8::String::Utf8Value utf8(isolate, str);
        out = (*utf8 != nullptr)
                  ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
                  : std::string();
        return true;
    };
    std::string input;
    if (!to_std(info[0], input)) {
        return;  // ToString threw
    }
    const bool has_base = info.Length() > 1 && !info[1]->IsUndefined();
    std::string base;
    if (has_base && !to_std(info[1], base)) {
        return;  // ToString threw
    }
    std::string href;
    if (!resolve_url(input, has_base, base, href)) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(isolate, "Invalid URL")));
        return;
    }
    const UrlParts parts = decompose_url(href);
    v8::Local<v8::Object> self = info.This();
    const auto define = [&](const char* name, const std::string& value) {
        (void)self->DefineOwnProperty(
            context, v8::String::NewFromUtf8(isolate, name).ToLocalChecked(),
            v8_string(isolate, value), v8::ReadOnly);
    };
    define("href", parts.href);
    define("origin", parts.origin);
    define("protocol", parts.protocol);
    define("host", parts.host);
    define("hostname", parts.hostname);
    define("pathname", parts.pathname);
    define("search", parts.search);
    define("hash", parts.hash);
    // M11-15: URLSearchParams snapshot of this URL's search. It is NOT live:
    // mutating the snapshot does not rewrite the URL (no live-link state).
    v8::Local<v8::Value> usp_value;
    v8::Local<v8::Value> search_params = v8::Null(isolate);
    if (context->Global()
            ->Get(context, v8_string(isolate, "URLSearchParams"))
            .ToLocal(&usp_value) &&
        usp_value->IsFunction()) {
        v8::Local<v8::Value> search_arg = v8_string(isolate, parts.search);
        v8::Local<v8::Object> parsed;
        if (usp_value.As<v8::Function>()
                ->NewInstance(context, 1, &search_arg)
                .ToLocal(&parsed)) {
            search_params = parsed;
        }
    }
    (void)self->DefineOwnProperty(
        context, v8::String::NewFromUtf8Literal(isolate, "searchParams"),
        search_params, v8::ReadOnly);
}

// url.toString() / url.toJSON() — both return this.href.
void url_to_href(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    v8::Local<v8::Value> href;
    if (info.This()->Get(context, v8_string(isolate, "href")).ToLocal(&href)) {
        info.GetReturnValue().Set(href);
    } else {
        info.GetReturnValue().Set(v8::String::Empty(isolate));
    }
}

// M8-3 URLSearchParams keeps canonical query text in its GC-managed private
// slot. The query_parameters module handles byte parsing/encoding; V8 state,
// coercion, and method mutation stay here. M11-15 links a non-live URL snapshot.
v8::Local<v8::Private> usp_private_key(v8::Isolate* isolate) {
    return v8::Private::ForApi(
        isolate,
        v8::String::NewFromUtf8Literal(isolate, "iv8::URLSearchParams::state"));
}

std::string usp_read_state(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           v8::Local<v8::Object> self) {
    v8::Local<v8::Value> v;
    if (self->GetPrivate(context, usp_private_key(isolate)).ToLocal(&v) &&
        v->IsString()) {
        v8::String::Utf8Value utf8(isolate, v);
        if (*utf8 != nullptr) {
            return std::string(*utf8, static_cast<std::size_t>(utf8.length()));
        }
    }
    return std::string();
}

void usp_write_state(v8::Isolate* isolate, v8::Local<v8::Context> context,
                     v8::Local<v8::Object> self, const std::string& s) {
    (void)self->SetPrivate(context, usp_private_key(isolate), v8_string(isolate, s));
}

std::string usp_arg_string(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           v8::Local<v8::Value> v) {
    v8::Local<v8::String> str;
    if (!v->ToString(context).ToLocal(&str)) {
        return std::string();
    }
    v8::String::Utf8Value utf8(isolate, str);
    return (*utf8 != nullptr)
               ? std::string(*utf8, static_cast<std::size_t>(utf8.length()))
               : std::string();
}

// `new URLSearchParams(init?)`. undefined/null/absent -> empty; another
// URLSearchParams -> copy; a string (via String()) -> parsed (a leading '?' is
// dropped); any other object (record / pair-list / etc.) -> TypeError. Requires new.
void url_search_params_constructor(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    if (!info.IsConstructCall()) {
        isolate->ThrowException(v8::Exception::TypeError(
            v8::String::NewFromUtf8Literal(
                isolate, "URLSearchParams constructor requires 'new'")));
        return;
    }
    v8::Local<v8::Object> self = info.This();
    v8::Local<v8::Value> init = info[0];
    std::string canonical;
    if (info.Length() == 0 || init->IsUndefined() || init->IsNull()) {
        canonical = std::string();
    } else if (init->IsObject()) {
        v8::Local<v8::Object> obj = init.As<v8::Object>();
        const v8::Maybe<bool> has = obj->HasPrivate(context, usp_private_key(isolate));
        if (has.IsJust() && has.FromJust()) {
            canonical = usp_read_state(isolate, context, obj);  // copy another USP
        } else {
            isolate->ThrowException(v8::Exception::TypeError(
                v8::String::NewFromUtf8Literal(
                    isolate, "URLSearchParams: unsupported init (only a string or "
                             "another URLSearchParams)")));
            return;
        }
    } else {
        std::string str = usp_arg_string(isolate, context, init);
        if (!str.empty() && str[0] == '?') {
            str = str.substr(1);
        }
        canonical = serialize_query_parameters(parse_query_parameters(str));  // canonicalize
    }
    usp_write_state(isolate, context, self, canonical);
}

void usp_get(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::string name = usp_arg_string(isolate, context, info[0]);
    for (const auto& p :
         parse_query_parameters(usp_read_state(isolate, context, info.This()))) {
        if (p.first == name) {
            info.GetReturnValue().Set(v8_string(isolate, p.second));
            return;
        }
    }
    info.GetReturnValue().Set(v8::Null(isolate));
}

void usp_get_all(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::string name = usp_arg_string(isolate, context, info[0]);
    v8::Local<v8::Array> arr = v8::Array::New(isolate);
    std::uint32_t idx = 0;
    for (const auto& p :
         parse_query_parameters(usp_read_state(isolate, context, info.This()))) {
        if (p.first == name) {
            (void)arr->Set(context, idx++, v8_string(isolate, p.second));
        }
    }
    info.GetReturnValue().Set(arr);
}

void usp_has(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::string name = usp_arg_string(isolate, context, info[0]);
    for (const auto& p :
         parse_query_parameters(usp_read_state(isolate, context, info.This()))) {
        if (p.first == name) {
            info.GetReturnValue().Set(true);
            return;
        }
    }
    info.GetReturnValue().Set(false);
}

void usp_append(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::string name = usp_arg_string(isolate, context, info[0]);
    const std::string value = usp_arg_string(isolate, context, info[1]);
    auto pairs = parse_query_parameters(usp_read_state(isolate, context, info.This()));
    pairs.emplace_back(name, value);
    usp_write_state(isolate, context, info.This(),
                    serialize_query_parameters(pairs));
}

void usp_set(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::string name = usp_arg_string(isolate, context, info[0]);
    const std::string value = usp_arg_string(isolate, context, info[1]);
    const auto pairs = parse_query_parameters(usp_read_state(isolate, context, info.This()));
    std::vector<std::pair<std::string, std::string>> out;
    bool replaced = false;
    for (const auto& p : pairs) {
        if (p.first == name) {
            if (!replaced) {  // keep the first occurrence's position, drop the rest
                out.emplace_back(name, value);
                replaced = true;
            }
        } else {
            out.push_back(p);
        }
    }
    if (!replaced) {
        out.emplace_back(name, value);
    }
    usp_write_state(isolate, context, info.This(),
                    serialize_query_parameters(out));
}

void usp_delete(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    const std::string name = usp_arg_string(isolate, context, info[0]);
    const auto pairs = parse_query_parameters(usp_read_state(isolate, context, info.This()));
    std::vector<std::pair<std::string, std::string>> out;
    for (const auto& p : pairs) {
        if (p.first != name) {
            out.push_back(p);
        }
    }
    usp_write_state(isolate, context, info.This(),
                    serialize_query_parameters(out));
}

void usp_to_string(const v8::FunctionCallbackInfo<v8::Value>& info) {
    v8::Isolate* isolate = info.GetIsolate();
    v8::Local<v8::Context> context = isolate->GetCurrentContext();
    info.GetReturnValue().Set(
        v8_string(isolate, usp_read_state(isolate, context, info.This())));
}

}  // namespace iv8::browser
