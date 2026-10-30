"""Step 5a: access trace for independent native BOM callbacks."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://access-bom.test/"
HTML = "<html><body></body></html>"


def _load(page):
    page.load(HTML, BASE)


def _events(page):
    return page.read_access_trace(clear=False)["events"]


@on_only
def test_performance_crypto_storage_calls_are_traced():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval(
            """
            performance.now();
            performance.mark('a');
            performance.measure('m', 'a');
            const view = new Uint8Array(4);
            crypto.getRandomValues(view);
            crypto.randomUUID();
            localStorage.setItem('a', '1');
            localStorage.getItem('a');
            localStorage.removeItem('a');
            localStorage.length;
            localStorage.key(0);
            localStorage.clear();
            sessionStorage.setItem('b', '2');
            """,
            name="probe.js")
        events = _events(page)
        observed = [(event["object_type"], event["member"], event["operation"])
                    for event in events]
        assert observed == [
            ("performance", "now", "call"),
            ("performance", "mark", "call"),
            ("performance", "measure", "call"),
            ("crypto", "getRandomValues", "call"),
            ("crypto", "randomUUID", "call"),
            ("storage", "setItem", "call"),
            ("storage", "getItem", "call"),
            ("storage", "removeItem", "call"),
            ("storage", "length", "get"),
            ("storage", "key", "call"),
            ("storage", "clear", "call"),
            ("storage", "setItem", "call"),
        ]
        assert all(event["source_kind"] == "eval" for event in events)
        assert all(event["resource_name"] == "probe.js" for event in events)
        performance_events = [
            event for event in events if event["object_type"] == "performance"]
        assert len({event["object_id"] for event in performance_events}) == 1
        crypto_events = [
            event for event in events if event["object_type"] == "crypto"]
        assert len({event["object_id"] for event in crypto_events}) == 1
        storage_events = [
            event for event in events if event["object_type"] == "storage"]
        assert len({event["object_id"] for event in storage_events}) == 2


@on_only
def test_object_type_filter_applies_to_native_bom_calls():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace(object_types=["performance"])
        page.eval("performance.now(); crypto.randomUUID(); localStorage.clear();",
                  name="probe.js")
        events = _events(page)
        assert [(event["object_type"], event["member"]) for event in events] == [
            ("performance", "now")]


@on_only
def test_native_bom_calls_across_generation_keep_configuration():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace(object_types=["performance"])
        page.eval("performance.now();", name="probe.js")
        first = _events(page)[0]
        page.load(HTML, BASE)
        page.eval("performance.now();", name="probe.js")
        events = [event for event in _events(page)
                  if event["object_type"] == "performance"
                  and event["member"] == "now"]
        assert len(events) == 2
        assert events[0]["generation"] == 2
        assert events[1]["generation"] == 3
        assert first["object_id"] != events[1]["object_id"]


@on_only
def test_time_origin_and_storage_length_reads():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval("performance.timeOrigin; localStorage.length;",
                  name="probe.js")
        events = _events(page)
        assert [(event["object_type"], event["member"], event["operation"])
                for event in events] == [
            ("performance", "timeOrigin", "get"),
            ("storage", "length", "get"),
        ]
