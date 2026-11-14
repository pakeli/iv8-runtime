#pragma once

#include <algorithm>
#include <cctype>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "../internal/common.h"
#include "iv8/page_state.h"
#include "iv8/host_object.h"
#include "iv8/access_summary.h"
#include "iv8/web_interface.h"
#include "iv8/browser_compatibility.h"
#include "iv8/access_summary.h"

namespace py = pybind11;

namespace iv8 {

namespace browser {

// M2-2/M11-28 minimal `console`. A host object exposing the classic logging
// methods plus debug/trace/table/dir/group/groupEnd/time/timeEnd/assert/count.
// Arguments are stringified with JS's own ToString (minimal, deterministic; NOT
// browser console format-string compatible) and joined by a single space. The
// message lands in Python `logging` on the "iv8.console" logger:
//   log/info/table/dir/group/time -> INFO ; debug/trace -> DEBUG ;
//   warn -> WARNING ; error/assert -> ERROR.
// There is no clock, stack capture, table formatting, group nesting, or timing.
// The callback runs during eval with the GIL released, so it reacquires the GIL
// only for the logging call and never lets a Python error escape into V8.
class ConsoleHost : public HostObject {
public:
    std::string global_name() const override { return "console"; }
    std::string trace_object_type() const override { return "console"; }

    std::vector<std::string> property_names() const override { return {}; }

    std::vector<std::string> method_names() const override {
        return {"log",   "info",  "warn",  "error", "debug",
                "trace", "table", "dir",   "group", "groupEnd",
                "time",  "timeEnd", "assert", "count"};
    }

    v8::Local<v8::Value> get_property(v8::Isolate* isolate, v8::Local<v8::Context>,
                                      const std::string&) override {
        return v8::Undefined(isolate);  // console has no data properties
    }

    v8::Local<v8::Value> call_method(
        v8::Isolate* isolate, v8::Local<v8::Context> context,
        const std::string& name,
        const v8::FunctionCallbackInfo<v8::Value>& args) override {
        if (name == "groupEnd") {
            return v8::Undefined(isolate);  // no nesting state in this slice
        }
        if (name == "assert") {
            const bool condition =
                args.Length() > 0 && args[0]->BooleanValue(isolate);
            if (condition) {
                return v8::Undefined(isolate);
            }
            const std::string detail =
                join_args(isolate, context, args, 1);
            log_message("error", detail.empty()
                                     ? std::string("Assertion failed")
                                     : "Assertion failed: " + detail);
            return v8::Undefined(isolate);
        }
        if (name == "count") {
            const std::string label =
                args.Length() > 0
                    ? join_args(isolate, context, args)
                    : std::string("default");
            const int next = ++counts_[label];
            log_message("info", label + ": " + std::to_string(next));
            return v8::Undefined(isolate);
        }
        if (name == "time" || name == "timeEnd") {
            const std::string label =
                args.Length() > 0
                    ? join_args(isolate, context, args)
                    : std::string("default");
            log_message("info",
                        (name == "time" ? "Timer started: "
                                        : "Timer ended: ") + label);
            return v8::Undefined(isolate);
        }
        const std::string message = join_args(isolate, context, args);
        const char* level = "info";  // log, info, table, dir, group
        if (name == "debug" || name == "trace") {
            level = "debug";
        } else if (name == "warn") {
            level = "warning";
        } else if (name == "error") {
            level = "error";
        }
        log_message(level, message);
        return v8::Undefined(isolate);
    }

private:
    static void log_message(const char* level, const std::string& message) {
        py::gil_scoped_acquire gil;
        try {
            py::module_::import("logging")
                .attr("getLogger")("iv8.console")
                .attr(level)(py::str(message));
        } catch (const py::error_already_set&) {
            // A logging failure must never break JS execution; swallow it
            // (the GIL is held here, so clearing the Python error is safe).
        }
    }

    // Minimal deterministic stringification: JS ToString per argument, joined by
    // a single space. A throwing toString is swallowed (console stays
    // non-throwing) and rendered as a fixed placeholder. `start` supports
    // console.assert, which excludes the condition argument.
    static std::string join_args(v8::Isolate* isolate,
                                 v8::Local<v8::Context> context,
                                 const v8::FunctionCallbackInfo<v8::Value>& args,
                                 int start = 0) {
        std::string message;
        for (int i = start; i < args.Length(); ++i) {
            if (i > start) {
                message += ' ';
            }
            v8::TryCatch try_catch(isolate);
            v8::Local<v8::String> str;
            if (args[i]->ToString(context).ToLocal(&str)) {
                v8::String::Utf8Value utf8(isolate, str);
                if (*utf8 != nullptr) {
                    message.append(*utf8, static_cast<size_t>(utf8.length()));
                }
            } else {
                try_catch.Reset();
                message += "<unprintable>";
            }
        }
        return message;
    }

    // Per-console-instance count labels. ConsoleHost is recreated per generation,
    // so counts reset on Page.load(), matching the rest of the page state.
    std::unordered_map<std::string, int> counts_;
};

}  // namespace browser
}  // namespace iv8
