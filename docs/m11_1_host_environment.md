# M11-1 — configurable host-environment baseline

M11-1 is the first implementation slice under the compatibility-first M11 line (see
`docs/m11_compatibility_plan.md`). It makes the page's read-only host-environment
surface **configurable at construction** instead of hard-coded, and adds the `screen`
global plus three `window` dimension properties.

It is the first new **Python-side** `Page` surface since M3-5's `resources`
parameter: one constructor parameter and one static method.

## Public API

- **`Page(environment=None)`** — an optional mapping configuring the page's
  environment baseline. Omitted / `None` / `{}` yields the documented defaults;
  supplied keys override, and the rest are inherited. The mapping is **copied**, so
  mutating it after construction does not affect the page. The environment is
  **fixed for the page's lifetime** — there is no setter and no re-configuration API.
- **`Page.get_default_environment()`** — returns the default template as a plain
  `dict`. It is a **fresh copy** on each call (mutating the result affects nothing),
  is callable on the class without a page, and works in a V8-free skeleton build and
  after `dispose()`, since it reads no native state.

The intended usage is read-template / override / pass back:

```python
env = iv8.Page.get_default_environment()
env["userAgent"] = "Mozilla/5.0 (X11; Linux x86_64) custom/1.0"
env["devicePixelRatio"] = 2.0
page = iv8.Page(environment=env)
```

## Supported keys and defaults

The key set is **closed** — exactly these eleven, with these types:

| Key | Type | Default | JS surface |
|---|---|---|---|
| `userAgent` | `str` | `"Mozilla/5.0 (compatible; iv8)"` | `navigator.userAgent` |
| `platform` | `str` | `"iv8"` | `navigator.platform` |
| `language` | `str` | `"en-US"` | `navigator.language` |
| `webdriver` | `bool` | `False` | `navigator.webdriver` |
| `screenWidth` | `int` | `1920` | `screen.width` |
| `screenHeight` | `int` | `1080` | `screen.height` |
| `availWidth` | `int` | `1920` | `screen.availWidth` |
| `availHeight` | `int` | `1040` | `screen.availHeight` |
| `devicePixelRatio` | `int` or `float` → stored `float` | `1.0` | `window.devicePixelRatio` |
| `innerWidth` | `int` | `1280` | `window.innerWidth` |
| `innerHeight` | `int` | `720` | `window.innerHeight` |

The four `navigator` defaults are exactly the constants M2-3 shipped, so an
**unconfigured page is behaviourally unchanged** by this milestone.

Every default is a fixed constant. Nothing is derived from the real host OS, display,
or locale — a page reports identical values on Linux and Windows, which keeps the
runtime deterministic. `availWidth` / `availHeight` are independent configured values,
never computed from `screenWidth` / `screenHeight`.

## Validation

Strict, and performed entirely in the Python facade **before any native page object
exists**, so a rejected environment can never leave a partially-installed page:

- a non-mapping `environment` → `TypeError("environment must be a mapping")`;
- an unknown key → `TypeError("unknown environment key: ...")` (no silent ignore, no
  case-insensitive matching);
- a wrong-typed value → `TypeError("environment['<key>'] must be ...")`.

The integer keys **reject `bool`** explicitly: `bool` is a subclass of `int` in
Python, so `screenWidth=True` would otherwise pass as a dimension.
`devicePixelRatio` accepts an `int` or a `float` and stores a `float`.

Validation runs **before** the V8-link check, so in a V8-free skeleton build a bad
environment still raises `TypeError` rather than `RuntimeError`.

## Semantics

- **Read-only from JS.** `navigator` / `screen` use the host-object framework's
  read-only accessors; the three `window` properties are defined on the global with
  `ReadOnly | DontDelete`. A JS write is a silent no-op in sloppy mode and a
  `TypeError` in strict mode — matching the existing `navigator` / `location` 口径.
- **Stable across `load()`.** The environment is Page-level state that outlives each
  generation, and `install_page` re-applies it, so every generation — including
  scripts running *during* a `load()` — observes identical values. A failed load does
  not change or lose the environment.
- **Per-page.** Two pages with different environments do not interfere, and a default
  page alongside configured ones still reports the defaults.
- **`dispose()`** reuses the existing M1 error paths: reading any environment value
  after disposal raises `JSContextDisposedError`. `get_default_environment()` still
  works (it is a static template read).

## Scope / non-goals

M11-1 deliberately does **not** add:

- any setter, `set_environment`, or re-configuration API — the baseline is
  construction-time only;
- `navigator.userAgentData` or any UA client-hints surface;
- `outerWidth` / `outerHeight` / `screenX` / `screenY` / `scrollX` / `pageXOffset`,
  scroll or resize behavior, or `resize` events;
- `screen.colorDepth` / `pixelDepth` / `availLeft` / `availTop` / `orientation`, or
  `ScreenOrientation`;
- `matchMedia` or any media-query / layout integration (these dimensions feed no
  layout engine — there is none);
- networking, `requestAnimationFrame`, `canvas`, or `webgl`;
- any fingerprint-defeating or randomization behavior — values are exactly what the
  caller configured.

`JSContext` is untouched. The first-layer DOM / script / lifecycle / form / M8
web-platform semantics and the M9–M10 monitoring channels are unchanged; `navigator`
and `screen` expose no methods, so the `watch_apis` host-method channel is unaffected.

## Files changed

- `python/iv8/page.py` — `_DEFAULT_ENVIRONMENT` (the single source of truth),
  `_normalize_environment` (strict validation + resolution),
  `Page.get_default_environment()`, and the `Page(environment=...)` parameter;
- `include/iv8/page_state.h` — the `HostEnvironment` struct, the `PageState`
  constructor signature, and the `environment_` member (declared before
  `host_objects_` so it outlives the hosts that point at it);
- `src/core/page_state.cpp` — `NavigatorHost` reads the environment instead of
  constants, the new `ScreenHost`, the three read-only `window` properties, and the
  per-generation install;
- `src/binding/page_binding.cpp` — unpacks the already-validated dict into
  `HostEnvironment`;
- `python/iv8/__init__.py` — docstring;
- `test/test_host_environment.py` — the acceptance suite;
- `docs/runtime_surface.md`, `docs/current_status.md`, `docs/next_tasks.md`.

## Platforms

Unchanged: Linux and Windows build with real V8; macOS is skeleton-only. A skeleton
build exports the same public shape — `get_default_environment()` works there (no
native state), while `Page(...)` still raises `RuntimeError`, so the runtime tests are
`@on_only`.
