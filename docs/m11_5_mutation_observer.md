# M11-5 — `MutationObserver` (manual microtask delivery)

M11-5 is the fifth implementation slice under the compatibility-first M11 line.
It adds a minimal, deterministic `MutationObserver` whose records are delivered
by the existing `Page.run_jobs()` microtask checkpoint. It adds no new Python
API, no new pump, and no background observation.

The scope was selected from `docs/m11_compatibility_backlog.md`; the target
community edition lists `MutationObserver` in its DOM/micro-task surface.

## Public API

Installed per generation by `install_page`:

- **`new MutationObserver(callback)`** — `callback` must be callable. Calling it
  without `new` throws a JS `TypeError`.
- **`observer.observe(target, options)`** — registers `target`; returns
  `undefined`.
- **`observer.disconnect()`** — drops all registrations and pending records;
  returns `undefined`.
- **`observer.takeRecords()`** — returns pending records as a plain JS `Array`
  and clears them.
- `window.MutationObserver === MutationObserver` holds because `window` is the
  intrinsic global object.
- **No `MutationRecord` global.** Records are plain objects with the fields
  below.
- **Python: no new method.** There is no `Page.observe()` and no module-level
  `iv8.MutationObserver`.

## `observe()` target and options

`target` must be an element host object or `document`.

- `document` is accepted as a whole-document target. Because this DOM never
  mutates the document node itself, a document registration only produces
  records when `subtree: true`; without `subtree` it is accepted but inert.
- Anything else (`null`, strings, plain JS objects, non-element host objects)
  throws a JS `TypeError`.

`options` must be an object (`null` / missing / non-object → `TypeError`).

- Boolean fields use WebIDL truthiness: `childList`, `attributes`,
  `characterData`, `subtree`, `attributeOldValue`, `characterDataOldValue`.
- At least one of `childList` / `attributes` / `characterData` must be true.
- `attributeOldValue: true` without `attributes: true` → `TypeError`.
- `attributeFilter` present without `attributes: true` → `TypeError`. It must be
  a JS `Array`; each entry is `String()`-converted and ASCII-lowercased. An
  empty filter matches no attributes; an absent filter matches all.
- `characterDataOldValue: true` without `characterData: true` → `TypeError`.
- `characterData` is accepted but inert: this DOM is element-only, so no
  characterData records can ever be produced.
- Re-observing the same `(observer, target)` replaces the old options; pending
  records are not cleared by that replacement.
- Calling `observe` / `disconnect` / `takeRecords` on a non-MutationObserver
  receiver throws `TypeError("Illegal invocation")`.

## Observed mutations

### `childList`

- `appendChild` / `insertBefore` queue one addition record on the new parent.
  `previousSibling` / `nextSibling` are the siblings after insertion.
- `removeChild` queues one removal record on the old parent. Siblings are
  captured before detaching.
- Moving a node across parents queues a removal record on the old parent first,
  then an addition record on the new parent. Same-parent reordering also queues
  removal + addition.
- `insertBefore(child, child)` remains the existing no-op and queues no record.
- `textContent = ...` with children queues one removal record containing all
  removed children (`previousSibling` / `nextSibling` null). With no children it
  queues nothing.
- `createElement` is creation, not a tree mutation; it queues nothing.

### `attributes`

- `setAttribute(name, value)` queues a record even when the value is unchanged.
  This matches the DOM specification and browser behavior.
- `removeAttribute(name)` queues a record only when the attribute existed.
- `attributeName` is lowercase; `attributeNamespace` is always `null`.
- When `attributeOldValue: true`, `oldValue` is the previous string, or `null`
  when the attribute was absent. Otherwise `oldValue` is `null`.
- The `id` and `class` dedicated fields are observed through `setAttribute` /
  `removeAttribute` as attribute names `id` / `class`.
- **Out of scope:** `.id = ...` and `.className = ...` property setters did not
  exist when M11-5 landed (their getters were read-only), so they produced no
  records. **Superseded by M11-12:** both are now writable and queue the same
  `id` / `class` attribute records as `setAttribute`. `.value` / `.checked` /
  `.selected` and the form runtime slots are not attribute mutations and produce
  no records. `form.reset()` produces none.

### Matching

- Without `subtree`, a registration matches only mutations whose mutation target
  is the registered node.
- With `subtree`, it also matches mutations on descendants (via the existing
  parent links).
- A document registration with `subtree` matches all descendants; without
  `subtree` it matches nothing, since no JS API mutates the document node.
- Multiple matching registrations on the same observer produce at most one
  record for that mutation.

## Record shape

Each record is a fresh plain JS object:

- `type` — `"childList"` or `"attributes"`.
- `target` — the parent (childList) or the element (attributes), wrapped as an
  element host object.
- `addedNodes` / `removedNodes` — plain JS `Array`s of element host objects.
- `previousSibling` / `nextSibling` — element host object or `null`
  (childList).
- `attributeName` — string (attributes) or `null` (childList).
- `attributeNamespace` — always `null`.
- `oldValue` — string or `null`.

Wrapper identity is **not** guaranteed: nodes in records are built through the
existing `wrap_element` path, which creates a fresh JS object per call (the same
model as the existing DOM collections). Tests assert properties such as
`record.target.id`, not object identity.

## Delivery model

- Records are queued in mutation order per observer.
- On the first pending record for an observer, one delivery is scheduled with
  `isolate->EnqueueMicrotask(...)` — the same V8 microtask queue used by
  `queueMicrotask` and `Promise`.
- Nothing runs until Python calls `Page.run_jobs()`. `run_timers()`,
  `run_animation_frame()`, `run_idle_callbacks()`, and ordinary `eval()` do not
  deliver observer records.
- The callback receives `(recordsArray, observer)` with `this === observer`; it
  runs once per observer per pending batch.
- A throwing callback is swallowed; other observers still run, `run_jobs()` does
  not raise, and the page remains usable.
- Mutations made inside an observer callback enqueue a new delivery microtask;
  the same `run_jobs()` checkpoint drains it (the M11-4 nested-microtask model).
- No new pump, clock, background thread, or scheduler is introduced.

## `takeRecords()` / `disconnect()`

- `takeRecords()` returns the pending records and clears the queue. A delivery
  microtask already queued then finds nothing and does not call the callback.
- `disconnect()` drops all registrations and pending records. A queued delivery
  microtask becomes a no-op.

## Generation behavior

- Observer state belongs to the current generation's DOM (`DocumentHost`).
- `Page.load()` tears down the generation; old observers and pending records are
  discarded. The new generation installs a fresh `MutationObserver` global.
- There are no cross-generation observers.
- `Page.run_jobs()` after `dispose()` still raises `JSContextDisposedError`
  (the existing rule).
- No JS-finalizer-based unregistration: observer C++ state lives until
  generation teardown, like `createElement` nodes and element wrappers. V8
  persistent handles are released by the existing isolate-alive teardown hook.

## Independence from the other queues

`MutationObserver` uses the existing microtask queue:

- `run_jobs()` drains Promise reactions, `queueMicrotask` callbacks, and
  observer deliveries; it does not fire timers, frames, or idle callbacks;
- `run_timers()` / `run_animation_frame()` / `run_idle_callbacks()` do not drain
  microtasks and do not deliver observer records;
- there is no separate observer pump.

The runtime still has exactly four manual pumps.

## Scope / non-goals

M11-5 deliberately does **not** add:

- a Python API, a fifth pump, or an automatic event loop;
- `characterData` / text-node mutation records (this DOM has no text nodes);
- a `MutationRecord` constructor/global;
- `id` / `className` property setters (existing read-only behavior at M11-5;
  M11-12 later added them);
- document-node mutation records beyond subtree matching;
- JS-finalizer-based observer cleanup;
- `watch_apis()` integration for observer methods;
- CSS/layout, networking, navigation, storage, workers, or a real scheduler;
- changes to `JSContext` or the native `ContextState` API.

## Files changed

- `src/core/page_state.cpp` — observer state/records, mutation hooks,
  constructor/prototype/delivery, installation, teardown release;
- `test/test_mutation_observer.py` — focused acceptance tests;
- `python/iv8/__init__.py` — module-docstring paragraph;
- `python/iv8/page.py` — `run_jobs()` docstring;
- docs: this boundary note, `docs/m11_5_report.md`,
  `docs/m11_compatibility_backlog.md`, `docs/runtime_surface.md`,
  `docs/current_status.md`, `docs/next_tasks.md`, `docs/decision_log.md`, and
  pointer updates in the M11-4 non-goal lists.

`JSContext` and the native `ContextState` API are unchanged.

## Verification

Status: **verified on real V8 (Linux + Windows), `2026-10-28`.**

- Focused tests: `test/test_mutation_observer.py` — 52 items.
- Real-V8 CI on commit `4fa6469`: Linux run `40632597636` (cp311–cp314) and
  Windows run `40632601387` (Python 3.11–3.14); every wheel lane reported
  `1265 passed, 2 skipped` — the M11-4 baseline (`1213 passed, 2 skipped`)
  plus the 52 new items. `test/test_mutation_observer.py` has its own progress
  line in every lane.
- Two post-implementation fixes were required and are part of the verified
  commit: `ab1d507` (V8 15.0 requires `ExternalPointerTypeTag` on
  `External::New` / `External::Value`) and `4fa6469` (the tests set ids via
  `setAttribute`, since this runtime has no `.id` setter). See
  `docs/m11_5_report.md`.
- `ci.yml` remains the skeleton shape-guard lane; macOS is outside acceptance
  scope.

## Open points

- The four-pump composition question remains open (`docs/decision_log.md` §A).
- Wrapper identity remains the runtime's existing limitation; a future contract
  decision would be needed to change it.
- A ranked compatibility matrix now exists
  (`docs/m11_compatibility_backlog.md`); M11-6 selection should use it.
