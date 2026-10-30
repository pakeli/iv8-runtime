"""Step 5c: nested connection/orientation objects and performance.timeOrigin."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://access-nested.test/"
HTML = "<html><body></body></html>"


def _load(page):
    page.load(HTML, BASE)


def _events(page):
    return page.read_access_trace(clear=False)["events"]


@on_only
def test_nested_connection_orientation_and_time_origin_reads():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        assert page.eval(
            """
            [navigator.connection.effectiveType,
             navigator.connection.rtt,
             navigator.connection.downlink,
             navigator.connection.saveData,
             screen.orientation.type,
             screen.orientation.angle,
             performance.timeOrigin].join('|')
            """,
            name="probe.js"
        ) == "4g|50|10|false|landscape-primary|0|0"
        events = _events(page)
        observed = [(event["object_type"], event["label"], event["member"],
                     event["operation"]) for event in events]
        assert observed == [
            ("navigator", "navigator", "connection", "get"),
            ("connection", "navigator.connection", "effectiveType", "get"),
            ("navigator", "navigator", "connection", "get"),
            ("connection", "navigator.connection", "rtt", "get"),
            ("navigator", "navigator", "connection", "get"),
            ("connection", "navigator.connection", "downlink", "get"),
            ("navigator", "navigator", "connection", "get"),
            ("connection", "navigator.connection", "saveData", "get"),
            ("screen", "screen", "orientation", "get"),
            ("orientation", "screen.orientation", "type", "get"),
            ("screen", "screen", "orientation", "get"),
            ("orientation", "screen.orientation", "angle", "get"),
            ("performance", "performance", "timeOrigin", "get"),
        ]
        assert all(event["source_kind"] == "eval" for event in events)
        assert all(event["resource_name"] == "probe.js" for event in events)
        connection_ids = {event["object_id"] for event in events
                          if event["object_type"] == "connection"}
        orientation_ids = {event["object_id"] for event in events
                           if event["object_type"] == "orientation"}
        assert len(connection_ids) == 1
        assert len(orientation_ids) == 1


@on_only
def test_connection_identity_is_stable_across_host_wrappers():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval(
            "navigator.connection.effectiveType;"
            "navigator.connection.effectiveType;",
            name="probe.js")
        events = [
            event for event in _events(page)
            if event["object_type"] == "connection"
            and event["member"] == "effectiveType"
        ]
        assert len(events) == 2
        assert events[0]["object_id"] == events[1]["object_id"]


@on_only
def test_nested_missing_probes_and_filter():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval(
            "navigator.connection.missing; screen.orientation.missing;",
            name="probe.js")
        probes = [
            (event["object_type"], event["member"], event["resolution"])
            for event in _events(page)
            if event["operation"] == "get_probe"
        ]
        assert probes == [
            ("connection", "missing", "missing"),
            ("orientation", "missing", "missing"),
        ]

        page.start_access_trace(object_types=["orientation"])
        page.eval("navigator.connection.effectiveType;"
                  "screen.orientation.type; performance.timeOrigin;",
                  name="probe.js")
        events = _events(page)
        assert [event["object_type"] for event in events] == ["orientation"]
