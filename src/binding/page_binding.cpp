#include <memory>
#include <string>
#include <utility>

#include <pybind11/pybind11.h>
#include <pybind11/stl.h>  // M9-2: list[str] <-> std::vector<std::string> for watch_apis

#include "iv8/page_state.h"

namespace py = pybind11;

// Registers the native Page type (M2-1). Kept thin: all lifecycle/host-object
// logic lives in iv8::PageState. Lifecycle errors reuse the exception
// translators registered by register_context (JSContextDisposedError /
// JSContextBusyError) since PageState delegates to the M1 ContextState.
void register_page(py::module_& module) {
    py::class_<iv8::PageState>(module, "Page")
        // M11-1: the host-environment baseline arrives as a dict that the Python
        // facade has ALREADY validated and fully resolved (every key present, every
        // value the right type), so this only unpacks it — no defaults, no merging,
        // and no re-validation here. A missing key would be a facade bug, so `cast`
        // is allowed to raise rather than silently substituting a value.
        .def(py::init([](const py::dict& environment) {
            iv8::HostEnvironment resolved;
            resolved.user_agent = environment["userAgent"].cast<std::string>();
            resolved.platform = environment["platform"].cast<std::string>();
            resolved.language = environment["language"].cast<std::string>();
            resolved.webdriver = environment["webdriver"].cast<bool>();
            resolved.screen_width = environment["screenWidth"].cast<int>();
            resolved.screen_height = environment["screenHeight"].cast<int>();
            resolved.avail_width = environment["availWidth"].cast<int>();
            resolved.avail_height = environment["availHeight"].cast<int>();
            resolved.device_pixel_ratio =
                environment["devicePixelRatio"].cast<double>();
            resolved.inner_width = environment["innerWidth"].cast<int>();
            resolved.inner_height = environment["innerHeight"].cast<int>();
            return std::make_unique<iv8::PageState>(std::move(resolved));
        }), py::arg("environment"))
        .def_property_readonly("disposed", &iv8::PageState::disposed)
        .def("eval", &iv8::PageState::eval, py::arg("source"), py::arg("to_py"),
             py::arg("name"))
        .def("dispose", &iv8::PageState::dispose)
        .def("run_timers", &iv8::PageState::run_timers)
        .def("run_jobs", &iv8::PageState::run_jobs)
        .def("load", &iv8::PageState::load, py::arg("html"), py::arg("base_url"))
        .def("dispatch_lifecycle_events",
             &iv8::PageState::dispatch_lifecycle_events)
        .def("html_scripts", &iv8::PageState::html_scripts)
        .def("run_html_script", &iv8::PageState::run_html_script, py::arg("index"),
             py::arg("code"), py::arg("name"))
        // M9-1 DevTools attach base (internal; public surface is Page.devtools_url()).
        .def("devtools_enable", &iv8::PageState::devtools_enable)
        .def("devtools_dispatch", &iv8::PageState::devtools_dispatch,
             py::arg("message"))
        // M9-2 watch-apis record面 (internal; public surface is Page.watch_apis /
        // Page.read_watch_api_hits — type validation done in the Python facade).
        .def("watch_apis", &iv8::PageState::watch_apis, py::arg("paths"),
             py::arg("break_on_hit"))
        .def("read_watch_api_hits", &iv8::PageState::read_watch_api_hits)
        // M9-3 internal pause probe (public surface stays Page.watch_apis only).
        .def("devtools_pause_count", &iv8::PageState::devtools_pause_count)
        // M9-4 Reflect.get monitoring (public surface is Page.watch_reflect_get /
        // Page.read_reflect_get_hits — bool validation done in the Python facade).
        .def("watch_reflect_get", &iv8::PageState::watch_reflect_get,
             py::arg("enabled"), py::arg("break_on_hit"))
        .def("read_reflect_get_hits", &iv8::PageState::read_reflect_get_hits)
        // M9-5 Reflect.has monitoring.
        .def("watch_reflect_has", &iv8::PageState::watch_reflect_has,
             py::arg("enabled"), py::arg("break_on_hit"))
        .def("read_reflect_has_hits", &iv8::PageState::read_reflect_has_hits)
        // M10-1 Reflect.ownKeys monitoring.
        .def("watch_reflect_ownkeys", &iv8::PageState::watch_reflect_ownkeys,
             py::arg("enabled"), py::arg("break_on_hit"))
        .def("read_reflect_ownkeys_hits",
             &iv8::PageState::read_reflect_ownkeys_hits)
        // M10-2 Object.keys monitoring.
        .def("watch_object_keys", &iv8::PageState::watch_object_keys,
             py::arg("enabled"), py::arg("break_on_hit"))
        .def("read_object_keys_hits", &iv8::PageState::read_object_keys_hits);
}
