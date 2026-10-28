"""M11-23 acceptance tests: deterministic document metadata."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://meta.test:8443/path"
DOC = "<html><head><title>T</title></head><body><div id='d'></div></body></html>"


@on_only
def test_metadata_values():
    with iv8.Page() as page:
        page.load(DOC, BASE)
        assert page.eval("document.characterSet") == "UTF-8"
        assert page.eval("document.contentType") == "text/html"
        assert page.eval("document.compatMode") == "CSS1Compat"
        assert page.eval("document.referrer") == ""
        assert page.eval("document.domain") == "meta.test"
        assert page.eval("document.visibilityState") == "visible"
        assert page.eval("document.hidden") is False
        assert page.eval("document.hasFocus()") is False


@on_only
def test_active_element_is_current_body():
    with iv8.Page() as page:
        page.load(DOC, BASE)
        assert page.eval("document.activeElement.tagName") == "BODY"
        page.load("<html><body><span id='x'></span></body></html>",
                  "https://other.test/")
        assert page.eval("document.activeElement.tagName") == "BODY"
        assert page.eval("document.domain") == "other.test"


@on_only
def test_metadata_is_read_only():
    with iv8.Page() as page:
        page.load(DOC, BASE)
        assert page.eval("""
            (() => {
                document.characterSet = 'x';
                document.contentType = 'x';
                document.compatMode = 'x';
                document.referrer = 'x';
                document.domain = 'x';
                document.visibilityState = 'x';
                document.hidden = true;
                document.activeElement = null;
                return [document.characterSet, document.contentType,
                        document.compatMode, document.referrer,
                        document.domain, document.visibilityState,
                        document.hidden,
                        document.activeElement.tagName].join('|');
            })()
        """) == "UTF-8|text/html|CSS1Compat||meta.test|visible|false|BODY"


@on_only
def test_domain_without_host():
    with iv8.Page() as page:
        page.load(DOC, "not-an-absolute-url")
        assert page.eval("document.domain") == ""


def test_no_new_python_surface():
    for attr in ("characterSet", "contentType", "compatMode", "referrer",
                 "domain", "visibilityState", "hidden", "activeElement",
                 "hasFocus"):
        assert not hasattr(iv8.Page, attr)
