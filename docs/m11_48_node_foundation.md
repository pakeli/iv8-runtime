# M11-48 — Node foundation: text/comment nodes, DocumentFragment, live textContent

Status: **VERIFIED — real V8 green on Linux + Windows + macOS arm64 (`2026-11-03`)**

This is the foundational slice of the object-oriented common-API track's
text-node architecture. It replaces the element-only DOM (and its stored
`textContent` aggregate) with first-class Text/Comment/DocumentFragment nodes
and browser-live `textContent`. It unblocks `innerHTML`/`outerHTML`,
string-argument tree methods, and eventually `document.write`, which are later
slices.

## Surface

```js
const t = document.createTextNode('hello');   // nodeType 3, data/nodeValue
const c = document.createComment('note');     // nodeType 8
const f = document.createDocumentFragment();  // nodeType 11
el.appendChild(t);
// el.append(...) is a later slice (string/variadic semantics).
document.title = 'New';                       // reflected into the <title> text node
```

- `NodeKind` numeric values: Element 1, Text 3, Comment 8, DocumentFragment 11.
- `document.createTextNode(data)`, `createComment(data)`,
  `createDocumentFragment()` return detached nodes owned by the page
  generation (fragments stay detached until inserted).
- Parser text emission:
  - every non-empty character run becomes a Text child at its source position,
    including whitespace and top-level text; adjacent runs merge;
  - `<!-- ... -->` becomes a Comment node (doctype/other declarations are
    skipped);
  - `script`/`style` are raw-text elements and `title`/`textarea` are RCDATA:
    their bodies are one Text child (not tag-parsed); `<script>` still produces
    a ScriptRecord and stays inert when created by script;
  - a `<` that cannot start a tag is literal text;
  - entities are not decoded (unchanged limitation).
- `textContent` is computed live:
  - Element/DocumentFragment concatenate descendant Text data in tree order and
    exclude Comment data;
  - Text/Comment return their own `data`;
  - Element/Fragment assignment stringifies, removes all children, and adds
    zero (empty string) or one Text node; it queues one `childList` record;
  - Text/Comment `textContent`/`data`/`nodeValue` assignment writes `data` and
    queues a `characterData` record (old value exposed when requested).
- CharacterData exposes `nodeValue`, `data`, `length` (UTF-16 code units), and
  `textContent`; `data`/`nodeValue`/`textContent` are writable.
- Node navigation:
  - Element/DocumentFragment/Text/Comment: `nodeType`, `nodeName`, `parentNode`,
    `parentElement`, `previousSibling`/`nextSibling`, `ownerDocument`,
    `isConnected`, `contains`, `cloneNode`, `isSameNode`, `isEqualNode`,
    `remove`, `hasChildNodes`;
  - Element/DocumentFragment: `childNodes` (all kinds), `children` (elements),
    `firstChild`/`lastChild` (all kinds), `firstElementChild`/
    `lastElementChild`/`childElementCount`, plus element sibling variants on
    Elements; Text/Comment report empty `childNodes` and null first/last child;
  - `previousElementSibling`/`nextElementSibling` skip Text/Comment nodes.
- Tree editing accepts any node kind. A DocumentFragment argument moves its
  children into the target and empties the fragment:
  - append/insert queue a fragment `childList` removal record then a target
    addition record (previous/next siblings captured at the insertion point);
  - `replaceChild(fragment, old)` queues fragment removal, target removal of the
    old child, then target addition of the moved children;
  - an empty fragment is a validated no-op for append/insert and acts as
    "remove old" for replaceChild;
  - cycle insertion throws a TypeError.
- Element/comment/text identity and clone semantics include `kind`/`data`.
- Selectors and document collections remain element-only:
  - `selector_match_compound` rejects non-elements; `*` and tag queries only
    match elements; `+`/`~` use previous element siblings (whitespace text does
    not break them);
  - `document.children`, `children`, query results, and document collections
    stay element-only; `document.childNodes` remains absent (Document is not a
    Node in this slice).
- MutationObserver:
  - `characterData`/`characterDataOldValue` are now active (previously
    accepted-but-inert);
  - Text/Comment targets and subtree/document-subtree matching are supported;
  - `childList` records wrap added/removed/sibling nodes by kind;
  - `characterData` record shape: `type: "characterData"`, target, null
    siblings/attributeName, oldValue only when requested, empty
    added/removed arrays.
- Access trace: Text/Comment/DocumentFragment hosts report object types
  `text`, `comment`, `document_fragment`; these are valid `object_types`
  filters and appear in trace coverage.
- EventTarget methods are **not** exposed on Text/Comment/DocumentFragment in
  this slice; element/document/window bubbling is unchanged.

## Deviations / explicit limits

- Document is not a Node: no `document.childNodes`, `document.firstChild`,
  `document.appendChild`, or DocumentType nodes; top-level text/comments are
  internal roots.
- No HTML5 parser: entity decoding, foster parenting, implied html/head/body,
  CDATA, and full error recovery remain absent.
- No CharacterData mutation helpers (`appendData`/`insertData`/`deleteData`/
  `replaceData`/`substringData`/`splitText`), `normalize`,
  `getRootNode`, or `compareDocumentPosition`.
- No global `Node`/`Text`/`Comment`/`DocumentFragment`/`Element` constructors
  and no `instanceof` support.
- Collections remain plain Arrays (no NodeList/HTMLCollection, no wrapper
  identity guarantee); `childNodes` and `children` are snapshots per access.
- Fragment queries return descendant elements only; Element queries retain the
  runtime's include-self behavior.
- `document.title` keeps the M11-47 extension: without a parsed `<title>` it
  stores a document-only string and creates no element. With a title element it
  is live over that element's Text children.
- Fragment insertion mutation-record ordering is the runtime contract pinned
  above (not necessarily identical to a full browser engine).
- No DOM Range/Selection/TreeWalker/NodeIterator, `document.write`, or
  `innerHTML` family yet.

## Non-goals (later milestones)

- `innerHTML`/`outerHTML`/`insertAdjacentHTML|Element|Text` and HTML
  serialization/escaping.
- `append`/`prepend`/`before`/`after`/`replaceWith`/`replaceChildren` string
  arguments and variadic node methods.
- `document.write`/`writeln`/`open`/`close`; `importNode`/`adoptNode`.
- Document-as-Node, Node constructors, text/comment/fragment event methods,
  CharacterData helpers, Range/Selection/TreeWalker, live NodeList objects.

## Tests

`test/test_m11_48_node_foundation.py` covers parsing order/whitespace/comments/
raw-RCDATA, creation shape, live textContent, fragments and insertion records,
node-vs-element navigation, selector filtering, clone/equality/contains/
connectivity, tree editing with text/fragments, characterData and childList
observers, title/textarea/option consumers, and access-trace object types.

Historical element-only guards were updated intentionally:
`test_node_element.py`, `test_children_collection.py`, `test_tree_editing.py`,
`test_contains.py`, `test_create_element.py`, `test_m3_contract.py`,
`test_m4_a_contract.py`, `test_m4_b_contract.py`, `test_m5_contract.py`,
`test_m6_contract.py`, `test_m7_contract.py`, `test_mutation_observer.py`.

## Verification

Real V8 is green for the release-tested tree `3f4940f` on Linux, Windows, and
macOS arm64; every wheel lane reports `1744 passed, 2 skipped` and the 13 new
tests execute. Run IDs, artifact IDs, and the test-expectation corrections are
recorded in `docs/m11_48_report.md`.
