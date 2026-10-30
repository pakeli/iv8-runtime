"""First-slice HostObject get/set/call access-trace runtime tests."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://access-trace.test/"
HTML = "<html><body><div id='app'>hello</div></body></html>"


def _load(page):
    page.load(HTML, BASE)


def _events(page, **matches):
    report = page.read_access_trace(clear=False)
    events = report["events"]
    if not matches:
        return events
    return [
        event for event in events
        if all(event[key] == value for key, value in matches.items())
    ]


@on_only
def test_native_property_gets_and_bracket_access():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval("navigator.userAgent; navigator['language'];", name="probe.js")
        events = page.read_access_trace()
        assert [event["member"] for event in events["events"]] == [
            "userAgent", "language"]
        for event in events["events"]:
            assert event["object_type"] == "navigator"
            assert event["label"] == "navigator"
            assert event["operation"] == "get"
            assert event["phase"] == "enter"
            assert event["source_kind"] == "eval"
            assert event["resource_name"] == "probe.js"
        assert events["events"][0]["seq"] == 1
        assert events["events"][0]["object_id"] == events["events"][1]["object_id"]
        assert events["events"][0]["generation"] == 2


@on_only
def test_element_set_and_get_keep_behavior():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval(
            "const el = document.querySelector('#app');"
            "el.textContent = 'ready';"
            "el.textContent;",
            name="probe.js")
        events = page.read_access_trace()["events"]
        assert [event["operation"] for event in events] == [
            "call", "set", "get"]
        assert events[0]["object_type"] == "document"
        assert events[0]["member"] == "querySelector"
        assert events[1]["object_type"] == "element"
        assert events[1]["label"] == "div"
        assert events[1]["member"] == "textContent"
        assert events[2]["operation"] == "get"
        assert page.eval("document.querySelector('#app').textContent") == "ready"


@on_only
def test_read_only_assignment_is_not_faked_as_a_set():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval("navigator.userAgent = 'changed';", name="probe.js")
        events = page.read_access_trace()["events"]
        assert not [event for event in events if event["operation"] == "set"]
        assert page.eval("navigator.userAgent") == "Mozilla/5.0 (compatible; iv8)"

        page.start_access_trace()
        with pytest.raises(iv8.JSError):
            page.eval("'use strict'; navigator.userAgent = 'changed';",
                      name="probe.js")
        assert not [
            event for event in page.read_access_trace()["events"]
            if event["operation"] == "set"]


@on_only
def test_native_method_calls_and_throwing_call_enter():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval(
            "document.querySelector('#app');"
            "document.getElementById('app').getAttribute('id');"
            "document.hasFocus();"
            "location.toString();"
            "console.log('trace');",
            name="probe.js")
        events = page.read_access_trace()["events"]
        calls = [event for event in events if event["operation"] == "call"]
        assert [event["member"] for event in calls] == [
            "querySelector", "getElementById", "getAttribute", "hasFocus",
            "toString", "log"]
        assert [event["object_type"] for event in calls] == [
            "document", "document", "element", "document", "location", "console"]

        page.start_access_trace()
        with pytest.raises(iv8.JSError):
            page.eval("document.body.appendChild(1);", name="probe.js")
        calls = [
            event for event in page.read_access_trace()["events"]
            if event["operation"] == "call"
        ]
        assert [event["member"] for event in calls] == ["appendChild"]


@on_only
def test_method_reference_read_is_not_a_call():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval("const fn = document.querySelector; typeof fn;",
                  name="probe.js")
        assert page.read_access_trace()["events"] == []


@on_only
def test_element_object_id_tracks_native_backing_not_js_wrapper():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval(
            "const a1 = document.querySelector('#app');"
            "const a2 = document.querySelector('#app');"
            "const b = document.querySelector('body');"
            "a1.id; a2.id; b.id;",
            name="probe.js")
        events = page.read_access_trace()["events"]
        element_gets = [
            event for event in events
            if event["object_type"] == "element" and event["operation"] == "get"
        ]
        assert len(element_gets) == 3
        assert element_gets[0]["object_id"] == element_gets[1]["object_id"]
        assert element_gets[0]["object_id"] != element_gets[2]["object_id"]
        assert element_gets[0]["label"] == "div"
        assert element_gets[2]["label"] == "body"


@on_only
def test_generation_and_load_lifecycle():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace(max_events=20)
        page.eval("navigator.userAgent;", name="probe.js")
        first = _events(page, source_kind="eval", member="userAgent")[0]

        page.load("<html><body><div id='two'></div></body></html>", BASE)
        page.eval("navigator.userAgent;", name="probe.js")
        events = _events(page, source_kind="eval", member="userAgent")
        assert len(events) == 2
        assert events[0]["generation"] == 2
        assert events[1]["generation"] == 3
        assert events[0]["object_id"] != events[1]["object_id"]
        assert events[0]["seq"] < events[1]["seq"]
        assert first["generation"] == 2


@on_only
def test_source_attribution_internal_html_eval_and_callbacks():
    with iv8.Page() as page:
        page.start_access_trace()
        page.load(
            "<html><body><script>navigator.userAgent;</script></body></html>",
            BASE)
        report = page.read_access_trace(clear=False)
        internal = [
            event for event in report["events"]
            if event["source_kind"] == "internal"
        ]
        assert internal
        html_accesses = [
            event for event in report["events"]
            if event["source_kind"] == "html_script"
            and event["member"] == "userAgent"
        ]
        assert len(html_accesses) == 1
        assert html_accesses[0]["resource_name"] == (
            f"{BASE}#inline-script-1")

        page.eval(
            "setTimeout(() => navigator.language, 0);"
            "Promise.resolve().then(() => navigator.platform);",
            name="probe.js")
        assert not [
            event for event in _events(
                page, member="language", source_kind="unknown")
        ]
        page.run_timers()
        page.run_jobs()
        unknown = _events(page, source_kind="unknown")
        assert {event["member"] for event in unknown} >= {"language", "platform"}
        assert all(event["resource_name"] == "" for event in unknown)


@on_only
def test_capacity_filter_and_clear():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace(max_events=2, object_types=["navigator"])
        page.eval(
            "navigator.userAgent; navigator.language; navigator.platform;",
            name="probe.js")
        report = page.read_access_trace(clear=False)
        assert len(report["events"]) == 2
        assert report["dropped_events"] == 1
        assert report["filter_object_types"] == ["navigator"]

        page.start_access_trace(max_events=10, object_types=["document"])
        page.eval("navigator.userAgent; document.hasFocus();",
                  name="probe.js")
        report = page.read_access_trace(clear=True)
        assert [event["object_type"] for event in report["events"]] == ["document"]
        assert report["dropped_events"] == 0

        page.start_access_trace(max_events=10, object_types=[])
        page.eval("navigator.userAgent;", name="probe.js")
        report = page.read_access_trace(clear=False)
        assert report["events"] == []
        assert report["dropped_events"] == 0


@on_only
def test_old_channels_and_new_trace_coexist():
    with iv8.Page() as page:
        _load(page)
        page.watch_apis(["document.querySelector"])
        page.watch_reflect_get(enabled=True)
        page.start_access_trace()
        page.eval(
            "document.querySelector('#app');"
            "Reflect.get(navigator, 'userAgent');",
            name="probe.js")
        api_hits = page.read_watch_api_hits()
        reflect_hits = page.read_reflect_get_hits()
        assert [hit["path"] for hit in api_hits] == ["document.querySelector"]
        assert [hit["property"] for hit in reflect_hits] == ["userAgent"]
        trace = page.read_access_trace()["events"]
        document_calls = [
            event for event in trace
            if event["object_type"] == "document"
            and event["operation"] == "call"
        ]
        assert len(document_calls) == 1
        assert [event["member"] for event in trace
                if event["object_type"] == "navigator"] == ["userAgent"]


@on_only
def test_multiple_pages_and_read_does_not_execute_js():
    with iv8.Page() as first, iv8.Page() as second:
        _load(first)
        _load(second)
        first.start_access_trace()
        first.eval("navigator.userAgent;", name="probe.js")
        assert second.read_access_trace()["events"] == []
        first_report = first.read_access_trace(clear=True)
        assert len(first_report["events"]) == 1
        assert first.read_access_trace()["events"] == []
        assert second.read_access_trace()["events"] == []
