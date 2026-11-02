# M11-46 — Document.cookie

Status: **VERIFIED — real V8 green on Linux + Windows + macOS arm64 (`2026-11-02`)**

This is the first slice of the object-oriented common-API completion track:
finish a browser object's most-used surface before moving to the next object.
This slice starts with `Document` and implements `document.cookie` as a
deterministic, in-memory, page-scoped cookie jar. It does not add networking,
navigation, or a real browser cookie store.

## Surface

`document.cookie` becomes a writable HostObject property:

```js
document.cookie;                 // "" initially
document.cookie = "a=1";         // add or update
document.cookie = "b=2; path=/";
document.cookie;                 // "a=1; b=2"
document.cookie = "a=; max-age=0";
document.cookie;                 // "b=2"
```

- The value is a single string; the setter processes only the first
  `name=value` pair in a browser-style `Set-Cookie`-like string.
- Names and values are trimmed of ASCII whitespace. An input without `=` or
  with an empty name is ignored.
- Setting an existing name updates its value in place; a new name is appended.
- `max-age` with a value `<= 0` deletes the named cookie. `expires` deletes it
  when it parses to a time `<=` the deterministic cookie epoch
  `1970-01-01T00:00:00Z`. Positive `max-age` and later `expires` values do not
  expire during the Page lifetime (there is no real clock).
- Cookie attributes are case-insensitive; `path`, `domain`, `secure`, and
  `SameSite` are accepted but ignored. Cookies are visible to every document
  generation of the same `Page`; there is no origin/path filtering.
- The getter joins entries in insertion/update order with `"; "`.
- Assigning a non-string first coerces it with ordinary JavaScript
  `ToString`; a conversion exception propagates unchanged.

## Storage and lifetime

The jar is owned by `PageState`, not by a document generation:

- it survives `Page.load()` generations;
- separate `Page` instances have independent jars;
- it is never written to disk or shared across processes;
- `Page.dispose()` discards it with the Page.

## Access trace

The new property uses the existing HostObject getter/setter path, so tracing
records `object_type: "document"`, `label: "document"`, `member: "cookie"`,
with normal `get`/`set` completion metadata. No dedicated trace event is added.

## Descriptor / semantic differences

`document.cookie` is a writable native data property on the HostObject
instance, not the browser's `Document.prototype` accessor. As with the other
writable HostObject properties, `Object.getOwnPropertyDescriptor` materializes
the current value into a data descriptor and therefore invokes the getter:
`{value: <cookie string>, writable: true, enumerable: true,
configurable: false}`. Cookie strings are not percent-encoded or decoded;
CR/LF/control-character validation, quoted values, and cookie-name syntax
rules are not modeled.

## Explicit non-goals

- Real HTTP Cookie headers, network requests, `Set-Cookie` response handling.
- Origin/path/domain/port/secure/SameSite filtering.
- `HttpOnly`, `SameSite` enforcement, browser cookie UI, third-party cookies.
- Persistence across `Page` instances, processes, or disk.
- Cookie expiration based on wall-clock time or `Page.event_loop_time_ms`.
- Cookie change events (`cookieStore`, observers), `document.cookieStore`.
- `document.write`/`writeln`, text nodes, `createTextNode`, DocumentFragment,
  and other Document APIs (future Document slices).

## Tests

`test/test_m11_46_document_cookie.py` covers:

- initial empty value, add/update/order, coercion, and malformed input;
- attribute parsing, `max-age` deletion, epoch `expires` deletion, and ignored
  path/domain/secure/SameSite attributes;
- persistence across `load()` and isolation between Pages;
- access-trace get/set events for the `cookie` member;
- descriptor-visible writable behavior and no-JS-regression checks.

## Verification

Real V8 is green for the release-tested tree `18f1f4b` on Linux, Windows, and
macOS arm64; every wheel lane reports `1725 passed, 2 skipped` and the five new
tests execute. Verification counts, run IDs, and the descriptor-assumption fix
are recorded in `docs/m11_46_report.md`.
