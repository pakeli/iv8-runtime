"""M11-41..M11-45 acceptance tests: remaining deterministic BOM surfaces."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-41.test/"
VIEWPORT = {"innerWidth": 800, "innerHeight": 600}


def _load(page):
    page.load("<html><body></body></html>", BASE)


def test_no_new_python_surface():
    for name in ("visualViewport", "matchMedia", "storage", "permissions",
                 "plugins", "mimeTypes", "javaEnabled"):
        assert not hasattr(iv8, name)
        assert not hasattr(iv8.Page, name)


# --- M11-41 visual viewport and window flags -----------------------------------

@on_only
def test_visual_viewport_and_window_flags_are_static_metadata():
    with iv8.Page(environment=VIEWPORT) as page:
        _load(page)
        assert page.eval("typeof visualViewport") == "object"
        assert page.eval("visualViewport.width") == 800
        assert page.eval("visualViewport.height") == 600
        assert page.eval("visualViewport.offsetLeft") == 0
        assert page.eval("visualViewport.offsetTop") == 0
        assert page.eval("visualViewport.pageLeft") == 0
        assert page.eval("visualViewport.pageTop") == 0
        assert page.eval("visualViewport.scale") == 1
        assert page.eval("visualViewport.onresize") is None
        assert page.eval("visualViewport.onscroll") is None
        assert page.eval(
            "visualViewport.addEventListener('resize', () => {}) === undefined"
        ) is True
        assert page.eval(
            "visualViewport.removeEventListener('resize', () => {}) === undefined"
        ) is True
        assert page.eval("window.origin") == "https://m11-41.test"
        assert page.eval("window.crossOriginIsolated") is False
        assert page.eval("window.closed") is False
        assert page.eval("window.frameElement") is None


@on_only
def test_visual_viewport_and_window_flags_are_read_only():
    with iv8.Page(environment=VIEWPORT) as page:
        _load(page)
        assert page.eval(
            """
            (() => {
              visualViewport = null;
              visualViewport.width = 1;
              window.origin = 'https://changed.test';
              window.closed = true;
              window.crossOriginIsolated = true;
              window.frameElement = {};
              return [typeof visualViewport,
                      visualViewport.width,
                      window.origin,
                      window.closed,
                      window.crossOriginIsolated,
                      String(window.frameElement)].join('|');
            })();
            """
        ) == ("object|800|https://m11-41.test|false|false|null")


# --- M11-42 matchMedia ---------------------------------------------------------

@on_only
def test_match_media_evaluates_bounded_queries():
    with iv8.Page(environment=VIEWPORT) as page:
        _load(page)
        assert page.eval(
            """
            (() => {
              const q = s => matchMedia(s).matches;
              return [
                q('(min-width: 700px)'),
                q('(max-width: 700px)'),
                q('(width: 800px)'),
                q('(height: 600px)'),
                q('(orientation: landscape)'),
                q('(orientation: portrait)'),
                q('screen and (min-width: 800px)'),
                q('only screen and (min-width: 700px)'),
                q('(min-width: 700px) and (max-width: 900px)'),
                q('(min-width: 700px), print'),
                q('print'),
                q('not print'),
                q('(prefers-color-scheme: light)'),
                q('(prefers-reduced-motion: no-preference)'),
                q('(hover: hover)'),
                q('(pointer: fine)'),
                q('(display-mode: browser)'),
                q('(min-resolution: 2dppx)'),
                q('')
              ].join(',');
            })();
            """
        ) == ("true,false,true,true,true,false,true,true,true,true,"
               "false,true,true,true,true,true,true,false,true")


@on_only
def test_match_media_list_is_static_and_has_noop_listeners():
    with iv8.Page(environment=VIEWPORT) as page:
        _load(page)
        assert page.eval(
            """
            (() => {
              const m = matchMedia('screen and (min-width: 700px)');
              let called = 0;
              m.addListener(() => { called++; });
              m.addEventListener('change', () => { called++; });
              m.onchange = () => { called++; };
              return [m.media, m.matches, m.onchange !== null,
                      called,
                      matchMedia.toString().indexOf('[native code]') >= 0]
                  .join('|');
            })();
            """
        ) == "screen and (min-width: 700px)|true|true|0|false"


@on_only
def test_match_media_is_read_only():
    with iv8.Page(environment=VIEWPORT) as page:
        _load(page)
        assert page.eval(
            "matchMedia = null; matchMedia('screen').matches"
        ) is True
        assert page.eval(
            """
            (() => {
              const m = matchMedia('(width: 800px)');
              m.media = 'changed';
              m.matches = false;
              return [m.media, m.matches].join('|');
            })();
            """
        ) == "(width: 800px)|true"


# --- M11-43 navigator.storage --------------------------------------------------

@on_only
def test_storage_manager_is_deterministic_and_promise_based():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("typeof navigator.storage") == "object"
        for name in ("estimate", "persisted", "persist"):
            assert page.eval(f"typeof navigator.storage.{name}") == "function"
        page.eval("""
            globalThis.estimate = null;
            globalThis.persisted = null;
            globalThis.persist = null;
            navigator.storage.estimate().then(v => { estimate = v; });
            navigator.storage.persisted().then(v => { persisted = v; });
            navigator.storage.persist().then(v => { persist = v; });
        """)
        page.run_jobs()
        assert page.eval("estimate.usage") == 0
        assert page.eval("estimate.quota") == 10737418240
        assert page.eval("persisted") is False
        assert page.eval("persist") is False
        assert page.eval(
            "navigator.storage = null; typeof navigator.storage"
        ) == "object"
        assert page.eval(
            "navigator.storage.estimate = 1; typeof navigator.storage.estimate"
        ) == "function"


# --- M11-44 navigator.permissions ---------------------------------------------

@on_only
def test_permissions_query_returns_static_prompt_statuses():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("typeof navigator.permissions") == "object"
        page.eval("""
            globalThis.geo = null;
            globalThis.camera = null;
            globalThis.unknown = null;
            navigator.permissions.query({name: 'geolocation'})
                .then(s => { geo = s; });
            navigator.permissions.query({name: 'camera'})
                .then(s => { camera = s; });
            navigator.permissions.query({name: 'not-a-permission'})
                .catch(e => { unknown = e.name; });
        """)
        page.run_jobs()
        assert page.eval("geo.name") == "geolocation"
        assert page.eval("geo.state") == "prompt"
        assert page.eval("camera.name") == "camera"
        assert page.eval("camera.state") == "prompt"
        assert page.eval("unknown") == "TypeError"
        assert page.eval(
            "geo.addEventListener('change', () => {}) === undefined"
        ) is True
        assert page.eval("geo.onchange") is None
        assert page.eval(
            "navigator.permissions = null; typeof navigator.permissions"
        ) == "object"


# --- M11-45 legacy navigator metadata -----------------------------------------

@on_only
def test_legacy_navigator_metadata_is_empty_and_read_only():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("Array.isArray(navigator.plugins)") is True
        assert page.eval("navigator.plugins.length") == 0
        assert page.eval("Object.isFrozen(navigator.plugins)") is True
        assert page.eval("Array.isArray(navigator.mimeTypes)") is True
        assert page.eval("navigator.mimeTypes.length") == 0
        assert page.eval("Object.isFrozen(navigator.mimeTypes)") is True
        assert page.eval("navigator.javaEnabled()") is False
        assert page.eval(
            """
            navigator.plugins = [1];
            navigator.mimeTypes = [1];
            navigator.javaEnabled = () => true;
            [navigator.plugins.length,
             navigator.mimeTypes.length,
             navigator.javaEnabled()].join(',')
            """
        ) == "0,0,false"
