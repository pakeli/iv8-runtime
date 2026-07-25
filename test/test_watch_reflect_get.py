"""M9-4 acceptance tests: page.watch_reflect_get() / page.read_reflect_get_hits().

Minimal Reflect.get monitoring: when enabled, this page's Reflect.get is wrapped with
a recording hook that logs each call's property key and delegates to the real
Reflect.get (behavior unchanged). break_on_hit reuses the M9-3 semantics (pause when
a DevTools session is attached, record-only otherwise). This phase monitors ONLY
Reflect.get — not Reflect.has/ownKeys/Object.*, not plain obj.x access — and adds no
JS-visible monitoring global. read_reflect_get_hits() is read-and-clear. Config
persists across load; dispose() follows the existing disposed error path.
"""

import json

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://reflect.test/"

_STD_ALL = {
    "__version__", "_v8_version", "_v8_commit", "_v8_linked",
    "_v8_runtime_version", "JSContext", "JSContextDisposedError",
    "JSContextBusyError", "JSConversionError", "JSError", "JSUndefined",
    "JSValue", "Page",
}


# --- API-shape guard (both build modes) -----------------------------------------

def test_surface_shape():
    assert hasattr(iv8.Page, "watch_reflect_get")
    assert hasattr(iv8.Page, "read_reflect_get_hits")
    # the M9-2/3 host-method line is untouched
    assert hasattr(iv8.Page, "watch_apis")
    assert hasattr(iv8.Page, "read_watch_api_hits")
    assert set(iv8.__all__) == _STD_ALL
    # explicitly-frozen siblings absent (watch_reflect_has arrived in M9-5)
    for attr in ("watch_reflect_own_keys", "watch_object_keys"):
        assert not hasattr(iv8.Page, attr)


# --- pure-record mode ------------------------------------------------------------

@on_only
def test_records_reflect_get():
    with iv8.Page() as page:
        page.watch_reflect_get(enabled=True)
        value = page.eval("Reflect.get({x: 42}, 'x')")
        assert value == 42  # delegation preserved the return value
        hits = page.read_reflect_get_hits()
        assert isinstance(hits, list) and len(hits) == 1
        assert hits[0]["property"] == "x"
        assert hits[0]["type"] == "Reflect.get"


# --- not enabled -> no record ----------------------------------------------------

@on_only
def test_not_enabled_no_record():
    # never called
    with iv8.Page() as page:
        assert page.eval("Reflect.get({x: 1}, 'x')") == 1
        assert page.read_reflect_get_hits() == []
    # explicitly disabled
    with iv8.Page() as page:
        page.watch_reflect_get(enabled=False)
        assert page.eval("Reflect.get({x: 1}, 'x')") == 1
        assert page.read_reflect_get_hits() == []


# --- property key stringification (string + numeric) ----------------------------

@on_only
def test_property_key_strings():
    with iv8.Page() as page:
        page.watch_reflect_get(enabled=True)
        assert page.eval("Reflect.get({abc: 7}, 'abc')") == 7
        assert page.eval("Reflect.get([10, 20, 30], 1)") == 20  # numeric key
        props = [h["property"] for h in page.read_reflect_get_hits()]
        assert props == ["abc", "1"]


# --- read-and-clear --------------------------------------------------------------

@on_only
def test_read_clears():
    with iv8.Page() as page:
        page.watch_reflect_get(enabled=True)
        page.eval("Reflect.get({x: 1}, 'x')")
        assert len(page.read_reflect_get_hits()) == 1
        assert page.read_reflect_get_hits() == []


# --- break_on_hit=True with no attached session records only (no error) ---------

@on_only
def test_break_true_no_session_records_only():
    with iv8.Page() as page:
        page.watch_reflect_get(enabled=True, break_on_hit=True)
        assert page.eval("Reflect.get({x: 1}, 'x')") == 1
        assert len(page.read_reflect_get_hits()) == 1


# --- break_on_hit=True with an attached session -> observable pause -------------

@on_only
def test_break_true_pauses_with_attached_session():
    with iv8.Page() as page:
        page.load(html="<html><body></body></html>", base_url=BASE)
        page.devtools_url()
        page._native.devtools_dispatch(
            json.dumps({"id": 1, "method": "Debugger.enable"}))
        page.watch_reflect_get(enabled=True, break_on_hit=True)
        assert page._native.devtools_pause_count() == 0
        page.eval("Reflect.get({x: 1}, 'x')")
        assert page._native.devtools_pause_count() >= 1  # pause evidence (M9-3 path)
        assert len(page.read_reflect_get_hits()) == 1


# --- config persists across load() ----------------------------------------------

@on_only
def test_config_persists_across_load():
    with iv8.Page() as page:
        page.watch_reflect_get(enabled=True)
        page.load(html="<html><body></body></html>", base_url=BASE)
        page.eval("Reflect.get({x: 1}, 'x')")
        assert len(page.read_reflect_get_hits()) == 1
        # a fresh load WITHOUT re-enabling still records (config survived)
        page.load(html="<html><body></body></html>", base_url=BASE)
        page.eval("Reflect.get({y: 2}, 'y')")
        hits = page.read_reflect_get_hits()
        assert len(hits) == 1 and hits[0]["property"] == "y"


# --- the M9-2 host-method watch line is unaffected ------------------------------

@on_only
def test_coexists_with_watch_apis():
    with iv8.Page() as page:
        page.watch_apis(["document.querySelector"])
        page.watch_reflect_get(enabled=True)
        page.load(
            html="<html><body><div></div><script>"
                 "document.querySelector('div'); Reflect.get({x: 1}, 'x');"
                 "</script></body></html>",
            base_url=BASE,
        )
        api_hits = page.read_watch_api_hits()
        reflect_hits = page.read_reflect_get_hits()
        assert len(api_hits) == 1 and api_hits[0]["path"] == "document.querySelector"
        assert len(reflect_hits) == 1 and reflect_hits[0]["property"] == "x"


# --- only Reflect.get is monitored (not has / ownKeys) --------------------------

@on_only
def test_only_reflect_get_monitored():
    with iv8.Page() as page:
        page.watch_reflect_get(enabled=True)
        page.eval("Reflect.has({x: 1}, 'x')")
        page.eval("Reflect.ownKeys({x: 1})")
        page.eval("Object.keys({x: 1})")
        # none of those are Reflect.get -> nothing recorded
        assert page.read_reflect_get_hits() == []
        # Reflect.get still works and is monitored
        page.eval("Reflect.get({x: 1}, 'x')")
        assert len(page.read_reflect_get_hits()) == 1


# --- no JS-visible monitoring global; Reflect stays normal ----------------------

@on_only
def test_no_js_global():
    with iv8.Page() as page:
        page.watch_reflect_get(enabled=True)
        for g in ("watchReflectGet", "reflectGetHits", "__reflect__", "wrapNative"):
            assert page.eval(f"typeof globalThis.{g}") == "undefined"
        # Reflect.get is still a callable function
        assert page.eval("typeof Reflect.get") == "function"


# --- dispose() follows the existing disposed error path -------------------------

@on_only
def test_dispose_error_path():
    page = iv8.Page()
    page.watch_reflect_get(enabled=True)
    page.dispose()
    with pytest.raises(iv8.JSContextDisposedError):
        page.watch_reflect_get(enabled=True)
    with pytest.raises(iv8.JSContextDisposedError):
        page.read_reflect_get_hits()


# --- bad argument types raise TypeError -----------------------------------------

@on_only
def test_bad_arg_types():
    with iv8.Page() as page:
        with pytest.raises(TypeError):
            page.watch_reflect_get(enabled="yes")
        with pytest.raises(TypeError):
            page.watch_reflect_get(enabled=True, break_on_hit="no")
