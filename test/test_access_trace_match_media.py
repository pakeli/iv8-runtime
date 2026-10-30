"""Step 5g: access trace for matchMedia result objects."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://access-match-media.test/"
HTML = "<html><body></body></html>"
ENV = {"innerWidth": 800, "innerHeight": 600}


def _load(page):
    page.load(HTML, BASE)


def _events(page):
    return page.read_access_trace(clear=False)["events"]


@on_only
def test_match_media_result_reads_and_method_calls_are_traced():
    with iv8.Page(environment=ENV) as page:
        _load(page)
        page.start_access_trace()
        assert page.eval(
            """
            globalThis.m = matchMedia('(min-width: 700px)');
            [m.media, m.matches, m.onchange === null].join('|');
            """,
            name="probe.js"
        ) == "(min-width: 700px)|true|true"
        assert page.eval(
            """
            m.addEventListener('change', () => {});
            m.removeListener(() => {});
            m.dispatchEvent({type: 'change'});
            """,
            name="probe.js"
        ) is False

        events = _events(page)
        assert [(event["object_type"], event["label"], event["member"],
                 event["operation"]) for event in events] == [
            ("media_query_list", "matchMedia", "matchMedia", "call"),
            ("media_query_list", "matchMedia", "media", "get"),
            ("media_query_list", "matchMedia", "matches", "get"),
            ("media_query_list", "matchMedia", "addEventListener", "call"),
            ("media_query_list", "matchMedia", "removeListener", "call"),
            ("media_query_list", "matchMedia", "dispatchEvent", "call"),
        ]
        assert all(event["source_kind"] == "eval" for event in events)
        assert all(event["resource_name"] == "probe.js" for event in events)
        assert len({event["object_id"] for event in events}) == 1


@on_only
def test_match_media_identity_cache_independence_and_missing_probe():
    with iv8.Page(environment=ENV) as page:
        _load(page)
        page.start_access_trace()
        assert page.eval(
            """
            globalThis.a = matchMedia('screen');
            globalThis.b = matchMedia('screen');
            globalThis.c = matchMedia('print');
            [a !== b, a.media, b.media, c.media].join('|');
            """,
            name="probe.js"
        ) == "true|screen|screen|print"
        assert page.eval(
            """
            a.onchange = function () {};
            [typeof a.onchange, b.onchange === null].join('|');
            """,
            name="probe.js"
        ) == "function|true"
        assert page.eval("a.missing === undefined", name="probe.js") is True

        events = _events(page)
        calls = [event for event in events
                 if event["member"] == "matchMedia"]
        assert len(calls) == 3
        assert calls[0]["object_id"] == calls[1]["object_id"]
        assert calls[0]["object_id"] != calls[2]["object_id"]
        gets = [event for event in events
                if event["member"] == "media"]
        assert len(gets) == 3
        assert gets[0]["object_id"] == gets[1]["object_id"]
        assert gets[0]["object_id"] == calls[0]["object_id"]
        assert gets[2]["object_id"] == calls[2]["object_id"]
        probes = [event for event in events
                  if event["operation"] == "get_probe"]
        assert [(event["member"], event["resolution"]) for event in probes] == [
            ("missing", "missing")]


@on_only
def test_match_media_shape_and_filter_generation():
    with iv8.Page(environment=ENV) as page:
        _load(page)
        assert page.eval(
            "matchMedia.toString().indexOf('[native code]') >= 0") is False
        assert page.eval(
            """
            (() => {
              const m = matchMedia('screen');
              const media = Object.getOwnPropertyDescriptor(m, 'media');
              const matches = Object.getOwnPropertyDescriptor(m, 'matches');
              const onchange = Object.getOwnPropertyDescriptor(m, 'onchange');
              const add = Object.getOwnPropertyDescriptor(m, 'addListener');
              m.media = 'changed';
              m.matches = false;
              m.onchange = function () {};
              return [
                m.media, m.matches, typeof m.onchange,
                media.value, media.writable, media.enumerable,
                media.configurable, matches.value,
                String(onchange.value), onchange.writable,
                onchange.enumerable, onchange.configurable,
                typeof add.value, add.writable, add.enumerable, add.configurable,
                Object.getPrototypeOf(m) === Object.prototype,
                Object.getPrototypeOf(
                    Object.getPrototypeOf(m)) === Object.prototype
              ].join('|');
            })();
            """,
            name="shape.js"
        ) == ("screen|true|function|screen|false|true|false|true|null|true|"
              "true|true|function|true|true|true|false|true")

        page.start_access_trace(object_types=["media_query_list"])
        page.eval("matchMedia('screen').media; navigator.userAgent;",
                  name="probe.js")
        events = _events(page)
        assert [(event["object_type"], event["member"]) for event in events] == [
            ("media_query_list", "matchMedia"),
            ("media_query_list", "media"),
        ]
        first_id = events[0]["object_id"]
        assert events[0]["generation"] == 2

        page.load(HTML, BASE)
        page.eval("matchMedia('screen').media;", name="probe.js")
        events = [
            event for event in _events(page)
            if event["object_type"] == "media_query_list"
            and event["source_kind"] == "eval"
        ]
        assert [(event["generation"], event["member"]) for event in events] == [
            (2, "matchMedia"), (2, "media"), (3, "matchMedia"), (3, "media")]
        assert events[2]["object_id"] != first_id
