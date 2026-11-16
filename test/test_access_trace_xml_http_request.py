"""XHR-01 records native placeholder use without transport or stdout in prod."""

import json

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")
BASE = "https://xml-http-request.test/page/"
HTML = "<html><head></head><body></body></html>"
METHODS = ["abort", "getAllResponseHeaders", "getResponseHeader", "open", "overrideMimeType",
           "send", "setRequestHeader", "setAttributionReporting", "setPrivateToken"]


def bind_xhr(page):
    page.eval("""globalThis.xhr_ctor=XMLHttpRequest;globalThis.xhr=new XMLHttpRequest();
        globalThis.upload=xhr.upload;globalThis.xhr_proto=XMLHttpRequest.prototype;
        globalThis.xhr_event_proto=XMLHttpRequestEventTarget.prototype;
        globalThis.cached_send=xhr.send;globalThis.xhr_handler=()=>{};
        globalThis.xhr_effects=[];globalThis.xhr_sentinel=Error('must not run');
        globalThis.xhr_trap=()=>{xhr_effects.push('trap');throw xhr_sentinel;};
        globalThis.xhr_poison=new Proxy({},{get:xhr_trap,getPrototypeOf:xhr_trap,
            getOwnPropertyDescriptor:xhr_trap,ownKeys:xhr_trap,has:xhr_trap});
        globalThis.xhr_receiver_proxy=new Proxy(xhr,{get:xhr_trap,getPrototypeOf:xhr_trap,
            getOwnPropertyDescriptor:xhr_trap,ownKeys:xhr_trap,has:xhr_trap});""")


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, BASE)
        bind_xhr(value)
        yield value


def events(page):
    return page.read_access_trace(clear=False)["events"]


def keys(records):
    return [(event["object_type"], event["label"], event["member"], event["operation"])
            for event in records]


def test_cached_constructor_records_one_user_call_without_internal_upload_activity(page):
    page.watch_apis(["window.XMLHttpRequest"])
    page.start_access_trace(max_events=1)
    result = page.eval("new xhr_ctor()", name="xhr-constructor.js")
    assert isinstance(result, iv8.JSValue) and result.context_alive
    record, = events(page)
    assert keys([record]) == [("window", "window", "XMLHttpRequest", "call")]
    assert record["outcome"] == "returned" and record["return_summary"] == "host_object"
    assert record["argument_count"] == 0 and record["resource_name"] == "xhr-constructor.js"
    assert page.read_access_trace(clear=False)["dropped_events"] == 0
    assert [hit["path"] for hit in page.read_watch_api_hits()] == ["window.XMLHttpRequest"]


@pytest.mark.parametrize("method", METHODS)
def test_every_xhr_prototype_method_reference_has_a_completed_get_without_watch_hit(page, method):
    page.eval("globalThis.xhr_saved_method=xhr_proto[" + json.dumps(method) + "]")
    page.watch_apis(["XMLHttpRequest." + method])
    page.start_access_trace(max_events=1)
    assert page.eval("xhr_proto[" + json.dumps(method) + "]===xhr_saved_method", name="xhr-reference.js")
    record, = events(page)
    assert keys([record]) == [("host_object", "XMLHttpRequest.prototype", method, "get")]
    assert record["outcome"] == "returned" and record["value_summary"] == "function"
    assert record["resource_name"] == "xhr-reference.js" and record["source_kind"] == "eval"
    assert page.read_access_trace(clear=False)["dropped_events"] == 0
    assert page.read_watch_api_hits() == []


def test_instance_reference_cached_calls_and_direct_calls_keep_folding_and_identity(page):
    page.watch_apis(["XMLHttpRequest.send"])
    page.start_access_trace(max_events=4)
    assert page.eval("""(() => {
        const reference=xhr.send,initial=xhr.readyState;
        const cached=cached_send.call(xhr,'cached'),direct=xhr.send('direct');
        return reference===cached_send && initial===0 && cached===undefined && direct===undefined;
    })()""", name="xhr-calls.js")
    records = events(page)
    assert keys(records) == [
        ("xml_http_request", "XMLHttpRequest", "send", "get_method"),
        ("xml_http_request", "XMLHttpRequest", "readyState", "get"),
        ("xml_http_request", "XMLHttpRequest", "send", "call"),
        ("xml_http_request", "XMLHttpRequest", "send", "call"),
    ]
    assert len({event["object_id"] for event in records}) == 1
    assert [event["argument_summaries"] for event in records[2:]] == [["string:6:cached"], ["string:6:direct"]]
    assert all(event["outcome"] == "returned" and event["return_summary"] == "undefined"
               for event in records[2:])
    assert [hit["path"] for hit in page.read_watch_api_hits()] == ["XMLHttpRequest.send"] * 2
    assert page.read_access_trace(clear=False)["dropped_events"] == 0


def test_configuration_and_upload_handler_get_set_completions_have_distinct_identities(page):
    page.watch_apis(["XMLHttpRequest.timeout", "XMLHttpRequestUpload.onprogress"])
    page.start_access_trace(max_events=8)
    assert page.eval("""xhr.timeout=12;xhr.withCredentials=true;xhr.responseType='json';upload.onprogress=xhr_handler;
        xhr.timeout===12 && xhr.withCredentials && xhr.responseType==='json' && upload.onprogress===xhr_handler;
    """, name="xhr-fields.js")
    records = events(page)
    assert keys(records) == [
        ("xml_http_request", "XMLHttpRequest", "timeout", "set"),
        ("xml_http_request", "XMLHttpRequest", "withCredentials", "set"),
        ("xml_http_request", "XMLHttpRequest", "responseType", "set"),
        ("xml_http_request_upload", "XMLHttpRequestUpload", "onprogress", "set"),
        ("xml_http_request", "XMLHttpRequest", "timeout", "get"),
        ("xml_http_request", "XMLHttpRequest", "withCredentials", "get"),
        ("xml_http_request", "XMLHttpRequest", "responseType", "get"),
        ("xml_http_request_upload", "XMLHttpRequestUpload", "onprogress", "get"),
    ]
    assert [event["value_summary"] for event in records] == [
        "number:12", "boolean:true", "string:4:json", "function"] * 2
    assert len({event["object_id"] for event in records if event["object_type"] == "xml_http_request"}) == 1
    assert records[3]["object_id"] == records[7]["object_id"] != records[0]["object_id"]
    assert all(event["outcome"] == "returned" and event["resource_name"] == "xhr-fields.js" for event in records)
    assert page.read_watch_api_hits() == []


def test_response_getter_guard_records_its_original_synchronous_exception(page):
    page.eval("xhr.responseType='json'")
    page.start_access_trace(max_events=1)
    with pytest.raises(iv8.JSError) as caught:
        page.eval("xhr.responseText", name="xhr-guard.js")
    assert caught.value.name == "InvalidStateError"
    record, = events(page)
    assert keys([record]) == [("xml_http_request", "XMLHttpRequest", "responseText", "get")]
    # The existing safe diagnostic reports the native Error constructor name;
    # the public JS error retains its InvalidStateError name and message.
    assert record["outcome"] == "threw" and record["exception_name"] == "Error"
    assert "InvalidStateError" in record["exception_message"]
    assert caught.value.message in record["exception_message"]
    assert record["resource_name"] == "xhr-guard.js" and "return_summary" not in record
    assert page.read_access_trace(clear=False)["dropped_events"] == 0


@pytest.mark.parametrize("setup,expression,owner,member,operation,arguments", [
    ("globalThis.xhr_bad_fn=xhr_proto.open", "xhr_bad_fn.call({},xhr_poison,xhr_poison)",
     "XMLHttpRequest", "open", "call", ["object", "object"]),
    ("globalThis.xhr_bad_fn=xhr_proto.send", "xhr_bad_fn.call(xhr_receiver_proxy,xhr_poison)",
     "XMLHttpRequest", "send", "call", ["object"]),
    ("globalThis.xhr_bad_fn=Object.getOwnPropertyDescriptor(xhr_proto,'readyState').get", "xhr_bad_fn.call({})",
     "XMLHttpRequest", "readyState", "get", None),
    ("globalThis.xhr_bad_fn=Object.getOwnPropertyDescriptor(xhr_proto,'timeout').set", "xhr_bad_fn.call({},xhr_poison)",
     "XMLHttpRequest", "timeout", "set", None),
    ("globalThis.xhr_bad_fn=Object.getOwnPropertyDescriptor(xhr_event_proto,'onprogress').get", "xhr_bad_fn.call({})",
     "XMLHttpRequestEventTarget", "onprogress", "get", None),
    ("globalThis.xhr_bad_fn=Object.getOwnPropertyDescriptor(xhr_event_proto,'onprogress').set", "xhr_bad_fn.call({},xhr_poison)",
     "XMLHttpRequestEventTarget", "onprogress", "set", None),
], ids=["open", "send-proxy", "readonly-getter", "timeout-setter", "shared-handler-getter", "shared-handler-setter"])
def test_rejected_xhr_native_receivers_record_before_original_brand_errors_without_traps(
        page, setup, expression, owner, member, operation, arguments):
    page.eval(setup)
    page.start_access_trace(max_events=1)
    with pytest.raises(iv8.JSError) as caught:
        page.eval(expression, name="xhr-brand.js")
    assert caught.value.name == "TypeError" and caught.value.message == "Illegal invocation"
    record, = events(page)
    assert keys([record]) == [("host_object", owner + ".prototype", member, operation)]
    assert record["outcome"] == "threw" and record["exception_name"] == "TypeError"
    assert "Illegal invocation" in record["exception_message"] and record["resource_name"] == "xhr-brand.js"
    if arguments is not None:
        assert record["argument_summaries"] == arguments and record["argument_count"] == len(arguments)
    if operation == "set":
        assert record["value_summary"] == "object"
    assert page.read_access_trace(clear=False)["dropped_events"] == 0
    page.stop_access_trace()
    assert page.eval("xhr_effects.length") == 0


@pytest.mark.parametrize("payload_kind", ["proxy", "error"])
def test_valid_placeholder_argument_summaries_never_inspect_application_objects(page, payload_kind):
    if payload_kind == "proxy":
        page.eval("globalThis.xhr_payload=xhr_poison")
    else:
        page.eval("""globalThis.xhr_payload=Error('payload');
            for(const name of ['name','message','stack'])Object.defineProperty(xhr_payload,name,
                {get:xhr_trap,configurable:true});""")
    page.start_access_trace(max_events=1)
    assert page.eval("cached_send.call(xhr,xhr_payload)") is iv8.JSUndefined
    record, = events(page)
    assert keys([record]) == [("xml_http_request", "XMLHttpRequest", "send", "call")]
    assert record["argument_summaries"] == ["object"] and record["argument_count"] == 1
    assert record["outcome"] == "returned" and record["return_summary"] == "undefined"
    page.stop_access_trace()
    assert page.eval("xhr_effects.length") == 0


def test_debug_prints_native_send_parameters_once_and_prod_is_silent(capsys):
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        assert capsys.readouterr().out == ""
        bind_xhr(page)
        capsys.readouterr()
        assert page.eval("xhr.send('payload',{marker:1})") is iv8.JSUndefined
        assert capsys.readouterr().out.splitlines() == ['CALL XMLHttpRequest.send("payload", <Object>) -> undefined']
        assert page.read_access_trace()["events"] == []
        page.load(HTML, BASE, mode="prod")
        bind_xhr(page)
        assert page.eval("xhr.send('payload',{marker:1})") is iv8.JSUndefined
        page.run_jobs()
        page.run_timers()
        assert capsys.readouterr().out == ""
        assert not page.read_access_trace()["enabled"]


def test_debug_send_prints_full_long_values_all_arguments_and_no_object_side_effects(capsys):
    body = ("请求😀\n\t\\\0" * 700) + "..."
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        bind_xhr(page)
        capsys.readouterr()
        call = "xhr.send(" + json.dumps(body) + ",1,2,3,4,5,6,7,8,9,xhr_poison)"
        assert page.eval(call) is iv8.JSUndefined
        lines = capsys.readouterr().out.splitlines()
        assert len(lines) == 1 and lines[0].startswith("CALL XMLHttpRequest.send(")
        rendered_arguments = lines[0][len("CALL XMLHttpRequest.send("):]
        actual_body, end = json.JSONDecoder().raw_decode(rendered_arguments)
        assert actual_body == body
        assert rendered_arguments[end:] == ", 1, 2, 3, 4, 5, 6, 7, 8, 9, <Object>) -> undefined"
        assert page.eval("xhr_effects.length") == 0
        page.load(HTML, BASE, mode="prod")
        bind_xhr(page)
        assert page.eval(call) is iv8.JSUndefined
        assert page.eval("xhr_effects.length") == 0
        assert capsys.readouterr().out == ""


@pytest.mark.parametrize("target,object_type,label", [
    ("xhr", "xml_http_request", "XMLHttpRequest"),
    ("upload", "xml_http_request_upload", "XMLHttpRequestUpload"),
])
def test_manual_handler_user_access_is_traced_without_runtime_event_maintenance(
        page, target, object_type, label):
    page.eval("""globalThis.xhr_target=""" + target + """;
        globalThis.xhr_dispatch=xhr_target.dispatchEvent;globalThis.xhr_event=new Event('progress');
        globalThis.xhr_seen=[];xhr_target.onprogress=event=>xhr_seen.push(event.type,xhr.status);""")
    page.start_access_trace(max_events=3)
    assert page.eval("xhr_dispatch.call(xhr_target,xhr_event)")
    records = events(page)
    assert keys(records) == [
        (object_type, label, "dispatchEvent", "call"),
        ("host_object", "Event", "type", "get"),
        ("xml_http_request", "XMLHttpRequest", "status", "get"),
    ]
    assert all(event["outcome"] == "returned" for event in records)
    assert page.read_access_trace(clear=False)["dropped_events"] == 0
    page.stop_access_trace()
    assert page.eval("xhr_seen", to_py=True) == ["progress", 0]


def test_constant_reads_and_readonly_rejections_do_not_fabricate_set_events(page):
    page.start_access_trace(max_events=3)
    assert page.eval("""(() => {
        const values=[xhr_ctor.OPENED,xhr.OPENED,xhr.readyState];xhr.status=77;let rejected=false;
        try{(function(){'use strict';xhr.status=77;})()}catch(error){rejected=error instanceof TypeError;}
        return rejected && values.join(',')==='1,1,0';
    })()""")
    assert keys(events(page)) == [
        ("host_object", "XMLHttpRequest", "OPENED", "get"),
        ("host_object", "XMLHttpRequest.prototype", "OPENED", "get"),
        ("xml_http_request", "XMLHttpRequest", "readyState", "get"),
    ]
    assert page.read_access_trace(clear=False)["dropped_events"] == 0


def test_xhr_and_upload_type_filters_capacity_and_stop_keep_channels_separate(page):
    page.start_access_trace(max_events=2, object_types=["xml_http_request"])
    page.eval("upload.onprogress;xhr.timeout;xhr.status;xhr.readyState")
    before = page.read_access_trace(clear=False)
    assert keys(before["events"]) == [
        ("xml_http_request", "XMLHttpRequest", "timeout", "get"),
        ("xml_http_request", "XMLHttpRequest", "status", "get"),
    ]
    assert before["dropped_events"] == 1
    page.stop_access_trace()
    assert page.eval("xhr.send('stopped')") is iv8.JSUndefined
    after = page.read_access_trace(clear=False)
    assert after["events"] == before["events"] and after["dropped_events"] == 1
    page.start_access_trace(object_types=["xml_http_request_upload"])
    assert page.eval("xhr.status;upload.onprogress=xhr_handler;upload.onprogress===xhr_handler")
    assert keys(events(page)) == [
        ("xml_http_request_upload", "XMLHttpRequestUpload", "onprogress", "set"),
        ("xml_http_request_upload", "XMLHttpRequestUpload", "onprogress", "get"),
    ]


def test_monitoring_generation_and_page_isolation_preserve_new_upload_identities(page):
    old_xhr, old_upload = page.eval("xhr"), page.eval("upload")
    page.start_access_trace(object_types=["xml_http_request", "xml_http_request_upload"])
    page.eval("xhr.timeout;upload.onprogress")
    before = events(page)
    assert len(before) == 2 and before[0]["object_id"] != before[1]["object_id"]
    with iv8.Page() as other:
        other.load(HTML, BASE)
        bind_xhr(other)
        assert other.read_access_trace()["events"] == []
        page.load(HTML, BASE)
        bind_xhr(page)
        page.read_access_trace(clear=True)
        page.eval("xhr.timeout;upload.onprogress")
        after = events(page)
        assert keys(after) == keys(before)
        assert {event["generation"] for event in after} == {before[0]["generation"] + 1}
        assert {event["object_id"] for event in before}.isdisjoint(event["object_id"] for event in after)
        assert not old_xhr.context_alive and not old_upload.context_alive
