"""M11-22 acceptance tests: select length / item / namedItem."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-22.test/"
DOC = """
<html><body>
<select id='s'>
  <option id='o0' name='zero'>0</option>
  <option id='o1' name='one'>1</option>
  <option id='o2'>2</option>
</select>
<select id='e'></select>
<div id='dv'></div>
</body></html>
"""


def _setup(page):
    page.load(DOC, BASE)
    page.eval("""
        globalThis.s = document.getElementById('s');
        globalThis.e = document.getElementById('e');
        globalThis.dv = document.getElementById('dv');
        globalThis.o2 = document.getElementById('o2');
    """)


@on_only
def test_length_and_live_updates():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("s.length") == 3
        assert page.eval("e.length") == 0
        assert page.eval("""
            (() => {
                s.removeChild(o2);
                const after = s.length;
                s.appendChild(o2);
                return [after, s.length].join('|');
            })()
        """) == "2|3"


@on_only
def test_item():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("s.item(0).id") == "o0"
        assert page.eval("s.item(1).id") == "o1"
        assert page.eval("s.item(2).id") == "o2"
        assert page.eval("s.item().id") == "o0"
        assert page.eval("s.item('1').id") == "o1"
        assert page.eval("s.item(-1) === null") is True
        assert page.eval("s.item(3) === null") is True
        assert page.eval("e.item(0) === null") is True


@on_only
def test_named_item():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("s.namedItem('o1').id") == "o1"
        assert page.eval("s.namedItem('one').id") == "o1"
        assert page.eval("s.namedItem('missing') === null") is True
        assert page.eval("s.namedItem('') === null") is True
        assert page.eval("e.namedItem('x') === null") is True


@on_only
def test_length_is_read_only():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("(s.length = 99, s.length)") == 3


@on_only
def test_detached_select():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (() => {
                const sel = document.createElement('select');
                const a = document.createElement('option');
                a.id = 'a';
                a.setAttribute('name', 'aa');
                const b = document.createElement('option');
                b.id = 'b';
                sel.appendChild(a);
                sel.appendChild(b);
                return [sel.length, sel.item(1).id, sel.namedItem('aa').id]
                    .join('|');
            })()
        """) == "2|b|a"


@on_only
def test_shape():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("typeof s.item") == "function"
        assert page.eval("typeof s.namedItem") == "function"
        assert page.eval("typeof dv.item") == "undefined"
        assert page.eval("typeof dv.namedItem") == "undefined"
        assert page.eval("Array.isArray(s.options)") is True
        assert page.eval("typeof s.options.item") == "undefined"
        assert not hasattr(iv8.Page, "item")
