"""M9-5 acceptance tests: page.watch_reflect_has() / page.read_reflect_has_hits().

Minimal Reflect.has monitoring (mirrors M9-4's Reflect.get): when enabled, this page's
Reflect.has is wrapped with a recording hook that logs each call's property key and
delegates to the real Reflect.has (behavior unchanged). break_on_hit reuses the M9-3
semantics (pause when a DevTools session is attached, record-only otherwise). This
phase monitors ONLY Reflect.has — not Reflect.ownKeys/Object.*, not plain "x" in obj —
and adds no JS-visible monitoring global. read_reflect_has_hits() is read-and-clear.
Config persists across load; dispose() follows the existing disposed error path.
"""

import json

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://reflecthas.test/"

_STD_ALL = {
    "__version__", "_v8_version", "_v8_commit", "_v8_linked",
    "_v8_runtime_version", "JSContext", "JSContextDisposedError",
    "JSContextBusyError", "JSConversionError", "JSError", "JSUndefined",
    "JSValue", "Page",
}


# --- API-shape guard (both build modes) -----------------------------------------

def test_surface_shape():
    assert hasattr(iv8.Page, "watch_reflect_has")
    assert hasattr(iv8.Page, "read_reflect_has_hits")
    # the M9-2/3/4 lines are untouched
    for attr in ("watch_apis", "read_watch_api_hits", "watch_reflect_get",
                 "read_reflect_get_hits"):
        assert hasattr(iv8.Page, attr)
    assert set(iv8.__all__) == _STD_ALL
    # (Reflect.ownKeys monitoring arrived in M10-1 as watch_reflect_ownkeys)
    for attr in ("watch_object_keys", "watch_function_to_string"):
        assert not hasattr(iv8.Page, attr)


# --- pure-record mode ------------------------------------------------------------

@on_only
def test_records_reflect_has():
    with iv8.Page() as page:
        page.watch_reflect_has(enabled=True)
        value = page.eval("Reflect.has({x: 1}, 'x')")
        assert value is True  # delegation preserved the return value
        hits = page.read_reflect_has_hits()
        assert isinstance(hits, list) and len(hits) == 1
        assert hits[0]["property"] == "x"
        assert hits[0]["type"] == "Reflect.has"


# --- not enabled -> no record ----------------------------------------------------

@on_only
def test_not_enabled_no_record():
    with iv8.Page() as page:
        assert page.eval("Reflect.has({x: 1}, 'x')") is True
        assert page.read_reflect_has_hits() == []
    with iv8.Page() as page:
        page.watch_reflect_has(enabled=False)
        assert page.eval("Reflect.has({x: 1}, 'x')") is True
        assert page.read_reflect_has_hits() == []


# --- property key stringification (string + numeric) ----------------------------

@on_only
def test_property_key_strings():
    with iv8.Page() as page:
        page.watch_reflect_has(enabled=True)
        assert page.eval("Reflect.has({abc: 1}, 'abc')") is True
        assert page.eval("Reflect.has([10, 20], 1)") is True  # numeric key
        props = [h["property"] for h in page.read_reflect_has_hits()]
        assert props == ["abc", "1"]


# --- read-and-clear --------------------------------------------------------------

@on_only
def test_read_clears():
    with iv8.Page() as page:
        page.watch_reflect_has(enabled=True)
        page.eval("Reflect.has({x: 1}, 'x')")
        assert len(page.read_reflect_has_hits()) == 1
        assert page.read_reflect_has_hits() == []


# --- break_on_hit=True with no attached session records only (no error) ---------

@on_only
def test_break_true_no_session_records_only():
    with iv8.Page() as page:
        page.watch_reflect_has(enabled=True, break_on_hit=True)
        assert page.eval("Reflect.has({x: 1}, 'x')") is True
        assert len(page.read_reflect_has_hits()) == 1


# --- break_on_hit=True with an attached session -> observable pause -------------

@on_only
def test_break_true_pauses_with_attached_session():
    with iv8.Page() as page:
        page.load(html="<html><body></body></html>", base_url=BASE)
        page.devtools_url()
        page._native.devtools_dispatch(
            json.dumps({"id": 1, "method": "Debugger.enable"}))
        page.watch_reflect_has(enabled=True, break_on_hit=True)
        assert page._native.devtools_pause_count() == 0
        page.eval("Reflect.has({x: 1}, 'x')")
        assert page._native.devtools_pause_count() >= 1
        assert len(page.read_reflect_has_hits()) == 1


# --- config persists across load() ----------------------------------------------

@on_only
def test_config_persists_across_load():
    with iv8.Page() as page:
        page.watch_reflect_has(enabled=True)
        page.load(html="<html><body></body></html>", base_url=BASE)
        page.eval("Reflect.has({x: 1}, 'x')")
        assert len(page.read_reflect_has_hits()) == 1
        page.load(html="<html><body></body></html>", base_url=BASE)
        page.eval("Reflect.has({y: 1}, 'y')")
        hits = page.read_reflect_has_hits()
        assert len(hits) == 1 and hits[0]["property"] == "y"


# --- the M9-2 / M9-4 lines are unaffected ---------------------------------------

@on_only
def test_coexists_with_other_watches():
    with iv8.Page() as page:
        page.watch_apis(["document.querySelector"])
        page.watch_reflect_get(enabled=True)
        page.watch_reflect_has(enabled=True)
        page.load(
            html="<html><body><div></div><script>"
                 "document.querySelector('div');"
                 "Reflect.get({x: 1}, 'x');"
                 "Reflect.has({y: 1}, 'y');"
                 "</script></body></html>",
            base_url=BASE,
        )
        api_hits = page.read_watch_api_hits()
        get_hits = page.read_reflect_get_hits()
        has_hits = page.read_reflect_has_hits()
        assert len(api_hits) == 1 and api_hits[0]["path"] == "document.querySelector"
        assert len(get_hits) == 1 and get_hits[0]["property"] == "x"
        assert len(has_hits) == 1 and has_hits[0]["property"] == "y"


# --- only Reflect.has is monitored (not get / ownKeys) --------------------------

@on_only
def test_only_reflect_has_monitored():
    with iv8.Page() as page:
        page.watch_reflect_has(enabled=True)  # note: NOT watch_reflect_get
        page.eval("Reflect.get({x: 1}, 'x')")
        page.eval("Reflect.ownKeys({x: 1})")
        page.eval("'x' in {x: 1}")  # the `in` operator is not Reflect.has
        assert page.read_reflect_has_hits() == []
        # Reflect.has still works and is monitored
        assert page.eval("Reflect.has({x: 1}, 'x')") is True
        assert len(page.read_reflect_has_hits()) == 1
        # Reflect.get monitoring was not enabled -> its log is empty
        assert page.read_reflect_get_hits() == []


# --- no JS-visible monitoring global; Reflect.has stays normal ------------------

@on_only
def test_no_js_global():
    with iv8.Page() as page:
        page.watch_reflect_has(enabled=True)
        for g in ("watchReflectHas", "reflectHasHits", "__reflectHas__"):
            assert page.eval(f"typeof globalThis.{g}") == "undefined"
        assert page.eval("typeof Reflect.has") == "function"


# --- dispose() follows the existing disposed error path -------------------------

@on_only
def test_dispose_error_path():
    page = iv8.Page()
    page.watch_reflect_has(enabled=True)
    page.dispose()
    with pytest.raises(iv8.JSContextDisposedError):
        page.watch_reflect_has(enabled=True)
    with pytest.raises(iv8.JSContextDisposedError):
        page.read_reflect_has_hits()


# --- bad argument types raise TypeError -----------------------------------------

@on_only
def test_bad_arg_types():
    with iv8.Page() as page:
        with pytest.raises(TypeError):
            page.watch_reflect_has(enabled="yes")
        with pytest.raises(TypeError):
            page.watch_reflect_has(enabled=True, break_on_hit="no")
