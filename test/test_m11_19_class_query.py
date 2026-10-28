"""M11-19 acceptance tests: multi-token class queries."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-19.test/"
DOC = """
<html><body>
<div id='root' class='a b'>
  <span id='ab' class='a b'></span>
  <span id='a' class='a'></span>
  <span id='b' class='b c'></span>
  <div id='nested'><span id='abc' class='a b c'></span></div>
</div>
<div id='other' class='a b'></div>
<span id='upper' class='A B'></span>
</body></html>
"""


def _setup(page):
    page.load(DOC, BASE)
    page.eval("""
        globalThis.root = document.getElementById('root');
        globalThis.nested = document.getElementById('nested');
        globalThis.other = document.getElementById('other');
        globalThis.a = document.getElementById('a');
    """)


@on_only
def test_document_multi_token_intersection():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval(
            "document.getElementsByClassName('a b').map(e => e.id).join(',')"
        ) == "root,ab,abc,other"
        # Token order does not matter.
        assert page.eval(
            "document.getElementsByClassName('b a').map(e => e.id).join(',')"
        ) == "root,ab,abc,other"
        assert page.eval(
            "document.getElementsByClassName('a').map(e => e.id).join(',')"
        ) == "root,ab,a,abc,other"


@on_only
def test_document_empty_and_case_sensitive():
    with iv8.Page() as page:
        _setup(page)
        for query in ("", "   ", "a z", "missing"):
            assert page.eval(
                f"document.getElementsByClassName({query!r}).length") == 0
        assert page.eval(
            "document.getElementsByClassName('A B').map(e => e.id).join(',')"
        ) == "upper"


@on_only
def test_element_query_is_descendant_only():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval(
            "root.getElementsByClassName('a b').map(e => e.id).join(',')"
        ) == "ab,abc"
        assert page.eval(
            "nested.getElementsByClassName('a b').map(e => e.id).join(',')"
        ) == "abc"
        # The element itself is not part of its descendant query.
        assert page.eval(
            "root.getElementsByClassName('a').map(e => e.id).join(',')"
        ) == "ab,a,abc"
        assert page.eval(
            "document.createElement('div').getElementsByClassName('a').length"
        ) == 0


@on_only
def test_class_query_is_live_and_excludes_detached():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("""
            (() => {
                other.setAttribute('class', 'x');
                a.classList.add('b');
                const detached = document.createElement('span');
                detached.className = 'a b';
                return [document.getElementsByClassName('a b')
                            .map(e => e.id).join(','),
                        root.getElementsByClassName('a b')
                            .map(e => e.id).join(','),
                        detached.getElementsByClassName('a b').length].join('|');
            })()
        """) == "root,ab,a,abc|ab,a,abc|0"


@on_only
def test_class_query_shape():
    with iv8.Page() as page:
        _setup(page)
        assert page.eval("Array.isArray(document.getElementsByClassName('a'))") is True
        assert page.eval("Array.isArray(root.getElementsByClassName('a'))") is True
        assert page.eval(
            "typeof document.getElementsByClassName('a').item") == "undefined"
        assert page.eval(
            "typeof root.getElementsByClassName") == "function"
        assert not hasattr(iv8.Page, "getElementsByClassName")
