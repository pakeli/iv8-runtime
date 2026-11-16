# MutationRecord value-field tracing

Authorized `2026-11-16`: continue strict API observation with the reproduced
MutationObserver returned-record field gap. This slice covers the nine existing
fields, not Symbol/reflection paths or general array element/length interception.

## Contract

The current bounded M11-5/M11-48 profile returns plain extensible objects with
Object.prototype and own writable/enumerable/configurable data fields:
`type`, `target`, `addedNodes`, `removedNodes`, `previousSibling`, `nextSibling`,
`attributeName`, `attributeNamespace`, `oldValue`. No MutationRecord constructor
or new interface/prototype is introduced by this monitoring plumbing.

Each successfully materialized own configurable data field records native GET
and supported SET with safe completion, object_type `host_object`, label
`MutationRecord`. One generation-owned object identity is shared by all fields
of that record; distinct record objects retain distinct identities. Hooks are
installed even before tracing starts, so cached records remain observable.
watch_apis remains call-only; reading or writing a record is not a method hit.

Preserve original field initialization: perform the existing Set exactly once,
respecting inherited setters/read-only properties and original exceptions. Only
afterwards inspect the fresh object's own descriptor without reading its field.
Hook configurable data using its actual stored value/flags. If user code consumed
the field, made it an accessor or made it nonconfigurable, preserve that path
without forcing a native property. Diagnostic failures must not replace the
original setter result/error or invoke any accessor/Proxy trap.

Materialization/delivery bookkeeping remains runtime-origin and consumes no
events/capacity. User observer callbacks and user-provided initialization
accessors/coercions keep their user-origin API entries. Preserve node wrappers,
record values, namespace/old-value/null semantics, observer ordering and scheduling.
The surrounding records array and addedNodes/removedNodes arrays remain the
current plain arrays; their independent length/index reads are still open.

## Verification and limits

Use cached-before-tracing and asynchronous real records, assert actual field
events, flags/prototype/JSON/value identities, assignment/delete/redefinition,
freeze, prototype hooks, filters/capacity/stop/generation and Page isolation.
Keep mutation/namespace/characterData/adoption behavior and noise regressions in
acceptance. The standard native-data descriptor materialization semantics and
user-accessor replacement rule remain those of existing monitored value objects.

Source/binary/test evidence and release status are recorded in
`docs/mutation_record_monitoring_report.md`. New browser backend behavior,
Readonly/array/Symbol/query/other-returned-object gaps, full release verification,
SDK installation and version tags are separate from this bounded slice.
