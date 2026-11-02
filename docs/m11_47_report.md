# M11-47 Delivery Report — Document identity

Status: **VERIFIED — real V8 green on Linux + Windows + macOS arm64 (`2026-11-02`)**

Branch `m11-47-document-identity`, verified tree `cc1c47d`.

This is the second slice of the object-oriented common-API track: the remaining
commonly used `Document` identity members after M11-46 `document.cookie`.

## Evidence

- Linux `release-linux` run `41483937440` — cp311 / cp312 / cp313 / cp314,
  each `1731 passed, 2 skipped`.
- Windows `release-windows` run `41483943983` — Python 3.11 / 3.12 / 3.13 /
  3.14, each `1731 passed, 2 skipped`.
- macOS arm64 fallback run `41485210986` (`macos-14`, cached V8 monolith,
  `macosx_11_0_arm64` wheels) — cp311–cp314, each `1731 passed, 2 skipped`.

`test/test_m11_47_document_identity.py` ran in every lane (6 items). The full
suite collects 1733 items; 1731 pass and 2 are skipped.

macOS wheel artifacts: `13160583829` (cp311), `13161416896` (cp312),
`13162351165` (cp313), `13161382457` (cp314).

## Scope landed

- `document.title` is writable. Assignment uses ordinary `ToString` coercion
  (conversion failures propagate unchanged), updates the document title string,
  and reflects the value into the first parsed `<title>` element's aggregate
  `textContent`. Element children are detached like `textContent = ...`; a
  missing `<title>` is not created. A new `Page.load()` rebuilds the title from
  the new HTML.
- `document.documentURI` and `document.baseURI` return the current generation's
  base URL. They equal `document.URL`; `<base href>` is not resolved.
- `document.defaultView` returns the context's global object, so it is identical
  to `window` and `globalThis` (single browsing context).
- `document.location` returns the same `LocationHost` wrapper installed as the
  global `location` (identity preserved). It is read-only on the Document and
  adds no navigation.
- The four aliases use `ReadOnly | DontDelete`; strict-mode assignment throws a
  `TypeError`.
- Reads of all five members and writes to `title` use the generic HostObject
  path and are visible to the access trace with ordinary completion metadata.
  No new Python API, environment key, or JS-visible API was added.

## Verification notes

- The slice passed all three release lanes on the first attempt; there were no
  test corrections.
- `Uncaught Error: boom` stderr after the pytest summary is a pre-existing test
  artifact and does not affect the exit code.
- The macOS fallback build used the cached V8 monolith on the
  `macos-release` branch with deployment target 11.0; the official `macos-15`
  lane remains unavailable. The branch was restored to its previous tip after
  the run.

## Outcome

M11-47 is verified and merged to fork `master`. The Document track continues
with the next commonly used surface (see `docs/next_tasks.md`).
