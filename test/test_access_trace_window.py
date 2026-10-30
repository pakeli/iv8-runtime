"""Step 5b: access trace for intrinsic window metadata accessors."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://access-window.test/"
HTML = "<html><body></body></html>"
ENV = {
    "innerWidth": 800,
    "innerHeight": 600,
    "outerWidth": 900,
    "outerHeight": 700,
    "screenX": 3,
    "screenY": 4,
    "devicePixelRatio": 2.0,
}


def _load(page):
    page.load(HTML, BASE)


@on_only
def test_window_metadata_reads_are_traced():
    with iv8.Page(environment=ENV) as page:
        _load(page)
        page.start_access_trace()
        assert page.eval(
            """
            [innerWidth, window.innerHeight, outerWidth, devicePixelRatio,
             screenX, screenLeft, screenTop, length, isSecureContext,
             window.origin, crossOriginIsolated, closed,
             String(frameElement)].join('|')
            """,
            name="probe.js"
        ) == ("800|600|900|2|3|3|4|0|true|https://access-window.test|"
              "false|false|null")
        events = page.read_access_trace()["events"]
        names = [event["member"] for event in events]
        assert names == [
            "innerWidth", "innerHeight", "outerWidth", "devicePixelRatio",
            "screenX", "screenLeft", "screenTop", "length",
            "isSecureContext", "origin", "crossOriginIsolated", "closed",
            "frameElement",
        ]
        assert all(event["object_type"] == "window" for event in events)
        assert all(event["label"] == "window" for event in events)
        assert all(event["operation"] == "get" for event in events)
        assert all(event["source_kind"] == "eval" for event in events)
        assert all(event["resource_name"] == "probe.js" for event in events)
        assert len({event["object_id"] for event in events}) == 1


@on_only
def test_window_name_get_and_set_are_traced():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        assert page.eval("window.name = 'hello'; window.name;",
                         name="probe.js") == "hello"
        events = page.read_access_trace()["events"]
        assert [(event["member"], event["operation"]) for event in events] == [
            ("name", "set"), ("name", "get")]


@on_only
def test_window_read_only_assignment_keeps_behavior_and_has_no_set_event():
    with iv8.Page(environment=ENV) as page:
        _load(page)
        page.start_access_trace()
        assert page.eval("innerWidth = 9; innerWidth", name="probe.js") == 800
        events = page.read_access_trace()["events"]
        assert [(event["member"], event["operation"]) for event in events] == [
            ("innerWidth", "get")]
        page.start_access_trace()
        with pytest.raises(iv8.JSError):
            page.eval("'use strict'; innerWidth = 9;", name="probe.js")
        assert page.read_access_trace()["events"] == []


@on_only
def test_window_filter_and_generation():
    with iv8.Page(environment=ENV) as page:
        _load(page)
        page.start_access_trace(object_types=["window"])
        page.eval("innerWidth; navigator.userAgent;", name="probe.js")
        events = page.read_access_trace(clear=False)["events"]
        assert [(event["object_type"], event["member"]) for event in events] == [
            ("window", "innerWidth")]
        first_id = events[0]["object_id"]

        page.load(HTML, BASE)
        page.eval("innerWidth;", name="probe.js")
        events = [
            event for event in page.read_access_trace(clear=False)["events"]
            if event["object_type"] == "window"
            and event["member"] == "innerWidth"
            and event["source_kind"] == "eval"
        ]
        assert len(events) == 2
        assert events[0]["generation"] == 2
        assert events[1]["generation"] == 3
        assert events[0]["object_id"] == first_id
        assert events[1]["object_id"] != first_id
