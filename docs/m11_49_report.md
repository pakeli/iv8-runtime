# M11-49 Delivery Report — HTML string APIs and variadic tree methods

Status: **VERIFIED — real V8 green on Linux + Windows + macOS arm64 (`2026-11-03`)**

Branch `m11-49-html-strings`, verified tree `52bafab`.

This slice turns the M11-48 node foundation into the common HTML-string and
variadic DOM manipulation surface: `innerHTML` / `outerHTML` /
`insertAdjacent*` and `append` / `prepend` / `before` / `after` /
`replaceWith` / `replaceChildren`, with bounded entity decoding and
deterministic serialization.

## Evidence

- Linux `release-linux` run `41577570846` — cp311 / cp312 / cp313 / cp314,
  each `1756 passed, 2 skipped`.
- Windows `release-windows` run `41577574466` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1756 passed, 2 skipped`.
- macOS arm64 fallback run `41578427318` (`macos-14`, cached V8 monolith,
  `macosx_11_0_arm64` wheels) — cp311–cp314, each `1756 passed, 2 skipped`.

`test/test_m11_49_html_strings.py` ran in every lane (12 items). The full suite
collects 1758 items; 1756 pass and 2 are skipped.

macOS wheel artifacts: `13204463364` (cp311), `13204892158` (cp312),
`13205061709` (cp313), `13205116549` (cp314).

## Scope landed

- Element `innerHTML`/`outerHTML` getters and setters. Fragment insertion uses
  the M11-48 parser: scripts stay inert; text/comments/whitespace are preserved;
  input/option/textarea/select/form values and reset baselines are initialized.
  `outerHTML` on a parentless element is a no-op.
- Deterministic HTML serialization: lowercase tags, id/class first, remaining
  attributes ASCII-sorted, double-quoted escaped values, void elements without
  end tags, comments as `<!--data-->`, script/style text raw, other text
  escaped.
- Bounded character-reference decoding in normal text, RCDATA
  (`title`/`textarea`), and attribute values: common named references plus
  decimal/hex numeric references; unknown references stay literal; invalid
  numeric references become U+FFFD.
- `insertAdjacentHTML` / `insertAdjacentElement` / `insertAdjacentText` with
  the four positions, SyntaxError for invalid positions, TypeError without a
  required parent, and Element-only `insertAdjacentElement`.
- Variadic `append`/`prepend`/`replaceChildren` on Element and
  DocumentFragment; `before`/`after`/`replaceWith` on Element, Text, and
  Comment. Node arguments preserve identity; other values are stringified into
  Text nodes; DocumentFragment arguments move and empty their children.
- Pinned mutation records: replace-all operations queue one childList record
  (removed + added); append/prepend/before/after queue one addition record;
  replaceWith/outerHTML queue removal + addition.
- Historical guards updated intentionally for the new element surface; no
  Python API, environment key, or top-level export change.

## Verification notes

- The first attempt (`e9af2c6`) failed 7 new tests because innerHTML/outerHTML
  were in `writable_property_names()` but missing from the setter's early name
  guard; the runtime never parsed the assignment. `8739b1a` routed them through
  the setter. Runtime design was correct.
- The second attempt (`8739b1a`) failed only two test expectations: innerHTML
  is children-only (attributes belong to outerHTML), and `Array.join` renders
  `null` as an empty field. `52bafab` corrected the tests without runtime
  changes.
- All historical tests passed on the first run; only the intentional guard
  updates and the new-file expectation fixes were needed.
- Context-sensitive fragment parsing (table/select/textarea insertion modes),
  the full named-entity table, `document.write`, and prototype chains remain
  out of scope and are documented in `docs/m11_49_html_strings.md`.
- The macOS fallback used the cached V8 monolith on `macos-release` with
  deployment target 11.0; the branch was restored afterward. The official
  `macos-15` lane remains unavailable.

## Outcome

M11-49 is verified and merged to fork `master`. At the user's direction the
next slice became M11-50 Window completion; `document.write`/`writeln`/
`open`/`close` plus `importNode`/`adoptNode` were deferred behind the
Window-first slices that followed.
