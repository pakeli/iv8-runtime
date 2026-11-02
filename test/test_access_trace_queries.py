"""Step 6d/6f: explicit has/descriptor queries and Reflect target association."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://access-queries.test/"
HTML = "<html><body></body></html>"


def _load(page):
    page.load(HTML, BASE)


def _events(page):
    return page.read_access_trace(clear=False)["events"]


def _query_events(page):
    return [event for event in _events(page)
            if event["operation"] in ("has", "get_own_property_descriptor")]


@on_only
def test_has_and_descriptor_wrappers_record_host_object_queries():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        result = page.eval(
            """
            (() => {
              const a = Object.prototype.hasOwnProperty.call(
                  navigator, 'userAgent');
              const b = Object.hasOwn(navigator, 'userAgent');
              const c = Reflect.has(navigator, 'userAgent');
              const d = Object.getOwnPropertyDescriptor(
                  navigator, 'userAgent');
              const e = Reflect.getOwnPropertyDescriptor(
                  navigator, 'userAgent');
              return [a, b, c, typeof d.value, d.writable, e.enumerable,
                      Object.getOwnPropertyDescriptor(
                          navigator, 'missing') === undefined,
                      Reflect.has(navigator, 'missing')].join('|');
            })();
            """,
            name="queries.js"
        )
        assert result == "true|true|true|string|false|true|true|false"

        events = _query_events(page)
        assert [(event["member"], event["operation"], event["object_type"])
                for event in events] == [
            ("userAgent", "has", "navigator"),
            ("userAgent", "has", "navigator"),
            ("userAgent", "has", "navigator"),
            ("userAgent", "get_own_property_descriptor", "navigator"),
            ("userAgent", "get_own_property_descriptor", "navigator"),
            ("missing", "get_own_property_descriptor", "navigator"),
            ("missing", "has", "navigator"),
        ]
        assert all(event["resource_name"] == "queries.js" for event in events)
        assert all(event["source_kind"] == "eval" for event in events)
        assert len({event["object_id"] for event in events}) == 1
        assert all(event["outcome"] == "returned" for event in events)
        assert [event["return_summary"] for event in events
                if event["operation"] == "has"] == [
            "boolean:true", "boolean:true", "boolean:true", "boolean:false"]
        assert [event["return_summary"] for event in events
                if event["operation"] == "get_own_property_descriptor"] == [
            "object", "object", "undefined"]


@on_only
def test_has_and_descriptor_targets_filter_and_same_member_identity():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval(
            "Object.prototype.hasOwnProperty.call(navigator, 'same');"
            "Object.prototype.hasOwnProperty.call(screen, 'same');"
            "Object.getOwnPropertyDescriptor(navigator, 'same');"
            "Object.getOwnPropertyDescriptor(screen, 'same');",
            name="targets.js"
        )
        events = _query_events(page)
        has = [event for event in events if event["operation"] == "has"]
        descriptors = [event for event in events
                       if event["operation"] == "get_own_property_descriptor"]
        assert [(event["object_type"], event["label"], event["member"])
                for event in has] == [
            ("navigator", "navigator", "same"),
            ("screen", "screen", "same"),
        ]
        assert [(event["object_type"], event["label"], event["member"])
                for event in descriptors] == [
            ("navigator", "navigator", "same"),
            ("screen", "screen", "same"),
        ]
        assert has[0]["object_id"] != has[1]["object_id"]
        assert descriptors[0]["object_id"] != descriptors[1]["object_id"]

        page.start_access_trace(object_types=["screen"])
        page.eval(
            "Object.prototype.hasOwnProperty.call(navigator, 'same');"
            "Object.prototype.hasOwnProperty.call(screen, 'same');"
            "Object.getOwnPropertyDescriptor(navigator, 'same');"
            "Object.getOwnPropertyDescriptor(screen, 'same');",
            name="targets.js"
        )
        assert [(event["object_type"], event["member"], event["operation"])
                for event in _query_events(page)] == [
            ("screen", "same", "has"),
            ("screen", "same", "get_own_property_descriptor"),
        ]


@on_only
def test_query_wrappers_preserve_behavior_and_document_blind_spots():
    with iv8.Page() as page:
        _load(page)
        # Cache the originals before tracing starts: these references are not
        # retroactively wrapped. This is the documented wrapper blind spot.
        page.eval(
            "globalThis.cachedDesc = Object.getOwnPropertyDescriptor;"
            "globalThis.cachedHas = Object.prototype.hasOwnProperty;"
        )
        page.start_access_trace()
        page.eval(
            "cachedDesc(navigator, 'userAgent');"
            "cachedHas.call(navigator, 'userAgent');"
            "'userAgent' in navigator;",
            name="cached.js"
        )
        assert _query_events(page) == []

        page.start_access_trace()
        page.eval(
            "Object.getOwnPropertyDescriptor(navigator, 'userAgent');"
            "navigator.hasOwnProperty('userAgent');",
            name="live.js"
        )
        assert [(event["operation"], event["member"])
                for event in _query_events(page)] == [
            ("get_own_property_descriptor", "userAgent"),
            ("has", "userAgent"),
        ]


@on_only
def test_query_wrappers_skip_plain_object_targets():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval(
            "Object.getOwnPropertyDescriptor({x: 1}, 'x');"
            "Object.hasOwn({x: 1}, 'x');"
            "Reflect.has({x: 1}, 'x');"
            "Object.prototype.hasOwnProperty.call({x: 1}, 'x');",
            name="plain.js"
        )
        assert _query_events(page) == []


@on_only
def test_query_wrappers_are_reinstalled_after_load():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.load(HTML, BASE)
        page.eval(
            "Object.getOwnPropertyDescriptor(navigator, 'userAgent');"
            "Reflect.has(navigator, 'userAgent');",
            name="generation.js"
        )
        events = _query_events(page)
        assert [(event["generation"], event["operation"], event["member"])
                for event in events] == [
            (3, "get_own_property_descriptor", "userAgent"),
            (3, "has", "userAgent"),
        ]
