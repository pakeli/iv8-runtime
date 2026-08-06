"""M11-1 acceptance tests: configurable host-environment baseline.

`Page(environment=...)` configures the page's read-only environment surface and
`Page.get_default_environment()` returns the default template as a plain dict. The
eleven supported keys map onto navigator.userAgent / platform / language / webdriver,
screen.width / height / availWidth / availHeight, and window.devicePixelRatio /
innerWidth / innerHeight.

Every value is a fixed constant (never OS/display/locale-derived), the whole surface
is read-only from JS, values stay identical across repeated `load()` generations, and
validation is strict and happens in the Python facade before any page exists — so a
bad environment never leaves a partially-installed page. M11-1 adds no network,
animation-frame, UA-CH, or canvas/webgl surface.
"""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://environment.test/"

# The full key set Page(environment=...) accepts, with each key's required type.
EXPECTED_KEYS = {
    "userAgent": str,
    "platform": str,
    "language": str,
    "webdriver": bool,
    "screenWidth": int,
    "screenHeight": int,
    "availWidth": int,
    "availHeight": int,
    "devicePixelRatio": float,
    "innerWidth": int,
    "innerHeight": int,
}

# The documented defaults. The four navigator values must match what M2-3 shipped,
# so an unconfigured page is behaviourally unchanged by M11-1.
EXPECTED_DEFAULTS = {
    "userAgent": "Mozilla/5.0 (compatible; iv8)",
    "platform": "iv8",
    "language": "en-US",
    "webdriver": False,
    "screenWidth": 1920,
    "screenHeight": 1080,
    "availWidth": 1920,
    "availHeight": 1040,
    "devicePixelRatio": 1.0,
    "innerWidth": 1280,
    "innerHeight": 720,
}

# The JS expression that reads each environment key, for value round-tripping.
JS_READER = {
    "userAgent": "navigator.userAgent",
    "platform": "navigator.platform",
    "language": "navigator.language",
    "webdriver": "navigator.webdriver",
    "screenWidth": "screen.width",
    "screenHeight": "screen.height",
    "availWidth": "screen.availWidth",
    "availHeight": "screen.availHeight",
    "devicePixelRatio": "devicePixelRatio",
    "innerWidth": "innerWidth",
    "innerHeight": "innerHeight",
}

# A full override set, every value distinct from the default so a leaked default is
# always visible as a failure.
OVERRIDE = {
    "userAgent": "Mozilla/5.0 (X11; Linux x86_64) custom/1.0",
    "platform": "Linux x86_64",
    "language": "zh-CN",
    "webdriver": True,
    "screenWidth": 2560,
    "screenHeight": 1440,
    "availWidth": 2550,
    "availHeight": 1400,
    "devicePixelRatio": 2.5,
    "innerWidth": 1024,
    "innerHeight": 768,
}


# --- API-shape guards (both build modes) -----------------------------------------

def test_surface_shape():
    assert callable(getattr(iv8.Page, "get_default_environment", None))
    # M11-1 adds exactly one new Page method; no setter / reconfigure surface.
    for forbidden in ("set_environment", "environment", "update_environment",
                      "configure", "set_user_agent"):
        assert not hasattr(iv8.Page, forbidden)


def test_no_new_module_level_surface():
    for forbidden in ("screen", "navigator", "devicePixelRatio", "Environment",
                      "HostEnvironment"):
        assert not hasattr(iv8, forbidden)


def test_non_goals_absent_from_page():
    # M11-1 must not drag in networking, animation frames, UA-CH, or canvas/webgl.
    for forbidden in ("fetch", "request_animation_frame", "user_agent_data",
                      "canvas", "webgl", "set_viewport"):
        assert not hasattr(iv8.Page, forbidden)


# --- get_default_environment(): shape, types, isolation --------------------------

def test_default_environment_shape_and_types():
    env = iv8.Page.get_default_environment()
    assert isinstance(env, dict)
    assert set(env) == set(EXPECTED_KEYS)
    for key, expected_type in EXPECTED_KEYS.items():
        assert type(env[key]) is expected_type, key


def test_default_environment_values():
    assert iv8.Page.get_default_environment() == EXPECTED_DEFAULTS


def test_default_environment_returns_a_fresh_copy():
    first = iv8.Page.get_default_environment()
    first["userAgent"] = "mutated"
    first["screenWidth"] = -1
    del first["language"]
    # A later call is unaffected by mutating an earlier result.
    assert iv8.Page.get_default_environment() == EXPECTED_DEFAULTS


def test_default_environment_callable_without_a_page():
    # Available on the class in BOTH build modes (it touches no native state), so a
    # skeleton build can still read the template.
    assert iv8.Page.get_default_environment()["platform"] == "iv8"


# --- default values are visible in JS --------------------------------------------

@on_only
def test_js_reports_defaults_when_unconfigured():
    with iv8.Page() as page:
        for key, expected in EXPECTED_DEFAULTS.items():
            assert page.eval(JS_READER[key]) == expected, key


@on_only
def test_environment_none_matches_omitted():
    with iv8.Page(environment=None) as page:
        assert page.eval("navigator.userAgent") == EXPECTED_DEFAULTS["userAgent"]
        assert page.eval("screen.width") == EXPECTED_DEFAULTS["screenWidth"]


@on_only
def test_empty_mapping_yields_defaults():
    with iv8.Page(environment={}) as page:
        for key, expected in EXPECTED_DEFAULTS.items():
            assert page.eval(JS_READER[key]) == expected, key


@on_only
def test_screen_and_window_objects_exist():
    with iv8.Page() as page:
        assert page.eval("typeof screen") == "object"
        assert page.eval("typeof devicePixelRatio") == "number"
        assert page.eval("typeof innerWidth") == "number"
        # window is the global, so window.innerWidth is the same property.
        assert page.eval("window.innerWidth === innerWidth") is True
        assert page.eval("window.screen === screen") is True


# --- override values --------------------------------------------------------------

@on_only
def test_full_override_visible_in_js():
    with iv8.Page(environment=OVERRIDE) as page:
        for key, expected in OVERRIDE.items():
            assert page.eval(JS_READER[key]) == expected, key


@on_only
@pytest.mark.parametrize("key", sorted(EXPECTED_KEYS))
def test_single_key_override_leaves_the_rest_at_default(key):
    with iv8.Page(environment={key: OVERRIDE[key]}) as page:
        for other, default in EXPECTED_DEFAULTS.items():
            expected = OVERRIDE[key] if other == key else default
            assert page.eval(JS_READER[other]) == expected, other


@on_only
def test_int_accepted_for_device_pixel_ratio():
    # devicePixelRatio takes an int or a float and is stored as float Python-side.
    assert type(iv8.Page.get_default_environment()["devicePixelRatio"]) is float
    with iv8.Page(environment={"devicePixelRatio": 3}) as page:
        assert page.eval("devicePixelRatio") == 3
        # JS sees a Number; the M1 converter maps an integral Number back to a
        # Python int, so compare numerically rather than by Python type.
        assert page.eval("typeof devicePixelRatio") == "number"


@on_only
def test_fractional_device_pixel_ratio_round_trips():
    # A non-integral ratio must survive the double round-trip exactly.
    with iv8.Page(environment={"devicePixelRatio": 2.5}) as page:
        value = page.eval("devicePixelRatio")
        assert value == 2.5
        assert isinstance(value, float)
    with iv8.Page(environment={"devicePixelRatio": 1.25}) as page:
        assert page.eval("devicePixelRatio") == 1.25


@on_only
def test_avail_dimensions_are_independent_of_screen_dimensions():
    # avail* are configured values, never computed from width/height.
    env = {"screenWidth": 800, "screenHeight": 600,
           "availWidth": 1234, "availHeight": 567}
    with iv8.Page(environment=env) as page:
        assert page.eval("screen.width") == 800
        assert page.eval("screen.availWidth") == 1234
        assert page.eval("screen.availHeight") == 567


@on_only
def test_environment_mapping_is_copied_not_aliased():
    env = dict(OVERRIDE)
    with iv8.Page(environment=env) as page:
        env["userAgent"] = "mutated-after-construction"
        env["screenWidth"] = -1
        # The page keeps the values it was constructed with.
        assert page.eval("navigator.userAgent") == OVERRIDE["userAgent"]
        assert page.eval("screen.width") == OVERRIDE["screenWidth"]


@on_only
def test_pages_are_independent():
    with iv8.Page(environment={"userAgent": "page-a"}) as a, \
            iv8.Page(environment={"userAgent": "page-b"}) as b:
        assert a.eval("navigator.userAgent") == "page-a"
        assert b.eval("navigator.userAgent") == "page-b"
        # A default page alongside configured ones still reports the default.
        with iv8.Page() as c:
            assert c.eval("navigator.userAgent") == EXPECTED_DEFAULTS["userAgent"]


# --- read-only from JS ------------------------------------------------------------

@on_only
def test_environment_surface_is_read_only():
    with iv8.Page(environment=OVERRIDE) as page:
        # Sloppy-mode writes to read-only properties are silent no-ops.
        assert page.eval("navigator.userAgent = 'x'; navigator.userAgent") \
            == OVERRIDE["userAgent"]
        assert page.eval("navigator.webdriver = false; navigator.webdriver") is True
        assert page.eval("screen.width = 1; screen.width") \
            == OVERRIDE["screenWidth"]
        assert page.eval("screen.availHeight = 1; screen.availHeight") \
            == OVERRIDE["availHeight"]
        assert page.eval("devicePixelRatio = 9; devicePixelRatio") \
            == OVERRIDE["devicePixelRatio"]
        assert page.eval("innerWidth = 9; innerWidth") == OVERRIDE["innerWidth"]
        assert page.eval("innerHeight = 9; innerHeight") == OVERRIDE["innerHeight"]


@on_only
def test_strict_mode_write_raises_type_error():
    with iv8.Page() as page:
        with pytest.raises(iv8.JSError) as info:
            page.eval("'use strict'; innerWidth = 5")
        assert info.value.name == "TypeError"
        with pytest.raises(iv8.JSError):
            page.eval("'use strict'; screen.width = 5")


# --- values persist across repeated load() ----------------------------------------

@on_only
def test_values_survive_repeated_load():
    with iv8.Page(environment=OVERRIDE) as page:
        for generation in range(3):
            page.load("<html><body></body></html>", f"{BASE}{generation}")
            for key, expected in OVERRIDE.items():
                assert page.eval(JS_READER[key]) == expected, (generation, key)


@on_only
def test_defaults_survive_repeated_load():
    with iv8.Page() as page:
        page.load("<html><body></body></html>", BASE)
        for key, expected in EXPECTED_DEFAULTS.items():
            assert page.eval(JS_READER[key]) == expected, key


@on_only
def test_html_scripts_observe_the_configured_environment():
    # A script running during load() sees the configured values, not the defaults.
    html = """
    <html><body><script>
      globalThis.seenAgent = navigator.userAgent;
      globalThis.seenWidth = screen.width;
      globalThis.seenRatio = devicePixelRatio;
    </script></body></html>
    """
    with iv8.Page(environment=OVERRIDE) as page:
        page.load(html, BASE)
        assert page.eval("seenAgent") == OVERRIDE["userAgent"]
        assert page.eval("seenWidth") == OVERRIDE["screenWidth"]
        assert page.eval("seenRatio") == OVERRIDE["devicePixelRatio"]


@on_only
def test_failed_load_does_not_change_the_environment():
    with iv8.Page(environment=OVERRIDE) as page:
        with pytest.raises(iv8.JSError):
            page.load("<html><body><script>throw new Error('x')</script></body></html>",
                      BASE)
        # The generation installed before the failing script still reports the
        # configured values (no rollback, no environment loss).
        assert page.eval("navigator.userAgent") == OVERRIDE["userAgent"]
        assert page.eval("screen.width") == OVERRIDE["screenWidth"]
        assert page.eval("innerHeight") == OVERRIDE["innerHeight"]


# --- strict validation: type errors ----------------------------------------------

def test_non_mapping_environment_raises_type_error():
    for bad in ("string", 42, 1.5, True, [("userAgent", "x")], object(),
                ("userAgent", "x")):
        with pytest.raises(TypeError, match="environment must be a mapping"):
            iv8.Page(environment=bad)


def test_unknown_key_raises_type_error():
    for bad_key in ("userAgentData", "colorDepth", "outerWidth", "", "useragent",
                    "UserAgent"):
        with pytest.raises(TypeError, match="unknown environment key"):
            iv8.Page(environment={bad_key: 1})


def test_unknown_key_rejected_even_beside_valid_keys():
    with pytest.raises(TypeError, match="unknown environment key"):
        iv8.Page(environment={"userAgent": "ok", "colorDepth": 24})


@pytest.mark.parametrize("key", ["userAgent", "platform", "language"])
def test_string_keys_reject_non_strings(key):
    for bad in (1, 1.0, True, None, [], {}, b"bytes"):
        with pytest.raises(TypeError, match="must be a str"):
            iv8.Page(environment={key: bad})


def test_webdriver_rejects_non_bool():
    for bad in (1, 0, "true", None, [], 1.0):
        with pytest.raises(TypeError, match="must be a bool"):
            iv8.Page(environment={"webdriver": bad})


@pytest.mark.parametrize("key", ["screenWidth", "screenHeight", "availWidth",
                                 "availHeight", "innerWidth", "innerHeight"])
def test_int_keys_reject_non_ints(key):
    for bad in ("1920", 1920.0, None, [], {}, 1920.5):
        with pytest.raises(TypeError, match="must be an int"):
            iv8.Page(environment={key: bad})


@pytest.mark.parametrize("key", ["screenWidth", "screenHeight", "availWidth",
                                 "availHeight", "innerWidth", "innerHeight"])
def test_int_keys_reject_bool(key):
    # bool is a subclass of int in Python; a dimension must not silently accept it.
    for bad in (True, False):
        with pytest.raises(TypeError, match="must be an int"):
            iv8.Page(environment={key: bad})


def test_device_pixel_ratio_rejects_non_numbers():
    for bad in ("2.0", None, [], {}, True, False):
        with pytest.raises(TypeError, match="must be a real number"):
            iv8.Page(environment={"devicePixelRatio": bad})


def test_validation_happens_before_any_page_is_created():
    # A TypeError must be raised by the facade before native state exists, so no
    # partially-installed page can leak. Verified by the error type/message coming
    # from validation rather than from the native layer or the V8-link check.
    with pytest.raises(TypeError):
        iv8.Page(environment={"screenWidth": "wide"})
    # A valid page still constructs normally afterwards (no global state damaged).
    if v8_linked:
        with iv8.Page() as page:
            assert page.eval("screen.width") == EXPECTED_DEFAULTS["screenWidth"]


def test_validation_precedes_the_v8_link_check():
    # In a skeleton build Page() raises RuntimeError, but a bad environment must
    # still raise TypeError — validation runs first in both build modes.
    with pytest.raises(TypeError):
        iv8.Page(environment={"userAgent": 123})


# --- dispose() error paths --------------------------------------------------------

@on_only
def test_environment_reads_after_dispose_use_the_disposed_error_path():
    page = iv8.Page(environment=OVERRIDE)
    assert page.eval("navigator.userAgent") == OVERRIDE["userAgent"]
    page.dispose()
    for expression in ("navigator.userAgent", "navigator.webdriver", "screen.width",
                       "screen.availHeight", "devicePixelRatio", "innerWidth"):
        with pytest.raises(iv8.JSContextDisposedError):
            page.eval(expression)


@on_only
def test_load_after_dispose_still_raises():
    page = iv8.Page(environment=OVERRIDE)
    page.dispose()
    with pytest.raises(iv8.JSContextDisposedError):
        page.load("<html></html>", BASE)


@on_only
def test_get_default_environment_works_after_dispose():
    # It is a static template read, independent of any page's native state.
    page = iv8.Page(environment=OVERRIDE)
    page.dispose()
    assert page.get_default_environment() == EXPECTED_DEFAULTS
    assert iv8.Page.get_default_environment() == EXPECTED_DEFAULTS


@on_only
def test_dispose_is_still_idempotent_with_an_environment():
    page = iv8.Page(environment=OVERRIDE)
    page.dispose()
    page.dispose()
    assert page.disposed is True
