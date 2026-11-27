#include "iv8/value_converter.h"
#include "iv8/access_origin.h"

#include <cmath>
#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>
#include <vector>

namespace py = pybind11;

namespace iv8 {

namespace {

constexpr double kInt64UpperExclusive = 9223372036854775808.0;  // 2^63
constexpr double kInt64Lower = -9223372036854775808.0;          // -2^63

py::object convert_number(double value) {
    const bool integral = std::isfinite(value) && std::trunc(value) == value;
    const bool negative_zero = (value == 0.0) && std::signbit(value);
    if (integral && !negative_zero && value >= kInt64Lower &&
        value < kInt64UpperExclusive) {
        return py::int_(static_cast<std::int64_t>(value));
    }
    return py::float_(value);
}

bool extract_bigint_decimal(v8::Isolate* isolate, v8::Local<v8::Context> context,
                            v8::Local<v8::Value> value, std::string& out) {
    v8::Local<v8::String> as_string;
    if (value->ToString(context).ToLocal(&as_string)) {
        v8::String::Utf8Value utf8(isolate, as_string);
        if (*utf8 != nullptr) {
            out.assign(*utf8, static_cast<std::size_t>(utf8.length()));
            return true;
        }
    }
    return false;
}

py::object bigint_decimal_to_py(const std::string& decimal) {
    PyObject* number = PyLong_FromString(decimal.c_str(), nullptr, 10);
    if (number != nullptr) {
        return py::reinterpret_steal<py::object>(number);
    }
    PyErr_Clear();
    throw std::runtime_error("failed to convert BigInt value");
}

py::object convert_bigint(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Value> value) {
    std::string decimal;
    {
        py::gil_scoped_release release_gil;
        if (!extract_bigint_decimal(isolate, context, value, decimal)) {
            decimal.clear();
        }
    }
    if (decimal.empty()) {
        throw std::runtime_error("failed to convert BigInt value");
    }
    return bigint_decimal_to_py(decimal);
}

// A "plain" data object convertible to dict: an Object that is not any of the
// excluded special/host object kinds.
bool is_plain_object(v8::Local<v8::Value> value) {
    if (!value->IsObject() || value->IsArray()) {
        return false;
    }
    if (value->IsFunction() || value->IsPromise() || value->IsMap() ||
        value->IsSet() || value->IsWeakMap() || value->IsWeakSet() ||
        value->IsDate() || value->IsRegExp() || value->IsProxy() ||
        value->IsArrayBuffer() || value->IsTypedArray() || value->IsDataView() ||
        value->IsSymbolObject() || value->IsBooleanObject() ||
        value->IsNumberObject() || value->IsStringObject() ||
        value->IsBigIntObject() || value->IsExternal() ||
        value->IsNativeError()) {
        return false;
    }
    return true;
}

struct UndefinedValue {};
struct NullValue {};
struct BigIntDecimal {
    std::string digits;
};

struct ConvertedNode {
    using ArrayItems = std::vector<ConvertedNode>;
    using ObjectEntries = std::vector<std::pair<std::string, ConvertedNode>>;
    using Storage = std::variant<
        UndefinedValue,
        NullValue,
        bool,
        double,
        BigIntDecimal,
        std::string,
        ArrayItems,
        ObjectEntries>;

    Storage value;
};

enum class ExtractFailureKind {
    kNone,
    kConversionError,
    kRuntimeError,
};

struct ExtractError {
    ExtractFailureKind kind = ExtractFailureKind::kNone;
    std::string message;
};

bool extract_deep_nogil(v8::Isolate* isolate, v8::Local<v8::Context> context,
                        v8::Local<v8::Value> value, int depth,
                        std::vector<v8::Local<v8::Object>>& ancestors,
                        ConvertedNode& out, ExtractError& err) {
    if (value->IsUndefined()) {
        out.value = UndefinedValue{};
        return true;
    }
    if (value->IsNull()) {
        out.value = NullValue{};
        return true;
    }
    if (value->IsBoolean()) {
        out.value = value.As<v8::Boolean>()->Value();
        return true;
    }
    if (value->IsBigInt()) {
        std::string decimal;
        if (!extract_bigint_decimal(isolate, context, value, decimal)) {
            err = {ExtractFailureKind::kRuntimeError,
                   "failed to convert BigInt value"};
            return false;
        }
        out.value = BigIntDecimal{std::move(decimal)};
        return true;
    }
    if (value->IsNumber()) {
        out.value = value.As<v8::Number>()->Value();
        return true;
    }
    if (value->IsString()) {
        v8::String::Utf8Value utf8(isolate, value);
        if (*utf8 == nullptr) {
            err = {ExtractFailureKind::kRuntimeError,
                   "failed to convert JavaScript string"};
            return false;
        }
        out.value = std::string(*utf8, static_cast<std::size_t>(utf8.length()));
        return true;
    }

    const bool is_array = value->IsArray();
    const bool is_object = !is_array && is_plain_object(value);
    if (!is_array && !is_object) {
        err = {ExtractFailureKind::kConversionError,
               "unsupported JavaScript type for to_py=True: " +
                   describe_js_type(value)};
        return false;
    }

    if (depth > kMaxConversionDepth) {
        err = {ExtractFailureKind::kConversionError,
               "maximum conversion depth (" +
                   std::to_string(kMaxConversionDepth) +
                   ") exceeded during to_py conversion"};
        return false;
    }

    v8::Local<v8::Object> object = value.As<v8::Object>();
    for (const v8::Local<v8::Object>& ancestor : ancestors) {
        if (object->SameValue(ancestor)) {
            err = {ExtractFailureKind::kConversionError,
                   "circular reference detected during to_py conversion"};
            return false;
        }
    }
    ancestors.push_back(object);

    if (is_array) {
        v8::Local<v8::Array> array = value.As<v8::Array>();
        const uint32_t length = array->Length();
        ConvertedNode::ArrayItems items;
        items.reserve(length);
        for (uint32_t index = 0; index < length; ++index) {
            v8::TryCatch try_catch(isolate);
            v8::Local<v8::Value> element;
            if (!array->Get(context, index).ToLocal(&element)) {
                ancestors.pop_back();
                err = {ExtractFailureKind::kConversionError,
                       "property access raised during to_py conversion"};
                return false;
            }
            ConvertedNode child;
            if (!extract_deep_nogil(isolate, context, element, depth + 1,
                                    ancestors, child, err)) {
                ancestors.pop_back();
                return false;
            }
            items.push_back(std::move(child));
        }
        out.value = std::move(items);
    } else {
        v8::Local<v8::Array> keys;
        {
            v8::TryCatch try_catch(isolate);
            if (!object->GetOwnPropertyNames(context).ToLocal(&keys)) {
                ancestors.pop_back();
                err = {ExtractFailureKind::kConversionError,
                       "failed to enumerate object properties"};
                return false;
            }
        }
        const uint32_t count = keys->Length();
        ConvertedNode::ObjectEntries entries;
        entries.reserve(count);
        for (uint32_t index = 0; index < count; ++index) {
            v8::Local<v8::Value> key;
            if (!keys->Get(context, index).ToLocal(&key)) {
                ancestors.pop_back();
                err = {ExtractFailureKind::kConversionError,
                       "failed to read object property name"};
                return false;
            }
            v8::TryCatch try_catch(isolate);
            v8::Local<v8::Value> property;
            if (!object->Get(context, key).ToLocal(&property)) {
                ancestors.pop_back();
                err = {ExtractFailureKind::kConversionError,
                       "property access raised during to_py conversion"};
                return false;
            }
            v8::String::Utf8Value key_utf8(isolate, key);
            std::string key_string =
                (*key_utf8 != nullptr)
                    ? std::string(*key_utf8, static_cast<size_t>(key_utf8.length()))
                    : std::string();
            ConvertedNode child;
            if (!extract_deep_nogil(isolate, context, property, depth + 1,
                                    ancestors, child, err)) {
                ancestors.pop_back();
                return false;
            }
            entries.emplace_back(std::move(key_string), std::move(child));
        }
        out.value = std::move(entries);
    }

    ancestors.pop_back();
    return true;
}

py::object materialize_py_node(const ConvertedNode& node,
                               const py::object& js_undefined_singleton) {
    return std::visit(
        [&](const auto& item) -> py::object {
            using T = std::decay_t<decltype(item)>;
            if constexpr (std::is_same_v<T, UndefinedValue>) {
                return js_undefined_singleton;
            } else if constexpr (std::is_same_v<T, NullValue>) {
                return py::none();
            } else if constexpr (std::is_same_v<T, bool>) {
                return py::bool_(item);
            } else if constexpr (std::is_same_v<T, double>) {
                return convert_number(item);
            } else if constexpr (std::is_same_v<T, BigIntDecimal>) {
                return bigint_decimal_to_py(item.digits);
            } else if constexpr (std::is_same_v<T, std::string>) {
                return py::str(item.data(), item.size());
            } else if constexpr (std::is_same_v<T, ConvertedNode::ArrayItems>) {
                py::list list(item.size());
                for (std::size_t i = 0; i < item.size(); ++i) {
                    list[i] = materialize_py_node(item[i], js_undefined_singleton);
                }
                return list;
            } else {
                py::dict mapping;
                for (const auto& [k, v] : item) {
                    mapping[py::str(k.data(), k.size())] =
                        materialize_py_node(v, js_undefined_singleton);
                }
                return mapping;
            }
        },
        node.value);
}

}  // namespace

std::string describe_js_type(v8::Local<v8::Value> value) {
    if (value->IsArray()) return "Array";
    if (value->IsFunction()) return "Function";
    if (value->IsPromise()) return "Promise";
    if (value->IsMap()) return "Map";
    if (value->IsSet()) return "Set";
    if (value->IsWeakMap()) return "WeakMap";
    if (value->IsWeakSet()) return "WeakSet";
    if (value->IsDate()) return "Date";
    if (value->IsRegExp()) return "RegExp";
    if (value->IsProxy()) return "Proxy";
    if (value->IsArrayBuffer()) return "ArrayBuffer";
    if (value->IsTypedArray()) return "TypedArray";
    if (value->IsDataView()) return "DataView";
    if (value->IsSymbol() || value->IsSymbolObject()) return "Symbol";
    if (value->IsNativeError()) return "Error";
    if (value->IsObject()) return "Object";
    return "object";
}

bool try_convert_primitive(v8::Isolate* isolate, v8::Local<v8::Context> context,
                           v8::Local<v8::Value> value, py::object& out) {
    if (value->IsUndefined()) {
        out = py::module_::import("iv8").attr("JSUndefined");
        return true;
    }
    if (value->IsNull()) {
        out = py::none();
        return true;
    }
    if (value->IsBoolean()) {
        out = py::bool_(value.As<v8::Boolean>()->Value());
        return true;
    }
    if (value->IsBigInt()) {
        out = convert_bigint(isolate, context, value);
        return true;
    }
    if (value->IsNumber()) {
        out = convert_number(value.As<v8::Number>()->Value());
        return true;
    }
    if (value->IsString()) {
        v8::String::Utf8Value utf8(isolate, value);
        if (*utf8 == nullptr) {
            throw std::runtime_error("failed to convert JavaScript string");
        }
        out = py::str(*utf8, static_cast<size_t>(utf8.length()));
        return true;
    }
    return false;
}

py::object to_python_deep(v8::Isolate* isolate, v8::Local<v8::Context> context,
                          v8::Local<v8::Value> value) {
    py::object primitive;
    if (try_convert_primitive(isolate, context, value, primitive)) {
        return primitive;
    }

    ConvertedNode root;
    ExtractError err;
    bool ok = false;
    {
        py::gil_scoped_release release_gil;
        auto user_origin = access_user_scope(isolate);
        std::vector<v8::Local<v8::Object>> ancestors;
        ok = extract_deep_nogil(isolate, context, value, 0, ancestors, root, err);
    }

    if (!ok) {
        if (err.kind == ExtractFailureKind::kRuntimeError) {
            throw std::runtime_error(err.message);
        }
        throw ConversionError(err.message);
    }

    py::object js_undefined_singleton =
        py::module_::import("iv8").attr("JSUndefined");
    return materialize_py_node(root, js_undefined_singleton);
}

}  // namespace iv8
