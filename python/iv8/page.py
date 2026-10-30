"""Public ``Page`` — the M2 container that owns one execution context plus the
native host objects and browser-like globals installed into it.

A page provides ``eval`` (delegated to its owned context), disposal, ``disposed``,
and context-manager use. Inside its JS context it installs: the global roots
``window`` / ``globalThis`` / ``self`` (all the same object); a minimal
``console`` (``log`` / ``info`` / ``warn`` / ``error`` → Python ``logging``);
static read-only ``navigator`` and ``location``; and JS-visible timers
(``setTimeout`` / ``clearTimeout`` / ``setInterval`` / ``clearInterval``) that run
only via the manual pumps ``run_timers()`` / ``run_jobs()``. ``load()`` refreshes
this page state from static HTML + base URL (``location`` then reflects the base
URL). It is still NOT a full page object — no public document/DOM, navigation,
history, or network. (The M2-1 framework probe ``hostProbe`` is also installed as
internal infrastructure, not a stable API.)

The public API shape is identical in both build modes: when V8 is linked,
``Page()`` creates a context and installs the above; in a V8-free skeleton build
``Page()`` raises ``RuntimeError`` (mirroring ``JSContext``).
"""

from collections.abc import Mapping
from urllib.parse import urljoin

from . import _core
from .errors import JSContextDisposedError, JSError

__all__ = ["Page"]

# Host access trace (first slice): the native object types that can emit
# get/set/call events. Keep this in sync with AccessTrace::supported_object_types().
_ACCESS_TRACE_OBJECT_TYPES = (
    "connection", "console", "crypto", "document", "element",
    "host_object", "host_probe", "location", "navigator", "orientation",
    "performance", "screen", "storage", "window",
)

# M11-1 host-environment baseline. This mapping is the SINGLE source of truth for
# the default environment: the native layer holds no defaults of its own and always
# receives a fully-resolved set of values from here. Every value is a fixed,
# deterministic constant — never derived from the host OS, screen, or locale — so a
# page reports identical values on Linux and Windows. The four navigator values match
# the constants the M2-3 navigator shipped with, so the default page is unchanged.
_DEFAULT_ENVIRONMENT = {
    "userAgent": "Mozilla/5.0 (compatible; iv8)",
    "platform": "iv8",
    "language": "en-US",
    "webdriver": False,
    "languages": ["en-US", "en"],
    "onLine": True,
    "cookieEnabled": True,
    "hardwareConcurrency": 8,
    "appName": "Netscape",
    "appCodeName": "Mozilla",
    "product": "Gecko",
    "vendor": "",
    "vendorSub": "",
    "productSub": "20100101",
    "maxTouchPoints": 0,
    "pdfViewerEnabled": False,
    "doNotTrack": "0",
    "connectionEffectiveType": "4g",
    "connectionRtt": 50,
    "connectionDownlink": 10.0,
    "connectionSaveData": False,
    "screenWidth": 1920,
    "screenHeight": 1080,
    "availWidth": 1920,
    "availHeight": 1040,
    "screenAvailLeft": 0,
    "screenAvailTop": 0,
    "screenColorDepth": 24,
    "screenPixelDepth": 24,
    "devicePixelRatio": 1.0,
    "innerWidth": 1280,
    "innerHeight": 720,
    "outerWidth": 1280,
    "outerHeight": 720,
    "screenX": 0,
    "screenY": 0,
}

# Per-key type rules for _normalize_environment. The integer keys reject `bool`
# explicitly (in Python `bool` is a subclass of `int`, so `isinstance(True, int)` is
# True and a stray boolean would otherwise pass as a dimension).
_STR_KEYS = (
    "userAgent",
    "platform",
    "language",
    "appName",
    "appCodeName",
    "product",
    "vendor",
    "vendorSub",
    "productSub",
    "doNotTrack",
    "connectionEffectiveType",
)
_BOOL_KEYS = ("webdriver", "onLine", "cookieEnabled", "pdfViewerEnabled",
              "connectionSaveData")
_INT_KEYS = (
    "hardwareConcurrency",
    "maxTouchPoints",
    "connectionRtt",
    "screenWidth",
    "screenHeight",
    "availWidth",
    "availHeight",
    "screenAvailLeft",
    "screenAvailTop",
    "screenColorDepth",
    "screenPixelDepth",
    "innerWidth",
    "innerHeight",
    "outerWidth",
    "outerHeight",
    "screenX",
    "screenY",
)
_FLOAT_KEYS = ("devicePixelRatio", "connectionDownlink")
_LIST_STR_KEYS = ("languages",)

# M11-17: nested environment sections -> flat native keys.
_NESTED_ENVIRONMENT = {
    "navigator": {
        "userAgent": "userAgent",
        "platform": "platform",
        "language": "language",
        "webdriver": "webdriver",
        "languages": "languages",
        "onLine": "onLine",
        "cookieEnabled": "cookieEnabled",
        "hardwareConcurrency": "hardwareConcurrency",
        "appName": "appName",
        "appCodeName": "appCodeName",
        "product": "product",
        "vendor": "vendor",
        "vendorSub": "vendorSub",
        "productSub": "productSub",
        "maxTouchPoints": "maxTouchPoints",
        "pdfViewerEnabled": "pdfViewerEnabled",
        "doNotTrack": "doNotTrack",
        "connectionEffectiveType": "connectionEffectiveType",
        "connectionRtt": "connectionRtt",
        "connectionDownlink": "connectionDownlink",
        "connectionSaveData": "connectionSaveData",
    },
    "screen": {
        "width": "screenWidth",
        "height": "screenHeight",
        "availWidth": "availWidth",
        "availHeight": "availHeight",
        "availLeft": "screenAvailLeft",
        "availTop": "screenAvailTop",
        "colorDepth": "screenColorDepth",
        "pixelDepth": "screenPixelDepth",
    },
    "window": {
        "devicePixelRatio": "devicePixelRatio",
        "innerWidth": "innerWidth",
        "innerHeight": "innerHeight",
        "outerWidth": "outerWidth",
        "outerHeight": "outerHeight",
        "screenX": "screenX",
        "screenY": "screenY",
    },
}


def _normalize_environment(environment):
    """Validate the M11-1 ``environment`` input and return a complete environment.

    ``environment`` must be ``None`` (or omitted) or a mapping whose keys are a
    subset of ``_DEFAULT_ENVIRONMENT``. The result is always a full dict with all
    supported keys: supplied values override the defaults and the rest are
    inherited.

    Validation is strict and happens entirely here, before any native page state
    exists — so a bad environment can never leave a partially-installed page:

    - a non-mapping ``environment`` raises ``TypeError``;
    - an unknown key raises ``TypeError`` (the key set is closed);
    - a value of the wrong type for its key raises ``TypeError``.

    ``devicePixelRatio`` accepts an ``int`` or a ``float`` and is stored as
    ``float``; ``languages`` requires a non-empty list of non-empty strings.
    Every other key requires its exact type.
    """
    resolved = dict(_DEFAULT_ENVIRONMENT)
    resolved["languages"] = list(resolved["languages"])
    if environment is None:
        return resolved
    if not isinstance(environment, Mapping):
        raise TypeError("environment must be a mapping")
    # M11-17: accept the nested navigator/screen/window form (or the original
    # flat form, but not a mixture).
    if any(section in environment for section in _NESTED_ENVIRONMENT):
        extra = [key for key in environment if key not in _NESTED_ENVIRONMENT]
        if extra:
            raise TypeError(
                "environment must not mix nested and flat keys: "
                f"{extra!r}"
            )
        flat = {}
        for section, values in environment.items():
            if not isinstance(values, Mapping):
                raise TypeError(f"environment[{section!r}] must be a mapping")
            for key, value in values.items():
                target = _NESTED_ENVIRONMENT[section].get(key)
                if target is None:
                    raise TypeError(
                        f"unknown environment key: {section}.{key}"
                    )
                flat[target] = value
        environment = flat
    for key in environment:
        if key not in _DEFAULT_ENVIRONMENT:
            raise TypeError(f"unknown environment key: {key!r}")
    for key in _STR_KEYS:
        if key in environment:
            value = environment[key]
            if not isinstance(value, str):
                raise TypeError(f"environment[{key!r}] must be a str")
            resolved[key] = value
    for key in _BOOL_KEYS:
        if key in environment:
            value = environment[key]
            if not isinstance(value, bool):
                raise TypeError(f"environment[{key!r}] must be a bool")
            resolved[key] = value
    for key in _INT_KEYS:
        if key in environment:
            value = environment[key]
            if isinstance(value, bool) or not isinstance(value, int):
                raise TypeError(f"environment[{key!r}] must be an int")
            resolved[key] = value
    for key in _FLOAT_KEYS:
        if key in environment:
            value = environment[key]
            if isinstance(value, bool) or not isinstance(value, (int, float)):
                raise TypeError(f"environment[{key!r}] must be a real number")
            resolved[key] = float(value)
    for key in _LIST_STR_KEYS:
        if key in environment:
            value = environment[key]
            if (not isinstance(value, list) or not value or
                    any(not isinstance(item, str) or not item for item in value)):
                raise TypeError(
                    f"environment[{key!r}] must be a non-empty list of str"
                )
            resolved[key] = list(value)
    return resolved


def _normalize_scripts(scripts):
    """Validate the M3-1 ``scripts`` input and return a list of (name, code).

    ``scripts`` must be ``None`` (or omitted) or a ``list`` of mappings, each with
    string ``name`` and ``code`` keys (extra keys are ignored). Any type error
    raises ``TypeError`` before any page state is touched.
    """
    if scripts is None:
        return []
    if not isinstance(scripts, list):
        raise TypeError("scripts must be a list")
    normalized = []
    for index, item in enumerate(scripts):
        if not isinstance(item, Mapping):
            raise TypeError(f"scripts[{index}] must be a mapping")
        name = item.get("name")
        code = item.get("code")
        if not isinstance(name, str):
            raise TypeError(f"scripts[{index}]['name'] must be a str")
        if not isinstance(code, str):
            raise TypeError(f"scripts[{index}]['code'] must be a str")
        normalized.append((name, code))
    return normalized


def _normalize_resources(resources):
    """Validate the M3-5 ``resources`` input and return a plain ``dict``.

    ``resources`` must be ``None`` (or omitted) or a mapping from absolute-URL
    strings to script-source strings. Any type error raises ``TypeError`` before
    any page state is touched (a bad ``resources`` argument does not reload the
    page). It is a host-provided lookup only — no network is ever performed.
    """
    if resources is None:
        return {}
    if not isinstance(resources, Mapping):
        raise TypeError("resources must be a mapping")
    normalized = {}
    for key, value in resources.items():
        if not isinstance(key, str):
            raise TypeError("resources keys must be str (absolute URLs)")
        if not isinstance(value, str):
            raise TypeError("resources values must be str (script source)")
        normalized[key] = value
    return normalized


class Page:
    """A minimal M2 page: owns one V8 execution context plus its host objects.

    Lifecycle delegates to the owned context, so overlapping/after-dispose
    operations raise the same ``JSContextBusyError`` / ``JSContextDisposedError``
    as ``JSContext``.

    M11-1 adds the optional ``environment`` constructor parameter — a mapping that
    configures the page's read-only host-environment baseline (see
    :meth:`get_default_environment` for the accepted keys). It is fixed for the
    page's lifetime: there is no setter, and every ``load()`` generation reports the
    same values. Omitting it yields the documented defaults.

    M11-17 additionally accepts the nested ``navigator`` / ``screen`` / ``window``
    form and adds :meth:`get_defaults` for that shape. Mixing nested and flat keys
    in one mapping is rejected. M11-24..M11-27 extend both forms with navigator
    metadata/identity, screen depth, and outer window geometry; M11-35 adds
    navigator touch/PDF/DNT/connection metadata and screen avail offsets
    (36 flat keys total).
    """

    @staticmethod
    def get_defaults() -> dict:
        """M11-17: return the nested default environment template.

        Sections: ``navigator`` (``userAgent`` / ``platform`` / ``language`` /
        ``webdriver`` / ``languages`` / ``onLine`` / ``cookieEnabled`` /
        ``hardwareConcurrency`` / ``appName`` / ``appCodeName`` / ``product`` /
        ``vendor`` / ``vendorSub`` / ``productSub`` / ``maxTouchPoints`` /
        ``pdfViewerEnabled`` / ``doNotTrack`` / ``connectionEffectiveType`` /
        ``connectionRtt`` / ``connectionDownlink`` / ``connectionSaveData``),
        ``screen`` (``width`` / ``height`` / ``availWidth`` / ``availHeight`` /
        ``availLeft`` / ``availTop`` / ``colorDepth`` / ``pixelDepth``), and
        ``window`` (``devicePixelRatio`` / ``innerWidth`` / ``innerHeight`` /
        ``outerWidth`` / ``outerHeight`` / ``screenX`` / ``screenY``). The result
        is a fresh, fully nested ``dict``; it is accepted directly by
        ``Page(environment=...)``.
        """
        return {
            "navigator": {
                "userAgent": _DEFAULT_ENVIRONMENT["userAgent"],
                "platform": _DEFAULT_ENVIRONMENT["platform"],
                "language": _DEFAULT_ENVIRONMENT["language"],
                "webdriver": _DEFAULT_ENVIRONMENT["webdriver"],
                "languages": list(_DEFAULT_ENVIRONMENT["languages"]),
                "onLine": _DEFAULT_ENVIRONMENT["onLine"],
                "cookieEnabled": _DEFAULT_ENVIRONMENT["cookieEnabled"],
                "hardwareConcurrency":
                    _DEFAULT_ENVIRONMENT["hardwareConcurrency"],
                "appName": _DEFAULT_ENVIRONMENT["appName"],
                "appCodeName": _DEFAULT_ENVIRONMENT["appCodeName"],
                "product": _DEFAULT_ENVIRONMENT["product"],
                "vendor": _DEFAULT_ENVIRONMENT["vendor"],
                "vendorSub": _DEFAULT_ENVIRONMENT["vendorSub"],
                "productSub": _DEFAULT_ENVIRONMENT["productSub"],
                "maxTouchPoints": _DEFAULT_ENVIRONMENT["maxTouchPoints"],
                "pdfViewerEnabled": _DEFAULT_ENVIRONMENT["pdfViewerEnabled"],
                "doNotTrack": _DEFAULT_ENVIRONMENT["doNotTrack"],
                "connectionEffectiveType":
                    _DEFAULT_ENVIRONMENT["connectionEffectiveType"],
                "connectionRtt": _DEFAULT_ENVIRONMENT["connectionRtt"],
                "connectionDownlink":
                    _DEFAULT_ENVIRONMENT["connectionDownlink"],
                "connectionSaveData":
                    _DEFAULT_ENVIRONMENT["connectionSaveData"],
            },
            "screen": {
                "width": _DEFAULT_ENVIRONMENT["screenWidth"],
                "height": _DEFAULT_ENVIRONMENT["screenHeight"],
                "availWidth": _DEFAULT_ENVIRONMENT["availWidth"],
                "availHeight": _DEFAULT_ENVIRONMENT["availHeight"],
                "availLeft": _DEFAULT_ENVIRONMENT["screenAvailLeft"],
                "availTop": _DEFAULT_ENVIRONMENT["screenAvailTop"],
                "colorDepth": _DEFAULT_ENVIRONMENT["screenColorDepth"],
                "pixelDepth": _DEFAULT_ENVIRONMENT["screenPixelDepth"],
            },
            "window": {
                "devicePixelRatio": _DEFAULT_ENVIRONMENT["devicePixelRatio"],
                "innerWidth": _DEFAULT_ENVIRONMENT["innerWidth"],
                "innerHeight": _DEFAULT_ENVIRONMENT["innerHeight"],
                "outerWidth": _DEFAULT_ENVIRONMENT["outerWidth"],
                "outerHeight": _DEFAULT_ENVIRONMENT["outerHeight"],
                "screenX": _DEFAULT_ENVIRONMENT["screenX"],
                "screenY": _DEFAULT_ENVIRONMENT["screenY"],
            },
        }

    @staticmethod
    def get_default_environment() -> dict:
        """Return the M11-1 default host-environment template as a plain ``dict``.

        The result is a **fresh copy**, so mutating it never affects this class, any
        existing page, or a later ``Page()``. Its keys are exactly the keys
        ``Page(environment=...)`` accepts:

        Navigator values: ``userAgent`` / ``platform`` / ``language`` /
        ``appName`` / ``appCodeName`` / ``product`` / ``vendor`` / ``vendorSub``
        / ``productSub`` (``str``), ``webdriver`` / ``onLine`` /
        ``cookieEnabled`` (``bool``), ``languages`` (non-empty ``list[str]``),
        and ``hardwareConcurrency`` (``int``). Screen/window values:
        ``screenWidth`` / ``screenHeight`` / ``availWidth`` / ``availHeight`` /
        ``screenColorDepth`` / ``screenPixelDepth`` / ``innerWidth`` /
        ``innerHeight`` / ``outerWidth`` / ``outerHeight`` / ``screenX`` /
        ``screenY`` (``int``), and ``devicePixelRatio`` (``float``).

        The intended use is to read the template, override the keys you care about,
        and pass it back:

        ```python
        env = iv8.Page.get_default_environment()
        env["userAgent"] = "custom-agent"
        page = iv8.Page(environment=env)
        ```

        Every default is a fixed constant — never derived from the host OS, display,
        or locale — so pages are deterministic and identical across platforms. It is
        callable on the class (no page needed) and in a V8-free skeleton build, since
        it touches no native state.
        """
        env = dict(_DEFAULT_ENVIRONMENT)
        env["languages"] = list(env["languages"])
        return env

    def __init__(self, environment=None) -> None:
        # M11-1: validate and resolve the environment BEFORE the V8-link check and
        # before any native object exists, so a bad environment raises TypeError
        # without creating a page or partially installing anything.
        resolved_environment = _normalize_environment(environment)
        if not _core._v8_linked:
            raise RuntimeError(
                "this build does not link V8; Page is unavailable "
                "(rebuild with IV8_LINK_V8=ON)"
            )
        # M11-1: the resolved environment is Page-level state — the native layer
        # re-reads it on every generation, so load() keeps the same values.
        self._environment = resolved_environment
        # Native page holder: creates the context and installs host objects.
        self._native = _core.Page(resolved_environment)
        # M3-2 page lifecycle: a fresh page has its (blank) default generation
        # installed, so it starts "complete". load() drives loading -> complete.
        self._ready_state = "complete"
        # M9-1 DevTools: lazily created by devtools_url() (None => never enabled,
        # so the Inspector + local server are never started; zero runtime impact).
        self._devtools = None

    @property
    def disposed(self) -> bool:
        return self._native.disposed

    @property
    def ready_state(self) -> str:
        """M3-2 page lifecycle state — ``"loading"`` or ``"complete"``.

        Read-only. ``"complete"`` on a fresh page and after a successful
        ``load()``; ``"loading"`` while a ``load()`` is in progress and after a
        ``load()`` that did not complete (a script raised, or the context was
        disposed/busy) until a later successful ``load()``. This is the Python
        page lifecycle; it is distinct from the JS ``document.readyState`` (which
        stays ``"complete"``). Not affected by ``dispose()`` — use ``disposed``.
        """
        return self._ready_state

    def eval(self, source: str, *, to_py: bool = False, name: str = "<eval>") -> object:
        """Compile and run JavaScript in this page's context.

        Semantics match ``JSContext.eval``. Host objects installed on the page
        (M2-1: ``hostProbe``) are reachable from the evaluated code through their
        native property/method plumbing.
        """
        if not isinstance(source, str):
            raise TypeError("source must be a str")
        if not isinstance(name, str):
            raise TypeError("name must be a str")
        if not isinstance(to_py, bool):
            raise TypeError("to_py must be a bool")
        return self._native.eval(source, to_py, name)

    def run_timers(self) -> None:
        """Manually pump timers: fire every currently-scheduled ``setTimeout`` /
        ``setInterval`` callback once, ordered by ``(delay, registration order)``.

        Timers never run in the background — only this call executes them. Delay
        determines firing order within a pump, not real-time waiting. One-shot
        (``setTimeout``) timers are removed after firing; interval
        (``setInterval``) timers fire again on the next call. Timers scheduled by
        a callback during the pump fire on the next call. An exception raised by a
        callback is swallowed; the page/context stays usable. Raises
        ``JSContextDisposedError`` after ``dispose()``.
        """
        self._native.run_timers()

    def run_jobs(self) -> None:
        """Manually pump jobs: drain the pending microtask queue.

        This executes queued ``Promise`` reactions, ``queueMicrotask`` callbacks
        (M11-4), and MutationObserver delivery batches (M11-5), in FIFO order.
        Microtasks never run automatically. It does not fire timers,
        animation-frame callbacks, or idle callbacks — those have their own
        pumps. Raises ``JSContextDisposedError`` after ``dispose()``.
        """
        self._native.run_jobs()

    @property
    def event_loop_time_ms(self) -> int:
        """M11-29: current generation's virtual event-loop time in milliseconds.

        Starts at 0 and is reset by ``load()``. The legacy manual pumps do not
        advance it; only ``tick`` (never), ``advance`` and ``run_until_idle`` do.
        Raises ``JSContextDisposedError`` after ``dispose()``.
        """
        if self._native.disposed:
            raise JSContextDisposedError("Page is disposed")
        return self._native.event_loop_time_ms

    def tick(self) -> bool:
        """Run one virtual event-loop turn (timers + microtasks + rAF/idle).

        Drains pending microtasks, then runs at most one current-time task:
        the earliest due timer, otherwise a due animation frame, otherwise one
        idle round. A microtask checkpoint follows the task. Does not advance the
        virtual clock. Returns ``True`` if a task ran.
        """
        return self._native.tick()

    def advance(self, ms: int, *, max_steps: int = 10_000) -> bool:
        """Advance the virtual clock by ``ms`` and run due work.

        Timers due within the window run in ``(due_ms, registration order)``
        order; animation frames run at 16ms virtual boundaries; idle callbacks
        run when no timer/frame is currently due. Every task is followed by a
        microtask checkpoint. Work scheduled by a callback is eligible if it is
        also due inside the window. The clock is set to the target when the
        window completes.

        Returns ``True`` when the full window was processed, or ``False`` when
        ``max_steps`` stopped it first (the clock then stays at the last
        processed task's virtual time). ``ms`` must be a non-negative int;
        ``max_steps`` must be a positive int. A repeating timer is rescheduled
        with a 1ms virtual-time floor so zero-delay intervals cannot spin
        without progress. Raises ``JSContextDisposedError`` after ``dispose()``.
        """
        if isinstance(ms, bool) or not isinstance(ms, int) or ms < 0:
            raise TypeError("ms must be a non-negative int")
        if ms > 9_223_372_036_854_775_807:
            raise TypeError("ms is too large")
        if (isinstance(max_steps, bool) or not isinstance(max_steps, int) or
                max_steps <= 0 or max_steps > 4_294_967_295):
            raise TypeError("max_steps must be a positive int")
        return self._native.advance(ms, max_steps)

    def run_until_idle(self, *, max_steps: int = 1_000) -> bool:
        """Run timers, animation frames, idle callbacks, and microtasks until idle.

        Each turn runs the earliest current task, advancing the virtual clock to
        timer/frame due times; idle callbacks run one round when no timer/frame
        is currently due. Returns ``True`` only when no timers, frame callbacks,
        or idle callbacks remain; an interval or self-rescheduling rAF/idle
        normally returns ``False`` at ``max_steps``. Raises
        ``JSContextDisposedError`` after ``dispose()``.
        """
        if (isinstance(max_steps, bool) or not isinstance(max_steps, int) or
                max_steps <= 0 or max_steps > 4_294_967_295):
            raise TypeError("max_steps must be a positive int")
        return self._native.run_until_idle(max_steps)

    def run_animation_frame(self) -> None:
        """Manually pump **one** animation frame: run the ``requestAnimationFrame``
        callbacks queued for the current frame, in registration order.

        This remains the one-queue manual pump. The M11-30 event-loop facade also
        runs animation-frame callbacks at fixed 16ms virtual boundaries; there is
        still no real clock, background thread, or automatic loop. One call here
        advances exactly one legacy frame and does not move the facade clock.

        Frame boundary: the callbacks queued when the call begins are the frame. A
        callback that calls ``requestAnimationFrame()`` again schedules into the
        **next** frame and will not run during this call, so a self-rescheduling
        callback advances one frame per call instead of spinning. Pumping with an
        empty queue is a harmless no-op.

        ``cancelAnimationFrame(handle)`` prevents a queued callback from running,
        including one cancelled by an earlier callback in the same frame. Callbacks
        receive **no timestamp argument** (this runtime has no clock) and never
        repeat — a callback runs at most once per registration.

        Animation frames are a queue independent of the M2-4 timers: this pump does
        not fire ``setTimeout`` / ``setInterval`` callbacks (use ``run_timers()``) or
        drain microtasks (use ``run_jobs()``), and ``cancelAnimationFrame`` does not
        cancel a timer. An exception raised by a callback is swallowed; the
        page/context stays usable. Raises ``JSContextDisposedError`` after
        ``dispose()``.
        """
        self._native.run_animation_frame()

    def run_idle_callbacks(self) -> None:
        """Manually pump **one** idle round: run the ``requestIdleCallback``
        callbacks queued for the current round, in registration order.

        This remains the one-queue manual pump. The M11-30 event-loop facade can
        also run idle rounds when no timer/frame is currently due; there is still
        no real clock, background thread, or automatic loop. One call here advances
        exactly one legacy round and does not move the facade clock.

        Round boundary: the callbacks queued when the call begins are the round. A
        callback that calls ``requestIdleCallback()`` again schedules into the
        **next** round and will not run during this call, so a self-rescheduling
        callback advances one round per call instead of spinning. Pumping with an
        empty queue is a harmless no-op.

        Each callback receives one argument: a minimal ``IdleDeadline``-style object
        with the read-only ``didTimeout`` (always ``False`` this phase) and
        ``timeRemaining()`` (returns a fixed 50ms budget). Both are deliberately
        **deterministic** — with no clock, a "real" remaining time would be
        fabricated. ``timeRemaining()`` does not decrease as a callback runs.

        ``requestIdleCallback(callback, options)`` accepts an optional ``options``
        object whose ``timeout`` must be a non-negative finite number; a malformed
        ``options`` or ``timeout`` raises a JS ``TypeError``. A valid ``timeout`` is
        **recorded only** — this phase implements **no** real timeout firing, so a
        callback never runs early or late because of it.

        ``cancelIdleCallback(handle)`` prevents a queued callback from running,
        including one cancelled by an earlier callback in the same round. Callbacks
        never repeat — one runs at most once per registration.

        Idle callbacks are a queue independent of the M2-4 timers, the M11-2
        animation frames, and the microtask queue: this pump runs none of those (use
        ``run_timers()`` / ``run_animation_frame()`` / ``run_jobs()``), and
        ``cancelIdleCallback`` cancels neither a timer nor a frame callback. An
        exception raised by a callback is swallowed; the page/context stays usable.
        Raises ``JSContextDisposedError`` after ``dispose()``.
        """
        self._native.run_idle_callbacks()

    def load(self, html: str, base_url: str, scripts=None, resources=None) -> None:
        """Refresh the page state from static HTML and a base URL.

        Replaces the current page state: the JS context is rebuilt (globals reset)
        and ``location`` is re-derived from ``base_url``. ``html`` is captured as
        internal document-bootstrap state. This is NOT a real navigation/loader —
        no network, subresources, or history.

        M3-5 HTML script integration: the document's own scripts are executed
        first, in **HTML document order** (inline ``<script>...</script>`` and
        ``<script src="...">`` interleaved). An inline script runs its source
        directly; a ``<script src>`` is resolved against ``base_url`` and its
        source is looked up in ``resources`` (a host-provided mapping — NO
        network). ``resources`` is ``None``/omitted or a mapping of absolute-URL
        strings to script-source strings; a bad shape raises ``TypeError`` before
        any load. A ``<script src>`` with no matching ``resources`` entry fails
        loudly (never silently skipped) via ``JSError`` (``resource_name`` = the
        resolved URL) — no rollback. M3-11: an inline ``<script>`` that fails
        reports a deterministic ``JSError.resource_name`` of
        ``"{base_url}#inline-script-{n}"``, where ``n`` is the inline script's
        1-based document-order position among inline ``<script>`` nodes (external
        ``<script src>`` and host ``scripts`` are not counted; a non-executable
        inline script still occupies its number). M3-7: while each HTML script runs,
        ``document.currentScript`` points at that ``<script>`` element (cleared to
        ``null`` after, even on error); the host ``scripts`` below never set it.
        M3-10: only minimal *classic* scripts execute — a ``<script>`` with no
        ``type``, an empty/whitespace ``type``, or ``type`` (trimmed, ASCII
        case-insensitive) ``text/javascript`` / ``application/javascript``. Any
        other type (``module`` / ``importmap`` / ``application/json`` /
        ``text/plain`` / …) stays in the DOM and ``document.scripts`` but is NOT
        run: it is not resolved against ``resources`` (so a missing resource is not
        an error) and never sets ``document.currentScript``.

        M3-1 external scripts (optional): ``scripts`` is a ``list`` of mappings,
        each with string ``name`` and ``code``. They run **after** the HTML
        document scripts, **synchronously, in list order**, in the same context
        (so they see globals from the HTML scripts and vice-versa). Each script's
        ``name`` is its resource/origin name, so a failure surfaces as ``JSError``
        with ``resource_name == name``. There is no rollback: if any script fails
        the exception propagates and the page keeps whatever earlier scripts
        already did. Timers/jobs scheduled by scripts do NOT run in the background
        — only ``run_timers()`` / ``run_jobs()`` execute them. ``scripts=None`` /
        ``[]`` and ``resources=None`` / ``{}`` degrade to the plain load path.

        Lifecycle (M3-4 + M3-6): the whole load runs with JS
        ``document.readyState === "loading"`` — every script (HTML + ``scripts``)
        observes ``"loading"``. On success, after all scripts run, a fixed
        sequence fires on ``document`` then ``window``: readyState →
        ``"interactive"``, ``readystatechange`` (document), ``DOMContentLoaded``
        (document), readyState → ``"complete"``, ``readystatechange`` (document),
        ``load`` (window). A load that failed (a script raised, or a missing
        resource) dispatches none of these and leaves ``document.readyState`` at
        ``"loading"``. Each successful (repeated) load re-walks the sequence in its
        fresh generation. (A fresh ``Page()`` never entered ``"loading"`` — its
        default generation reads ``"complete"``.) ``Page.ready_state`` keeps its
        separate M3-2 semantics.

        Repeated calls replace the prior page state; a retained ``JSValue`` from a
        previous load follows the usual disposed/invalidation rules. Raises
        ``JSContextBusyError`` if an operation is active, and
        ``JSContextDisposedError`` after ``dispose()``.
        """
        if not isinstance(html, str):
            raise TypeError("html must be a str")
        if not isinstance(base_url, str):
            raise TypeError("base_url must be a str")
        # Validate scripts + resources before touching page state or the lifecycle
        # (bad input must neither reload the page nor enter "loading").
        normalized = _normalize_scripts(scripts)
        resource_map = _normalize_resources(resources)
        # M3-2: enter "loading" for the whole install + scripts phase. It only
        # returns to "complete" once everything below succeeds — so a failure
        # (script JSError, missing resource, or a disposed/busy context) leaves
        # ready_state "loading" until a later successful load.
        self._ready_state = "loading"
        self._native.load(html, base_url)
        # M3-5: run the document's own scripts first, in HTML document order
        # (inline + external interleaved). A `<script src>` is resolved against
        # base_url and looked up in the host-provided resources
        # (resource_name = the resolved URL). A missing resource fails loudly (no
        # silent skip) via the existing JSError path, with no rollback.
        # M3-11: an inline `<script>` gets a deterministic resource_name
        # "{base_url}#inline-script-{n}" where n is its 1-based document-order
        # position among inline <script> nodes (external <script src> and host
        # scripts=[...] are NOT counted; a non-executable inline script still
        # occupies its number). n is a document-structure property, so it is
        # stable regardless of which scripts actually execute or fail.
        inline_index = 0
        for index, entry in enumerate(self._native.html_scripts()):
            src = entry["src"]
            if src is None:
                # Count every inline <script>, executable or not (M3-11 numbering).
                inline_index += 1
            # M3-10: only minimal classic scripts execute. A non-classic <script>
            # (type=module / importmap / application/json / text/plain / any other
            # non-empty type) stays in the DOM and document.scripts but does not
            # run — so it is not resolved against resources, never errors on a
            # missing resource, and never sets document.currentScript.
            if not entry["executable"]:
                continue
            if src is None:
                code = entry["code"]
                name = f"{base_url}#inline-script-{inline_index}"
            else:
                url = urljoin(base_url, src)
                if url not in resource_map:
                    raise JSError(
                        "Error",
                        f"no host resource for <script src>: {url}",
                        "",
                        url,
                        None,
                        None,
                    )
                code, name = resource_map[url], url
            # M3-7: run via run_html_script so document.currentScript points at this
            # <script> element during execution (and is cleared to null after, even
            # on error). Host scripts=[...] below use plain eval (no currentScript).
            self._native.run_html_script(index, code, name)
        # Then the M3-1 host-provided scripts (after the HTML scripts), in list
        # order. eval reuses the existing JSError path (resource_name = name) and
        # shares globals across all scripts; a failure propagates (no rollback).
        for name, code in normalized:
            self._native.eval(code, False, name)
        # M3-4: on a successful load, auto-dispatch the lifecycle events in a fixed
        # order — DOMContentLoaded on document, then load on window. This runs only
        # after every script (HTML + M3-1) succeeded (a failure raised above and
        # skipped this), so a failed load dispatches no lifecycle events.
        self._native.dispatch_lifecycle_events()
        self._ready_state = "complete"

    def devtools_url(self) -> str:
        """Lazily start this page's DevTools/Inspector attach base and return the
        WebSocket URL an external CDP client can connect to.

        The first call starts a minimal localhost server (Chrome DevTools Protocol
        discovery at ``/json/version`` + ``/json`` / ``/json/list``, plus a
        WebSocket endpoint bridged to this page's V8 Inspector) and creates the
        Inspector for the current context; later calls on the same ``Page`` return
        the **same** URL. The URL stays stable across ``load()`` (each generation
        gets a fresh Inspector behind the same endpoint). If never called, the
        Inspector and server are never started and runtime behavior is unchanged.

        This is an *attach base* only — there is no message loop, so without an
        external client driving it, and for asynchronous CDP events, behavior is
        best-effort; synchronous CDP request/response works. It does not pause
        execution, alter ``debugger;`` / ``console`` semantics, or add any JS
        global. Raises ``JSContextDisposedError`` after ``dispose()``.
        """
        if self._native.disposed:
            raise JSContextDisposedError("Page is disposed")
        if self._devtools is None:
            # Enable the native Inspector for the current generation, then start
            # the local discovery/WS server bridged to its CDP dispatch.
            self._native.devtools_enable()
            from ._devtools import DevToolsServer

            self._devtools = DevToolsServer(self._native.devtools_dispatch)
        return self._devtools.ws_url()

    def watch_apis(self, paths, break_on_hit: bool = False) -> None:
        """Register a set of host-method paths to record when page scripts call them.

        ``paths`` is a ``list[str]`` of minimal dotted ``"receiver.method"`` paths —
        ``"document.querySelector"``, ``"script.getAttribute"``,
        ``"window.setTimeout"`` — naming a known host method. Duplicates are
        deduped; an empty list is valid (enabled, watches nothing). Calling it
        replaces any prior set and takes effect for subsequent script execution on
        this ``Page``; the registration (and ``break_on_hit``) **persists across
        ``load()``** (the same page's observation intent continues).

        With ``break_on_hit=False`` (default) this is pure recording — no
        breakpoint/pause. With ``break_on_hit=True``, a matched host-method call is
        recorded **and** schedules a V8 Inspector pause on the next statement, so an
        attached DevTools client observes a pause. If no Inspector session is
        attached (``devtools_url()`` never called, or no client connected), it is
        recorded only — no pause, no error (the gentle 口径). This phase does **not**
        change ``debugger;`` semantics, add a resume/continue API, or capture
        arguments / return values / stacks; the path support range is unchanged
        (calls only, no wildcard/regex/property watches, no JS-visible API).

        A non-``list`` ``paths``, a non-``str`` element, or a non-``bool``
        ``break_on_hit`` raises ``TypeError`` (before any state changes); after
        ``dispose()`` it raises ``JSContextDisposedError``.
        """
        if not isinstance(paths, list):
            raise TypeError("paths must be a list")
        for index, path in enumerate(paths):
            if not isinstance(path, str):
                raise TypeError(f"paths[{index}] must be a str")
        if not isinstance(break_on_hit, bool):
            raise TypeError("break_on_hit must be a bool")
        self._native.watch_apis(paths, break_on_hit)

    def read_watch_api_hits(self) -> list:
        """Return the recorded watch-API call hits and clear the log (read-and-clear).

        Each hit is a ``dict`` with ``path`` (the watched ``"receiver.method"``
        string that was hit), ``resource_name`` (the executing script's resource
        name, or ``""`` if unattributed), and ``type`` (always ``"call"`` this
        phase). Returns ``[]`` if nothing was hit. A subsequent call returns ``[]``
        until new hits accumulate. Raises ``JSContextDisposedError`` after
        ``dispose()``.
        """
        return self._native.read_watch_api_hits()

    def watch_reflect_get(self, enabled: bool = True,
                          break_on_hit: bool = False) -> None:
        """Enable/disable monitoring of ``Reflect.get(target, key[, receiver])`` calls.

        When ``enabled`` is true, this page's ``Reflect.get`` is wrapped with a
        recording hook (installed per generation) that logs each call's property
        key; the call itself is delegated to the real ``Reflect.get`` so behavior is
        unchanged. ``break_on_hit`` behaves exactly as for ``watch_apis``: with
        ``False`` it only records; with ``True`` a hit additionally pauses the V8
        Inspector **when a DevTools session is attached** (else it records only — no
        pause, no error). Disabling (``enabled=False``) stops recording. Calling it
        again overrides the current switch; the configuration **persists across
        ``load()``**.

        This phase monitors only ``Reflect.get`` — **not** ``Reflect.has`` /
        ``Reflect.ownKeys`` / ``Object.*`` / ``Function.prototype.toString``, not
        plain ``obj.x`` access, and it adds no JS-visible monitoring global. A
        non-``bool`` ``enabled`` / ``break_on_hit`` raises ``TypeError``; after
        ``dispose()`` it raises ``JSContextDisposedError``.
        """
        if not isinstance(enabled, bool):
            raise TypeError("enabled must be a bool")
        if not isinstance(break_on_hit, bool):
            raise TypeError("break_on_hit must be a bool")
        self._native.watch_reflect_get(enabled, break_on_hit)

    def read_reflect_get_hits(self) -> list:
        """Return the recorded ``Reflect.get`` hits and clear the log (read-and-clear).

        Each hit is a ``dict`` with ``property`` (the stringified property key —
        e.g. ``"x"`` for a string key, ``"5"`` for a numeric key), ``resource_name``
        (the executing script's resource name, or ``""``), and ``type`` (always
        ``"Reflect.get"``). Returns ``[]`` if nothing was hit; a subsequent call
        returns ``[]`` until new hits accumulate. Raises ``JSContextDisposedError``
        after ``dispose()``.
        """
        return self._native.read_reflect_get_hits()

    def watch_reflect_has(self, enabled: bool = True,
                          break_on_hit: bool = False) -> None:
        """Enable/disable monitoring of ``Reflect.has(target, propertyKey)`` calls.

        Mirrors :meth:`watch_reflect_get` for ``Reflect.has``: when ``enabled``,
        this page's ``Reflect.has`` is wrapped with a recording hook (per generation)
        that logs each call's property key and delegates to the real ``Reflect.has``
        (behavior unchanged). ``break_on_hit`` behaves as in :meth:`watch_apis`
        (pause when a DevTools session is attached, else record-only). Disabling
        stops recording; a later call overrides the switch; the configuration
        **persists across ``load()``**.

        This phase monitors only ``Reflect.has`` — **not** ``Reflect.ownKeys`` /
        ``Object.*`` — not plain ``"x" in obj``, and it adds no JS-visible monitoring
        global. A non-``bool`` argument raises ``TypeError``; after ``dispose()`` it
        raises ``JSContextDisposedError``.
        """
        if not isinstance(enabled, bool):
            raise TypeError("enabled must be a bool")
        if not isinstance(break_on_hit, bool):
            raise TypeError("break_on_hit must be a bool")
        self._native.watch_reflect_has(enabled, break_on_hit)

    def read_reflect_has_hits(self) -> list:
        """Return the recorded ``Reflect.has`` hits and clear the log (read-and-clear).

        Each hit is a ``dict`` with ``property`` (the stringified property key),
        ``resource_name`` (the executing script's resource name, or ``""``), and
        ``type`` (always ``"Reflect.has"``). Returns ``[]`` if nothing was hit.
        Raises ``JSContextDisposedError`` after ``dispose()``.
        """
        return self._native.read_reflect_has_hits()

    def watch_reflect_ownkeys(self, enabled: bool = True,
                              break_on_hit: bool = False) -> None:
        """Enable/disable monitoring of ``Reflect.ownKeys(target)`` calls.

        Mirrors :meth:`watch_reflect_get` / :meth:`watch_reflect_has` for
        ``Reflect.ownKeys``: when ``enabled``, this page's ``Reflect.ownKeys`` is
        wrapped with a recording hook (per generation) that records each call and
        delegates to the real ``Reflect.ownKeys`` (behavior unchanged).
        ``Reflect.ownKeys(target)`` has no property key, so a hit records only the
        script resource name. ``break_on_hit`` behaves as in :meth:`watch_apis`
        (pause when a DevTools session is attached, else record-only). Disabling
        stops recording; a later call overrides the switch; the configuration
        **persists across ``load()``**.

        This phase monitors only ``Reflect.ownKeys`` — **not** ``Object.keys`` /
        ``Object.getOwnPropertyNames`` / ``Object.getOwnPropertySymbols`` /
        ``Object.defineProperty`` / ``Function.prototype.toString`` — and adds no
        JS-visible monitoring global. A non-``bool`` argument raises ``TypeError``;
        after ``dispose()`` it raises ``JSContextDisposedError``.
        """
        if not isinstance(enabled, bool):
            raise TypeError("enabled must be a bool")
        if not isinstance(break_on_hit, bool):
            raise TypeError("break_on_hit must be a bool")
        self._native.watch_reflect_ownkeys(enabled, break_on_hit)

    def read_reflect_ownkeys_hits(self) -> list:
        """Return the recorded ``Reflect.ownKeys`` hits and clear the log.

        Each hit is a ``dict`` with ``resource_name`` (the executing script's
        resource name, or ``""``) and ``type`` (always ``"Reflect.ownKeys"``) —
        there is no ``property`` field, since ``Reflect.ownKeys`` takes no key.
        Returns ``[]`` if nothing was hit (read-and-clear). Raises
        ``JSContextDisposedError`` after ``dispose()``.
        """
        return self._native.read_reflect_ownkeys_hits()

    def watch_object_keys(self, enabled: bool = True,
                          break_on_hit: bool = False) -> None:
        """Enable/disable monitoring of ``Object.keys(target)`` calls.

        Mirrors :meth:`watch_reflect_ownkeys` for ``Object.keys``: when ``enabled``,
        this page's ``Object.keys`` is wrapped with a recording hook (per generation)
        that records each call and delegates to the real ``Object.keys`` (behavior
        unchanged). ``Object.keys(target)`` has no property key, so a hit records only
        the script resource name. ``break_on_hit`` behaves as elsewhere (pause when a
        DevTools session is attached, else record-only). Disabling stops recording; a
        later call overrides the switch; the configuration **persists across
        ``load()``**.

        This phase monitors only ``Object.keys`` — **not**
        ``Object.getOwnPropertyNames`` / ``Object.getOwnPropertySymbols`` /
        ``Object.defineProperty`` / ``Reflect.ownKeys`` (M10-1) — and adds no
        JS-visible monitoring global. A non-``bool`` argument raises ``TypeError``;
        after ``dispose()`` it raises ``JSContextDisposedError``.
        """
        if not isinstance(enabled, bool):
            raise TypeError("enabled must be a bool")
        if not isinstance(break_on_hit, bool):
            raise TypeError("break_on_hit must be a bool")
        self._native.watch_object_keys(enabled, break_on_hit)

    def read_object_keys_hits(self) -> list:
        """Return the recorded ``Object.keys`` hits and clear the log.

        Each hit is a ``dict`` with ``resource_name`` (the executing script's
        resource name, or ``""``) and ``type`` (always ``"Object.keys"``) — there is
        no ``property`` field, since ``Object.keys`` takes no key. Returns ``[]`` if
        nothing was hit (read-and-clear). Raises ``JSContextDisposedError`` after
        ``dispose()``.
        """
        return self._native.read_object_keys_hits()

    def start_access_trace(self, max_events: int = 10000,
                           object_types=None) -> None:
        """Start recording native HostObject get/set/call accesses.

        This is a structured, Page-level trace independent of :meth:`watch_apis`
        and the Reflect/Object.keys channels. It records operations that enter the
        generic HostObject property getter/setter or method trampoline; it is not
        a V8-wide property hook. It covers ``console``, ``document``, ``element``,
        ``host_object``, ``host_probe``, ``location``, ``navigator``, and
        ``screen``. Missing string-key reads on those hosts produce
        ``get_probe``/``resolution: "missing"`` events. The first step-5 groups
        also record ``performance`` / ``crypto`` native method calls, Storage
        method calls and its ``length`` getter, and traced window metadata
        accessors (``innerWidth`` / geometry / flags plus ``window.name``).
        ``parent`` / ``top`` / ``frames`` / ``opener``, plain BOM property reads,
        returned plain objects/arrays, non-HostObject missing probes, ``in``,
        delete, enumeration, descriptors, method-reference reads, and
        user-created plain objects are not covered yet.

        ``max_events`` is the retained-event capacity (earliest N retained; later
        events increment ``dropped_events``). It must be a positive ``int``; a
        ``bool`` is rejected. ``object_types`` is ``None`` or a list of supported
        object-type strings; unknown names raise ``ValueError`` and duplicates are
        removed. An empty list is valid and records nothing.

        ``start`` begins a new session and clears old events/drops. The
        configuration and events survive ``load()``; a load increments the
        generation and resets native object-ID bindings. Recording is off until
        this method is called. Raises ``JSContextDisposedError`` after
        ``dispose()``.
        """
        if isinstance(max_events, bool) or not isinstance(max_events, int):
            raise TypeError("max_events must be an int")
        if max_events < 1:
            raise ValueError("max_events must be >= 1")
        if object_types is not None:
            if not isinstance(object_types, list):
                raise TypeError("object_types must be None or a list")
            deduped = []
            seen = set()
            for index, object_type in enumerate(object_types):
                if not isinstance(object_type, str):
                    raise TypeError(f"object_types[{index}] must be a str")
                if object_type not in _ACCESS_TRACE_OBJECT_TYPES:
                    raise ValueError(
                        f"unsupported access-trace object type: {object_type!r}")
                if object_type not in seen:
                    seen.add(object_type)
                    deduped.append(object_type)
            object_types = deduped
        self._native.start_access_trace(max_events, object_types)

    def stop_access_trace(self) -> None:
        """Stop recording new access events while retaining the current log.

        A later :meth:`start_access_trace` begins a new session. Raises
        ``JSContextDisposedError`` after ``dispose()``.
        """
        self._native.stop_access_trace()

    def read_access_trace(self, clear: bool = True) -> dict:
        """Return the access-trace report, optionally clearing the event log.

        The report has ``events``, ``enabled``, ``generation``, ``max_events``,
        ``dropped_events``, ``filter_object_types``, and a ``coverage`` dictionary
        describing this slice's explicit scope. Each event contains ``seq``,
        ``generation``, ``object_id``, ``object_type``, ``label``, ``member``,
        ``operation`` (``get``/``set``/``call``/``get_probe``), ``phase``
        (``enter``), ``resource_name``, and ``source_kind`` (``eval``/
        ``html_script``/``internal``/``unknown``). ``get_probe`` events also
        carry ``resolution`` (currently ``"missing"`` for absent HostObject
        string-key reads).

        ``clear=True`` clears events and ``dropped_events`` but preserves the
        sequence, configuration, generation, and object-ID mappings. Reading never
        executes page code. Raises ``JSContextDisposedError`` after ``dispose()``.
        """
        if not isinstance(clear, bool):
            raise TypeError("clear must be a bool")
        return self._native.read_access_trace(clear)

    def dispose(self) -> None:
        """Release the page's context-owned native resources. Idempotent."""
        # M9-1: stop the DevTools server first (best-effort) so its port is freed;
        # the native Inspector is torn down with the context by dispose() below.
        if self._devtools is not None:
            self._devtools.shutdown()
            self._devtools = None
        self._native.dispose()

    def __enter__(self) -> "Page":
        if self._native.disposed:
            raise JSContextDisposedError("Page is already disposed")
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> bool:
        # Dispose on exit without suppressing any in-flight user exception.
        self.dispose()
        return False
