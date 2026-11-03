# M11-49 — HTML string APIs and variadic tree methods

Status: **VERIFIED — real V8 green on Linux + Windows + macOS arm64 (`2026-11-03`)**

This slice adds the most common remaining DOM manipulation APIs needed for a
usable headless-browser surface: HTML serialization/parsing through
`innerHTML` / `outerHTML` / `insertAdjacent*`, and string-or-node variadic tree
methods. It builds on the M11-48 Text/Comment/DocumentFragment foundation.

## Surface

```js
el.innerHTML;                    // serialized children
el.innerHTML = '<b>x</b>';       // parse fragment, replace children
el.outerHTML;                    // serialization including this element
el.outerHTML = '<p>x</p>';       // replace this element in its parent

el.insertAdjacentHTML('beforeend', '<i>x</i>');
el.insertAdjacentElement('afterbegin', child);
el.insertAdjacentText('afterend', 'text');

parent.append('text', node, fragment);
parent.prepend(node, 'text');
parent.replaceChildren('a', node);
el.before('x'); el.after(node); el.replaceWith('replacement');
text.replaceWith('replacement');
```

- `innerHTML`/`outerHTML` are writable Element properties; `outerHTML` on a
  parentless element is a no-op. Fragments are parsed with the M11-48 parser:
  scripts inserted this way stay inert, text/comments/whitespace are preserved,
  and parsed form/select/textarea controls are initialized (values, select
  normalization, reset baselines).
- Serialization is deterministic: tag names are lowercase, `id`/`class` come
  first, remaining attributes are ASCII-sorted by name, and values are
  double-quoted. Void elements have no end tag/children. Comments serialize as
  `<!--data-->`. Script/style text is emitted raw; other text/attribute values
  are escaped.
- Bounded character-reference decoding: `&amp; &lt; &gt; &quot; &apos; &nbsp;`
  and decimal/hex numeric references are decoded in normal text, RCDATA
  (`title`/`textarea`), and attribute values. Unknown references stay literal;
  invalid numeric references become U+FFFD. Script/style bodies are raw.
- `append`/`prepend`/`replaceChildren` are available on elements and
  DocumentFragments; `before`/`after`/`replaceWith` on elements, Text, and
  Comment. Node arguments keep identity; strings (and other non-node values)
  are converted with `ToString` into new Text nodes. DocumentFragment arguments
  move their children and empty the fragment.
- `insertAdjacentHTML`/`Element`/`Text` accept the four positions
  `beforebegin`, `afterbegin`, `beforeend`, `afterend`. Invalid positions throw
  a JS `SyntaxError`; `beforebegin`/`afterend` without a parent throws
  `TypeError`; `insertAdjacentElement` requires an Element.
- All methods return `undefined` except `insertAdjacentElement`, which returns
  the inserted element.

## Mutation-record contract

- `innerHTML = ...` and `replaceChildren(...)` queue one `childList` record on
  the target with all removed old children and all added new children
  (previous/next siblings null).
- `append`/`prepend`/`before`/`after` queue one target `childList` addition
  record per call (all inserted nodes, with the insertion-point siblings).
- `replaceWith`/`outerHTML = ...` queue a removal record for the old node and
  an addition record for the new nodes (after queuing any old-parent removals
  for moved nodes).
- Internal temporary fragments are not externally observable, so their
  bookkeeping records are not part of the contract. Adjacent runtime-created
  Text nodes are **not** auto-merged (unlike a full browser DOM insertion).

## Explicit non-goals

- Context-sensitive HTML fragment parsing (`table`/`select`/`textarea`
  insertion modes, foster parenting, implied html/head/body, full HTML5 error
  recovery). The parser remains the M11-48 minimal parser.
- The full HTML named-entity table; only the common bounded set + numeric
  references are decoded.
- `document.write`/`writeln`/`open`/`close`; `importNode`/`adoptNode`.
- `document.innerHTML`/`document.outerHTML`, `<template>` content fragments,
  Document-as-Node, DOM prototypes/`instanceof`.
- CSS/layout-dependent HTML behavior, live NodeList/HTMLCollection objects,
  path/URL resolution changes, and mutation-record ordering beyond the pinned
  contract above.
- EventTarget methods on Text/Comment/Fragment and CharacterData helpers.

## Tests

`test/test_m11_49_html_strings.py` covers serialization/attribute ordering,
entity decoding/escaping and roundtrips, fragment control initialization and
script inertness, outerHTML replacement, all four adjacent positions and
errors, variadic string/node/fragment handling, ChildNode methods, empty
replaceChildren, script/style raw text vs title/textarea escaping, and pinned
mutation records.

Historical guards were updated intentionally in `test_create_element.py`,
`test_document.py`, `test_m3_contract.py`, `test_m4_a_contract.py`, and
`test_tree_editing.py` for the newly exposed members.

## Verification

Real V8 is green for the release-tested tree `52bafab` on Linux, Windows, and
macOS arm64; every wheel lane reports `1756 passed, 2 skipped` and the 12 new
tests execute. Run IDs, artifact IDs, and the test-expectation corrections are
recorded in `docs/m11_49_report.md`.
