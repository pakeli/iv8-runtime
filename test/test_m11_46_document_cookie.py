"""M11-46: deterministic Page-level document.cookie jar."""

import json

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://document-cookie.test/"
HTML = "<html><body></body></html>"


def _load(page, html=HTML, base=BASE):
    page.load(html, base)


@on_only
def test_cookie_get_set_update_and_order():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("document.cookie") == ""
        assert page.eval(
            """
            document.cookie = 'a=1';
            document.cookie = 'b=2';
            document.cookie;
            """
        ) == "a=1; b=2"
        assert page.eval(
            """
            document.cookie = 'a=3';
            document.cookie;
            """
        ) == "a=3; b=2"
        assert page.eval(
            """
            document.cookie = 'c=4; Path=/; Secure; SameSite=Lax';
            document.cookie;
            """
        ) == "a=3; b=2; c=4"
        assert page.eval(
            """
            document.cookie = 'token=a=b';
            document.cookie;
            """
        ) == "a=3; b=2; c=4; token=a=b"


@on_only
def test_cookie_invalid_inputs_and_to_string_coercion():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            """
            document.cookie = '';
            document.cookie = 'noequals';
            document.cookie = '=empty-name';
            document.cookie = 'x=1';
            document.cookie = 'x=2';
            document.cookie = {toString() { return 'y=3'; }};
            document.cookie;
            """
        ) == "x=2; y=3"
        assert page.eval("document.cookie = 5; document.cookie") == "x=2; y=3"


@on_only
def test_cookie_deletion_with_max_age_and_epoch_expires():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            """
            document.cookie = 'a=1';
            document.cookie = 'b=2';
            document.cookie = 'a=; Max-Age=0';
            document.cookie;
            """
        ) == "b=2"
        assert page.eval(
            """
            document.cookie = 'c=3';
            document.cookie = 'c=; expires=Thu, 01 Jan 1970 00:00:00 GMT';
            document.cookie;
            """
        ) == "b=2"
        assert page.eval(
            """
            document.cookie = 'd=4; max-age=-1';
            document.cookie = 'e=5; expires=Thu, 01 Jan 2026 00:00:00 GMT';
            document.cookie = 'f=6; max-age=invalid';
            document.cookie;
            """
        ) == "b=2; e=5; f=6"


@on_only
def test_cookie_persists_across_load_and_is_page_isolated():
    with iv8.Page() as page:
        _load(page)
        page.eval("document.cookie = 'session=abc'")
        _load(page, "<html><body><p>new</p></body></html>",
              "https://other-cookie-origin.test/")
        assert page.eval("document.cookie") == "session=abc"
    with iv8.Page() as other:
        _load(other)
        assert other.eval("document.cookie") == ""


@on_only
def test_cookie_is_a_writable_traced_document_property():
    with iv8.Page() as page:
        _load(page)
        # Native data properties materialize their value into a data descriptor
        # on Object.getOwnPropertyDescriptor, which also invokes the getter.
        assert page.eval(
            """
            (() => {
              document.cookie = 'a=1';
              const d = Object.getOwnPropertyDescriptor(document, 'cookie');
              return [d.value, d.writable, d.enumerable,
                      d.configurable].join('|');
            })();
            """
        ) == "a=1|true|true|false"
        page.start_access_trace()
        page.eval(
            "document.cookie = 'a=1'; document.cookie;",
            name="cookie.js"
        )
        events = page.read_access_trace()["events"]
        assert [(event["object_type"], event["member"], event["operation"])
                for event in events] == [
            ("document", "cookie", "set"),
            ("document", "cookie", "get"),
        ]
        assert events[0]["outcome"] == "returned"
        assert events[1]["value_summary"] == "string:3:a=1"


@on_only
@pytest.mark.parametrize("expires", [
    "Thu, 01 Jan 1970 00:00:00 GMT",
    "Thu, 01-Jan-1970 00:00:00 GMT",
    "Thu Jan 01 00:00:00 1970",
])
def test_cookie_supported_expiry_date_forms(expires):
    with iv8.Page() as page:
        _load(page)
        page.eval("document.cookie = 'item=old'")
        page.eval("document.cookie = " + json.dumps("item=; expires=" + expires))
        assert page.eval("document.cookie") == ""


@on_only
def test_cookie_max_age_precedence_and_invalid_attributes():
    past = "Thu, 01 Jan 1970 00:00:00 GMT"
    future = "Thu, 01 Jan 2026 00:00:00 GMT"
    cases = [
        (f"item=new; expires={past}; max-age=10", "item=new"),
        (f"item=new; max-age=10; expires={past}", "item=new"),
        (f"item=new; max-age=invalid; expires={past}", ""),
        (f"item=new; max-age=0; expires={future}", ""),
        ("item=new; max-age=0; MAX-AGE=10", "item=new"),
        ("item=new; max-age=10; max-age=invalid", "item=new"),
        ("item=new; expires=invalid", "item=new"),
    ]
    with iv8.Page() as page:
        _load(page)
        for assignment, expected in cases:
            page.eval("document.cookie = 'item=old'")
            page.eval("document.cookie = " + json.dumps(assignment))
            assert page.eval("document.cookie") == expected, assignment


@on_only
def test_cookie_coercion_failure_preserves_jar_and_trace_exception():
    with iv8.Page() as page:
        _load(page)
        page.eval("document.cookie = 'session=kept'")
        page.start_access_trace()
        with pytest.raises(iv8.JSError) as info:
            page.eval("document.cookie = {toString() {"
                      " throw new RangeError('cookie coercion'); }}")
        assert info.value.name == "RangeError"
        assert info.value.message == "cookie coercion"
        events = page.read_access_trace()["events"]
        assert [(event["member"], event["operation"], event["outcome"])
                for event in events] == [("cookie", "set", "threw")]
        assert page.eval("document.cookie") == "session=kept"


@on_only
def test_cookie_delete_and_reinsert_appends_after_retained_entries():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("""
            document.cookie = ' a = 1 ; path=/';
            document.cookie = 'b=2';
            document.cookie = 'a=; max-age=0';
            document.cookie = 'a=3';
            document.cookie;
        """) == "b=2; a=3"
