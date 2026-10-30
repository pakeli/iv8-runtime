# Host Access Trace

Status: **VERIFIED — HostObject get/set/call + missing-property probes + native
BOM callback calls + window/nested metadata + static visualViewport + navigator
storage/permissions manager reads/calls green on real V8 Linux + Windows
(`2026-10-30`)**

This document freezes steps 1–4 and the first groups of step 5 of
`docs/host_access_tracing_plan.md`: native HostObject get/set/call tracing,
string-key missing-property probes, call/get coverage for independent native BOM
callbacks (performance, crypto, Storage), intrinsic window metadata reads,
nested `navigator.connection` / `screen.orientation` / `performance.timeOrigin`
reads, the static `visualViewport` HostObject, the `navigator.storage`
manager's parent read plus method calls, and `navigator.permissions` parent
reads plus query calls and returned PermissionStatus accesses. It is a
second-layer monitoring feature. It does not change the M1 `JSContext` contract,
the existing `watch_apis` / Reflect / Object.keys channels, or DOM/BOM
values/assignment behavior.

## 1. Entry-point contract

The only operations covered by this slice are the generic callbacks in
`src/core/host_object.cpp`:

| Callback | What it proves |
|---|---|
| `property_getter` | A script read a property registered in `HostObject::property_names()`. |
| `property_setter` | A script wrote a property registered in `HostObject::writable_property_names()`. |
| `method_callback` | A script invoked a method registered in `HostObject::method_names()`. |
| `named_property_getter` | A script read a missing string key after normal lookup; recorded as `get_probe`. |
| independent native callbacks | performance/crypto/Storage methods, plus the Storage `length` accessor, record `call`/`get` with native identity tokens. |

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
| `performance.now()` / `crypto.randomUUID()` | records `call` |
| `localStorage.getItem()` / `sessionStorage.setItem()` / `localStorage.length` | records `call`; `length` records `get` |
| `performance.timeOrigin` | records `get` |
| `navigator.connection.effectiveType` / `screen.orientation.type` | records `get`; nested reads also show their parent getter |
| `visualViewport.width` / `height` / offsets / `scale` | records `get` with `object_type: "visual_viewport"` |
| `visualViewport.addEventListener()` / `removeEventListener()` / `dispatchEvent()` | records `call`; results and no-op behavior unchanged |
| `visualViewport.onresize = fn` / reads of `onresize` / `onscroll` | records `set` / `get`; the assigned value is returned, no event is dispatched |
| `visualViewport.someMissingProperty` | records `get_probe` / `resolution: "missing"` |
| `navigator.storage.estimate()` / `persisted()` / `persist()` | records a navigator `get` for `storage` plus a `storage_manager` `call` |
| `navigator.storage.someProperty` / method-reference reads | not covered; the manager stays a plain object |
| `navigator.permissions.query({name})` | records a navigator `get` for `permissions` plus a `permissions` `call` |
| `status.name` / `status.state` / `status.addEventListener()` / `dispatchEvent()` | records `get`/`call` with `object_type: "permission_status"` |
| permission-status `onchange` reads/writes and missing-property probes | not covered; those stay plain properties |
| other plain property reads on native BOM objects | not covered yet |
| assignment to a read-only host property | no event; original write behavior/error unchanged |
| `window.innerWidth` / `outerWidth` / `innerHeight` | records `get` from the window metadata accessor |
| `window.name` | records `get` and `set` |
| `window.origin` / `isSecureContext` / `crossOriginIsolated` / `closed` / `frameElement` | records `get`; read-only behavior unchanged |
| `window.parent` / `top` / `frames` / `opener` | not covered by this accessor group |
| `matchMedia(...)` result objects / `navigator.plugins` / `mimeTypes` | not covered yet |
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
| `visualViewport` | `VisualViewportHost` | Installed as a read-only global; metadata properties plus no-op event-target methods. |
| `navigator.storage` | plain cached object + native callbacks | Returned by `NavigatorHost::get_property("storage")`; the parent read is `navigator`, method calls carry `storage_manager` identity. |
| `navigator.permissions` | plain cached object + native callback | Returned by `NavigatorHost::get_property("permissions")`; `query()` records `permissions` identity, returned statuses use fixed per-name identity. |
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
  `screen`, `location`, `document`, `element`, `connection`, `orientation`,
  `performance`, `permission_status`, `permissions`, `crypto`, `storage`,
  `storage_manager`, `visual_viewport`, `window`, or the fallback
  `host_object`.
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

## 4.2 Independent native BOM callbacks (step 5a)

The first step-5 group records calls without converting the plain BOM objects to
accessor-based wrappers:

- `performance` methods: `now`, `mark`, `measure`, `getEntries`,
  `getEntriesByName`, `getEntriesByType`, `clearMarks`, `clearMeasures`;
- `crypto` methods: `getRandomValues`, `randomUUID`;
- Storage methods: `getItem`, `setItem`, `removeItem`, `clear`, `key`, plus the
  `length` property getter. `localStorage` and `sessionStorage` share
  `object_type: "storage"` and label `"storage"` but have distinct backing
  object IDs.

The callbacks use stable native tokens/backings for `object_id`; no plain-object
descriptor, prototype, function identity, or `this` behavior is changed. A
method is recorded when its native callback is entered, before implementation.
Property reads on `performance`, `crypto`, or Storage — except the Storage
`length` getter and `performance.timeOrigin` (§4.4) — remain unimplemented.

## 4.3 Intrinsic window metadata (step 5b)

The following global properties are now read-only native accessors that record a
`get` with `object_type: "window"`, `label: "window"`, and `member` equal to the
property name:

`devicePixelRatio`, `innerWidth`, `innerHeight`, `outerWidth`, `outerHeight`,
`screenX`, `screenY`, `screenLeft`, `screenTop`, `length`, `isSecureContext`,
`origin`, `crossOriginIsolated`, `closed`, and `frameElement`.

`window.name` records `get` and `set` through its existing accessor. Values and
read-only assignment behavior are unchanged. The data-property-to-accessor
change is intentional for these traceable fields; `parent` / `top` / `frames` /
`opener` keep their prior descriptors and remain untraced in this group. Missing
global-property probes and value/argument/return capture are not included.

## 4.4 Nested metadata objects (step 5c)

`navigator.connection` and `screen.orientation` are nested HostObjects. Each
access returns a wrapper over a stable Page-owned backing, so:
- their property reads record `get` with `object_type: "connection"` /
  `"orientation"` and labels `navigator.connection` / `screen.orientation`;
- absent nested string keys produce `get_probe`/`missing` events;
- repeated access through the parent records the parent getter each time, then
  the nested property event (e.g. `navigator.connection.effectiveType` emits a
  `navigator.connection` get followed by a `connection.effectiveType` get);
- the nested object ID is stable across fresh wrappers.

`performance.timeOrigin` is a traced accessor on the otherwise plain
`performance` object. Other performance/crypto/Storage property reads and
`matchMedia` results remain for later step-5 groups.

## 4.5 Static visual viewport (step 5d)

`visualViewport` is now a HostObject (`VisualViewportHost`) instead of a plain JS
object, so:

- `width` / `height` / `offsetLeft` / `offsetTop` / `pageLeft` / `pageTop` /
  `scale` reads record `get` with `object_type: "visual_viewport"` and label
  `"visualViewport"`; width/height still come from the Page environment and the
  offsets/scale remain the same constants;
- `addEventListener` / `removeEventListener` / `dispatchEvent` calls record
  `call`; results are unchanged (`undefined` / `undefined` / `false`) and no
  listener is stored;
- `onresize` / `onscroll` record `set` and `get`; an assigned handler is stored
  for the generation and returned on read, but no event is ever dispatched;
- absent string keys produce `get_probe` / `resolution: "missing"` under the
  same interceptor as other HostObjects.

The global property remains read-only (`ReadOnly | DontDelete`), and values plus
sloppy/strict write behavior are unchanged. Moving from a plain object to a
HostObject intentionally changes some non-value characteristics:

- the instance prototype is now the host-object template prototype (whose
  prototype is `Object.prototype`); `visualViewport instanceof Object` and
  inherited helpers still work, but
  `Object.getPrototypeOf(visualViewport) === Object.prototype` is now false;
- the no-op event-target methods are writable/configurable like other
  host-object methods (the previous plain-object versions were read-only);
- the writable `onresize` / `onscroll` descriptors remain data descriptors with
  `writable: true` but are now non-configurable;
- the read-only metadata descriptors keep their data shape (`value`,
  `writable: false`, `enumerable: true`, `configurable: false`). Reading such a
  descriptor through `Object.getOwnPropertyDescriptor` evaluates the native
  getter, so it also emits a `get` event — native data-property descriptors are
  materialized by calling the getter.

## 4.6 `navigator.storage` manager (step 5e)

`navigator.storage` is exposed by `NavigatorHost` as a cached plain object, and
its three methods are native callbacks:

- reading `navigator.storage` records a `get` on the `navigator` object;
- calling `estimate()` / `persisted()` / `persist()` records a `call` with
  `object_type: "storage_manager"` and label `"navigator.storage"`;
- the manager object stays a plain object: it keeps `Object.prototype`, the
  cached identity (`navigator.storage === navigator.storage`), the read-only
  method descriptors, and the `estimate` / `persisted` / `persist` results
  (`{usage: 0, quota: 10737418240}` / `false` / `false`);
- `navigator.storage = null` remains a no-op and
  `navigator.storage.estimate = 1` is still rejected by the read-only method
  property.

Not covered for this group: method-reference reads
(`const f = navigator.storage.estimate`), property reads/probes on the manager
object itself, and the Promise/estimate values. `object_id` for manager calls
uses a stable native token: it is stable within a generation and gets a new ID
after `load()` because the generation identity map resets.

## 4.7 `navigator.permissions` and PermissionStatus (step 5f)

`navigator.permissions` is a cached plain object whose `query()` method is a
native callback. The returned PermissionStatus objects are fresh plain objects
per query, but their observable metadata is exposed through native accessors:

- reading `navigator.permissions` records a `get` on `navigator`; calling
  `query(descriptor)` records a `permissions` `call` with label
  `"navigator.permissions"`;
- a successful query resolves to a plain PermissionStatus whose `name` / `state`
  reads record `get` and whose `addEventListener` / `removeEventListener` /
  `dispatchEvent` calls record `call`, all with `object_type:
  "permission_status"` and label `"permissionStatus"`;
- the status keeps `Object.prototype`, its data descriptor shapes, a writable
  `onchange`, and the original `"prompt"` state; each query returns a distinct
  JS object;
- `navigator.permissions = null` and `navigator.permissions.query = 1` remain
  no-ops; the five query error messages and their synchronous descriptor-getter
  exception behavior match M11-44.

`object_id` for statuses is the fixed per-permission descriptor, so two statuses
for the same permission share an ID and different permissions do not; it resets
with a new generation like other identities. Not covered: `onchange` reads and
writes, missing-property probes on a status (it is a plain object), and the
Promise value itself.

## 5. Source attribution

- `eval` — `Page.eval`, direct context evaluation, and host-provided
  `scripts=[...]` (host scripts are not distinguished from direct eval in this
  slice).
- `html_script` — inline/external HTML script execution through
  `PageState::run_html_script`.
- `internal` — generation bootstrap during `install_page`. The current fixed
  bootstrap reads `window.innerWidth` / `innerHeight` once for the static media
  evaluator, so those two `get` events appear with `source_kind: "internal"` on
  every load.
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

- Window `parent` / `top` / `frames` / `opener` and other intrinsic/global data
  properties not listed in §4.3.
- `matchMedia(...)` result objects, `navigator.plugins` / `mimeTypes`, and
  nested BOM objects not listed in §4.4–§4.7.
- Method-reference/property reads on `performance`, `crypto`, and Storage (their
  independent native method calls and the covered getters are described in step
  5a/5e); `navigator.plugins` / `mimeTypes`, `matchMedia` results, and nested
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

The remaining step-5 groups expand `matchMedia` results, `navigator.plugins` /
`mimeTypes`, and other plain property reads; step 6 adds method-reference reads,
`in`, delete, enumeration, descriptors, and value/argument/return summaries.
None is implied by this slice.

## 8. Verification

Verified on real V8 at commit `18e674e` (branch
`host-access-permissions`):

- Linux `release-linux` run `41031830520` — cp311 / cp312 / cp313 / cp314,
  each `1680 passed, 2 skipped`;
- Windows `release-windows` run `41031835192` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1680 passed, 2 skipped`.

`test/test_access_trace_api.py` (9 items),
`test/test_access_trace_runtime.py` (11 items),
`test/test_access_trace_probes.py` (10 items),
`test/test_access_trace_bom_calls.py` (4 items),
`test/test_access_trace_window.py` (4 items),
`test/test_access_trace_nested.py` (3 items),
`test/test_access_trace_visual_viewport.py` (4 items),
`test/test_access_trace_storage_manager.py` (4 items), and
`test/test_access_trace_permissions.py` (5 items) ran in every lane. The full
suite collects 1682 items; 1680 pass and 2 are skipped.

The permissions slice needed a second CI run only because `Array.join`
stringifies `null` as an empty field in the descriptor assertion; the final
commit `18e674e` corrects the test expectation. The storage-manager slice passed
on its first CI attempt. Earlier groups recorded their test-expectation
corrections in this document; the visual viewport attempt needed a second run
for descriptor/getter and `Array.join` semantics, and the nested attempt needed
a second run for repeated parent getters.

The initial probe attempt (`76baaa2`) showed that V8 invokes the named getter
for query-only operations and some existing-property paths. `8995b89` added the
no-op named query callback and the real own/prototype property guard; the
interceptor still returns `kNo` so normal lookup and descriptors are unchanged.
The BOM-callback group (`ac80ee8`) added independent native callback recording
without changing plain-object descriptors or function identity.

Coverage in this slice:

- get/set/call for all installed `HostObject` properties/methods, including
  `navigator.userAgent`, `screen.width`, `location.toString()`, `document`
  queries, element properties/methods, and `console` methods;
- missing string-key reads on those hosts produce exactly one
  `get_probe`/`resolution: "missing"` event;
- own properties with value `undefined`, prototype properties, existing
  properties, `in`, `hasOwnProperty`, `Object.keys`, descriptors, Symbols, and
  numeric reads do not produce missing probes;
- independent native BOM callbacks record performance/crypto/Storage calls and
  the Storage `length` getter; properties on those plain objects remain
  untraced in this group;
- window metadata accessors record `get` for the §4.3 fields and `get`/`set`
  for `window.name`; read-only assignment, strict-mode errors, and values are
  unchanged (the geometry/flag data properties are now accessors);
- nested connection/orientation properties and `performance.timeOrigin` record
  reads, including repeated parent getter accesses and nested missing probes;
- `visualViewport` metadata, writable handlers, no-op methods, and missing
  probes record get/set/call/get_probe events; read-only writes still have no
  set event, and the documented prototype/descriptor changes are asserted;
- `navigator.storage` parent reads and all three manager method calls record
  events while preserving plain-object identity, descriptors, and results;
- `navigator.permissions` parent reads, query calls, status name/state gets,
  status method calls, per-name identity grouping, and all rejection messages
  are covered while keeping the plain-object shapes;
- read-only writes and method-reference reads stay untraced;
- source attribution: `eval`, inline HTML script, internal bootstrap, and
  unknown (timer/Promise callback) evidence;
- native object IDs remain stable across fresh element wrappers, and reset with
  a new generation;
- capacity, dropped-event counts, object-type filters, read-and-clear, stop,
  multiple Pages, and disposal behavior are covered by tests.

The remaining step-5 blind spots (`matchMedia` results, navigator
plugins/mimeTypes, permission-status `onchange`/missing probes, and other plain
BOM property reads) and step 6 (remaining operations and values) remain
intentionally out of scope.
