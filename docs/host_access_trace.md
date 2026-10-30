# Host Access Trace

Status: **IN PROGRESS — boundary frozen; implementation pending (`2026-10-30`)**

This document freezes the first slice of
`docs/host_access_tracing_plan.md`: native HostObject get/set/call tracing. It is
a second-layer monitoring feature. It does not change the M1 `JSContext`
contract, the existing `watch_apis` / Reflect / Object.keys channels, or any
DOM/BOM behavior.

## 1. Entry-point contract

The only operations covered by this slice are the generic callbacks in
`src/core/host_object.cpp`:

| Callback | What it proves |
|---|---|
| `property_getter` | A script read a property registered in `HostObject::property_names()`. |
| `property_setter` | A script wrote a property registered in `HostObject::writable_property_names()`. |
| `method_callback` | A script invoked a method registered in `HostObject::method_names()`. |

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
| `navigator.someMissingProperty` | not covered in this slice; step 4 |
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

The current implementation must be audited before this boundary is frozen for
verification: any new `HostObject` subclass or new `make_host_object` path must
either be added to this table or explicitly listed as not covered.

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
operation, phase, resource_name, source_kind
```

- `member` is the property or method name as registered by the HostObject.
- `operation` is `get`, `set`, or `call`.
- `phase` is always `enter` in this slice.
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
        "scope": "installed HostObject getter/setter/method callbacks",
        "object_types": [...],
        "operations": ["get", "set", "call"],
        "phase": "enter",
        "not_covered": [...],
    },
}
```

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
- Method-reference reads (`const fn = document.querySelector`), missing-property
  probes, `in`, delete, enumeration, property descriptors, and dynamic
  defineProperty/shadowing behavior.
- Read-only write attempts that V8 rejects before reaching a native setter.
- Arguments, return values, stacks, timing, Inspector pauses /
  `break_on_hit`, and DevTools evaluation attribution.
- Any JS-visible global or new top-level Python export; any M1 `JSContext`
  behavior change.

Step 4 of the implementation plan will address missing-property probes only
after the named-property-interceptor behavior is validated on the pinned V8
version. Step 5 expands runtime-owned plain BOM objects; step 6 adds the other
operations. Neither is implied by this slice.

## 8. Verification

Focused tests will cover API shape/validation, get/set/call recording, resource
and source attribution, object identity, generation/load behavior, capacity and
filters, coexistence with the old channels, and error/read-only behavior. Real
V8 Linux and Windows release workflows must run the new trace tests in every
wheel lane. This status changes to **VERIFIED** only after those runs are green.
