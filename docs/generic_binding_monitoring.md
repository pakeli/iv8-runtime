# Generic binding reference and rejection tracing

Authorized `2026-11-16`: close the next two reproduced monitoring gaps in the
shared HostObject binding path: direct prototype method references and native
entries rejected by their receiver brand checks.

## Contract

- Known generic interface prototype method reads record `get`,
  `object_type="host_object"`, `label="<DefiningInterface>.prototype"`, with
  safe returned/function completion. Inherited references use the actual
  defining prototype. Registration remains lazy with its interface/host slice.
- Existing canonical instance references retain `get_method`, the instance
  identity and adjacent-call folding. They return the original stored Function
  directly, avoiding a second prototype event for the same inherited read.
- Generic method/getter/setter entries that fail receiver validation record
  `call`/`get`/`set`, synchronous thrown completion and safe original arguments
  or assigned value before the same `TypeError("Illegal invocation")`.
  No argument conversion, user getter, Proxy trap or host method runs first.
- A rejection is attributed to the known binding owner, never an arbitrary
  wrong receiver: defining prototype for shared IDL members; actual owner for
  own Location/Document bindings and legacy external-owner methods. The owner
  metadata is diagnostic only; execution always validates `info.This()`.
- Prototype native-data paths preserve data descriptors, original functions,
  names/lengths/native source/prototypes/constructability, flags, inherited
  assignment, deletion and redefinition. User accessors retain normal lookup.
  Location methods remain own properties; no new Location.prototype API appears.
- References do not create call-only watch hits. Independent watch channels
  retain their existing dispatch contract. The user-origin gate, stop/filter/
  capacity and generation/Page isolation rules apply to all new entries.

## Limits and acceptance

This patch concerns generic HostObject bindings, not all independently installed
Event/URL/Blob callbacks or engine-rejected construction that never reaches a
native callback. Symbol/structural queries, missing probes and returned-object
coverage gaps remain separate work. It changes no browser behavior or M1 API.

Offline regressions cache prototypes/functions before tracing, assert real
entries and original exceptions, and protect native shapes, instance folding,
borrowed/base calls, invalid receivers without side effects, overrides, filters,
reloads and quiet user-origin debug output. Local results and source/binary
identities are recorded in `docs/generic_binding_monitoring_report.md`.
