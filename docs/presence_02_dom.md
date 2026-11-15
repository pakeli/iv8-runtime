# PRES-02 — DOM logic and attribute reflection

Authorized `2026-11-15`: continue API-family coverage after PRES-01, preserving
shared native prototypes and generation ownership. This batch belongs to the
Page host layer and does not change the M1 JSContext kernel.

## Approved local batch

- Live HTMLElement dataset/DOMStringMap and Element attributes/NamedNodeMap.
  Attr nodes expose names, value, namespace and owner information; attribute
  node/map operations use the same backing values as existing get/set/remove/
  toggleAttribute, reflected fields and MutationObserver records.
- Namespace-aware element/attribute creation and attribute lookup/write/remove.
  namespaceURI/localName/prefix are explicit backing metadata, with shared
  Element/HTMLElement/Attr prototypes chosen from the actual interface kind.
  Namespaced elements do not imply SVG rendering or a complete SVG interface set.
- Node getRootNode, normalize and compareDocumentPosition, reusing Node constants.
  Normalize preserves the first non-empty Text identity, merges adjacent Text
  nodes, removes empty Text nodes and notifies existing observers.
- Document importNode/adoptNode within one Page context. Import clones into the
  receiving document; adoption keeps the original wrappers, listeners and live
  attribute views while changing the subtree's owning document. Document nodes
  remain unsupported inputs. Cross-context/Python callbacks are not introduced.
- DOMParser with functional text/html parsing into an inert, detached Document.
  Reuse the existing bounded HTML parser; ensure html/head/body structure and do
  not execute scripts or load resources. Other valid XML MIME types explicitly
  throw NotSupportedError; invalid MIME types throw TypeError. XMLSerializer and
  XML/HTML5 parser completeness remain later API-family work.

## Integration and acceptance

Use the existing per-context native interface registry for all added members;
methods live on their defining prototypes. Stable nested wrappers are owned for
the generation and release V8 handles before backing objects/isolate teardown.
Cross-document moves must preserve wrapper identity and mutation/collection
ownership rather than creating a second JS object for the same node.

Capture versioned real Chrome observations with execution conditions and probe
hashes, add focused offline functional/prototype/lifecycle tests, then run one
full local real-V8 suite. Preserve History, named access and PRES-01 regressions.
Record candidate artifact/source identity and presence deltas. This development
batch does not automatically run the full release matrix, publish a PR/package
or replace the shared BIND-01 SDK. Latest implemented/local acceptance and latest
release-verified remain separate statuses.
