"""M9 contract tests — high-level, cross-cutting acceptance for M9-1 … M9-5.

A "collar" suite: it does NOT repeat the per-phase detail tests. It checks the
consolidated M9 second-layer boundary — the approved DevTools attach base + host
method / reflection monitoring surfaces exist, no out-of-scope surface leaked (Python
+ JS), the frozen second-layer items stay absent (auto-breakpoints, vconsole,
wrapNative, iframe/Worker/CDP-domain/trusted-input/profile), the three monitoring
channels cooperate over a live page without changing DOM/script behavior, and the
inherited repeated/failed/dispose semantics did not regress. See docs/m9_summary.md
for the authoritative boundary.
"""

import json

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m9.test/"

# The complete, approved set of M9 public Page methods (second layer).
_M9_PAGE_API = (
    "devtools_url",
    "watch_apis", "read_watch_api_hits",
    "watch_reflect_get", "read_reflect_get_hits",
    "watch_reflect_has", "read_reflect_has_hits",
)


# --- (1) Python / top-level surface is not exceeded (both build modes) ----------

def test_python_top_level_frozen():
    assert set(iv8.__all__) == {
        "__version__", "_v8_version", "_v8_commit", "_v8_linked",
        "_v8_runtime_version", "JSContext", "JSContextDisposedError",
        "JSContextBusyError", "JSConversionError", "JSError", "JSUndefined",
        "JSValue", "Page",
    }
    # M9 added no top-level object / exception.
    for name in ("DevTools", "Inspector", "WatchRegistry", "CDP"):
        assert not hasattr(iv8, name)


def test_jserror_fields_frozen():
    err = iv8.JSError("n", "m", "s", "r", 1, 2)
    assert set(vars(err)) == {
        "name", "message", "stack", "resource_name", "line", "column",
    }


# --- (2) M9 positive capabilities exist -----------------------------------------

def test_m9_capabilities_present():
    for attr in _M9_PAGE_API:
        assert callable(getattr(iv8.Page, attr, None)), attr


# --- (3) key frozen (second-layer) items still absent ---------------------------

def test_frozen_page_api_absent():
    # no auto-breakpoint / pause-platform / anti-debug / iframe-worker / CDP-domain /
    # trusted-input / geometry / profile Python API leaked onto Page.
    for attr in (
        "clear_watch_apis", "pause", "resume",
        "vconsole", "vdebugger", "wrap_native", "wrapNative",
        "set_debugger", "disable_debugger",
        "watch_iframe", "watch_worker", "attach_worker",
        "add_cdp_domain", "cdp_domain",
        "watch_reflect_own_keys", "watch_object_keys", "watch_function_to_string",
        "trusted_input", "geometry", "profile", "fingerprint",
    ):
        assert not hasattr(iv8.Page, attr)


# --- (4) mixed-page cooperation of all three channels over a live tree ----------

@on_only
def test_mixed_page_cooperation():
    with iv8.Page() as page:
        # a stable devtools endpoint ...
        url = page.devtools_url()
        assert url.startswith("ws://127.0.0.1:") and "/devtools/page/" in url
        # ... plus all three monitoring channels active at once
        page.watch_apis(["document.querySelector"])
        page.watch_reflect_get(enabled=True)
        page.watch_reflect_has(enabled=True)
        page.load(
            html="<html><body><div id='d'>hi</div><script>"
                 "document.querySelector('div');"
                 "Reflect.get({a: 1}, 'a');"
                 "Reflect.has({b: 1}, 'b');"
                 "</script></body></html>",
            base_url=BASE,
        )
        # each channel recorded its own hit, independently
        api = page.read_watch_api_hits()
        get = page.read_reflect_get_hits()
        has = page.read_reflect_has_hits()
        assert [h["path"] for h in api] == ["document.querySelector"]
        assert [h["property"] for h in get] == ["a"]
        assert [h["property"] for h in has] == ["b"]
        # the DOM / script / web-platform behavior is unchanged
        assert page.eval("document.getElementById('d').textContent") == "hi"
        assert page.eval("btoa('abc')") == "YWJj"
        assert page.eval("new URL('https://x.test/p').pathname") == "/p"
        # devtools URL is still stable after all of that
        assert page.devtools_url() == url


# --- (5) break_on_hit configs coexist; pause only with an attached session ------

@on_only
def test_break_on_hit_configs_coexist():
    with iv8.Page() as page:
        page.load(html="<html><body><div id='d'></div></body></html>", base_url=BASE)
        page.devtools_url()
        page._native.devtools_dispatch(
            json.dumps({"id": 1, "method": "Debugger.enable"}))
        page.watch_apis(["document.querySelector"], break_on_hit=True)
        page.watch_reflect_get(enabled=True, break_on_hit=True)
        page.watch_reflect_has(enabled=True, break_on_hit=True)
        assert page._native.devtools_pause_count() == 0
        # each watched entry point pauses the attached Inspector
        page.eval("document.querySelector('div'); Reflect.get({a: 1}, 'a');"
                  " Reflect.has({b: 1}, 'b');")
        assert page._native.devtools_pause_count() >= 1
        # and all three logs recorded
        assert len(page.read_watch_api_hits()) >= 1
        assert len(page.read_reflect_get_hits()) == 1
        assert len(page.read_reflect_has_hits()) == 1


# --- (6) repeated / failed / disposed load contract (unchanged by M9) -----------

@on_only
def test_repeated_failed_dispose_contract():
    with iv8.Page() as page:
        page.devtools_url()
        page.watch_apis(["document.querySelector"])
        page.watch_reflect_get(enabled=True)
        page.watch_reflect_has(enabled=True)
        url = page.devtools_url()

        # repeated load: the endpoint URL is stable AND monitoring config persists
        page.load(
            html="<html><body><div></div><script>"
                 "document.querySelector('div'); Reflect.get({a:1},'a');"
                 " Reflect.has({b:1},'b');</script></body></html>",
            base_url=BASE,
        )
        assert page.devtools_url() == url
        assert len(page.read_watch_api_hits()) == 1
        assert len(page.read_reflect_get_hits()) == 1
        assert len(page.read_reflect_has_hits()) == 1

        # failed load: no rollback; monitoring config survives and still records
        with pytest.raises(iv8.JSError):
            page.load(
                html="<html><body><div></div><script>document.querySelector('div');"
                     "throw new Error('boom');</script></body></html>",
                base_url=BASE)
        assert page.ready_state == "loading"
        assert page.devtools_url() == url
        assert len(page.read_watch_api_hits()) == 1  # the pre-throw call was recorded

    # dispose: every M9 read/config method reuses the disposed error path
    p = iv8.Page()
    p.devtools_url()
    p.dispose()
    for call in (
        lambda: p.devtools_url(),
        lambda: p.watch_apis(["document.querySelector"]),
        lambda: p.read_watch_api_hits(),
        lambda: p.watch_reflect_get(enabled=True),
        lambda: p.read_reflect_get_hits(),
        lambda: p.watch_reflect_has(enabled=True),
        lambda: p.read_reflect_has_hits(),
    ):
        with pytest.raises(iv8.JSContextDisposedError):
            call()


# --- (7) no JS-visible watch/monitor global; no second-layer leak into JS -------

@on_only
def test_no_js_monitoring_global():
    with iv8.Page() as page:
        page.watch_apis(["document.querySelector"])
        page.watch_reflect_get(enabled=True)
        page.watch_reflect_has(enabled=True)
        page.load(html="<html><body></body></html>", base_url=BASE)
        for g in ("watchApis", "watchReflectGet", "watchReflectHas", "vconsole",
                  "vdebugger", "wrapNative", "__cdp__", "TrustedTypes",
                  "trustedTypes", "Worker"):
            assert page.eval(f"typeof globalThis.{g}") == "undefined"
        # Reflect stays a normal, callable object even while its entries are watched
        assert page.eval("typeof Reflect.get") == "function"
        assert page.eval("typeof Reflect.has") == "function"
        # debugger; is unchanged (inert with no attached debugger)
        assert page.eval("debugger; 40 + 2") == 42
