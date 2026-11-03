"""M11-51: Window IDL event handler attributes (`window.onload`, ...)."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-51.test/"
EMPTY = "<html><body></body></html>"

HANDLER_NAMES = [
    "onload", "onunload", "onbeforeunload", "onerror", "onresize",
    "onscroll", "onfocus", "onblur", "onhashchange", "onpopstate",
    "onmessage", "onstorage", "ononline", "onoffline", "onpagehide",
    "onpageshow", "onunhandledrejection", "onrejectionhandled",
]


def test_no_new_python_surface():
    for attr in HANDLER_NAMES + ["onclick", "addEventListener"]:
        assert not hasattr(iv8.Page, attr)


@on_only
def test_handler_properties_start_as_null():
    with iv8.Page() as page:
        page.load(EMPTY, BASE)
        names = ",".join(repr(name) for name in HANDLER_NAMES)
        assert page.eval(
            f"[{names}].every(name => window[name] === null)"
        ) is True
        assert page.eval("typeof window.onload") == "object"
        assert page.eval("window.onload") is None


@on_only
def test_handler_assignment_and_non_callable_clearing():
    with iv8.Page() as page:
        page.load(EMPTY, BASE)
        assert page.eval(
            """
            (() => {
              const fn = () => {};
              const before = window.onload;
              window.onload = fn;
              const assigned = window.onload === fn;
              window.onload = 42;
              const clearedByNumber = window.onload;
              window.onload = fn;
              window.onload = null;
              const clearedByNull = window.onload;
              window.onload = fn;
              window.onload = {};
              const clearedByObject = window.onload;
              window.onload = fn;
              return [before === null, assigned, clearedByNumber === null,
                      clearedByNull === null, clearedByObject === null,
                      window.onload === fn].join('|');
            })();
            """
        ) == "true|true|true|true|true|true"
        assert page.eval(
            "(() => { 'use strict'; window.onload = 1;"
            " return window.onload === null; })()"
        ) is True


@on_only
def test_inline_onload_runs_after_regular_listeners():
    html = """<html><body><script>
      window.__order = [];
      window.addEventListener('load', function () {
        window.__order.push('listener');
      });
      window.onload = function (event) {
        window.__order.push('handler');
        window.__handler_seen = [this === window, event.type,
          event.target === window, event.currentTarget === window,
          event.eventPhase].join('|');
      };
    </script></body></html>"""
    with iv8.Page() as page:
        page.load(html, BASE)
        assert page.eval("__order.join(',')") == "listener,handler"
        assert page.eval("__handler_seen") == "true|load|true|true|2"
        assert page.eval("typeof window.onload") == "function"


@on_only
def test_onload_replacement_and_removal_before_dispatch():
    replaced = """<html><body><script>
      window.__hits = [];
      window.onload = function () { window.__hits.push('first'); };
      window.onload = function () { window.__hits.push('second'); };
    </script></body></html>"""
    removed = """<html><body><script>
      window.__hits = [];
      window.onload = function () { window.__hits.push('removed'); };
      window.onload = null;
    </script></body></html>"""
    with iv8.Page() as page:
        page.load(replaced, BASE)
        assert page.eval("__hits.join(',')") == "second"
    with iv8.Page() as page:
        page.load(removed, BASE)
        assert page.eval("__hits.join(',')") == ""


@on_only
def test_handler_runs_at_target_and_during_bubbling():
    html = """<html><body><div id='d'></div><script>
      window.__rows = [];
      window.onmessage = function (event) {
        window.__rows.push([event.type, event.eventPhase,
          event.target === window, event.currentTarget === window]
          .join(':'));
      };
      window.dispatchEvent(new Event('message'));
      document.getElementById('d').dispatchEvent(
        new Event('message', {bubbles: true}));
    </script></body></html>"""
    with iv8.Page() as page:
        page.load(html, BASE)
        assert page.eval("__rows.join('|')") == \
            "message:2:true:true|message:3:false:true"


@on_only
def test_false_return_cancels_only_cancelable_events():
    with iv8.Page() as page:
        page.load(EMPTY, BASE)
        assert page.eval(
            """
            (() => {
              window.onmessage = function () { return false; };
              const cancelable = new Event('message', {cancelable: true});
              const cancelableResult = window.dispatchEvent(cancelable);
              const plain = new Event('message');
              const plainResult = window.dispatchEvent(plain);
              return [cancelable.defaultPrevented, cancelableResult,
                      plain.defaultPrevented, plainResult].join('|');
            })();
            """
        ) == "true|false|false|true"
        page.eval("window.onmessage = null")
        assert page.eval(
            "(() => { const e = new Event('message', {cancelable: true});"
            " return [window.dispatchEvent(e), e.defaultPrevented].join('|'); })()"
        ) == "true|false"


@on_only
def test_handler_exceptions_are_swallowed():
    with iv8.Page() as page:
        page.load(EMPTY, BASE)
        assert page.eval(
            """
            (() => {
              window.onmessage = function () { throw new Error('boom'); };
              const event = new Event('message', {cancelable: true});
              return [window.dispatchEvent(event), event.defaultPrevented]
                  .join('|');
            })();
            """
        ) == "true|false"


@on_only
def test_stop_immediate_propagation_skips_handler_but_stop_propagation_does_not():
    with iv8.Page() as page:
        page.load(EMPTY, BASE)
        assert page.eval(
            """
            (() => {
              window.__r = [];
              window.addEventListener('message', function (e) {
                __r.push('listener');
                e.stopImmediatePropagation();
              });
              window.onmessage = function () { __r.push('handler'); };
              window.dispatchEvent(new Event('message'));
              return __r.join(',');
            })();
            """
        ) == "listener"
    with iv8.Page() as page:
        page.load(EMPTY, BASE)
        assert page.eval(
            """
            (() => {
              window.__r = [];
              window.addEventListener('message', function (e) {
                __r.push('listener');
                e.stopPropagation();
              });
              window.onmessage = function () { __r.push('handler'); };
              window.dispatchEvent(new Event('message'));
              return __r.join(',');
            })();
            """
        ) == "listener,handler"


@on_only
def test_handlers_reset_between_generations():
    html = """<html><body><script>
      window.__hits = [];
      window.onload = function () { window.__hits.push('load'); };
    </script></body></html>"""
    with iv8.Page() as page:
        page.load(html, BASE)
        assert page.eval("__hits.join(',')") == "load"
        assert page.eval("typeof window.onload") == "function"
        page.load(EMPTY, BASE)
        assert page.eval("window.onload === null") is True


@on_only
def test_unlisted_onclick_stays_an_ordinary_property():
    with iv8.Page() as page:
        page.load(EMPTY, BASE)
        assert page.eval(
            """
            (() => {
              window.__hits = 0;
              window.onclick = function () { __hits += 1; };
              window.dispatchEvent(new Event('click'));
              return [typeof window.onclick, __hits].join('|');
            })();
            """
        ) == "function|0"
