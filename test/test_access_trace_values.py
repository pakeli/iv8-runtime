"""Step 6a: value, argument, and return summaries for HostObject access."""

import json

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
def test_all_arguments_and_full_string_values_are_captured():
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
            "string:100:" + long_value]
        assert events[0]["return_summary"] == (
            "string:100:" + long_value)
        assert events[1]["argument_count"] == 10
        assert events[1]["argument_summaries"] == [
            "number:1", "number:2", "number:3", "number:4",
            "number:5", "number:6", "number:7", "number:8", "number:9", "number:10",
        ]
        assert events[1]["return_summary"] == "number:1"
        assert page.read_access_trace(clear=False)["coverage"]["string_capture"] == "full"


@on_only
def test_late_arguments_are_summarized_without_proxy_or_error_getters():
    with iv8.Page() as page:
        _load(page)
        page.eval("""globalThis.summary_reads=0;
            const trap=()=>{summary_reads++;throw Error('must not inspect');};
            globalThis.summary_proxy=new Proxy({},{get:trap,getPrototypeOf:trap,
                getOwnPropertyDescriptor:trap,ownKeys:trap});
            globalThis.summary_error=Error('argument');
            for(const name of ['name','message','stack'])Object.defineProperty(summary_error,name,
                {get:trap,configurable:true});""")
        page.start_access_trace()
        assert page.eval("hostProbe.echo('first',1,2,3,4,5,6,7,8,summary_proxy,summary_error)") == "first"
        record, = _events(page)
        assert record["argument_count"] == 11
        assert record["argument_summaries"] == ["string:5:first", *["number:" + str(index) for index in range(1, 9)],
                                                 "object", "object"]
        assert record["return_summary"] == "string:5:first"
        assert page.eval("summary_reads") == 0


@on_only
@pytest.mark.parametrize("payload", [("桥😀\\\n" * 1200) + "...", "\0" * 1024],
                         ids=["large-unicode", "escaped-size-exceeds-original-size"])
def test_script_bootstrap_bridge_keeps_full_strings_and_every_argument(payload):
    with iv8.Page() as page:
        _load(page)
        page.eval("""globalThis.post_alias=postMessage;globalThis.bridge_reads=0;
            globalThis.bridge_poison=new Proxy({},{get(){bridge_reads++;throw Error('must not inspect');}});""")
        page.start_access_trace(max_events=1)
        assert page.eval("post_alias(" + json.dumps(payload) + ",'*',null,3,4,5,6,7,8,9,10,bridge_poison)") is iv8.JSUndefined
        report = page.read_access_trace(clear=False)
        record, = report["events"]
        assert (record["label"], record["member"], record["operation"]) == ("window", "postMessage", "call")
        assert record["argument_count"] == len(record["argument_summaries"]) == 12
        assert record["argument_summaries"][1:] == ["string:1:*", "null",
            *["number:" + str(index) for index in range(3, 11)], "object"]
        assert record["argument_summaries"][0].startswith("string:" + str(len(payload.encode("utf-8"))) + ":")
        assert record["outcome"] == "returned" and record["return_summary"] == "undefined"
        assert report["coverage"]["string_capture"] == "full" and report["dropped_events"] == 0
        output = page.read_access_trace(clear=False, format="text")
        rendered_arguments = output[len("CALL window.postMessage("):]
        encoded, end = json.JSONDecoder().raw_decode(rendered_arguments)
        assert encoded == payload
        assert rendered_arguments[end:] == ', "*", null, 3, 4, 5, 6, 7, 8, 9, 10, <Object>) -> undefined'
        assert len(output.splitlines()) == 1
        assert page.eval("bridge_reads") == 0


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
        page.eval("performance.now; performance.now(); navigator.notThere;", name="probe.js")
        events = _events(page)
        assert [(event["object_type"], event["member"], event["operation"])
                for event in events] == [
            ("window", "performance", "get"),
            ("performance", "now", "get_method"),
            ("window", "performance", "get"),
            ("performance", "now", "call"),
            ("window", "navigator", "get"),
            ("navigator", "notThere", "get_probe"),
        ]
        method_reference, performance, probe = events[1], events[3], events[5]
        assert method_reference["object_id"] == performance["object_id"]
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
