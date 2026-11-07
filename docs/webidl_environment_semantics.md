# Web IDL environment semantics repair

Approved and release-verified on **2026-11-07**. This batch follows the user's `iv8环境语义问题清单.md` and its 75
deterministic Chrome comparisons.

## Acceptance and sequence

1. ENV-01 through ENV-03. Native methods expose their declared name, required
   argument count and native source string, reject construction, and validate
   receivers by their defining interface. Valid inherited and borrowed calls
   operate on the actual receiver. Method identities remain stable.
2. ENV-04. Context-owned interface constructors and prototypes define the
   Window, Document, Navigator, Screen, Location and DOM node inheritance trees.
   Constructors follow their interface-specific creation rules. Instances carry
   the interface tag and resolve constructor through the interface prototype.
3. ENV-05 and ENV-06. Attributes and methods live on their defining prototypes.
   Legacy unforgeable members have instance descriptors. Reflection, assignment,
   enumeration, receiver checks and access tracing use the same member metadata.

The 75 supplied probes are the primary comparison set. Related tests exercise
wrong HostObject receivers, legitimate sibling/node receivers, per-context
prototype isolation, function identity, mutation, disposal, reloading and tracing.

## Ownership and API rules

Interface templates, constructor functions and native method objects are owned
by the current V8 context. Node wrappers retain the existing generation ownership
and stable identity. Native persistent handles are released while the isolate is
alive. pybind11 registration delegates to the Page runtime.

Native method creation covers the HostObject factory, the common native Window
globals and Storage callbacks. Existing utility interfaces keep their earlier
milestone contracts. Constructor creation and ordinary method creation
have distinct constructability rules. Methods carry defining-interface metadata
for receiver checks, including cross-tag Node/Element members.

Window and HTMLDocument participate in EventTarget inheritance. Document exposes
its Node behavior and document-specific tree constraints. Standalone EventTarget
and Document construction have meaningful native state. Element factories select
the appropriate HTML element prototype. CharacterData and DocumentFragment use
their corresponding Node branches.

Document.location and Window.location forward assignments into the loaded
Location's href setter. Location.href, assign, replace and reload produce offline
navigation intents. The loaded document changes when the existing load operation
commits its replacement. The normal navigation-read channel observes intent.

## Execution scope

The work targets function binding and object semantics. The probe also asks for
fetch, XMLHttpRequest, Worker and open entry points. The user confirmed the
object-model and explicit-failure scope on 2026-11-07.

- `fetch(input)` returns a Promise rejected with an Error named
  `NotSupportedError`. Missing input rejects with `TypeError`.
- `XMLHttpRequest` and `Worker` expose native constructor functions and their
  interface prototype chains. Construction throws an Error named
  `NotSupportedError` explaining that execution is unavailable in the offline
  Page runtime.
- `open()` returns the blocked-popup result, `null`.
- `Document`, `EventTarget`, `Text`, `Comment` and `DocumentFragment` support
  native construction. Calling them without `new` throws `TypeError`.
  The other registered DOM/BOM interfaces throw `Illegal constructor`.

`new Document()` creates a detached document with `application/xml` content
identity, `about:blank` URL, null location/defaultView/ownerDocument and native
Node state. Its elements use Element identity and retain tag case. The existing
HTML-oriented parser and attribute algorithms remain the bounded implementation.
XML parsing and namespaces require a later contract. Cross-document insertion
throws `WrongDocumentError` before moving nodes. Automatic adoption belongs with
the deferred import/adopt work.

Document root edits validate child kinds and the single-element constraint before
moving nodes and reuse the existing child-list mutation notifications.
Root outerHTML writes and adjacent HTML parsing fail with
`NoModificationAllowedError`. Variadic edits convert all arguments before moving
nodes and retain their original parent/reference when arguments include the
receiver. Nullable Node value setters normalize null/undefined to empty text.
Element.nodeValue writes preserve its children and null node value. Text/Comment
inherit EventTarget methods and use the existing tree
event dispatch. The earlier event listener dispatch contract still applies.

Canvas uses the existing unavailable-context result. M1's Python JSContext API,
per-context isolate ownership, concurrency guard and exception translation remain
governed by the existing kernel contracts.

## Verification record

The implementation proceeded in the sequence above, with focused checks before
full-suite verification. Source `f42acae`, tree
`3d2d9351d45815e85f53f0f91fa4ad805fff49f8`, passed Linux x64, Windows x64
and macOS arm64 release matrices for Python 3.11–3.14. Every lane reported
**2050 passed, 2 skipped**. The macOS focused set reported **189 passed** in
every interpreter. The installed cp311 wheel matched the user's original
capture probe **75/75** and the previous browser report **27/27**.

Native method reads remain observable through the instance interceptor. Methods
are ordinary prototype data properties and attributes are real function
accessors. Descriptor queries do not evaluate these accessors. Explicit query
tracing retains its HostObject-instance target boundary. JavaEnabled method
reads/calls now use the Navigator host identity and the usual adjacent read/call
folding. The getter-only navigator metadata retains its read-only behavior.

Source, test counts, platform runs and artifacts are recorded in
`docs/webidl_environment_report.md`.
