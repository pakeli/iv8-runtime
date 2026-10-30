"""Step 5d: access trace for the static visualViewport HostObject."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://access-visual-viewport.test/"
HTML = "<html><body></body></html>"
ENV = {"innerWidth": 1024, "innerHeight": 768}


def _load(page):
    page.load(HTML, BASE)


def _events(page):
    return page.read_access_trace(clear=False)["events"]


@on_only
def test_visual_viewport_reads_are_traced():
    with iv8.Page(environment=ENV) as page:
        _load(page)
        page.start_access_trace()
        assert page.eval(
            """
            [visualViewport.width, visualViewport.height,
             visualViewport.offsetLeft, visualViewport.offsetTop,
             visualViewport.pageLeft, visualViewport.pageTop,
             visualViewport.scale].join('|')
            """,
            name="probe.js"
        ) == "1024|768|0|0|0|0|1"
        events = _events(page)
        assert [(event["object_type"], event["label"], event["member"],
                 event["operation"]) for event in events] == [
            ("visual_viewport", "visualViewport", "width", "get"),
            ("visual_viewport", "visualViewport", "height", "get"),
            ("visual_viewport", "visualViewport", "offsetLeft", "get"),
            ("visual_viewport", "visualViewport", "offsetTop", "get"),
            ("visual_viewport", "visualViewport", "pageLeft", "get"),
            ("visual_viewport", "visualViewport", "pageTop", "get"),
            ("visual_viewport", "visualViewport", "scale", "get"),
        ]
        assert all(event["source_kind"] == "eval" for event in events)
        assert all(event["resource_name"] == "probe.js" for event in events)
        assert len({event["object_id"] for event in events}) == 1


@on_only
def test_visual_viewport_method_calls_and_writable_handlers():
    with iv8.Page(environment=ENV) as page:
        _load(page)
        page.start_access_trace()
        assert page.eval(
            """
            (() => {
              const handler = function () {};
              visualViewport.onresize = handler;
              const result = [
                String(visualViewport.addEventListener('resize', handler)),
                String(visualViewport.removeEventListener('resize', handler)),
                String(visualViewport.dispatchEvent({type: 'resize'})),
                typeof visualViewport.onresize,
                visualViewport.onresize === handler,
                visualViewport.onscroll === null
              ].join('|');
              return result;
            })();
            """,
            name="probe.js"
        ) == "undefined|undefined|false|function|true|true"
        events = _events(page)
        assert [(event["member"], event["operation"]) for event in events] == [
            ("onresize", "set"),
            ("addEventListener", "call"),
            ("removeEventListener", "call"),
            ("dispatchEvent", "call"),
            ("onresize", "get"),
            ("onresize", "get"),
            ("onscroll", "get"),
        ]
        assert all(event["object_type"] == "visual_viewport"
                   for event in events)


@on_only
def test_visual_viewport_read_only_and_missing_probe_behavior():
    with iv8.Page(environment=ENV) as page:
        _load(page)
        page.start_access_trace()
        assert page.eval(
            """
            visualViewport.width = 1;
            Object.getOwnPropertyDescriptor(visualViewport, 'width');
            [visualViewport.width,
             String(visualViewport.missing)].join('|');
            """,
            name="probe.js"
        ) == "1024|undefined"
        events = _events(page)
        # The native-data-property descriptor read itself evaluates the getter;
        # it is indistinguishable from an ordinary read in the trace.
        assert [(event["member"], event["operation"],
                 event.get("resolution"))
                for event in events] == [
            ("width", "get", None),
            ("width", "get", None),
            ("missing", "get_probe", "missing"),
        ]

        # Converting the plain JS object into a HostObject keeps the values and
        # read-only/writable behavior but intentionally changes the prototype
        # (host-object instances have an intermediate template prototype) and
        # makes the no-op event methods writable/configurable like other host
        # methods. The read-only metadata descriptors keep their data shape.
        page.start_access_trace()
        descriptor = page.eval(
            """
            (() => {
              const width = Object.getOwnPropertyDescriptor(
                  visualViewport, 'width');
              const onresize = Object.getOwnPropertyDescriptor(
                  visualViewport, 'onresize');
              const add = Object.getOwnPropertyDescriptor(
                  visualViewport, 'addEventListener');
              return [
                width.value, width.writable, width.enumerable,
                width.configurable, String(onresize.value),
                onresize.writable, onresize.enumerable, onresize.configurable,
                typeof add.value, add.writable, add.enumerable,
                add.configurable,
                Object.getPrototypeOf(visualViewport) === Object.prototype,
                Object.getPrototypeOf(
                    Object.getPrototypeOf(visualViewport)) === Object.prototype
              ].join('|');
            })();
            """,
            name="probe.js"
        )
        assert descriptor == ("1024|false|true|false|null|true|true|false|"
                              "function|true|true|true|false|true")

        page.start_access_trace()
        with pytest.raises(iv8.JSError):
            page.eval("'use strict'; visualViewport.width = 1;",
                      name="probe.js")
        assert _events(page) == []


@on_only
def test_visual_viewport_filter_and_generation():
    with iv8.Page(environment=ENV) as page:
        _load(page)
        page.start_access_trace(object_types=["visual_viewport"])
        page.eval("visualViewport.width; navigator.userAgent;",
                  name="probe.js")
        events = _events(page)
        assert [(event["object_type"], event["member"]) for event in events] == [
            ("visual_viewport", "width")]
        first_id = events[0]["object_id"]
        assert events[0]["generation"] == 2

        page.load(HTML, BASE)
        assert page.eval("visualViewport.width", name="probe.js") == 1024
        events = [
            event for event in _events(page)
            if event["object_type"] == "visual_viewport"
            and event["member"] == "width"
            and event["source_kind"] == "eval"
        ]
        assert len(events) == 2
        assert events[0]["generation"] == 2
        assert events[1]["generation"] == 3
        assert events[0]["object_id"] == first_id
        assert events[1]["object_id"] != first_id
