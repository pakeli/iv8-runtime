"""M11-7 acceptance tests: per-Page localStorage / sessionStorage.

The two stores are plain in-memory key/value maps owned by PageState; they
survive load() generations of one Page and never touch disk, origins, events,
or the Python API.
"""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://storage.test/"
DOC = "<html><body><div id='root'></div></body></html>"


def _setup(page):
    page.load(DOC, BASE)


def test_no_new_python_surface():
    for name in ("localStorage", "sessionStorage", "Storage"):
        assert not hasattr(iv8, name)
    for name in ("localStorage", "sessionStorage", "storage"):
        assert not hasattr(iv8.Page, name)


@on_only
def test_global_shape():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("typeof localStorage") == "object"
        assert page.eval("typeof sessionStorage") == "object"
        assert page.eval("localStorage !== sessionStorage") is True
        assert page.eval("window.localStorage === localStorage") is True
        assert page.eval("window.sessionStorage === sessionStorage") is True
        for name in ("getItem", "setItem", "removeItem", "clear", "key"):
            assert page.eval(f"typeof localStorage.{name}") == "function"
            assert page.eval(f"typeof sessionStorage.{name}") == "function"
        assert page.eval("typeof localStorage.length") == "number"
        assert page.eval("typeof localStorage.addEventListener") == "undefined"


@on_only
def test_defaults():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("localStorage.getItem('missing')") is None
        assert page.eval("localStorage.length") == 0
        assert page.eval("localStorage.key(0)") is None
        assert page.eval("sessionStorage.getItem('missing')") is None
        assert page.eval("sessionStorage.length") == 0


@on_only
def test_string_coercion():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            localStorage.setItem(1, 2);
            localStorage.setItem('null', null);
            localStorage.setItem('undef', undefined);
            localStorage.setItem('bool', true);
        """)
        assert page.eval("localStorage.getItem('1')") == "2"
        assert page.eval("localStorage.getItem(1)") == "2"
        assert page.eval("localStorage.getItem('null')") == "null"
        assert page.eval("localStorage.getItem('undef')") == "undefined"
        assert page.eval("localStorage.getItem('bool')") == "true"
        assert page.eval("localStorage.length") == 4


@on_only
def test_overwrite_keeps_insertion_order():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            localStorage.setItem('a', '1');
            localStorage.setItem('b', '2');
            localStorage.setItem('a', '3');
        """)
        assert page.eval("localStorage.getItem('a')") == "3"
        assert page.eval("localStorage.length") == 2
        assert page.eval("localStorage.key(0)") == "a"
        assert page.eval("localStorage.key(1)") == "b"


@on_only
def test_remove_item():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            localStorage.setItem('a', '1');
            localStorage.setItem('b', '2');
            localStorage.removeItem('a');
            localStorage.removeItem('missing');
        """)
        assert page.eval("localStorage.length") == 1
        assert page.eval("localStorage.getItem('a') === null") is True
        assert page.eval("localStorage.key(0)") == "b"


@on_only
def test_clear():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            localStorage.setItem('a', '1');
            localStorage.setItem('b', '2');
            localStorage.clear();
        """)
        assert page.eval("localStorage.length") == 0
        assert page.eval("localStorage.getItem('a') === null") is True
        assert page.eval("localStorage.key(0) === null") is True


@on_only
def test_key_order_and_bounds():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            localStorage.setItem('x', '1');
            localStorage.setItem('y', '2');
            localStorage.setItem('z', '3');
        """)
        assert page.eval("localStorage.key(0)") == "x"
        assert page.eval("localStorage.key(1)") == "y"
        assert page.eval("localStorage.key(2)") == "z"
        assert page.eval("localStorage.key(3) === null") is True
        assert page.eval("localStorage.key(-1) === null") is True
        assert page.eval("localStorage.key()") == "x"


@on_only
def test_length_is_read_only():
    with iv8.Page() as page:
        _setup(page)
        page.eval("localStorage.setItem('a', '1');")
        assert page.eval("""
            (function () {
                localStorage.length = 99;
                return localStorage.length;
            })()
        """) == 1
        assert page.eval("""
            (function () {
                'use strict';
                try { localStorage.length = 99; return 'no'; }
                catch (err) { return err.name; }
            })()
        """) == "TypeError"


@on_only
def test_stores_are_independent():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            localStorage.setItem('a', 'local');
            sessionStorage.setItem('a', 'session');
            sessionStorage.setItem('b', 'session-only');
        """)
        assert page.eval("localStorage.getItem('a')") == "local"
        assert page.eval("sessionStorage.getItem('a')") == "session"
        assert page.eval("localStorage.getItem('b') === null") is True
        assert page.eval("localStorage.length") == 1
        assert page.eval("sessionStorage.length") == 2


@on_only
def test_storage_survives_load():
    with iv8.Page() as page:
        _setup(page)
        page.eval("""
            localStorage.setItem('a', 'local');
            sessionStorage.setItem('b', 'session');
        """)
        page.load(DOC, BASE)
        assert page.eval("localStorage.getItem('a')") == "local"
        assert page.eval("sessionStorage.getItem('b')") == "session"
        page.load("<html><body></body></html>", "https://other.test/x")
        assert page.eval("localStorage.getItem('a')") == "local"
        assert page.eval("sessionStorage.length") == 1


@on_only
def test_storage_globals_are_read_only():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                var original = localStorage;
                localStorage = 5;
                return localStorage === original;
            })()
        """) is True
        assert page.eval("""
            (function () {
                'use strict';
                try { localStorage = 5; return 'no'; }
                catch (err) { return err.name; }
            })()
        """) == "TypeError"


@on_only
def test_detached_method_uses_the_same_store():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (function () {
                localStorage.setItem('a', '1');
                var get = localStorage.getItem;
                var set = localStorage.setItem;
                set('b', '2');
                return [get('a'), get('b'), localStorage.length].join('|');
            })()
        """) == "1|2|2"
