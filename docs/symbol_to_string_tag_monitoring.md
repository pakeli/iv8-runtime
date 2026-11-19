# TAG-MON-01 — native Symbol.toStringTag reads

Authorized `2026-11-17`: observe direct and Reflect.get reads of existing
runtime-owned browser tag data properties. No Symbol.iterator work, new browser
API/tag, general Symbol interception or M1 intrinsic instrumentation is included.

## Installed surface and event contract

Replace each original runtime tag definition with the existing native-data
observation path, retaining value, own-property location, insertion order and
writable/enumerable/configurable flags. Covered definitions are shared web
interface prototypes (including Window/WindowProperties), live HTMLCollection /
HTMLAllCollection prototypes, namespace shells that already have an own tag,
and the existing CSSStyleDeclaration, BatteryManager and DeprecatedStorageQuota
bootstrap tag definitions. Do not manufacture tags on currently untagged objects
or fix unrelated pre-existing browser/profile differences.

Data getters use the existing defining-owner identity and object_type/label.
Shared prototypes use host_object / <Interface>.prototype; shell own tags retain
window_shell / shell-name; Battery/Quota own tags use their existing host_object
identities. Events have operation get, member `Symbol.toStringTag`, and optional
`member_kind="symbol"`. Text renders `<owner>[Symbol.toStringTag]`, distinct from
a string property literally named `Symbol.toStringTag`. Completion uses the
existing full-string value_summary. Reflected descriptor materialization can
invoke a native-data getter and record GET, as in the existing data-property
monitor; this does not execute a user getter.

Install where properties are originally created, even with tracing off, so
cached objects/prototypes and later lazy instances work. Actual data lookup via
direct/Reflect.get, arbitrary receiver, inheritance or a forwarding user Proxy
uses the same native callback once. CSS's existing Proxy bridge delegates this
specific key to the prototype hook to avoid duplicate GETs. Preserve JS overrides,
shadows, delete/redefinition/freeze and errors; do not forcibly reinstall over
user replacements or wrap their getters. A value-only redefinition follows V8's
native-data behavior; continued observation is not forcibly removed or invented.

## Origin and safety

Object.prototype.toString's own automatic lookup of an original native tag stays
runtime-origin and consumes no event capacity. Explicit browser-tag reads from
actual user JS tag getters/Proxy traps remain observable. Bound native tag
accessors/traps need user attribution without adding a JS frame. Classify only
through non-executing engine/descriptor metadata; do not pre-read a tag or invoke
a getter/trap. Pass the original raw receiver to the original builtin unchanged.
No additional name/constructor/tag getter, coercion or user callback is executed
for diagnostic labels or values. Existing toString interposer/internal-kind
limits remain those in `docs/to_string_monitoring.md`.

Unknown/exotic paths must retain ordinary results and side-effect counts rather
than force inspection. A dynamic Proxy-handler get/apply accessor (or Proxy handler metadata) cannot be
classified without running user code; conservative user attribution may retain
an extra underlying tag GET in those exotic cases. This preserves callback
visibility and side-effect counts, but is not a universal no-noise guarantee.
Registered runtime CSS get traps are followed through their engine target for
metadata classification so bound user tag accessors on that target remain
observable. Record these boundaries in the report. Other Symbol keys, direct ordinary JS properties, replaced user
accessors and language-intrinsic tags remain separate coverage work.

## Verification

Use fresh Chrome offline comparison fixtures and baseline controls for tag
owners/values/flags/key order, Object.toString results, getter counts, explicit
reads inside user accessors, raw receivers, Proxy forwarding, descriptor-native
materialization, delete/redefine/readonly behavior, cached objects, filters,
capacity, stop, reload/disposal/Page isolation and debug/prod. Pure text tests
must distinguish real Symbols from identically named strings. Tests do not
contact a network. Actual source/build/results/release status:
`docs/symbol_to_string_tag_monitoring_report.md`.
