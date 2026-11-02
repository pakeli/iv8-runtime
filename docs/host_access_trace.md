# Host Access Trace

Status: **VERIFIED — HostObject get/set/call + missing-property probes + native
BOM callback calls + window/nested metadata + static visualViewport + navigator
storage/permissions managers + matchMedia results + legacy navigator metadata +
window aliases + step-6a value/argument/return summaries + step-6b method
references + step-6c delete/enumeration/define operations (descriptor queries
and HasProperty deferred) green on real V8 Linux + Windows + macOS arm64
(`2026-11-02`)**

This document freezes steps 1–4, the step-5 object groups, and steps 6a–6c of
`docs/host_access_tracing_plan.md`: native HostObject get/set/call tracing,
string-key missing-property probes, call/get coverage for independent native BOM
callbacks (performance, crypto, Storage), intrinsic window metadata reads,
nested `navigator.connection` / `screen.orientation` / `performance.timeOrigin`
reads, the static `visualViewport` HostObject, the `navigator.storage`
manager's parent read plus method calls, `navigator.permissions` parent reads
plus query calls and returned PermissionStatus accesses, matchMedia result
objects, legacy navigator metadata, window browsing-context aliases,
primitive value/argument/return summaries with returned/threw outcomes,
method-reference reads, and delete/enumeration/define operation events. It is a
second-layer monitoring feature. It does not change the M1 `JSContext` contract,
the existing `watch_apis` / Reflect / Object.keys channels, or DOM/BOM
values/assignment/deletion/enumeration/definition behavior.

## 1. Entry-point contract

The only operations covered by this slice are the generic callbacks in
`src/core/host_object.cpp`:

| Callback | What it proves |
|---|---|
| `property_getter` | A script read a property registered in `HostObject::property_names()`. |
| `property_setter` | A script wrote a property registered in `HostObject::writable_property_names()`. |
| `method_callback` | A script invoked a method registered in `HostObject::method_names()`. |
| `method_getter` | A script read a function-valued method property; recorded as `get_method` (or folded into the following call). |
| `named_property_getter` | A script read a missing string key after normal lookup; recorded as `get_probe`. |
| `named_property_deleter` | A script requested deletion of a string key; recorded as `delete`. |
| `named_property_enumerator` | A script requested own-key enumeration; recorded as `own_keys` with an empty member. |
| `named_property_definer` | A script requested definition of a string property; recorded as `define_property` when V8 routes the definition through the named definer. |
| `named_property_query` | No-op; keeps `in` / `hasOwnProperty` / descriptor lookup on the normal path and is not recorded. |
| independent native callbacks | performance/crypto/Storage methods, plus the Storage `length` accessor, record `call`/`get` with native identity tokens. |

The named descriptor callback is intentionally **not** installed; descriptor
queries remain untraced (see §4.13).

This is **not** a V8-wide property-access hook. It proves that a native host
entry point ran; it does not prove every `obj.x` expression was observed.

The event is written before the concrete host implementation runs. Its
`phase` is therefore `"enter"`; a later implementation error or an ignored
setter does not retroactively remove the event. Step 6a attaches optional
completion fields (`outcome`, value/argument/return summaries) to that same
enter event after dispatch; it does not create a second event or change `seq`.

### Acceptance table

| JavaScript behavior | First slice |
|---|---|
| `navigator.userAgent` / `navigator['userAgent']` | records `get` |
| `element.textContent = 'abc'` | records `set`, where the property is declared writable |
| `document.querySelector('#app')` | records `call` |
| `const fn = document.querySelector` | records `get_method`; it is not a `call` |
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
| `matchMedia(query)` / `m.media` / `m.matches` / `m.addEventListener()` | records a `media_query_list` `call` plus `get`/`call` entries |
| `m.onchange` reads/writes | not covered; it stays a per-wrapper plain property |
| `navigator.plugins` / `navigator.mimeTypes` | records a navigator `get`; array element reads are not traced |
| `navigator.javaEnabled()` | records a navigator `get` plus `call`; returns `false` |
| `hostProbe.answer` / `element.textContent` reads | record `get` with `value_summary` and `outcome` |
| `element.textContent = 'x'` | records `set` with the assigned `value_summary` |
| `hostProbe.add(2, 3)` | records one `call` (the immediately preceding method read is folded); includes `argument_count`, up to 8 `argument_summaries`, `return_summary`, and `outcome` |
| method read, other trace event, then call | keeps the `get_method` and adds a separate `call` |
| assignment to `hostProbe.echo` | records `set`; a non-function override read is an ordinary read, not `get_method` |
| method that throws | records `call` with `outcome: "threw"` (no return summary), exception still propagates |
| other plain property reads on native BOM objects | not covered yet |
| assignment to a read-only host property | no event; original write behavior/error unchanged |
| `window.innerWidth` / `outerWidth` / `innerHeight` | records `get` from the window metadata accessor |
| `window.name` | records `get` and `set` |
| `window.origin` / `isSecureContext` / `crossOriginIsolated` / `closed` / `frameElement` | records `get`; read-only behavior unchanged |
| `window.parent` / `top` / `frames` / `opener` | records `get`; parent/frames/opener also record `set`, top stays read-only |
| `delete navigator.userAgent` | records `delete` before V8 applies the deletion; the return value (including `false` for a non-configurable property) is unchanged |
| `delete navigator.missing` | records `delete`; the normal `true` result is unchanged |
| `Object.keys(navigator)` / `for...in` | records one member-less `own_keys` event per own-key request; keys and order are unchanged |
| `Object.defineProperty(navigator, 'x', {...})` | records `define_property` when V8 routes the definition through the named definer; the result and property behavior are unchanged |
| method deletion and redefinition | records `delete` / `define_property`; descriptor materialization may still record `get_method` |
| `'x' in navigator`, `hasOwnProperty`, descriptor queries | not covered; a descriptor read may still evaluate a getter and record `get`/`get_method` |
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
| matchMedia results | `MediaQueryListHost` | Fresh JS wrapper per call over a Page-owned backing cached by query/matches; the JS `matchMedia` function still evaluates the query. |
| legacy navigator metadata | cached arrays + native function | `navigator.plugins` / `mimeTypes` / `javaEnabled` are returned by `NavigatorHost::get_property`; javaEnabled calls use their own native token. |
| window aliases | native global accessors | `parent` / `top` / `frames` / `opener` are traced accessors backed by per-generation PageState overrides. |
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
  `performance`, `permission_status`, `permissions`, `media_query_list`,
  `crypto`, `storage`, `storage_manager`, `visual_viewport`, `window`, or the
  fallback `host_object`.
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
operation, phase, resolution?, resource_name, source_kind,
outcome?, value_summary?, argument_count?, argument_summaries?,
return_summary?
```

- `member` is the property or method name as registered by the HostObject; it is
  empty for `own_keys`.
- `operation` is `get`, `set`, `get_method`, `call`, `get_probe`, `delete`,
  `own_keys`, or `define_property`.
- `phase` is always `enter` in this slice.
- `resolution` is present only for `get_probe`; current value is `"missing"`.
  Present/unknown classification is not emitted.
- `source_kind` is `eval`, `html_script`, `internal`, or `unknown`.
- Step 6a optional completion fields (generic HostObject `get`/`set`/`call`):
  - `outcome`: `"returned"` or `"threw"` (a pending exception after dispatch;
    the exception is never swallowed);
  - `value_summary`: for `get` (returned value) and `set` (assigned value) when
    `outcome` is `"returned"`;
  - `argument_count`: full argument count for `call`;
  - `argument_summaries`: first up to 8 argument summaries for `call`;
  - `return_summary`: for `call` when `outcome` is `"returned"`.
  These fields are absent on `get_method`, `get_probe`, `delete`, `own_keys`,
  `define_property`, on independent native callbacks, and when completion was
  unavailable (e.g. the event was cleared or the trace was stopped).

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
        "operations": ["get", "set", "get_method", "call", "get_probe",
                       "delete", "own_keys", "define_property"],
        "phase": "enter",
        "summaries": "generic HostObject get/set/call: returned/threw outcome, ...",
        "not_covered": [...],
    },
}
```

## 4.1 Missing-property probes (step 4)

Host-object templates install a named getter with `kOnlyInterceptStrings`; the
previous `kNonMasking` flag was removed in step 6c so existing string properties
also reach the named operation callbacks. A no-op named query handler keeps
`in`, `hasOwnProperty`, and descriptor queries on the normal path. The getter
also checks real own/prototype properties without invoking getters before
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
objects, and `in` / `hasOwnProperty` / descriptor queries remain outside this
slice. Deletion, own-key enumeration, and property definition are covered in
§4.13.

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
MediaQueryList `onchange` remain out of scope.

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

## 4.8 matchMedia result objects (step 5g)

`matchMedia(query)` remains a JS function (so its non-native `toString`
behavior is unchanged) and still evaluates the bounded query in JS. It calls a
temporary native factory captured in its closure; the factory is deleted from
the global immediately, leaving no JS-visible surface. Each result is a fresh JS
wrapper over a Page-owned backing cached by (media string, matches):

- every `matchMedia()` call records a `call` with `object_type:
  "media_query_list"`, label `"matchMedia"`, and member `matchMedia`; repeated
  calls with the same query share the backing `object_id`, different queries do
  not;
- `media` / `matches` reads record `get`; `addListener` / `removeListener` /
  `addEventListener` / `removeEventListener` / `dispatchEvent` calls record
  `call`; absent string keys produce `get_probe` / `"missing"`;
- `onchange` stays a plain per-wrapper own property (`null` initially, writable),
  independent between wrappers over the same backing; its reads/writes are not
  traced;
- values, no-op listener behavior, `dispatchEvent() === false`, read-only media/
  matches writes, and the non-native `matchMedia.toString()` result are
  unchanged.

Because results are HostObjects, their instance prototype is the host-object
template prototype, the no-op methods are writable/configurable, and descriptor
reads for `media`/`matches` evaluate the native getters (an extra `get`).
Media strings are stored as UTF-16 so the exact query text (including lone
surrogates) is preserved. Backings live until the next `load()`; each unique
query/matches pair allocates one small backing per generation.

## 4.9 Legacy navigator metadata (step 5h)

`navigator.plugins` and `navigator.mimeTypes` are cached frozen empty arrays,
and `navigator.javaEnabled` is a cached native function:

- reading `plugins` / `mimeTypes` records a navigator `get`; the arrays keep
  `Array.prototype`, zero length, frozen state, and identity across reads;
- reading `javaEnabled` records a navigator `get`; calling it records a `call`
  with its own stable native token and returns `false`, including when the
  function is detached first;
- the read-only data descriptors, assignment no-ops, and array values are
  unchanged. The javaEnabled function is now native, so its `toString()` differs
  from the previous JS closure.

Array element/length reads on `plugins` / `mimeTypes` stay untraced; only the
navigator property read is observed.

## 4.10 Window browsing-context aliases (step 5i)

`window.parent`, `window.top`, `window.frames`, and `window.opener` are now
traced native accessors:

- `parent` / `frames` default to the window object, `top` is always the window,
  and `opener` defaults to `null`;
- every read records a `get` with `object_type: "window"`; assignments to
  `parent` / `frames` / `opener` record `set` and store a per-generation
  replacement; `top` stays read-only and produces no set event;
- values, write results, delete behavior, and top-level `let` declarations
  remain the same (the writable aliases stay configurable; `let top` remains
  illegal);
- replacements reset on `load()`; descriptor reads evaluate the getters and
  therefore also emit `get` events.

## 4.11 Step 6a value/argument/return summaries

The generic HostObject `get`, `set`, and `call` path now attaches completion
metadata to the existing enter event. Summaries are deliberately primitive-only
and never execute user code (`toString`/`valueOf`, property Gets, or JSON
serialization are not used):

| Summary | Meaning |
|---|---|
| `undefined` / `null` | The primitive value. |
| `boolean:true` / `boolean:false` | Boolean primitive. |
| `number:<value>` | Number formatted with `%.17g` (including `nan`/`inf`). |
| `string:<utf8-byte-length>:<content>` | UTF-8 string. Content is capped at 64 bytes (rounded down to a complete UTF-8 sequence); longer content gets a trailing `...`. If the source is over 4096 bytes, content is omitted and only the length plus `...` is stored. `\`, newline, CR, tab, and other control bytes are escaped. |
| `bigint` / `symbol` | Primitive type only; no value/description conversion. |
| `function` / `array` / `host_object` / `object` | Type-only summary. |

`call` arguments are summarized for the first 8 positions; `argument_count`
always carries the full count. `outcome` is `"returned"` when no exception was
pending after dispatch and `"threw"` otherwise. Completion is attached by the
recorded `seq`, so a nested host access during the implementation cannot steal
it. Summaries are best-effort: allocation failure leaves the enter event intact.

Not covered by step 6a: summaries for independent native callbacks
(performance/crypto/Storage/permissions-status/matchMedia/visualViewport/window
metadata), arguments or return values for get/set beyond the summary fields,
exception messages, stacks, timing, and the other operations listed in §7. The
generic path's event count is unchanged; only fields are added.

## 4.12 Method-reference reads (step 6b)

HostObject methods are now function-valued accessors rather than plain data
properties. Reading one records:

```text
operation: "get_method", phase: "enter"
```

The function is created lazily and cached in a private slot on the JS wrapper, so
identity is stable and the property stays writable/configurable/enumerable like
the previous data property. Assigning a function or non-function value records a
`set`; reading a non-function override is an ordinary read and is not reported as
`get_method`.

For the common `receiver.method(...)` form, the method read is normally the last
event before the call; `method_callback` **folds that event into a single
`call`** instead of emitting two events. If another traced access occurs between
the read and the call (for example an argument expression that reads another host
property), the `get_method` event is retained and a separate `call` event is
recorded:

```text
const fn = hostProbe.echo;       // get_method
hostProbe.answer;                // get
fn.call(hostProbe, 'x');         // call (not folded)
```

Descriptor reads such as `Object.getOwnPropertyDescriptor(host, 'method')`
materialize the accessor and therefore also produce `get_method`. Detached method
references are not calls; calling one later with the host receiver can still
produce a separate `call`. The folding rule is deliberately conservative: it
never removes a non-adjacent `get_method` event, and no user `toString`/`valueOf`
or property Get is executed to produce method metadata.

## 4.13 Delete, enumeration, and define operations (step 6c)

Host-object templates keep the named interceptor active for existing string
properties (they no longer set `kNonMasking`), so the named deleter, enumerator,
and definer observe requests that previously bypassed the interceptor. Every
callback returns `Intercepted::kNo` or an empty extra-key array, so V8 still
performs the real operation and results/errors are unchanged.

| JavaScript behavior | Event | Notes |
|---|---|---|
| `delete host.userAgent` | `delete` | Recorded before V8 applies deletion. A non-configurable property still returns `false`; the event is evidence of the request, not proof of deletion. |
| `delete host.missing` | `delete` | The normal `true` result is unchanged. |
| `Object.keys(host)` / `for...in` | `own_keys` | One event per request with `member: ""`. The enumerator returns no extra keys; V8's normal own-key set and order are preserved. |
| `Object.defineProperty(host, 'x', {...})` | `define_property` | Recorded when V8 routes the definition through the named definer, for missing or existing keys. The definition result and descriptors are unchanged. |
| method deletion/redefinition | `delete` / `define_property` | A preceding descriptor read may still materialize the method accessor and record `get_method`. |

These operations carry only the common event fields; there are no outcome,
value, argument, or return summaries for them yet. They use the same object
identity, generation, filter, resource, and source attribution as get/set/call.

Descriptor and HasProperty operations are deliberately deferred:

- The named descriptor callback is not installed. When it was present (attempt
  `d314256`), V8 also invoked it for internal property bookkeeping, such as
  adding the per-wrapper `matchMedia` `onchange` property via native `Set`.
  Those events were indistinguishable from explicit
  `Object.getOwnPropertyDescriptor` calls and polluted ordinary matchMedia
  traces.
- `in` / `hasOwnProperty` do not have a reliable, non-invasive record point in
  this interceptor design. The no-op query handler keeps their lookup normal;
  recording them by returning `kYes`/`kNo` would change semantics.
- An explicit descriptor read still performs the normal lookup. Reading a native
  data-property descriptor can evaluate its getter (emitting `get`), and reading
  a method descriptor can materialize the method accessor (emitting
  `get_method`); there is simply no `get_own_property_descriptor` event.

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

## 7. Not covered

- Other intrinsic/global data properties not listed in §4.3/§4.10 (for example
  reads of global function/host-object bindings themselves).
- MediaQueryList/PermissionStatus `onchange` and missing probes, and nested BOM
  objects not listed in §4.4–§4.10.
- Method-reference/property reads on `performance`, `crypto`, and Storage (their
  independent native method calls and the covered getters are described in step
  5a/5e); array element reads on `navigator.plugins` / `mimeTypes`,
  MediaQueryList/PermissionStatus `onchange`, and nested `classList` /
  `validity` objects.
- Summaries for independent native callbacks and for `delete`/`own_keys`/
  `define_property` (only generic HostObject get/set/call has them in step 6a);
  exception messages, stacks, and timing.
- Arrays and other plain values returned from host getters/methods.
- User-created plain JS objects, object literals, class instances, destructuring
  beyond covered getter execution, Proxy/AST instrumentation, and explicit
  object-level hooks.
- `in` / `hasOwnProperty`, descriptor queries, non-HostObject deletion /
  enumeration / definition / dynamic shadowing behavior, and missing-property
  probes on non-HostObject objects (including numeric keys and Symbols).
- Read-only write attempts that V8 rejects before reaching a native setter.
- Arguments/returns for non-generic trace sources, exception messages, stacks,
  timing, Inspector pauses / `break_on_hit`, and DevTools evaluation attribution.
- Any JS-visible global or new top-level Python export; any M1 `JSContext`
  behavior change.

The step-5 object groups and step-6a/6b slices are complete. Step 6c adds
request events for delete, own-key enumeration, and property definition on
HostObject string members while preserving their results and descriptors.
Remaining step-6 work: `in`/`hasOwnProperty`, descriptor queries, target
association for the existing Reflect channels, native-callback summaries, and
exception messages/stacks/timing. Plain-property/returned-collection gaps
(onchange, array elements, plain returned values, global binding reads) remain
explicit non-goals.

## 8. Verification

Verified on real V8 against the tree of commit `96c7c00` (branch
`host-access-operations`; the release runs used the identical tree `2addf62`,
cleanly re-committed as `96c7c00`):

- Linux `release-linux` run `41399273630` — cp311 / cp312 / cp313 / cp314,
  each `1704 passed, 2 skipped`;
- Windows `release-windows` run `41399284773` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1704 passed, 2 skipped`;
- macOS arm64 `release-macos` run `41399294680` — cp311 / cp312 / cp313 /
  cp314 `macosx_11_0_arm64` wheels, each `1704 passed, 2 skipped`.

`test/test_access_trace_api.py` (9 items),
`test/test_access_trace_runtime.py` (11 items),
`test/test_access_trace_probes.py` (10 items),
`test/test_access_trace_bom_calls.py` (4 items),
`test/test_access_trace_window.py` (4 items),
`test/test_access_trace_nested.py` (3 items),
`test/test_access_trace_visual_viewport.py` (4 items),
`test/test_access_trace_storage_manager.py` (4 items),
`test/test_access_trace_permissions.py` (5 items),
`test/test_access_trace_match_media.py` (3 items),
`test/test_access_trace_legacy_navigator.py` (3 items),
`test/test_access_trace_window_aliases.py` (4 items),
`test/test_access_trace_values.py` (6 items),
`test/test_access_trace_method_reads.py` (3 items), and
`test/test_access_trace_operations.py` (5 items) ran in every lane. The full
suite collects 1706 items; 1704 pass and 2 are skipped.

The step-6a value-summary, step-6b method-reference, and legacy-navigator slices
passed on their first CI attempts. The first step-6c attempt (`d314256`, Linux
`41398033669` / Windows `41398037880`) failed three existing matchMedia /
visualViewport expectations because the named descriptor callback also fired for
V8's internal property bookkeeping (for example, adding the per-wrapper
matchMedia `onchange` through native `Set`). The final slice omits that callback,
records delete/enumeration/define only, and passed Linux, Windows, and macOS on
the runs above. The first window-alias attempt (`ac4e1f5`) made the writable
aliases non-configurable, which broke top-level `let frames`; `b936307` keeps
their configurable attributes and passes. The matchMedia and storage-manager slices also passed on
their first CI attempts. The permissions slice needed a second CI run only
because `Array.join` stringifies `null` as an empty field in the descriptor
assertion; `18e674e` corrected the test expectation. Earlier groups recorded their test-expectation
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
  probes record get/set/call/get_probe (and method references get_method)
  events; read-only writes still have no
  set event, and the documented prototype/descriptor changes are asserted;
- `navigator.storage` parent reads and all three manager method calls record
  events while preserving plain-object identity, descriptors, and results;
- `navigator.permissions` parent reads, query calls, status name/state gets,
  status method calls, per-name identity grouping, and all rejection messages
  are covered while keeping the plain-object shapes;
- `matchMedia` result calls/gets/method calls/missing probes, query-keyed
  identity, per-wrapper onchange independence, and the documented HostObject
  differences are covered;
- legacy navigator plugin/mime-type reads and javaEnabled get/call events keep
  array/function identity and read-only behavior;
- window parent/top/frames/opener get/set events preserve replaceable,
  read-only, delete, and lexical-declaration behavior across loads;
- generic HostObject get/set/call completion records returned/threw outcomes,
  primitive value and return summaries, 8-argument summaries with full counts,
  UTF-8-safe string truncation, and nested-completion attachment by seq;
- method references record `get_method`; adjacent calls fold into one `call`,
  intervening accesses keep both, method identity/override/descriptor behavior
  and non-function override reads are covered;
- delete, own-key enumeration, and define requests record `delete`, `own_keys`
  (empty member), and `define_property` while preserving deletion results,
  enumeration order/identity, and definition results; the new operations work
  for existing and missing members, native properties, and methods, and obey
  object-type filters;
- read-only writes stay untraced;
- source attribution: `eval`, inline HTML script, internal bootstrap, and
  unknown (timer/Promise callback) evidence;
- native object IDs remain stable across fresh element wrappers, and reset with
  a new generation;
- capacity, dropped-event counts, object-type filters, read-and-clear, stop,
  multiple Pages, and disposal behavior are covered by tests.

The remaining blind spots are plain-property/returned-collection gaps
(MediaQueryList/PermissionStatus `onchange`, array elements, plain returned
values, global binding reads), the rest of step 6 (`in`/`hasOwnProperty`,
descriptor queries, Reflect target identity, native-callback summaries,
exception messages/stacks/timing), and step 7 (runtime report/usage flow).
