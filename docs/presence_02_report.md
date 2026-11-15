# PRES-02 local acceptance report

Date: `2026-11-15`. **Implemented and locally accepted; release verification
pending.** Scope: [presence_02_dom.md](presence_02_dom.md). The shared SDK and
latest release/deployment-verified version remain BIND-01.

## Delivered behavior and prototypes

- HTMLElement dataset is a stable, live DOMStringMap. Reads, writes, deletes,
  key conversion, enumeration and data-property definition operate on actual
  data-* attributes. `__proto__` is an attribute key and leaves the prototype
  intact. Accessor property definitions fail explicitly.
- Element attributes is a stable, live NamedNodeMap with indexed/item/named
  lookup, insertion/replacement/removal and namespace variants. Attr wrappers
  keep identity, reflect value changes from other attribute APIs, retain values
  when detached and expose ownerElement/ownerDocument and namespace information.
  Attribute-node and namespace operations share existing reflected fields and
  MutationObserver records; namespaced attributes also serialize.
- Namespace-aware element/attribute creation validates names and namespace
  constraints. namespaceURI/localName/prefix and case-sensitive namespace
  queries use explicit metadata. getElementsByTagNameNS returns a live collection
  with namespace/local-name wildcards and excludes the Element receiver itself.
- Node getRootNode, normalize and compareDocumentPosition use the existing
  shared Node interface/constants. Normalize removes empty Text nodes, merges
  adjacent text while retaining the first non-empty node, and emits the measured
  per-merge observation sequence. Namespace metadata/values participate in
  cloning and equality.
- Document importNode creates an independent shallow/deep clone in the receiving
  document without listeners/handlers. adoptNode retains the original wrappers,
  listeners, handlers, cached collection/classList/dataset/attribute views and
  changes the owning document throughout the subtree. Existing pending mutation
  records remain canonical through repeated cross-document moves.
- DOMParser text/html creates an inert independent HTMLDocument, including
  html/head/body recovery for fragment input. URL/documentURI/baseURI inherit
  the active document URL; defaultView and ownerDocument are null. Parsing
  executes no scripts or resource loads. Invalid MIME types throw TypeError;
  valid XML MIME types explicitly throw NotSupportedError in this batch.

| Family | Defining prototype / ownership |
|---|---|
| dataset | HTMLElement.prototype getter → DOMStringMap.prototype |
| attributes | Element.prototype getter → NamedNodeMap.prototype |
| Attr | Attr.prototype → Node.prototype; illegal public constructor |
| Namespace element properties/methods | Element.prototype |
| Tree helpers | Node.prototype |
| Creation/import/adopt | Document.prototype |
| HTML parsing | DOMParser.prototype.parseFromString; native public constructor |

NamedNodeMap and DOMStringMap also have illegal public constructors. All members
use the existing native interface registry, including native operation name,
arity, source and non-constructability. Attribute views share canonical storage;
they are not per-instance copies. Attr has its own backing representation and
is never cast to NodeHost. Its invalid child-tree edits throw explicit errors.
All added wrappers and native handles are generation-owned and released before
the isolate is destroyed.

## Measured presence change

The same Chrome 157 inventory and membership probe show these remaining missing
names. Before is the locally accepted PRES-01 candidate, not the shared SDK.
Every added name and all 22 targets are recorded in
[presence_02_api_delta.json](presence_02_api_delta.json). **No previously exposed
reference names disappeared.**

| Reference target | Before missing | After missing |
|---|---:|---:|
| Window, including interface entries | 874 | 870 |
| Document | 98 | 89 |
| div sample | 178 | 160 |
| form sample | 183 | 165 |
| input sample | 220 | 202 |
| canvas sample | 190 | 172 |

Other targets are unchanged. Window gains Attr, DOMParser, DOMStringMap and
NamedNodeMap; Document gains nine measured names, and each HTML sample gains
18 inherited names. Inherited paths overlap and must not be summed as independent
APIs. The original [inventory](browser_api_inventory.json) stays the BIND-01
baseline; exposed names do not certify complete browser implementation.

## Local acceptance and identity

- Base commit: `abe1bacd1ca42ea697cfa2ad209dcbdffeb36c2d`. The final local source
  commit/tree, changed file hashes and log hashes are recorded in
  `data/presence-02-2026-11-15/source-receipt.json`.
- Real V8 on macOS arm64/Python 3.13. Focused checks: **489 passed, 1 xfailed**.
  Full suite: **2454 passed, 2 skipped, 1 xfailed**. There are 37 new cases across
  the three presence_02 modules. Existing DOM, History, named access, PRES-01,
  native binding, monitoring and lifecycle regressions pass.
- Logs: `data/presence-02-2026-11-15/cp313/focused-accepted.log` and
  `cp313/full-accepted.log`. The isolated candidate installation is
  `data/presence-02-2026-11-15/cp313/site`.
- Wheel `iv8-0.0.1-cp313-cp313-macosx_11_0_arm64.whl`, SHA-256
  `f48ffae229bf34c6c75170807926afc3bab6b7679936073d15b5578384922eea`.
- Native SHA-256:
  `fe7f2476263e88951b09b5e498ca19ce43538341251943bcb748ecb1a97e225d`.
- Browser fixtures: `test/fixtures/presence_02/{probe.js,chrome.json}`. Chrome
  157.0.8103.51, fresh isolated headless profile on macOS arm64, only loopback
  HTTP, background networking disabled and non-loopback DNS blocked. All 21
  captured groups completed in Chrome; XML parsing/serialization observations
  are retained as later-work evidence and excluded from required parity checks.
  Probe SHA-256:
  `c965caaaaa3a3e0bb262cb00685985b694c21e97b0be15baa84bfb513d4a2b74`.
- The unchanged presence-inventory probe SHA-256 is
  `e25097de56865629168f2d48fc55fb48f881f62f540198ab7ad1bd83fdba3060`.
- No hosted run/artifact IDs or 12-lane release results exist for PRES-02. The
  shared iv8-test native remains BIND-01 SHA-256
  `d29024112409b373a9a714d9dcd9bb532e22716b2164d5d9b4f773c9b94eef89`.

## Recorded limits and next batch

The existing HTML parser is bounded rather than HTML5-complete. XML parsing and
XMLSerializer are absent. Non-HTML namespace elements use Element; complete SVG
interfaces/rendering and namespace-qualified selectors are not added. Existing
serialization retains its prior null-namespace ordering and does not implement
complete XML namespace fixup. Specialized prefixed HTML controls remain bounded.
Cross-document insertion still requires the explicit import/adopt path. Attr's
mixed-node position masks, EventTarget behavior and detailed DOMException class
fidelity remain later conformance work. These limits do not imply a network,
layout, trusted-input or cross-context execution subsystem.

Next coverage candidate: offline Web data objects, starting with DOMException,
AbortController/AbortSignal, Headers/Request/Response and FormData. Full release
verification and shared SDK replacement remain a later grouped delivery step.
