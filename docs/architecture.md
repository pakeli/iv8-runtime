# iv8-runtime M1 Architecture

> **Scope note.** This document describes the **M1 execution kernel** and remains the
> source of truth for ownership and lifecycle (process-wide V8 state, one isolate per
> `JSContext`, disposal order, the GIL boundary, the failure model). Its **§2
> Non-Goals list is historical**: the implementation at `HEAD` has since built the
> `Page` runtime, a minimal DOM, timers, and Inspector/monitoring features on top of
> the extension points in §11 — read §2 as "excluded from M1", not "absent from the
> project". For the current surface see `docs/runtime_surface.md`.

## 1. Purpose

M1 establishes a small, stable Python-to-V8 execution kernel. It is intentionally narrower than iv8 and does not emulate a browser.

M1 provides:

- process-safe V8 initialization;
- one isolated V8 runtime per Python `JSContext`;
- JavaScript compilation and execution;
- controlled JavaScript-to-Python value conversion;
- structured JavaScript exception translation;
- deterministic and idempotent disposal;
- parallel execution across independent contexts.

The architecture leaves explicit extension points for later browser runtime work without implementing those features prematurely.

## 2. Non-Goals

M1 does not provide:

- `window`, `document`, `navigator`, `location`, or DOM objects;
- HTML parsing or `page.load`;
- timers, event loops, logical time, or microtask control APIs;
- XHR, fetch, WebSocket, or resource loading;
- Python functions callable from JavaScript;
- DevTools, Inspector, API monitoring, or fingerprint simulation.

## 3. Technology Baseline

- Language: C++20.
- Python binding: pybind11.
- Native build: CMake.
- Python build backend: scikit-build-core.
- Tests: pytest.
- V8: one explicitly pinned stable release.
- Python baseline: Python 3.9 or newer, subject to the selected toolchain.

The exact V8 revision belongs in build configuration and release metadata. It must not be inferred from locally installed system libraries.

STPyV8 is the historical interoperability reference for the project. Its useful lessons are documented in `stpyv8_reference.md`; M1 intentionally reimplements the ownership, GIL, and error boundaries with pybind11 rather than inheriting its Boost.Python surface or callback model.

## 4. Layered Model

```text
Python user code
    |
    v
python/iv8 public package
    |
    v
pybind11 binding layer
    |
    +--> JSContext API validation
    +--> Python exception registration
    |
    v
native core
    |
    +--> EngineRuntime       process-wide V8 platform
    +--> IsolateHost         one isolate per JSContext
    +--> ContextHost         one V8 context and evaluation entry
    +--> ValueConverter      V8 values to Python values/wrappers
    +--> JsException         TryCatch to structured error data
```

The binding layer must not own V8 lifecycle rules. It delegates to the native core and translates native results into the public Python API.

## 5. Planned Repository Layout

```text
iv8-runtime/
├── AGENTS.md
├── CMakeLists.txt                 # created during implementation
├── pyproject.toml                 # created during implementation
├── docs/
│   ├── architecture.md
│   ├── api_contract.md
│   ├── implementation_plan.md
│   └── test_plan.md
├── include/iv8/
│   ├── engine_runtime.h
│   ├── isolate_host.h
│   ├── context_host.h
│   ├── value_converter.h
│   └── js_exception.h
├── src/
│   ├── binding/
│   │   ├── module.cpp
│   │   └── context_binding.cpp
│   └── core/
│       ├── engine_runtime.cpp
│       ├── isolate_host.cpp
│       ├── context_host.cpp
│       ├── value_converter.cpp
│       └── js_exception.cpp
├── python/iv8/
│   ├── __init__.py
│   ├── context.py
│   └── errors.py
├── test/
│   ├── test_context.py
│   ├── test_conversion.py
│   ├── test_exceptions.py
│   └── test_threading.py
└── data/
```

Only the documents and `AGENTS.md` are created during the current design task.

## 6. Core Components

### 6.1 EngineRuntime

`EngineRuntime` owns process-wide V8 infrastructure.

Responsibilities:

- initialize ICU and V8 startup data as required by the pinned build;
- create and own the V8 platform;
- call V8 process initialization exactly once;
- expose the pinned V8 version;
- coordinate safe process shutdown when supported by the embedding model.

Constraints:

- initialization must be thread-safe;
- it must not own a user context or isolate;
- normal `JSContext.dispose()` must not shut down process-wide V8 state;
- shutdown order must prevent platform destruction while isolates exist.

Preferred lifetime is lazy initialization on first `JSContext` creation followed by process-long ownership using `std::call_once` or an equivalent controlled singleton.

### 6.2 IsolateHost

Each Python `JSContext` owns one `IsolateHost`.

Responsibilities:

- allocate and own `v8::ArrayBuffer::Allocator`;
- create and own one `v8::Isolate`;
- configure required isolate callbacks;
- provide scoped isolate entry;
- dispose isolate-owned resources in the correct order.

Ownership:

```text
JSContext Python object
    -> native context holder
        -> ContextHost
        -> IsolateHost
            -> v8::Isolate
            -> ArrayBuffer allocator
```

Destruction order:

1. prevent new evaluation;
2. ensure no evaluation is active;
3. reset persistent context handles;
4. release isolate-scoped wrappers;
5. dispose the isolate;
6. release the allocator.

### 6.3 ContextHost

`ContextHost` owns the V8 execution context associated with one isolate.

Responsibilities:

- create and retain `v8::Global<v8::Context>`;
- enter the correct isolate and context scopes;
- compile and execute source text;
- preserve global state between `eval()` calls;
- reject evaluation after disposal;
- prevent concurrent use of the same context.

Every evaluation enters scopes in this conceptual order:

```text
context operation guard
v8::Locker, when required
v8::Isolate::Scope
v8::HandleScope
v8::Context::Scope
v8::TryCatch
compile
run
convert result
```

The concrete implementation may adjust locker placement, but it must document the selected model and test independent contexts from multiple Python threads.

### 6.4 ValueConverter

`ValueConverter` centralizes JavaScript-to-Python conversion.

Primitive conversions:

- `undefined` becomes the `iv8.JSUndefined` singleton;
- `null` becomes Python `None`;
- boolean becomes `bool`;
- exact integral Numbers become `int`;
- other Numbers become `float`;
- BigInt becomes Python `int`;
- String becomes Python `str`.

When `to_py=True`:

- Array becomes `list`;
- plain Object becomes `dict[str, object]`;
- maximum depth defaults to 64;
- cyclic references produce `JSConversionError`;
- functions, promises, proxies, typed arrays, dates, maps, sets, and host objects are outside M1 recursive conversion.

Object identity must be tracked during recursive traversal. A depth limit alone is insufficient for cycle detection.

### 6.5 JsException

`JsException` is a native structured record populated from `v8::TryCatch` and `v8::Message`.

Required fields:

- `name`;
- `message`;
- `stack`;
- `resource_name`;
- `line`;
- `column`.

Compile errors, runtime errors, disposed-context errors, busy errors, and conversion errors must remain distinguishable.

## 7. Public Python Object Model

### 7.1 JSContext

`JSContext` is a small Python-facing owner of the native context.

State model:

```text
created -> entered -> exited -> disposed
   |          |         |
   +----------+---------+--> disposed
```

Rules:

- construction creates native isolate and context resources;
- `__enter__()` returns the same context object;
- `__exit__()` disposes the context without suppressing user exceptions;
- `dispose()` is idempotent;
- evaluation after disposal raises a clear exception;
- nested entry is not required in M1;
- a context has at most one active operation.

### 7.2 JSUndefined

`JSUndefined` is a public singleton distinct from Python `None`.

Required behavior:

- stable singleton identity;
- readable `repr` and `str`;
- false boolean value;
- importable as `iv8.JSUndefined`.

### 7.3 Complex Results

For `to_py=False`, M1 returns a minimal opaque `JSValue` wrapper rather than silently converting arbitrary objects.

The wrapper:

- is tied to its owning `JSContext`;
- becomes unusable after context disposal;
- never exposes raw V8 pointers;
- does not promise property access or function invocation;
- provides type inspection and explicit recursive conversion.

If safe wrapper ownership proves too risky for M1, the API contract must be revised before implementation to use a documented unsupported-type error.

## 8. GIL and Threading Model

### 8.1 Independent Context Parallelism

Different Python threads may execute different `JSContext` instances concurrently. Each context owns its isolate, V8 context, allocator, and operation guard. No persistent handles or JavaScript globals are shared.

### 8.2 Same-Context Concurrency

M1 does not support simultaneous operations on the same context. The preferred behavior is to reject a second operation with `JSContextBusyError` rather than silently serialize it.

### 8.3 GIL Boundaries

The Python GIL may be released only after Python arguments have been validated and copied into native storage.

It remains released while V8 compiles and executes JavaScript. It must be reacquired before:

- creating Python return values;
- translating native errors to Python exceptions;
- touching Python-owned wrapper state.

M1 contains no JavaScript-to-Python callbacks, so V8 execution must not invoke Python while the GIL is released.

## 9. Failure Model

Required failure categories:

- `JSError`: JavaScript compile or runtime failure;
- `JSConversionError`: unsupported type, depth limit, invalid key, or cyclic conversion;
- `JSContextDisposedError`: operation attempted after disposal;
- `JSContextBusyError`: overlapping operation on the same context;
- native initialization failure: module import or construction error with actionable details.

C++ destructors must not propagate exceptions. Disposal errors should be surfaced through explicit operations where practical.

## 10. Security and Resource Limits

M1 is an embedding API, not a security sandbox.

Documentation must state:

- arbitrary JavaScript can consume CPU and memory;
- isolate separation is not operating-system process isolation;
- hard execution deadlines and memory quotas are not M1 guarantees;
- untrusted code should eventually use explicit termination/resource policies or a separate process.

Future limits must be designed around isolate creation constraints rather than mutable global limits.

## 11. Extension Points

M1 leaves these seams without implementing them:

- `RuntimeState` owned by each isolate/context pair;
- global-template installation before context creation;
- native object wrapper registry;
- Python callable bridge registry;
- Inspector/debug hooks;
- event loop and resource-store ownership.

Expected future ownership:

```text
JSContext
  -> IsolateHost
  -> ContextHost
  -> RuntimeState
       -> EnvironmentState
       -> LocationState
       -> DocumentState
       -> EventLoop
       -> ResourceStore
       -> NetLog
```

M1 must not create empty implementations of those future modules.

## 12. Architecture Acceptance Criteria

The M1 architecture is correctly realized when:

- process-wide and context-owned resources have unambiguous ownership;
- every context has a unique isolate;
- disposal is deterministic and idempotent;
- independent contexts can run in parallel;
- same-context concurrency has deterministic failure behavior;
- conversion and exception handling are centralized;
- the binding layer contains no substantial execution logic;
- tests cover lifecycle and failure transitions;
- no browser-runtime feature has leaked into M1.


## 13. Page interface registry and native document ownership

The approved Web IDL repair on `2026-11-07` adds a context-private registry for
Page's interface constructors, prototypes and native member identities. Registry
objects are reachable only from V8 private slots in their owning context. Their
callback data identifies the defining interface. Native backing fields validate
receivers independently of the mutable JavaScript prototype chain.

PageState owns standalone EventTarget and Document backings alongside the loaded
document for the current generation. DocumentHost derives from NodeHost and owns
its root DomNode, canonical wrapper and all descendant storage. Root children use
the same vector as the document node. Each document releases its wrapper and
listener persistent handles through the existing isolate-alive teardown hook.
A reload invalidates the complete context and its registry before replacing
native backings. The operation guard and GIL boundary apply to all constructor
and method callbacks. `docs/webidl_environment_semantics.md` defines the approved
surface and capability behavior.

## 14. Page runtime source layout (page_state.cpp split)

The `2026-11-08` structural split (`docs/page_state_split_plan.md`, progress in
`docs/page_state_split.md`) moves the Page runtime out of the single
`src/core/page_state.cpp` translation unit into `src/core/browser/`. It does not
change behavior, and the process/isolate/Page/monitoring separation above is
unchanged.

- Internals live in namespace `iv8::browser`, formerly an anonymous namespace.
  Helpers used by a single file stay in that file's own anonymous namespace.
- `src/core/browser/internal/common.h` (private, not under `include/`) holds the
  shared V8 helpers and the access-trace identity tokens. The tokens are
  `inline constexpr` so their addresses stay unique program-wide.
  `PageState`'s constructor verifies this across translation units.
- V8-linked builds precompile pybind11 and `v8_headers.h`
  (`IV8_PRECOMPILE_HEADERS`).

At the end of stage 0, `page_state.cpp` still holds all runtime code apart from
`internal/common.*`. Later stages move leaf modules, the DOM data layer, host
objects and monitoring, then split `install_page`.

Stage 0 acceptance is recorded on source `973ac99` in `docs/page_state_split.md`.
Linux and Windows used hosted CI. macOS arm64 used the same four-version
build/install/test matrix locally after hosted runner capacity failures. All
12 lanes passed with `2090 passed, 2 skipped, 1 xfailed`.

Stage 1.1 source `a3dcb20` extracts `web/crypto`, `web/base64_globals` and
`web/encoding`. Shared ASCII normalization is defined in `internal/common.cpp`
and watch/access recording in `internal/trace_hooks.cpp`. Private headers expose
callbacks to the existing Page installer. Platform entropy headers and the OS
RNG stay with the crypto module. Local full real-V8 tests and the required
Linux/Python 3.13 V8 check passed under the per-PR policy.

Complete stage 1 source `f5dbea5` also extracts URL, structured clone, performance,
event/capability callbacks, BOM hosts and the three unchanged JS bootstrap
scripts. Small host classes retain their inline definitions in private headers.
`PerformanceStore` and `MediaQueryListStore` keep their existing `iv8` types and
Page ownership. Shared non-DOM event helpers live in `internal/event_support`.
Extern constant declarations preserve one definition for cross-file identities.
The installer retains its original sequence and bootstrap array sizes. All 12
Linux/Windows/macOS release lanes passed; see `docs/page_state_split.md`.

Stage 2 moves DOM node data, parsing/serialization, queries/selectors and form
control helpers into private `browser/dom` modules. They depend only on the C++
standard library and existing V8-free selector/entity utilities. Case conversion
is defined in `internal/text`, included by both pure modules and `common.h`.
The unchanged `iv8::NodeKind` enum lives in `include/iv8/node_kind.h` so the data
layer does not include `HostObject` or V8; `host_object.h` includes it transitively.
Node pools and JS wrapper lifetimes remain owned by Page/DocumentHost. The seven
pure translation units opt out of PCH and compile without V8/Python headers.

Stage 2 source `be5bdb4` passed standalone strict compilation, 20,000 DOM
differential cases under UBSan, reflection checks and all 12 hosted native
release lanes. Stage 3 was authorized by the user on `2026-11-14`.

Step 3.1 moves the unchanged event-target classes into private
`browser/host/event_target.h`. Listener handle ownership and teardown remain
unchanged; shared event helpers stay in `internal/event_support` for web callers.

Step 3.3 separates DocumentHost's declaration/data layout from its 45 inline
member implementations. Binding/observer and FileReader state definitions remain
in the host layer, preserving generation ownership and avoiding an upward host
dependency on web callbacks. The member-aware audit gates both the declaration
contract and qualified method implementations; the runtime binding and teardown
contracts remain unchanged.

The remaining stage 3 sources separate live collections/classList/observers,
document-tree mutations and element/character-data/fragment implementations.
Screen and WindowEventTarget retain their original inline class bodies in private
headers. Window event/message callbacks, functions and metadata live in `window`,
while Blob/FileReader callbacks live in `web`. All use the same original V8
binding creation paths and Page-owned resources. Window metadata member definitions
move files while their state stays in PageState. The installer, JS bootstraps and
monitoring remain under their existing owners; monitoring extraction is stage 4.

Complete stage 3 source `2f02535` passed all 12 hosted native release lanes.
The full member-aware audit, standalone compilation and 29 object graphs
validate the unchanged ownership and binding contracts. Source/tree, run and
artifact evidence is in `docs/page_state_split.md`. Stage 4 remains next.
