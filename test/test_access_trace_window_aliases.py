"""Step 5i: access trace for window.parent/top/frames/opener."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://access-window-aliases.test/"
HTML = "<html><body></body></html>"


def _load(page):
    page.load(HTML, BASE)


def _events(page):
    return page.read_access_trace(clear=False)["events"]


@on_only
def test_window_alias_defaults_are_traced():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        assert page.eval(
            """
            [window.parent === window, window.top === window,
             window.frames === window, window.opener === null].join('|');
            """,
            name="probe.js"
        ) == "true|true|true|true"
        events = _events(page)
        assert [(event["object_type"], event["label"], event["member"],
                 event["operation"]) for event in events] == [
            ("window", "window", "window", "get"),
            ("window", "window", "parent", "get"),
            ("window", "window", "window", "get"),
            ("window", "window", "window", "get"),
            ("window", "window", "top", "get"),
            ("window", "window", "window", "get"),
            ("window", "window", "window", "get"),
            ("window", "window", "frames", "get"),
            ("window", "window", "window", "get"),
            ("window", "window", "window", "get"),
            ("window", "window", "opener", "get"),
        ]
        assert all(event["source_kind"] == "eval" for event in events)
        assert all(event["resource_name"] == "probe.js" for event in events)
        assert len({event["object_id"] for event in events}) == 1


@on_only
def test_window_aliases_keep_replaceable_and_read_only_behavior():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        assert page.eval(
            """
            var child = {};
            window.parent = child;
            window.frames = child;
            window.opener = child;
            window.top = child;
            [window.parent === child, window.frames === child,
             window.opener === child, window.top === window].join('|');
            """,
            name="probe.js"
        ) == "true|true|true|true"
        assert [(event["member"], event["operation"])
                for event in _events(page)] == [
            ("window", "get"), ("parent", "set"),
            ("window", "get"), ("frames", "set"),
            ("window", "get"), ("opener", "set"),
            ("window", "get"),
            ("window", "get"), ("parent", "get"),
            ("window", "get"), ("frames", "get"),
            ("window", "get"), ("opener", "get"),
            ("window", "get"), ("top", "get"), ("window", "get"),
        ]

        page.start_access_trace()
        with pytest.raises(iv8.JSError):
            page.eval("'use strict'; window.top = {};", name="probe.js")
        assert [(event["member"], event["operation"]) for event in _events(page)] == [("window", "get")]


@on_only
def test_window_alias_descriptor_shape():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        assert page.eval(
            """
            (() => {
              const parent = Object.getOwnPropertyDescriptor(
                  window, 'parent');
              const top = Object.getOwnPropertyDescriptor(window, 'top');
              return [
                parent.value === window, parent.writable, parent.enumerable,
                parent.configurable, top.value === window, top.writable,
                top.enumerable, top.configurable
              ].join('|');
            })();
            """,
            name="shape.js"
        ) == "true|true|true|true|true|false|true|false"
        assert [(event["member"], event["operation"])
                for event in _events(page)] == [
            ("window", "get"), ("parent", "get"), ("window", "get"), ("top", "get"),
            ("window", "get"), ("window", "get")]


@on_only
def test_window_alias_filter_and_generation():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace(object_types=["window"])
        page.eval("window.parent = null; window.parent;", name="probe.js")
        events = _events(page)
        assert [(event["member"], event["operation"]) for event in events] == [
            ("window", "get"), ("parent", "set"), ("window", "get"), ("parent", "get")]
        first_id = events[1]["object_id"]
        assert events[0]["generation"] == 2

        page.load(HTML, BASE)
        assert page.eval(
            "window.parent === window; window.opener === null;",
            name="probe.js"
        ) is True
        events = [
            event for event in _events(page)
            if event["source_kind"] == "eval"
        ]
        assert [(event["generation"], event["member"], event["operation"])
                for event in events] == [
            (2, "window", "get"),
            (2, "parent", "set"),
            (2, "window", "get"),
            (2, "parent", "get"),
            (3, "window", "get"),
            (3, "parent", "get"),
            (3, "window", "get"),
            (3, "window", "get"),
            (3, "opener", "get"),
        ]
        assert {event["object_id"] for event in events[:4]} == {first_id}
        assert len({event["object_id"] for event in events[4:]}) == 1
        assert events[4]["object_id"] != first_id
