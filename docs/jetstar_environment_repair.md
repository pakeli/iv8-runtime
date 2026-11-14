# Jetstar environment repair — incremental boundary

Authorized `2026-11-14`: repair the confirmed first blocker and the small
Navigator/environment/permission slice, testing the unchanged cached Jetstar
collector after each independently reviewable change. Stage 4 source extraction
and iframe/Intl/graphics/network subsystems remain separate work.

## Validation levels

1. Focused offline browser/API regressions.
2. Original cached collector, fixed HTML/resources/profile, bounded job/timer
   pumps, recording the first failure and implementation/script hashes.
3. Live same-session server result, only when a compatible project-runtime HTTP
   path exists. The old community-0.1.4 runner cannot establish this result.

Passing load, reaching a later error, and receiving a server success are distinct
outcomes. HTTP 201 alone does not establish `_abck` success. Current collector
probes omit upstream session material and do not freeze time/randomness, so they
prove execution blockers rather than final-output equivalence.

## JET-01: bounded same-document History

Baseline `98f5656` fails at the original collector's zero-based column 503099,
reading `pushState` from an undefined receiver. A separate, explicitly modified
diagnostic copy evaluates the original dynamic key once: `history`; its root is
both `window` and `globalThis`. The original source remains unchanged.

The implementation belongs to `browser/window`, with generation-owned state
and the existing native Web IDL binding paths. The M1 kernel/isolate ownership,
GIL/operation guard, transport and monitoring architecture stay intact.

Approved bounded surface:

- Native `window.history` getter, stable instance, shared History prototype and
  illegal public History constructor; reference-derived descriptors, native
  sources, arities, receiver checks and method constructability.
- `length`, `state`, `scrollRestoration`; fresh load has one entry, null state
  and auto restoration. Invalid restoration values are ignored like the reference.
- `pushState`/`replaceState`: required arguments and string conversion ordering,
  serialized clone isolation, cached current state identity, same-origin HTTP(S)
  URL validation, atomic entry updates and synchronized Location/Document URL.
  The ignored title is converted but does not change document.title. These
  operations do not fire navigation/popstate/hashchange events or issue HTTP.
- `back`/`forward`/`go` expose the native reference shape and use the existing
  offline navigation-intent model. Out-of-range traversals are no-ops; in-range
  traversal and reload are recorded without executing them. Session-history
  traversal, popstate/hashchange delivery and cross-document navigation remain
  unimplemented and explicitly tracked as profile gaps.

Existing bounded URL parsing applies. New URL changes are limited to same-origin
HTTP(S); opaque/file URL rewriting is outside this slice. History cloning uses
V8 serialization; shared-memory cloning is explicitly unavailable. Clone failures expose DataCloneError and URL failures expose
SecurityError through the runtime's existing Error-with-DOM-name representation.
A complete DOMException interface/brand is not added by this slice.

Native History state is released before isolate disposal and reset per Page.load;
independent Pages do not share entries or JS handles. No History API is installed
into bare JSContext.

## Reference and evidence

The installed reference binary is now Chrome `157.0.8103.51`. New History
captures use a separate headless profile and loopback HTTP fixture, with version
and execution conditions recorded. Existing Chrome156 site captures remain
unchanged. Evidence lives under ignored `data/jetstar-native-2026-11-14/` and the
user's existing `iv8-test/reverse-records/请求链路.md`.

## JET-01 local verification (`2026-11-14`)

Native syntax/link/import and standalone source compilation passed. The serializer
Delegate uses a no-RTTI/PCH-free source matching the pinned V8 SDK. Chrome-derived
metadata/descriptors and behavior tests report **30 passed**. Full real-V8 tests
report **2120 passed, 2 skipped, 1 xfailed**; **83 audit regressions** pass.

| Checkpoint | Before History | After History |
|---|---|---|
| Original collector hash | `1d9baa8754ebad40dc7d52940bef7ac6ec17710a26231968fc86c99808f13565` | same, unmodified |
| Page.load | TypeError reading pushState, column 503099 | completes |
| Bounded jobs/timers | not reached | asynchronous XHR NotSupportedError at advance_1000, column 51597 |
| Server success | not tested | not tested; separate live data-send approval pending |

The harness promotes asynchronous errors to the first-failure record, preserving
its explicit test boundary. A test-only JS XHR adapter has offline queue/response
checks, an exact-origin/path allowlist, two total POST maximum, no redirects,
worker wall bounds and no persisted Cookie/sensor values. It is not a product
HTTP implementation and changes XHR method source; any live result using it
must carry that qualification. No live request was made by the blocked command.

Status: locally verified; platform release/server verification pending.
No server-pass or final _abck root-cause claim.
