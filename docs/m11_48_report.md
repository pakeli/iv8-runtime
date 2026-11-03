# M11-48 Delivery Report — Node foundation

Status: **VERIFIED — real V8 green on Linux + Windows + macOS arm64 (`2026-11-03`)**

Branch `m11-48-node-foundation`, verified tree `3f4940f`.

This slice replaces the element-only DOM with the approved text-node
architecture: Text/Comment/DocumentFragment nodes, live `textContent`, and
active `characterData` MutationObserver records. It is the foundation for the
later HTML-string/variadic tree-method and `document.write` slices.

## Evidence

- Linux `release-linux` run `41562778133` — cp311 / cp312 / cp313 / cp314,
  each `1744 passed, 2 skipped`.
- Windows `release-windows` run `41562781320` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1744 passed, 2 skipped`.
- macOS arm64 fallback run `41563750355` (`macos-14`, cached V8 monolith,
  `macosx_11_0_arm64` wheels) — cp311–cp314, each `1744 passed, 2 skipped`.

`test/test_m11_48_node_foundation.py` ran in every lane (13 items). The full
suite collects 1746 items; 1744 pass and 2 are skipped.

macOS wheel artifacts: `13197948138` (cp311), `13197938406` (cp312),
`13197622993` (cp313), `13198587130` (cp314).

## Scope landed

- Parser emits Text nodes for all character runs (including whitespace and
  top-level text) and Comment nodes for `<!-- ... -->`; adjacent text merges;
  a non-tag `<` is literal text; doctype/other declarations are skipped.
- `script`/`style` are raw-text elements and `title`/`textarea` are RCDATA;
  their body is one Text child. `<script>` records/execution semantics are
  unchanged; inserted scripts stay inert.
- `NodeKind` numbers match DOM nodeType (1/3/8/11); `document.createTextNode`,
  `createComment`, and `createDocumentFragment` create detached nodes.
- Live `textContent`: Elements/Fragments concatenate descendant Text data
  (comments excluded); assignment replaces children with zero or one Text node;
  CharacterData `data`/`nodeValue`/`textContent` writes update data and queue
  `characterData` records.
- CharacterData exposes `length` in UTF-16 code units; Text/Comment expose
  `childNodes` (empty), null first/last child, raw siblings, parentElement,
  ownerDocument, isConnected, and clone/equality/contains/remove/hasChildNodes.
- DOCUMENTFragment supports child navigation, `textContent`, clone/equality/
  contains/remove, tree editing, `querySelector[All]`, and `getElementById`.
  Inserting a fragment moves its children and empties the fragment; record
  ordering is pinned in the tests and the boundary document.
- Navigation now distinguishes `childNodes` (all kinds) from element
  `children`, raw child/sibling aliases from element variants, and
  `parentElement` (null for a fragment parent).
- Selectors `*`/tag/compound matches, document collections, and query results
  remain element-only; `+`/`~` skip Text/Comment siblings.
- MutationObserver accepts Text/Comment/Fragment targets and document/element
  subtree observation; childList node arrays are node-kind-aware;
  `characterData` old values are exposed when requested.
- Access trace recognizes `text`, `comment`, and `document_fragment` object
  types; they are valid filters and appear in coverage.
- `document.title` now uses a real title Text child (live); the M11-47
  no-title-element extension remains. Textarea value seeds and option
  text/value/label fallbacks use live text.
- No Python API, environment key, or top-level export change.

## Verification notes

- The first full attempt (`0fbfe93`) failed only two new-test expectations:
  `document.contains` is intentionally absent (Document is not a Node yet), and
  `Page.eval` returned an int where the test expected a string. `f3223d7`
  corrected the test only; runtime behavior was unchanged.
- The access-trace object-type whitelists (Python + native) were extended for
  the new node kinds (`3f4940f`).
- Historical element-only guards were updated intentionally for live text,
  node creation, empty-element `textContent` childList records, and active
  `characterData`; no old assertion was weakened to hide a runtime regression.
- Entity decoding, doctype/CDATA nodes, Document-as-Node, and full HTML5
  parsing remain absent, as documented in `docs/m11_48_node_foundation.md`.
- The macOS fallback used the cached V8 monolith on `macos-release` with
  deployment target 11.0; the branch was restored afterward. The official
  `macos-15` lane remains unavailable.

## Outcome

M11-48 is verified and merged to fork `master`. M11-49 builds the HTML
string/serialization APIs and variadic string tree methods on this foundation;
`document.write` and import/adopt follow later.
