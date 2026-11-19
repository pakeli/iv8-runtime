# User-origin API tracing

Authorized `2026-11-16`: debug output must describe browser API use by user
JavaScript, without runtime initialization, synthetic event creation or event
state maintenance appearing as user accesses.

## Required behavior

- An empty debug load prints nothing. Inline/resource/host scripts still run
  and their API accesses remain observable.
- Evaluating `window.location.href` after that load produces exactly its three
  Window/Location gets.
- Explicit user `new Event(...)` records the constructor call. Internal field
  initialization and dispatch bookkeeping do not record additional gets/sets.
- Lifecycle/event listeners, IDL handlers, timer/frame/idle callbacks, Promise
  reactions, MutationObserver and FileReader/message callbacks retain API use.
  Nested dispatch, coercion, getters/setters and exceptions restore their caller's
  origin. User-supplied bound native functions also remain observable.
- Internal operations are excluded before sequence, identity and capacity
  accounting, not removed from text after recording. Manual/raw access tracing
  uses the same origin gate. The Python mode and event schemas do not change.

## Implementation boundary

Keep origin state owned by the Page trace/generation. Native API entry records
before entering a runtime implementation scope; user invocation/conversion
boundaries temporarily restore user origin. Engine stack metadata permits
implicit JS reentry inside an internal scope; explicit scopes handle bound native
callbacks that have no additional JavaScript frame. Runtime bootstrap scripts
are identified by compiled script IDs rather than user-controlled filenames.
Their existing begin/end bridge records the public entry and marks implementation
work internal until completion. Scope restoration is exception safe.

Only the active tracing path queries engine stack metadata, before taking the
event-store lock. Recording never calls Python or invokes user JS for diagnostics.
No JS wrappers replace native browser functions, no function source override is
installed, and browser identities/descriptors/coercions/results stay intact.
The independent watch channels retain their existing configuration and contract.

This corrects provenance and noise; the already documented prototype, receiver,
symbol/query and returned-object coverage gaps are separate unfinished work.
Local verification and release/deployment status are recorded in
`docs/access_trace_origins_report.md`; the last release-verified build remains
the PR #32 candidate until this change has its own complete release verification.
