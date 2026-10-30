"""Step 5h: access trace for legacy navigator plugins/mimeTypes/javaEnabled."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://access-legacy-navigator.test/"
HTML = "<html><body></body></html>"


def _load(page):
    page.load(HTML, BASE)


def _events(page):
    return page.read_access_trace(clear=False)["events"]


@on_only
def test_legacy_navigator_reads_and_java_enabled_call_are_traced():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        assert page.eval(
            """
            [navigator.plugins.length, navigator.mimeTypes.length,
             navigator.javaEnabled()].join('|');
            """,
            name="probe.js"
        ) == "0|0|false"
        events = _events(page)
        assert [(event["object_type"], event["label"], event["member"],
                 event["operation"]) for event in events] == [
            ("navigator", "navigator", "plugins", "get"),
            ("navigator", "navigator", "mimeTypes", "get"),
            ("navigator", "navigator", "javaEnabled", "get"),
            ("navigator", "navigator", "javaEnabled", "call"),
        ]
        assert all(event["source_kind"] == "eval" for event in events)
        assert all(event["resource_name"] == "probe.js" for event in events)
        navigator_ids = {event["object_id"] for event in events
                         if event["member"] != "javaEnabled"
                         or event["operation"] == "get"}
        assert len(navigator_ids) == 1
        # The call uses its own stable native token, not the navigator host.
        assert events[3]["object_id"] != events[0]["object_id"]


@on_only
def test_legacy_navigator_keeps_identity_shape_and_read_only_behavior():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("navigator.plugins === navigator.plugins") is True
        assert page.eval("navigator.mimeTypes === navigator.mimeTypes") is True
        assert page.eval(
            "navigator.javaEnabled === navigator.javaEnabled") is True
        assert page.eval(
            """
            [Array.isArray(navigator.plugins),
             Object.isFrozen(navigator.plugins),
             navigator.plugins.length,
             Object.getPrototypeOf(navigator.plugins) === Array.prototype,
             Array.isArray(navigator.mimeTypes),
             Object.isFrozen(navigator.mimeTypes),
             navigator.mimeTypes.length].join('|');
            """
        ) == "true|true|0|true|true|true|0"
        assert page.eval(
            """
            (() => {
              const d = Object.getOwnPropertyDescriptor(
                  navigator, 'javaEnabled');
              return [typeof d.value, d.writable, d.enumerable,
                      d.configurable].join('|');
            })();
            """
        ) == "function|false|true|false"
        assert page.eval(
            """
            navigator.plugins = [1];
            navigator.mimeTypes = [1];
            navigator.javaEnabled = () => true;
            [navigator.plugins.length, navigator.mimeTypes.length,
             navigator.javaEnabled()].join(',');
            """
        ) == "0,0,false"

        page.start_access_trace()
        assert page.eval(
            "const f = navigator.javaEnabled; f();", name="probe.js"
        ) is False
        assert [(event["member"], event["operation"])
                for event in _events(page)] == [
            ("javaEnabled", "get"), ("javaEnabled", "call")]


@on_only
def test_legacy_navigator_filter_and_generation():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace(object_types=["window"])
        page.eval("navigator.plugins; navigator.javaEnabled();",
                  name="probe.js")
        assert _events(page) == []

        page.start_access_trace(object_types=["navigator"])
        page.eval("navigator.plugins; navigator.javaEnabled();",
                  name="probe.js")
        events = _events(page)
        assert [(event["member"], event["operation"]) for event in events] == [
            ("plugins", "get"),
            ("javaEnabled", "get"),
            ("javaEnabled", "call"),
        ]
        first_plugin_id = events[0]["object_id"]
        first_call_id = events[2]["object_id"]
        assert events[0]["generation"] == 2

        page.load(HTML, BASE)
        page.eval("navigator.plugins; navigator.javaEnabled();",
                  name="probe.js")
        events = [
            event for event in _events(page)
            if event["source_kind"] == "eval"
        ]
        assert [event["generation"] for event in events] == [2, 2, 2, 3, 3, 3]
        assert events[3]["object_id"] != first_plugin_id
        assert events[5]["object_id"] != first_call_id
