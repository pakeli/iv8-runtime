# Host Access Trace

Status: **VERIFIED — native get/set/call plus HostObject missing-property
probes green on real V8 Linux + Windows (`2026-10-30`)**

This document freezes steps 1–4 of
`docs/host_access_tracing_plan.md`: native HostObject get/set/call tracing and
string-key missing-property probes. It is a second-layer monitoring feature. It
does not change the M1 `JSContext` contract, the existing `watch_apis` /
Reflect / Object.keys channels, or any DOM/BOM behavior.

## 1. Entry-point contract

The only operations covered by this slice are the generic callbacks in
`src/core/host_object.cpp`:

| Callback | What it proves |
|---|---|
| `property_getter` | A script read a property registered in `HostObject::property_names()`. |
| `property_setter` | A script wrote a property registered in `HostObject::writable_property_names()`. |
| `method_callback` | A script invoked a method registered in `HostObject::method_names()`. |
| `named_property_getter` | A script read a missing string key after normal lookup; recorded as `get_probe`. |

This is **not** a V8-wide property-access hook. It proves that a native host
entry point ran; it does not prove every `obj.x` expression was observed.

The event is written before the concrete host implementation runs. Its
`phase` is therefore `"enter"`; a later implementation error or an ignored
setter does not retroactively remove the event.

### Acceptance table

| JavaScript behavior | First slice |
|---|---|
| `navigator.userAgent` / `navigator['userAgent']` | records `get` |
| `element.textContent = 'abc'` | records `set`, where the property is declared writable |
| `document.querySelector('#app')` | records `call` |
| `const fn = document.querySelector` | records neither `get` nor `call` |
| `navigator.someMissingProperty` | records `get_probe` with `resolution: "missing"` for a HostObject string key |
| assignment to a read-only host property | no event; original write behavior/error unchanged |
| `window.innerWidth` | not covered in this slice; step 5 |
| `'x' in navigator`, `delete`, enumeration, descriptor queries | not covered in this slice; step 6 |
| `const obj = {x: 1}; obj.x` | not covered; user-created plain objects are a separate project |
| destructuring/spread | only if it actually executes a covered native getter; no broader syntax guarantee |

## 2. HostObject inventory

| Host object | Native class | Install / identity |
|---|---|---|
| `hostProbe` | `HostProbe` | Internal M2 probe; installed as a global. |
| `console` | `ConsoleHost` | Installed as a global. |
| `navigator` | `NavigatorHost` | Installed as a global; properties only. |
| `screen` | `ScreenHost` | Installed as a global; properties only. |
| `location` | `LocationHost` | Installed as a global; `toString()` is its only method. |
| `document` | `DocumentHost` | Installed as a global; property getters and methods. |
| element wrappers | `ElementHost` | Lazily created and cached per backing `DomNode`; not a global. Fresh JS wrappers can represent the same native backing. |
| — | `EventTargetHost` | Abstract/base plumbing. |
| — | `WindowEventTarget` | Listener store only; never installed. |
| — | `FileReaderEvents` | Listener store only; never installed. |

The inventory above was audited for this slice. Any later `HostObject` subclass
or `make_host_object` path must either be added to this table or explicitly
listed as not covered.

## 3. Identity and labels

- `object_id` is a Page-level monotonic number assigned to the native
  `HostObject` backing, not to a temporary JS wrapper. Re-reading one element
  through different JS wrappers keeps the same ID; different backing nodes get
  different IDs.
- IDs are scoped to a generation. A new `load()` generation clears the backing
  map; IDs are never reused.
- `object_type` is the native category: `host_probe`, `console`, `navigator`,
  `screen`, `location`, `document`, `element`, or the fallback `host_object`.
- `label` is a display hint. Global hosts use their global name; elements use
  their native HTML tag only. Labels must not be built by reading JS `id`,
  text, attributes, or other properties.

## 4. Public API and report

```python
page.start_access_trace(max_events=10000, object_types=None)
page.stop_access_trace()
report = page.read_access_trace(clear=True)
```

- `start` begins a new session: clears prior events/drops, resets `seq`, sets
  the capacity and optional object-type filter, and enables recording.
- `stop` stops new recording but retains existing events and configuration.
- `read(clear=False)` returns a snapshot without clearing it; `clear=True`
  clears events and `dropped_events` but preserves sequence, configuration,
  generation, and identity mappings.
- `start`/`stop`/`read` reject after `Page.dispose()` with
  `JSContextDisposedError`.
- `max_events` is a positive integer; `bool` is invalid. `object_types` is
  `None` or a list of supported object-type strings; duplicates are deduped and
  unknown types raise `ValueError`. An empty list is valid and records nothing.
- Capacity is earliest-N: once full, later events are dropped and
  `dropped_events` increments. Filtered-out events do not increment drops.
- `seq` starts at 1 for each `start()` and increments only for retained events.
- Configuration and recorded events are Page-level; `load()` does not clear
  them. Old events retain their old generation; object IDs for a new generation
  start from fresh backing identities.

Event shape:

```text
seq, generation, object_id, object_type, label, member,
operation, phase, resolution?, resource_name, source_kind
```

- `member` is the property or method name as registered by the HostObject.
- `operation` is `get`, `set`, `call`, or `get_probe`.
- `phase` is always `enter` in this slice.
- `resolution` is present only for `get_probe`; current value is `"missing"`.
  Present/unknown classification is not emitted.
- `source_kind` is `eval`, `html_script`, `internal`, or `unknown`.

Report shape:

```python
{
    "events": [event, ...],
    "enabled": bool,
    "generation": int,
    "max_events": int,
    "dropped_events": int,
    "filter_object_types": list[str] | None,
    "coverage": {
        "scope": "installed HostObject getter/setter/method/get_probe callbacks",
        "object_types": [...],
        "operations": ["get", "set", "call", "get_probe"],
        "phase": "enter",
        "not_covered": [...],
    },
}
```

## 4.1 Missing-property probes (step 4)

Host-object templates install a named getter with `kNonMasking` and
`kOnlyInterceptStrings`. A no-op named query handler keeps `in`,
`hasOwnProperty`, `Object.keys`, and descriptor queries on the normal path. The
getter also checks real own/prototype properties without invoking getters before
recording anything; a Proxy or host-object prototype is treated conservatively
as present, so it cannot produce a false missing verdict.

When the property is absent, the event is:

```text
operation: "get_probe", resolution: "missing", phase: "enter"
```

The interceptor returns `Intercepted::kNo`, so V8 performs the normal lookup and
the script still receives `undefined` (or the normal prototype result). It does
not create placeholders, proxies, or return values. The current implementation
emits only `resolution: "missing"`; it does not emit `present` or `unknown`
events for ordinary successful reads. Numeric indices, Symbols, non-HostObject
objects, and `in` / delete / enumeration / descriptor operations remain outside
this slice.

## 5. Source attribution

- `eval` — `Page.eval`, direct context evaluation, and host-provided
  `scripts=[...]` (host scripts are not distinguished from direct eval in this
  slice).
- `html_script` — inline/external HTML script execution through
  `PageState::run_html_script`.
- `internal` — generation bootstrap during `install_page`.
- `unknown` — timer/microtask/rAF/idle callbacks, lifecycle/event dispatch after
  the originating eval, DevTools evaluation, and `to_py` result conversion.

The source scope ends before result conversion. A conversion-triggered native
getter is `unknown`, not attributed to the script that produced the value.

## 6. Interaction with existing channels

`watch_apis` remains an exact host-method whitelist with its own log.
`watch_reflect_get` / `has` / `ownKeys` and `watch_object_keys` remain their
separate existing channels. The access trace does not call or replace them.

`Reflect.get(navigator, 'userAgent')` can produce one old Reflect hit and one new
native `get` event. These are independent evidence layers, not a duplicate
inside the new channel. A traced native method call is recorded once in the new
channel even if the old manual `watch_record` site also records the call in the
old channel.

## 7. Not covered in this first slice

- Intrinsic `window`/global data properties (`innerWidth`, `outerWidth`,
  `name`, `parent`, `visualViewport`, etc.).
- Plain/bootstrapped BOM objects: `performance`, `crypto`, Storage,
  `navigator.storage`, `navigator.permissions`, `navigator.plugins` /
  `mimeTypes`, `matchMedia` results, nested `connection` / `orientation` /
  `classList` / `validity` objects.
- Arrays and other plain values returned from host getters/methods.
- User-created plain JS objects, object literals, class instances, destructuring
  beyond covered getter execution, Proxy/AST instrumentation, and explicit
  object-level hooks.
- Method-reference reads (`const fn = document.querySelector`), `in`, delete,
  enumeration, property descriptors, dynamic defineProperty/shadowing behavior,
  and missing-property probes on non-HostObject objects (including numeric keys
  and Symbols).
- Read-only write attempts that V8 rejects before reaching a native setter.
- Arguments, return values, stacks, timing, Inspector pauses /
  `break_on_hit`, and DevTools evaluation attribution.
- Any JS-visible global or new top-level Python export; any M1 `JSContext`
  behavior change.

Step 5 expands runtime-owned plain BOM objects and intrinsic window metadata;
step 6 adds method-reference reads, `in`, delete, enumeration, descriptors, and
value/argument/return summaries. Neither is implied by this slice.

## 8. Verification

Verified on real V8 at commit `8995b89` (branch `host-access-probes`):

- Linux `release-linux` run `41017981567` — cp311 / cp312 / cp313 / cp314,
  each `1656 passed, 2 skipped`;
- Windows `release-windows` run `41017987566` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1656 passed, 2 skipped`.

`test/test_access_trace_api.py` (9 items),
`test/test_access_trace_runtime.py` (11 items), and
`test/test_access_trace_probes.py` (10 items) ran in every lane. The full suite
collects 1658 items; 1656 pass and 2 are skipped.

The initial probe attempt (`76baaa2`) showed that V8 invokes the named getter
for query-only operations and some existing-property paths. `8995b89` added the
no-op named query callback and the real own/prototype property guard; the
interceptor still returns `kNo` so normal lookup and descriptors are unchanged.

Coverage in this slice:

- get/set/call for all installed `HostObject` properties/methods, including
  `navigator.userAgent`, `screen.width`, `location.toString()`, `document`
  queries, element properties/methods, and `console` methods;
- missing string-key reads on those hosts produce exactly one
  `get_probe`/`resolution: "missing"` event;
- own properties with value `undefined`, prototype properties, existing
  properties, `in`, `hasOwnProperty`, `Object.keys`, descriptors, Symbols, and
  numeric reads do not produce missing probes;
- read-only writes and method-reference reads stay untraced;
- source attribution: `eval`, inline HTML script, internal bootstrap, and
  unknown (timer/Promise callback) evidence;
- native object IDs remain stable across fresh element wrappers and reset with
  a new generation;
- capacity, dropped-event counts, object-type filters, read-and-clear, stop,
  multiple Pages, and disposal behavior are covered by tests.

The known blind spots listed in §7 remain intentionally out of scope. Step 5
(runtime-owned BOM objects/window metadata) is the next slice; step 6 covers the
remaining operations and value/argument/return summaries.
