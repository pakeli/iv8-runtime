"""M11-47: writable document.title and Document identity aliases."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-47.test/path?q=1"
HTML = "<html><head><title>Old</title></head><body></body></html>"


def _load(page, html=HTML, base=BASE):
    page.load(html, base)


@on_only
def test_title_is_writable_and_reflects_into_the_title_element():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("document.title") == "Old"
        assert page.eval(
            """
            (() => {
              document.title = 'New';
              const a = [document.title,
                         document.querySelector('title').textContent].join('|');
              document.title = 42;
              const b = [document.title,
                         document.querySelector('title').textContent].join('|');
              document.title = {toString() { return 'Obj'; }};
              return a + '/' + b + '/' + document.title;
            })();
            """
        ) == "New|New/42|42/Obj"


@on_only
def test_title_without_a_title_element_and_conversion_errors():
    with iv8.Page() as page:
        _load(page, "<html><body></body></html>")
        assert page.eval("document.title") == ""
        assert page.eval(
            """
            document.title = 'x';
            [document.title,
             String(document.querySelector('title'))].join('|');
            """
        ) == "x|null"

    with iv8.Page() as page:
        _load(page)
        with pytest.raises(iv8.JSError):
            page.eval(
                "document.title = {toString() { throw new Error('bad'); }};"
            )
        assert page.eval("document.title") == "Old"


@on_only
def test_title_resets_on_load_and_is_page_isolated():
    with iv8.Page() as page:
        _load(page)
        page.eval("document.title = 'Override'")
        assert page.eval("document.title") == "Override"
        _load(page, HTML)
        assert page.eval("document.title") == "Old"
    with iv8.Page() as other:
        _load(other, "<html><head><title>Other</title></head></html>")
        assert other.eval("document.title") == "Other"


@on_only
def test_url_aliases_are_read_only_and_track_the_generation():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            "[document.URL, document.documentURI, document.baseURI].join('|')"
        ) == f"{BASE}|{BASE}|{BASE}"
        for member in ("documentURI", "baseURI"):
            with pytest.raises(iv8.JSError):
                page.eval(f"'use strict'; document.{member} = 'changed';")
            assert page.eval(f"document.{member}") == BASE
        _load(page, HTML, "https://other.test/x")
        assert page.eval(
            "[document.documentURI, document.baseURI].join('|')"
        ) == "https://other.test/x|https://other.test/x"


@on_only
def test_default_view_and_location_identity():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            """
            [document.defaultView === window,
             document.defaultView === globalThis,
             document.defaultView.document === document,
             document.location === location,
             document.location === document.location,
             document.location.href === document.URL,
             document.location.pathname].join('|');
            """
        ) == "true|true|true|true|true|true|/path"
        for member in ("defaultView", "location"):
            with pytest.raises(iv8.JSError):
                page.eval(f"'use strict'; document.{member} = 1;")


@on_only
def test_identity_descriptors_and_access_trace():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            """
            (() => {
              document.title = 'T';
              const t = Object.getOwnPropertyDescriptor(document, 'title');
              const u = Object.getOwnPropertyDescriptor(document, 'documentURI');
              return [t.value, t.writable, t.enumerable, t.configurable,
                      u.value, u.writable, u.enumerable, u.configurable].join('|');
            })();
            """
        ) == f"T|true|true|false|{BASE}|false|true|false"

        page.start_access_trace()
        page.eval(
            """
            document.title;
            document.title = 'X';
            document.documentURI;
            document.baseURI;
            document.defaultView;
            document.location;
            0;
            """,
            name="identity.js",
        )
        events = page.read_access_trace()["events"]
        assert [(event["object_type"], event["member"], event["operation"])
                for event in events] == [
            ("document", "title", "get"),
            ("document", "title", "set"),
            ("document", "documentURI", "get"),
            ("document", "baseURI", "get"),
            ("document", "defaultView", "get"),
            ("document", "location", "get"),
        ]
        assert events[0]["value_summary"] == "string:1:T"
        assert events[1]["value_summary"] == "string:1:X"
        assert events[1]["outcome"] == "returned"
