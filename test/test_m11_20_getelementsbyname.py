"""M11-20 acceptance tests: document.getElementsByName."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-20.test/"
DOC = """
<html><body>
<input id='i1' name='q'>
<div id='d1' name='q'></div>
<input id='i2' name='Q'>
<input id='i3' name=''>
<a id='a1' name='q'></a>
<form id='f1' name='q'></form>
<span id='s1' name='q'></span>
<div id='q' data-marker='id-only'></div>
</body></html>
"""


@on_only
def test_matches_exact_name_across_tags():
    with iv8.Page() as page:
        page.load(DOC, BASE)
        assert page.eval(
            "document.getElementsByName('q').map(e => e.id).join(',')"
        ) == "i1,d1,a1,f1,s1"
        assert page.eval(
            "document.getElementsByName('Q').map(e => e.id).join(',')"
        ) == "i2"
        assert page.eval(
            "document.getElementsByName('').map(e => e.id).join(',')"
        ) == "i3"


@on_only
def test_no_id_fallback_and_live_tree():
    with iv8.Page() as page:
        page.load(DOC, BASE)
        assert page.eval(
            "document.getElementsByName('q').some("
            "e => e.getAttribute('data-marker') === 'id-only')") is False
        assert page.eval("""
            (() => {
                const target = document.getElementById('d1');
                target.setAttribute('name', 'z');
                const afterRename = document.getElementsByName('q')
                    .map(e => e.id).join(',');
                const extra = document.createElement('span');
                extra.id = 'extra';
                extra.setAttribute('name', 'q');
                document.body.appendChild(extra);
                return [afterRename,
                        document.getElementsByName('q')
                            .map(e => e.id).join(',')].join('|');
            })()
        """) == "i1,a1,f1,s1|i1,a1,f1,s1,extra"


@on_only
def test_detached_and_empty_result():
    with iv8.Page() as page:
        page.load(DOC, BASE)
        assert page.eval("""
            (() => {
                const detached = document.createElement('span');
                detached.setAttribute('name', 'q');
                return [document.getElementsByName('missing').length,
                        document.getElementsByName('q')
                            .includes(detached)].join('|');
            })()
        """) == "0|false"


@on_only
def test_shape():
    with iv8.Page() as page:
        page.load(DOC, BASE)
        assert page.eval("Array.isArray(document.getElementsByName('q'))") is True
        assert page.eval(
            "typeof document.getElementsByName('q').item") == "undefined"
        assert not hasattr(iv8.Page, "getElementsByName")
