# Browser compatibility repair batch

Status: approved by the user on **2026-11-05**; implementation and real-V8
verification pending. This interrupts the planned focus/visibility slice without
renumbering historical milestones. M1 ownership and API contracts do not change.

## Contract and acceptance

The user's offline Chrome/iv8 comparison identifies 24 failing expressions among
27 probes. Tests use independent synthetic HTML, not a site's challenge program,
cookie, or network response. Acceptance is those expressions plus related
mutation, receiver, disposal, isolation, and descriptor cases. Passing these tests
does not establish acceptance of any cookie by a remote server.

- Reflect meta content/name/httpEquiv and script src/type/text/innerText through
  the underlying attributes/children; never store reflected state on one wrapper.
- Preserve one JS wrapper per native DOM node within a generation, including
  Text, Comment, Fragment, parent, query, event, and mutation-observer paths.
- Reparent content following an explicit closing body/html into the document's
  body, synthesizing a missing body where needed. Keep fragment parsing separate;
  this is a bounded tree-construction correction, not a full HTML parser.
- Add anchor/area href reflection and coordinated URL components, location.port,
  and prototype accessors for the repaired IDL surface with brand checks. Preserve
  the historical standalone URL constructor's explicitly minimal contract.
- Return live HTMLCollection objects from getElementsByTagName/ClassName, with
  descendant-only element queries, indexed access, item/namedItem and iteration.
  querySelectorAll remains a snapshot. Update obsolete Array expectations rather
  than retaining browser-incompatible Array methods on HTMLCollection.
- Expose Document tree-edit methods with real hierarchy/not-found validation.
- Record assign/replace/reload navigation **intent** without HTTP, context reload,
  or implicit execution. A Page read-and-clear channel exposes requested URL,
  operation, source and generation; location continues to describe the loaded
  document. No networking or actual navigation is approved by this batch.
- Provide a bounded inline CSS declaration model (attribute reflection and
  declaration methods; no layout/cascade), explicit unavailable canvas contexts,
  sendBeacon refusal without networking, and a deterministic Promise-based battery
  baseline. No fake successful rendering or transmission.
- Implement document.all's V8 undetectable-object behavior with a live element
  collection, rather than replacing it with undefined or an ordinary object.
- Preserve missing documentMode, navigator.standalone/battery and connection.type.
- Add an asynchronous-error observation channel if callback diagnostics can be
  integrated without changing nonblocking callback scheduling or GIL ownership.

Deprecated storage quota support is secondary to the actionable failures: it must
either have defined refusal/usage behavior with tests or remain explicitly absent;
an empty object does not satisfy compatibility. Capability expansions are bounded
and must be reported separately from the 27 regression expressions.

## Verification

Start with offline focused tests; run the user's independent probe against a
rebuilt V8-linked wheel. Run the full suite and every Linux/Windows/macOS release
lane before marking verified. Record actual commits, trees, run/artifact IDs and
passed/skipped counts in this report and the handoff set. Until then, the latest
verified browser feature remains M11-52 and the latest verified monitoring change
is readable access logs.
