"""Step 6a: value, argument, and return summaries for HostObject access."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://access-values.test/"
HTML = "<html><body></body></html>"


def _load(page):
    page.load(HTML, BASE)


def _events(page):
    return page.read_access_trace(clear=False)["events"]


@on_only
def test_getter_values_and_call_arguments_returns():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        assert page.eval(
            """
            [hostProbe.answer, hostProbe.label, hostProbe.echo('hi'),
             hostProbe.add(2, 3)].join('|');
            """,
            name="probe.js"
        ) == "42|iv8-host-probe|hi|5"
        events = _events(page)
        assert [(event["member"], event["operation"]) for event in events] == [
            ("answer", "get"),
            ("label", "get"),
            ("echo", "call"),
            ("add", "call"),
        ]
        assert events[0]["value_summary"] == "number:42"
        assert events[0]["outcome"] == "returned"
        assert events[1]["value_summary"] == "string:14:iv8-host-probe"
        assert events[2]["argument_count"] == 1
        assert events[2]["argument_summaries"] == ["string:2:hi"]
        assert events[2]["return_summary"] == "string:2:hi"
        assert events[3]["argument_count"] == 2
        assert events[3]["argument_summaries"] == ["number:2", "number:3"]
        assert events[3]["return_summary"] == "number:5"
        assert all(event["outcome"] == "returned" for event in events)


@on_only
def test_primitive_object_and_special_value_summaries():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval(
            """
            hostProbe.echo(undefined);
            hostProbe.echo(null);
            hostProbe.echo(true);
            hostProbe.echo(2.5);
            hostProbe.echo([]);
            hostProbe.echo(function () {});
            hostProbe.echo(10n);
            hostProbe.echo(Symbol('s'));
            hostProbe.echo(hostProbe);
            hostProbe.echo({a: 1});
            """
        )
        events = [event for event in _events(page)
                  if event["member"] == "echo" and event["operation"] == "call"]
        assert [event["return_summary"] for event in events] == [
            "undefined",
            "null",
            "boolean:true",
            "number:2.5",
            "array",
            "function",
            "bigint",
            "symbol",
            "host_object",
            "object",
        ]
        assert all(event["argument_summaries"] == [event["return_summary"]]
                   for event in events)


@on_only
def test_setter_value_and_read_back_summaries():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval(
            "document.body.textContent = 'hello';"
            "document.body.className = 'x';",
            name="probe.js"
        )
        events = _events(page)
        sets = {event["member"]: event for event in events
                if event["operation"] == "set"}
        assert sets["textContent"]["value_summary"] == "string:5:hello"
        assert sets["className"]["value_summary"] == "string:1:x"
        assert all(event["outcome"] == "returned" for event in sets.values())
        body_gets = [event for event in events
                     if event["member"] == "body"
                     and event["operation"] == "get"]
        assert body_gets
        assert all(event["value_summary"] == "host_object"
                   for event in body_gets)

        page.eval("document.body.textContent;", name="read.js")
        text_get = [
            event for event in _events(page)
            if event["member"] == "textContent"
            and event["operation"] == "get"
        ][-1]
        assert text_get["value_summary"] == "string:5:hello"
        assert text_get["outcome"] == "returned"


@on_only
def test_argument_cap_and_string_truncation():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        long_value = "a" * 100
        page.eval(f"hostProbe.echo('{long_value}');", name="probe.js")
        page.eval(
            "hostProbe.echo(1, 2, 3, 4, 5, 6, 7, 8, 9, 10);",
            name="probe.js"
        )
        events = [event for event in _events(page)
                  if event["member"] == "echo" and event["operation"] == "call"]
        assert events[0]["argument_summaries"] == [
            "string:100:" + "a" * 64 + "..."]
        assert events[0]["return_summary"] == (
            "string:100:" + "a" * 64 + "...")
        assert events[1]["argument_count"] == 10
        assert events[1]["argument_summaries"] == [
            "number:1", "number:2", "number:3", "number:4",
            "number:5", "number:6", "number:7", "number:8",
        ]
        assert events[1]["return_summary"] == "number:1"


@on_only
def test_thrown_call_and_nested_completion_stay_attached_to_the_event():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        with pytest.raises(iv8.JSError):
            page.eval(
                "hostProbe.add({valueOf() { throw new Error('boom'); }}, 1);",
                name="probe.js"
            )
        events = _events(page)
        add = [event for event in events if event["member"] == "add"][-1]
        assert add["argument_count"] == 2
        assert add["argument_summaries"] == ["object", "number:1"]
        assert add["outcome"] == "threw"
        assert add["exception_name"] == "Error"
        assert "boom" in add["exception_message"]
        assert add["exception_stack"]
        assert "return_summary" not in add

        page.start_access_trace()
        assert page.eval(
            "hostProbe.add({valueOf() { hostProbe.label; return 1; }}, 2);",
            name="probe.js"
        ) == 3
        events = _events(page)
        add = [event for event in events
               if event["member"] == "add" and event["operation"] == "call"][0]
        label = [event for event in events
                 if event["member"] == "label" and event["operation"] == "get"][0]
        assert add["argument_summaries"] == ["object", "number:2"]
        assert add["return_summary"] == "number:3"
        assert add["outcome"] == "returned"
        assert label["value_summary"] == "string:14:iv8-host-probe"
        assert events[0]["member"] == "add"


@on_only
def test_native_callbacks_have_summaries_and_probes_do_not():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval("performance.now(); navigator.notThere;", name="probe.js")
        events = _events(page)
        performance = [event for event in events
                       if event["object_type"] == "performance"][0]
        probe = [event for event in events
                 if event["operation"] == "get_probe"][0]
        assert performance["outcome"] == "returned"
        assert performance["argument_count"] == 0
        assert performance["argument_summaries"] == []
        assert performance["return_summary"].startswith("number:")
        assert "outcome" not in probe
        assert "value_summary" not in probe
        coverage = page.read_access_trace(clear=False)["coverage"]
        assert "summaries" in coverage


@on_only
def test_native_getters_have_value_summaries():
    with iv8.Page(environment={"innerWidth": 800, "innerHeight": 600}) as page:
        _load(page)
        page.start_access_trace()
        page.eval(
            "localStorage.length; performance.timeOrigin; window.innerWidth;",
            name="getters.js"
        )
        events = _events(page)
        storage = [event for event in events
                   if event["object_type"] == "storage"
                   and event["member"] == "length"][0]
        time_origin = [event for event in events
                       if event["object_type"] == "performance"
                       and event["member"] == "timeOrigin"][0]
        width = [event for event in events
                 if event["object_type"] == "window"
                 and event["member"] == "innerWidth"][0]
        assert storage["outcome"] == "returned"
        assert storage["value_summary"] == "number:0"
        assert time_origin["outcome"] == "returned"
        assert time_origin["value_summary"] == "number:0"
        assert width["outcome"] == "returned"
        assert width["value_summary"] == "number:800"


@on_only
def test_native_callback_exception_metadata_is_safe_and_attached():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        with pytest.raises(iv8.JSError):
            page.eval("crypto.getRandomValues([]);", name="throw.js")
        events = _events(page)
        call = [event for event in events
                if event["object_type"] == "crypto"
                and event["operation"] == "call"][0]
        assert call["outcome"] == "threw"
        assert call["exception_name"] == "TypeError"
        assert "getRandomValues" in call["exception_message"]
        assert call["exception_stack"]
        assert "return_summary" not in call
