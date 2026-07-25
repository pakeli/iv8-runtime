"""M10-1 acceptance tests: page.watch_reflect_ownkeys() / read_reflect_ownkeys_hits().

Minimal Reflect.ownKeys monitoring (mirrors M9-4/M9-5). When enabled, this page's
Reflect.ownKeys is wrapped with a recording hook that records each call and delegates
to the real Reflect.ownKeys (behavior unchanged). Reflect.ownKeys(target) has no
property key, so a hit is {"resource_name", "type": "Reflect.ownKeys"} (no property
field). break_on_hit reuses M9-3 (pause when a DevTools session is attached, else
record-only). Its record channel is independent of watch_apis / Reflect.get /
Reflect.has. read-and-clear. Config persists across load; dispose reuses the disposed
error path. Monitors ONLY Reflect.ownKeys (not Object.keys / getOwnPropertyNames /
getOwnPropertySymbols / ...) and adds no JS-visible monitoring global.
"""

import json

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://reflectownkeys.test/"

_STD_ALL = {
    "__version__", "_v8_version", "_v8_commit", "_v8_linked",
    "_v8_runtime_version", "JSContext", "JSContextDisposedError",
    "JSContextBusyError", "JSConversionError", "JSError", "JSUndefined",
    "JSValue", "Page",
}


# --- API-shape guard (both build modes) -----------------------------------------

def test_surface_shape():
    assert callable(getattr(iv8.Page, "watch_reflect_ownkeys", None))
    assert callable(getattr(iv8.Page, "read_reflect_ownkeys_hits", None))
    # the M9 lines are untouched
    for attr in ("watch_apis", "read_watch_api_hits", "watch_reflect_get",
                 "read_reflect_get_hits", "watch_reflect_has",
                 "read_reflect_has_hits"):
        assert hasattr(iv8.Page, attr)
    # no new top-level object / exception; __all__ unchanged
    assert set(iv8.__all__) == _STD_ALL
    # (watch_object_keys arrived in M10-2)
    for name in ("watch_reflect_get_own_property_descriptor",
                 "watch_function_to_string"):
        assert not hasattr(iv8.Page, name)


# --- not enabled -> no record ----------------------------------------------------

@on_only
def test_not_enabled_no_record():
    with iv8.Page() as page:
        assert page.eval("Reflect.ownKeys({x: 1}).length") == 1
        assert page.read_reflect_ownkeys_hits() == []
    with iv8.Page() as page:
        page.watch_reflect_ownkeys(enabled=False)
        assert page.eval("Reflect.ownKeys({x: 1}).length") == 1
        assert page.read_reflect_ownkeys_hits() == []


# --- enabled -> a Reflect.ownKeys call is recorded ------------------------------

@on_only
def test_records_reflect_ownkeys():
    with iv8.Page() as page:
        page.watch_reflect_ownkeys(enabled=True)
        assert page.eval("Reflect.ownKeys({x: 1, y: 2}).length") == 2  # delegation OK
        hits = page.read_reflect_ownkeys_hits()
        assert isinstance(hits, list) and len(hits) == 1
        assert hits[0]["type"] == "Reflect.ownKeys"
        assert "property" not in hits[0]  # ownKeys has no property key
        assert "resource_name" in hits[0]


# --- multiple hits keep insertion order -----------------------------------------

@on_only
def test_multiple_hits_stable_order():
    with iv8.Page() as page:
        page.watch_reflect_ownkeys(enabled=True)
        page.eval("Reflect.ownKeys({a: 1})", name="s1")
        page.eval("Reflect.ownKeys({b: 1})", name="s2")
        page.eval("Reflect.ownKeys({c: 1})", name="s3")
        hits = page.read_reflect_ownkeys_hits()
        assert len(hits) == 3
        assert [h["resource_name"] for h in hits] == ["s1", "s2", "s3"]


# --- read-and-clear --------------------------------------------------------------

@on_only
def test_read_clears():
    with iv8.Page() as page:
        page.watch_reflect_ownkeys(enabled=True)
        page.eval("Reflect.ownKeys({x: 1})")
        assert len(page.read_reflect_ownkeys_hits()) == 1
        assert page.read_reflect_ownkeys_hits() == []


# --- the four channels do not cross-talk ----------------------------------------

@on_only
def test_channels_independent():
    with iv8.Page() as page:
        page.watch_apis(["document.querySelector"])
        page.watch_reflect_get(enabled=True)
        page.watch_reflect_has(enabled=True)
        page.watch_reflect_ownkeys(enabled=True)
        page.load(
            html="<html><body><div></div><script>"
                 "document.querySelector('div');"
                 "Reflect.get({a: 1}, 'a');"
                 "Reflect.has({b: 1}, 'b');"
                 "Reflect.ownKeys({c: 1});"
                 "</script></body></html>",
            base_url=BASE,
        )
        assert len(page.read_watch_api_hits()) == 1
        assert len(page.read_reflect_get_hits()) == 1
        assert len(page.read_reflect_has_hits()) == 1
        assert len(page.read_reflect_ownkeys_hits()) == 1
        # reading one channel does not empty the others (checked by ordering above;
        # here confirm ownKeys alone records without touching the others)
        page.eval("Reflect.ownKeys({d: 1})")
        assert len(page.read_reflect_ownkeys_hits()) == 1
        assert page.read_reflect_get_hits() == []
        assert page.read_reflect_has_hits() == []


# --- break_on_hit=False only records (no pause) ---------------------------------

@on_only
def test_break_false_no_pause():
    with iv8.Page() as page:
        page.load(html="<html><body></body></html>", base_url=BASE)
        page.devtools_url()
        page._native.devtools_dispatch(
            json.dumps({"id": 1, "method": "Debugger.enable"}))
        page.watch_reflect_ownkeys(enabled=True, break_on_hit=False)
        page.eval("Reflect.ownKeys({x: 1})")
        assert page._native.devtools_pause_count() == 0  # recorded, not paused
        assert len(page.read_reflect_ownkeys_hits()) == 1


# --- break_on_hit=True + attached session -> observable pause -------------------

@on_only
def test_break_true_pauses_with_attached_session():
    with iv8.Page() as page:
        page.load(html="<html><body></body></html>", base_url=BASE)
        page.devtools_url()
        page._native.devtools_dispatch(
            json.dumps({"id": 1, "method": "Debugger.enable"}))
        page.watch_reflect_ownkeys(enabled=True, break_on_hit=True)
        assert page._native.devtools_pause_count() == 0
        page.eval("Reflect.ownKeys({x: 1})")
        assert page._native.devtools_pause_count() >= 1
        assert len(page.read_reflect_ownkeys_hits()) == 1


# --- break_on_hit=True + no session -> record only, no error --------------------

@on_only
def test_break_true_no_session_records_only():
    with iv8.Page() as page:
        page.watch_reflect_ownkeys(enabled=True, break_on_hit=True)
        assert page.eval("Reflect.ownKeys({x: 1}).length") == 1
        assert len(page.read_reflect_ownkeys_hits()) == 1


# --- config persists across load() ----------------------------------------------

@on_only
def test_config_persists_across_load():
    with iv8.Page() as page:
        page.watch_reflect_ownkeys(enabled=True)
        page.load(html="<html><body></body></html>", base_url=BASE)
        page.eval("Reflect.ownKeys({x: 1})")
        assert len(page.read_reflect_ownkeys_hits()) == 1
        page.load(html="<html><body></body></html>", base_url=BASE)
        page.eval("Reflect.ownKeys({y: 1})")
        assert len(page.read_reflect_ownkeys_hits()) == 1


# --- failed load does not roll back the recorded hit ----------------------------

@on_only
def test_failed_load_no_rollback():
    with iv8.Page() as page:
        page.watch_reflect_ownkeys(enabled=True)
        with pytest.raises(iv8.JSError):
            page.load(
                html="<html><body><script>"
                     "Reflect.ownKeys({x: 1}); throw new Error('boom');"
                     "</script></body></html>",
                base_url=BASE,
            )
        assert page.ready_state == "loading"
        # the pre-throw Reflect.ownKeys call stays recorded (no rollback)
        assert len(page.read_reflect_ownkeys_hits()) == 1


# --- no JS-visible monitoring global; Reflect.ownKeys stays normal --------------

@on_only
def test_no_js_global():
    with iv8.Page() as page:
        page.watch_reflect_ownkeys(enabled=True)
        for g in ("watchReflectOwnKeys", "reflectOwnKeysHits", "__reflectOwnKeys__"):
            assert page.eval(f"typeof globalThis.{g}") == "undefined"
        assert page.eval("typeof Reflect.ownKeys") == "function"


# --- dispose() follows the existing disposed error path -------------------------

@on_only
def test_dispose_error_path():
    page = iv8.Page()
    page.watch_reflect_ownkeys(enabled=True)
    page.dispose()
    with pytest.raises(iv8.JSContextDisposedError):
        page.watch_reflect_ownkeys(enabled=True)
    with pytest.raises(iv8.JSContextDisposedError):
        page.read_reflect_ownkeys_hits()


# --- bad argument types raise TypeError -----------------------------------------

@on_only
def test_bad_arg_types():
    with iv8.Page() as page:
        with pytest.raises(TypeError):
            page.watch_reflect_ownkeys(enabled="yes")
        with pytest.raises(TypeError):
            page.watch_reflect_ownkeys(enabled=True, break_on_hit="no")
