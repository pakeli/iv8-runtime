# M11-7 — Web Storage (per-Page in-memory)

M11-7 is the seventh implementation slice under the compatibility-first M11
line. It adds the JS globals `localStorage` and `sessionStorage` as minimal,
per-Page in-memory key/value stores. It adds no Python API, no new pump, no
event dispatch, and no disk persistence.

The slice was selected from `docs/m11_compatibility_backlog.md` (rank 3: Web
Storage), batched with M11-8 to share one real-V8 build.

## Public API

Installed per generation by `install_page`:

- **`localStorage`** and **`sessionStorage`** — two independent Storage
  objects. `window.localStorage === localStorage` and
  `window.sessionStorage === sessionStorage` hold because `window` is the
  intrinsic global object.
- **`getItem(key)`** — returns the stored string, or `null` when absent.
- **`setItem(key, value)`** — stores `String(value)` under `String(key)`;
  overwrites an existing key without changing its order.
- **`removeItem(key)`** — removes `String(key)`; a missing key is a no-op.
- **`clear()`** — removes every entry in that store.
- **`key(index)`** — returns the key at `index` in insertion order, or `null`
  when out of range.
- **`length`** — read-only count of entries; a write is a sloppy no-op /
  strict `TypeError`.

Rules:

- Keys and values use `String(...)` coercion, so numbers / booleans / `null`
  become their string forms.
- Insertion order is stable: updating an existing key does not move it;
  `removeItem` removes it from the order; a later `setItem` appends at the end.
- `key(index)` accepts a numeric argument (WebIDL-style unsigned conversion);
  non-finite / out-of-range values return `null`.
- `localStorage` and `sessionStorage` are separate stores; a key in one is not
  visible in the other.
- Methods are bound functions carrying their store pointer; a detached call
  still operates on the same store (a documented departure from browser
  brand checks).

## Lifetime and scope

- Both backings live in `PageState`, so entries persist across `Page.load()`
  generations of the **same** `Page`.
- Entries do **not** persist across `Page` instances, processes, or disk
  reloads, and there is no origin keying: every `load()` of the page sees the
  same stores.
- `Page.dispose()` ends the stores with the page.
- Each generation installs fresh JS Storage objects over the same backing;
  JS object identity never crosses generations.

## Non-goals

M11-7 deliberately does **not** add:

- `storage` events or any cross-page/cross-tab notification;
- disk persistence, a storage directory, or quota enforcement;
- origin isolation (different `base_url` values share one store);
- property access through the Storage object (`localStorage.foo`), a Proxy
  surface, `key()` enumeration helpers, or `storage.getItem === ...` identity
  guarantees;
- `document.cookie`, IndexedDB, Cache API, or `navigator.storage`;
- a Python API, a new pump, or DevTools `watch_apis` integration.

## Files changed

- `include/iv8/page_state.h` — two Page-lifetime storage backings;
- `src/core/page_state.cpp` — Storage globals, method callbacks, `length`
  accessor, per-generation installation;
- `test/test_web_storage.py` — focused acceptance tests;
- `python/iv8/__init__.py` — module-docstring paragraph;
- docs: this boundary note, `docs/m11_7_report.md` (created at verification),
  `docs/m11_compatibility_backlog.md`, `docs/dom_bom_gaps.md`,
  `docs/current_status.md`, `docs/next_tasks.md`, `docs/decision_log.md`, and
  `docs/runtime_surface.md`.

`JSContext`, the native `ContextState` API, and the rest of `Page` are
unchanged.

## Verification

Status: **implementation pending; real-V8 CI verification to follow (batched
with M11-6 and M11-8).**
