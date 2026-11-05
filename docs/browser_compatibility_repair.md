# Browser compatibility repair batch

Status: **VERIFIED on real V8, Linux + Windows + macOS arm64, 2026-11-06**.
Approved by the user on **2026-11-05**. This interrupts the planned focus/visibility slice without
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

Source commit: `15edd82e68c62d0c551a55e9935bf34427edfb2a`.
Git tree: `905d1f671bc31fd25b2aee78c13258a6318ec36a`.
All release lanes link pinned V8 **15.0.245.19**, with Python 3.11–3.14 each
**1931 passed, 2 skipped** (1,933 collected). The focused repair suite contains
53 items and ran before the full suite in each macOS lane.

| Lane | Run | Result |
|---|---|---|
| V8 CI, Linux/Python 3.13 | [42064100130](https://github.com/pakeli/iv8-runtime/actions/runs/42064100130) | 1931 passed, 2 skipped |
| Linux x64, Python 3.11–3.14 | [42064333308](https://github.com/pakeli/iv8-runtime/actions/runs/42064333308) | each 1931 passed, 2 skipped |
| Windows x64, Python 3.11–3.14 | [42064342679](https://github.com/pakeli/iv8-runtime/actions/runs/42064342679) | each 1931 passed, 2 skipped |
| macOS arm64, Python 3.11–3.14 | [42064322136](https://github.com/pakeli/iv8-runtime/actions/runs/42064322136) | each focused 53 passed; full 1931 passed, 2 skipped |

Linux artifact: `13432709384` (all four wheels). Windows artifacts:
`13434670337` (cp311), `13433570946` (cp312), `13433496059` (cp313),
`13434590465` (cp314). macOS arm64 artifacts: `13434600040` (cp311),
`13434330405` (cp312), `13434120944` (cp313), `13432951203` (cp314), tagged
`macosx_11_0_arm64`.

The original independent `repro_iv8_gaps.py` from the user's report was run
against the installed final macOS/Python 3.13 wheel: **27 passed, 0 failed**.
The wheel's archive SHA-256 matched GitHub's artifact digest before installation
in an isolated task venv. Native linkage, navigation/error reads and disposal
also passed a local smoke check. The user's Python environment and original
baseline report/results were not changed. Evidence is under ignored
`data/browser-compatibility/`, including `release-evidence.json` and
`independent-probe-final.json`. Independent string-only URL/Chrome comparisons
also matched the 16 saved cases under UBSan.

macOS uses the established cached macos-14 arm64 fallback. Driver commit
`62d5cbbcc2ae6980b98439323f8bdfc23ca85831` pins all five checkouts to source
`15edd82`; each checkout was confirmed in the log. Restore commit
`180218ec3616ee74ad09526cab04268566fdb85d` restores original workflow blob
`eb730d3e77a17d4d527e67646475385cebb2428c` without rewriting history.
These temporary drivers are outside the source branch. The required real-V8
merge gate remains strict and enforced for administrators. Handoff-only
followups preserve the tested runtime/build/test files.

## Implemented behavior and observation APIs

`meta.content/name/httpEquiv/scheme` and `script.src/type/text` reflect the
underlying attribute/text state. `script.innerText` reads its text. These values
survive repeated queries because each generation caches one JS wrapper per native
node. Attributes, query results, parents, event targets and MutationObserver
records refer to the same wrapper. Cache handles are reset before isolate disposal.

Element attributes use shared, per-tag prototype accessors with configurable,
enumerable descriptors. Navigator's repaired `webdriver` is a prototype getter.
Accessors have native getter/setter names, lengths 0/1 and no constructor behavior.
Shared Node/Element attributes accept valid sibling receivers; tag-specific
attributes such as form.action and script.src reject unrelated receivers.

`getElementsByTagName` and `getElementsByClassName` return live HTMLCollection
objects, including indexed access, item/namedItem, iteration, and a shared length
accessor. Element queries exclude the root itself. This intentionally replaces
the old Array/snapshot contract: use `Array.from(collection).map(...)`.
`document.all` is a live, callable V8 undetectable object: it is falsey, loosely
equals null, has typeof undefined, yet is not strictly undefined and exists in
the document property set. It is not a fabricated ordinary undefined value.

`location.port` and anchor/area URL attributes use a dedicated, V8-independent
DOM URL module. It resolves relative references, handles common authority/IPv6
ports, removes HTTP(S) default ports and dot segments, and coordinates URL
component setters with content attributes. The standalone URL constructor keeps
its previous milestone contract.

The Page observation methods are:

```python
page.capture_async_errors()  # opt in; default is disabled
page.load(html, base_url, resources=resources)
page.run_timers()
page.run_jobs()
navigation = page.read_navigation_requests(clear=True)
errors = page.read_async_errors(clear=True)
```

- Navigation returns `requests`, `dropped_requests`, `max_requests` (1000).
  Each request contains operation, resolved url, resource_name and generation.
  assign/replace/reload record intent only: the document/location do not change.
  Records survive load() until cleared; a new Page has an independent buffer.
- Async capture returns `errors`, `dropped_errors`, `max_errors` (1000).
  Each error contains kind, name, message, stack, resource_name, line and column.
  Timer, microtask, Promise rejection, animation-frame, idle and DOM listener
  errors are observable without stopping subsequent callbacks. A later Promise
  handler removes a buffered rejection. Configuration survives load(), while
  the error buffer belongs to one generation. Disabling retains current records.
- Both reads retain the normal busy/disposed guards and strict bool validation.
  No JS-to-Python callback, network stack, background pump or actual navigation is
  introduced. Async capture is disabled on ordinary JSContext and Page by default.

Inline style declarations have stable identity, cssText/attribute reflection,
property reads/writes, item/length, priorities, removal and iteration. Mutation
records use the existing native attribute path. Canvas getContext returns null
because contexts cannot be allocated; width/height reflect attributes. Beacon
calls validate the URL and return false because nothing is queued for sending.
The secure-context battery Promise supplies deterministic defaults (charging,
level 1, chargingTime 0, dischargingTime Infinity). Deprecated persistent quota
callbacks reuse the existing StorageManager estimate; positive allocations are
refused asynchronously with NotSupportedError, while a zero request grants zero.

## Explicit remaining boundaries

- This is not full HTML tree construction, CSS parsing/cascade/layout, rendering,
  or a Canvas 2D/WebGL implementation. General innerText uses the unrendered text
  model. Tail recovery is limited to documents with an explicit html element;
  fragment parsing keeps its previous contract.
- DOM URL reflection is bounded, not full WHATWG/IDNA/noncanonical IPv4 handling;
  base-element changes are not implemented. form.action values retain the M7 raw
  metadata-slot contract; this batch repairs its descriptor and receiver behavior.
- Complete DOM constructor/prototype inheritance, all HTMLAllCollection overloads,
  and conversion of the other historical Array collections are not claimed.
  querySelectorAll remains its historical Array snapshot.
- Battery/quota are deterministic host baselines, not real machine measurements.
  Beacon, navigation and canvas never fabricate successful transmission, loading
  or rendering. No remote challenge, generated cookie or server acceptance is an
  acceptance criterion for this repair batch.

Normative reference points: [DOM live collections](https://dom.spec.whatwg.org/#old-style-collections)
and [HTMLAllCollection](https://html.spec.whatwg.org/multipage/common-dom-interfaces.html#the-htmlallcollection-interface).
