cmake_minimum_required(VERSION 3.15)

# iv8 native extension. V8-linked builds provide the JSContext execution kernel,
# recursive conversion / JSValue, and the hosted Page runtime with monitoring.
# V8-free builds expose metadata and the Python API's unavailable-runtime guards.

project(iv8 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# The pinned V8 revision comes from a SEPARATE configuration source than the
# Python package version (which lives in pyproject.toml). This file only sets
# CMake cache/normal variables; it does not fetch or build V8.
include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/v8_pin.cmake)

# --- IV8_LINK_V8: whether to link the pinned V8 monolith --------------------
# Default: ON on non-Windows when the pinned monolith artifact is present under
# data/v8 (produced by the platform's tools/build_v8_* script); OFF otherwise
# (Windows, or when no artifact has been built). No fuzzy/system V8 discovery is
# ever performed.
if(WIN32)
  set(_iv8_v8_lib "${CMAKE_CURRENT_SOURCE_DIR}/data/v8/lib/v8_monolith.lib")
else()
  set(_iv8_v8_lib "${CMAKE_CURRENT_SOURCE_DIR}/data/v8/lib/libv8_monolith.a")
endif()
if(EXISTS "${_iv8_v8_lib}")
  set(_iv8_link_default ON)
else()
  set(_iv8_link_default OFF)
endif()
option(IV8_LINK_V8 "Link the pinned V8 monolith and enable EngineRuntime"
       ${_iv8_link_default})
message(STATUS "iv8: IV8_LINK_V8=${IV8_LINK_V8}")

find_package(pybind11 CONFIG REQUIRED)

set(_iv8_sources src/module.cpp)
if(IV8_LINK_V8)
  list(APPEND _iv8_sources
      src/core/engine_runtime.cpp
      src/core/isolate_host.cpp
      src/core/context_host.cpp
      src/core/value_converter.cpp
      src/core/js_exception.cpp
      src/core/js_value.cpp
      src/core/host_object.cpp
      src/core/web_method.cpp
      src/core/web_interface.cpp
      src/core/access_trace.cpp
      src/core/access_summary.cpp
      src/core/selector_parser.cpp
      src/core/cookie_jar.cpp
      src/core/url_parser.cpp
      src/core/dom_url.cpp
      src/core/browser_compatibility.cpp
      src/core/query_parameters.cpp
      src/core/base64.cpp
      src/core/html_entities.cpp
      src/core/page_state.cpp
      src/core/browser/host/document_host.cpp
      src/core/browser/host/document_tree_mutation.cpp
      src/core/browser/host/live_collection.cpp
      src/core/browser/host/mutation_observer.cpp
      src/core/browser/host/class_list.cpp
      src/core/browser/internal/text.cpp
      src/core/browser/dom/dom_node.cpp
      src/core/browser/dom/html_parser.cpp
      src/core/browser/dom/html_serializer.cpp
      src/core/browser/dom/dom_query.cpp
      src/core/browser/dom/selector_match.cpp
      src/core/browser/dom/form_controls.cpp
      src/core/browser/bom/web_storage.cpp
      src/core/browser/bom/navigator_parts.cpp
      src/core/browser/bom/media_query.cpp
      src/core/browser/web/performance.cpp
      src/core/browser/web/structured_clone.cpp
      src/core/browser/web/capabilities.cpp
      src/core/browser/web/event_constructors.cpp
      src/core/browser/web/url.cpp
      src/core/browser/bootstrap/bootstrap_scripts.cpp
      src/core/browser/internal/event_support.cpp
      src/core/browser/internal/common.cpp
      src/core/browser/internal/trace_hooks.cpp
      src/core/browser/web/crypto.cpp
      src/core/browser/web/base64_globals.cpp
      src/core/browser/web/encoding.cpp
      src/binding/context_binding.cpp
      src/binding/page_binding.cpp)
endif()

set_source_files_properties(
    src/core/browser/internal/text.cpp
    src/core/browser/dom/dom_node.cpp
    src/core/browser/dom/html_parser.cpp
    src/core/browser/dom/html_serializer.cpp
    src/core/browser/dom/dom_query.cpp
    src/core/browser/dom/selector_match.cpp
    src/core/browser/dom/form_controls.cpp
    PROPERTIES SKIP_PRECOMPILE_HEADERS ON)

pybind11_add_module(_core ${_iv8_sources})

target_include_directories(_core PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_features(_core PRIVATE cxx_std_20)

# Pinned V8 metadata (build-time), reported by the module regardless of linking.
target_compile_definitions(_core PRIVATE
    IV8_PINNED_V8_VERSION="${IV8_PINNED_V8_VERSION}"
    IV8_PINNED_V8_COMMIT="${IV8_PINNED_V8_COMMIT}")

if(IV8_LINK_V8)
  include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/v8_link.cmake)
  iv8_link_v8_monolith(_core)

  # Precompile the two heavy header sets every V8-linked TU includes. Order
  # matches the codebase convention: pybind11 (Python.h) first, then V8 through
  # v8_headers.h, which undefines Python's COMPILER macro. Added ahead of the
  # page_state.cpp split (docs/page_state_split_plan.md §3.7), which multiplies
  # the number of TUs. Disable with -DIV8_PRECOMPILE_HEADERS=OFF.
  option(IV8_PRECOMPILE_HEADERS "Precompile pybind11 and V8 headers" ON)
  if(IV8_PRECOMPILE_HEADERS AND NOT CMAKE_VERSION VERSION_LESS 3.16)
    target_precompile_headers(_core PRIVATE
        <pybind11/pybind11.h>
        "${CMAKE_CURRENT_SOURCE_DIR}/include/iv8/v8_headers.h")
  endif()

  # Bundle V8's license(s) into the wheel next to the package (required to
  # redistribute the statically-linked V8). Present only in V8-linked builds.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/data/v8/licenses")
    install(DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/data/v8/licenses/"
            DESTINATION iv8/licenses)
  endif()
endif()

# Install the extension into the iv8 package directory inside the wheel.
install(TARGETS _core DESTINATION iv8)
