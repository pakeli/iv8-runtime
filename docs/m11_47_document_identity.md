# M11-47 — Document identity: title, URL aliases, and view/location

Status: **IN PROGRESS — second Document slice of the common-API track**

This slice continues the object-oriented common-API track with the remaining
commonly used `Document` identity members: a writable `document.title`, the
read-only URL aliases `documentURI` / `baseURI`, and the single-context
`defaultView` / `location` aliases. It adds no navigation, layout, or new
Python API.

## Surface

```js
document.title;                 // parsed <title> text ("" when absent)
document.title = "New title";   // writable
document.documentURI;           // current generation base URL
document.baseURI;               // current generation base URL
document.defaultView;           // === window === globalThis
document.location;              // === location (same LocationHost object)
```

- `title` becomes a writable HostObject property. Assignment uses ordinary
  JavaScript `ToString` coercion; a conversion exception propagates unchanged
  and leaves the title untouched.
- On assignment the document title string and the first `<title>` element's
  aggregate `textContent` are updated together. As with `element.textContent =
  ...`, the title element's element children are detached and, when mutation
  observers exist, a `childList` record is queued for non-empty removals. A
  missing `<title>` element is not created; the document title string still
  updates.
- `documentURI` and `baseURI` both return the current generation base URL; the
  runtime has no `<base href>` resolution, so they equal `document.URL`.
- `defaultView` is the context's global object, so it is identical to `window`
  and `globalThis`; the runtime has exactly one browsing context.
- `location` is the same `LocationHost` wrapper installed as the global
  `location`, preserving identity across reads. It is read-only on `document`
  (no navigation is added).
- The four read-only aliases use the framework's `ReadOnly | DontDelete`
  descriptor shape; strict-mode assignment throws a `TypeError` and sloppy-mode
  assignment is a no-op.
- Values are per generation: a new `Page.load()` rebuilds the document from the
  new HTML/base URL, so a previous `title` override is not carried forward.

## Access trace

All five members use the generic HostObject path:

- reading `title`, `documentURI`, `baseURI`, `defaultView`, or `location`
  records `document` / `<member>` / `get`;
- writing `title` records `document` / `title` / `set` with normal completion
  metadata.

Reading `document.location.href` records the `document` get followed by the
existing `location` get. No new trace event type or Python/JS API is added.

## Descriptor / semantic differences

- `document.title` is a writable native data property; like the other writable
  HostObject properties, `Object.getOwnPropertyDescriptor` materializes a data
  descriptor and evaluates the getter.
- Titles are not escaped, entity-decoded, or re-parsed; assignment stores the
  string verbatim.
- The `title` node pointer is the first `<title>` element found at parse time.
  If scripts remove or replace it, later title assignments still update the
  document string but cannot retarget a new title element.
- `baseURI` does not honor `<base href>`.
- `document.location` is not writable and does not navigate.

## Explicit non-goals

- `document.lastModified`, `document.dir`, `document.designMode`, `document.all`.
- `<base href>` resolution, URL re-parsing, or navigation via
  `document.location`.
- Creating a missing `<title>` element, entity decoding, or text-node
  representation.
- Multi-window/iframe/browsing-context support, `defaultView` nullability.
- Any Python API, environment key, or top-level export change.

## Tests

`test/test_m11_47_document_identity.py` covers:

- initial title, writable assignment with coercion, `<title>.textContent`
  reflection, missing-title behavior, conversion-exception propagation, and
  per-generation reset;
- `documentURI` / `baseURI` values and read-only strict-mode behavior;
- `defaultView` / `location` identity with `window`, `globalThis`, and the
  global `location`, plus `href` agreement;
- access-trace get/set events and value summaries for the new members;
- descriptor-visible writable/read-only shape.
