"""Actual watch/structured-trace coverage of the PRES-03 Window slice."""

import json
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")
REFERENCE = json.loads((Path(__file__).parent / "fixtures/presence_03_window/chrome.json").read_text())
BASE = "https://presence-03-monitoring.test/"
HTML = "<html><head></head><body></body></html>"
GLOBAL_CALLS = [
    ("open", "open()", 0, "threw", "NotSupportedError"),
    ("find", "find('text')", 1, "threw", "NotSupportedError"),
    ("getSelection", "getSelection()", 0, "threw", "NotSupportedError"),
    ("getComputedStyle", "getComputedStyle(document.body)", 1, "threw", "NotSupportedError"),
    ("fetchLater", "fetchLater('https://presence-03-monitoring.test/never-sent')", 1, "threw", "NotSupportedError"),
    ("when", "when('click')", 1, "object", None),
    ("webkitRequestFileSystem", "webkitRequestFileSystem(0,0,()=>{})", 3, "threw", "NotSupportedError"),
    ("webkitResolveLocalFileSystemURL", "webkitResolveLocalFileSystemURL('filesystem:never',()=>{})", 2, "threw", "NotSupportedError"),
    ("createImageBitmap", "createImageBitmap(document.createElement('canvas'))", 1, "promise", None),
    ("getScreenDetails", "getScreenDetails()", 0, "promise", None),
    ("queryLocalFonts", "queryLocalFonts()", 0, "promise", None),
    ("showDirectoryPicker", "showDirectoryPicker()", 0, "promise", None),
    ("showOpenFilePicker", "showOpenFilePicker()", 0, "promise", None),
    ("showSaveFilePicker", "showSaveFilePicker()", 0, "promise", None),
    ("captureEvents", "captureEvents()", 0, "undefined", None),
    ("releaseEvents", "releaseEvents()", 0, "undefined", None),
    ("close", "close()", 0, "undefined", None),
    ("requestResize", "requestResize()", 0, "undefined", None),
    ("webkitRequestAnimationFrame", "webkitRequestAnimationFrame(()=>{})", 1, "number", None),
    ("webkitCancelAnimationFrame", "webkitCancelAnimationFrame(0)", 1, "undefined", None),
    ("reportError", "reportError(new Error('monitored report'))", 1, "undefined", None),
    ("NodeFilter", "NodeFilter()", 0, "threw", "TypeError"),
]
SHELL_CALLS = [
    ("CSS", "escape", "shell.escape('a b')", 1, "string"),
    ("chrome", "csi", "shell.csi()", 0, "threw"),
    ("external", "AddSearchProvider", "shell.AddSearchProvider('offline')", 1, "threw"),
    ("caches", "keys", "shell.keys()", 0, "promise"),
    ("cookieStore", "get", "shell.get('offline')", 1, "promise"),
    ("crashReport", "delete", "shell.delete('offline')", 1, "threw"),
    ("customElements", "get", "shell.get('x-offline')", 1, "threw"),
    ("documentPictureInPicture", "requestWindow", "shell.requestWindow()", 0, "promise"),
    ("indexedDB", "cmp", "shell.cmp(1,2)", 2, "threw"),
    ("launchQueue", "setConsumer", "shell.setConsumer(()=>{})", 1, "threw"),
    ("navigation", "entries", "shell.entries()", 0, "threw"),
    ("speechSynthesis", "getVoices", "shell.getVoices()", 0, "threw"),
    ("styleMedia", "matchMedium", "shell.matchMedium('screen')", 1, "threw"),
    ("trustedTypes", "isHTML", "shell.isHTML('offline')", 1, "boolean"),
]


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, BASE)
        yield value


def events(page):
    return page.read_access_trace(clear=False)["events"]


def invoke(page, expression, source="monitored-operation.js"):
    return json.loads(page.eval("""JSON.stringify((() => {
        try {
            const result=(""" + expression + """);
            if (result instanceof Promise) {
                result.catch(() => {});
                return {kind:'promise'};
            }
            return {kind:typeof result,value:typeof result === 'boolean' ? result : undefined};
        } catch (error) {return {kind:'threw',name:error.name};}
    })())""", name=source))


def assert_call_record(record, count, outcome, error_name=None):
    assert record["argument_count"] == count
    assert len(record["argument_summaries"]) == count
    assert record["source_kind"] == "eval"
    assert record["resource_name"] == "monitored-operation.js"
    if outcome == "threw":
        assert record["outcome"] == "threw"
        assert record["exception_name"]
        assert record["exception_message"]
        assert record["exception_stack"]
        assert "return_summary" not in record
        if error_name == "TypeError":
            assert record["exception_name"] == "TypeError"
    else:
        assert record["outcome"] == "returned"
        assert "exception_name" not in record
        expected = "object" if outcome == "promise" else "host_object" if outcome == "object" else "boolean:false" if outcome == "boolean" else outcome
        if expected in {"number", "string"}:
            assert record["return_summary"].startswith(expected + ":")
        else:
            assert record["return_summary"] == expected


def test_direct_global_reads_cover_all_49_without_inventing_watch_calls(page):
    names = list(REFERENCE["members"])
    page.watch_apis(["window." + name for name in names])
    page.start_access_trace()
    for name in names:
        page.read_access_trace(clear=True)
        page.eval("void window[" + json.dumps(name) + "]", name="read-global.js")
        expected_owner = (
            ("host_object", "EventTarget.prototype")
            if REFERENCE["members"][name].get("owner_depth") == 3
            else ("window", "window")
        )
        reads = [record for record in events(page)
                 if (record["object_type"], record["label"]) == expected_owner
                 and record["member"] == name and record["operation"] in {"get", "get_method"}]
        assert len(reads) == 1, name
        record = reads[0]
        assert record["outcome"] == "returned", name
        assert record["value_summary"], name
        assert record["resource_name"] == "read-global.js", name
        assert record["source_kind"] == "eval", name
    assert page.read_watch_api_hits() == []


@pytest.mark.parametrize("name,expression,count,outcome,error_name", GLOBAL_CALLS, ids=[row[0] for row in GLOBAL_CALLS])
def test_each_global_call_has_watch_hit_and_completed_trace(page, name, expression, count, outcome, error_name):
    page.eval("addEventListener('error', event => event.preventDefault())")
    page.watch_apis(["window." + name])
    page.start_access_trace()
    result = invoke(page, expression)
    assert result["kind"] == outcome
    if error_name:
        assert result["name"] == error_name
    hits = page.read_watch_api_hits()
    assert hits == [{"path": "window." + name, "resource_name": "monitored-operation.js", "type": "call"}]
    calls = [record for record in events(page)
             if record["object_type"] == "window" and record["member"] == name and record["operation"] == "call"]
    assert len(calls) == 1
    assert_call_record(calls[0], count, outcome, error_name)


@pytest.mark.parametrize("owner,member,expression,count,outcome", SHELL_CALLS, ids=[row[0]+'.'+row[1] for row in SHELL_CALLS])
def test_namespace_and_instance_methods_record_real_calls(page, owner, member, expression, count, outcome):
    page.eval("globalThis.shell=window[" + json.dumps(owner) + "]")
    page.watch_apis([owner + "." + member])
    page.start_access_trace()
    result = invoke(page, expression)
    assert result["kind"] == outcome
    if outcome == "threw":
        assert result["name"] == "NotSupportedError"
    elif outcome == "boolean":
        assert result["value"] is False
    assert page.read_watch_api_hits() == [{"path": owner+"."+member, "resource_name": "monitored-operation.js", "type": "call"}]
    calls = [record for record in events(page)
             if record["object_type"] == "window_shell" and record["label"] == owner
             and record["member"] == member and record["operation"] == "call"]
    assert len(calls) == 1
    assert_call_record(calls[0], count, outcome)


@pytest.mark.parametrize("owner,member,expected", [
    ("styleMedia", "type", "string:6:screen"),
    ("trustedTypes", "defaultPolicy", "null"),
    ("CSS", "highlights", None),
    ("navigation", "currentEntry", None),
    ("viewport", "segments", None),
])
def test_namespace_property_getters_have_completed_structured_records(page, owner, member, expected):
    page.eval("globalThis.shell=window[" + json.dumps(owner) + "]")
    page.watch_apis([owner + "." + member])
    page.start_access_trace()
    invoke(page, "shell[" + json.dumps(member) + "]")
    gets = [record for record in events(page)
            if record["object_type"] == "window_shell" and record["label"] == owner
            and record["member"] == member and record["operation"] == "get"]
    assert len(gets) == 1
    if expected is None:
        assert gets[0]["outcome"] == "threw"
        assert gets[0]["exception_name"] and gets[0]["exception_message"]
        assert "value_summary" not in gets[0]
    else:
        assert gets[0]["outcome"] == "returned"
        assert gets[0]["value_summary"] == expected
    assert page.read_watch_api_hits() == []


def test_plain_nested_data_reads_remain_outside_native_trace_scope(page):
    page.eval("globalThis.rawNamespaceApp=chrome.app")
    page.watch_apis(["chrome.app"])
    page.start_access_trace()
    assert page.eval("rawNamespaceApp.monitoringValue=7;rawNamespaceApp.monitoringValue") == 7
    assert events(page) == []
    assert page.read_watch_api_hits() == []


def test_initial_replaceable_and_shell_handler_writes_are_recorded(page):
    page.watch_apis(["window.event", "window.offscreenBuffering", "window.viewport", "cookieStore.onchange"])
    page.start_access_trace()
    assert page.eval("""(() => {
        const marker={toString(){throw Error('metadata must not coerce');}};
        const callback=()=>{};
        offscreenBuffering=marker;viewport=marker;window.event=marker;
        cookieStore.onchange=callback;
        return offscreenBuffering===marker && viewport===marker && window.event===marker && cookieStore.onchange===callback;
    })()""", name="write-properties.js")
    for member in ["offscreenBuffering", "viewport", "event"]:
        writes = [record for record in events(page)
                  if record["object_type"] == "window" and record["member"] == member and record["operation"] == "set"]
        assert len(writes) == 1, member
        assert writes[0]["outcome"] == "returned" and writes[0]["value_summary"] == "object"
    shell_writes = [record for record in events(page)
                    if record["object_type"] == "window_shell" and record["label"] == "cookieStore"
                    and record["member"] == "onchange" and record["operation"] == "set"]
    assert len(shell_writes) == 1
    assert shell_writes[0]["value_summary"] == "function" and shell_writes[0]["outcome"] == "returned"
    assert page.read_watch_api_hits() == []


def test_promises_are_recorded_as_returned_values_not_settlement(page):
    page.watch_apis(["window.getScreenDetails", "caches.keys"])
    page.start_access_trace()
    page.eval("""globalThis.rejections=[];
        getScreenDetails().catch(error => rejections.push(error.name));
        caches.keys().catch(error => rejections.push(error.name));""", name="promise-calls.js")
    before = [record for record in events(page)
              if record["operation"] == "call" and record["member"] in {"getScreenDetails", "keys"}]
    assert len(before) == 2
    assert all(record["outcome"] == "returned" and record["return_summary"] == "object" for record in before)
    assert all("exception_name" not in record for record in before)
    page.run_jobs()
    assert json.loads(page.eval("JSON.stringify(rejections)")) == ["NotSupportedError", "NotSupportedError"]
    after = [record for record in events(page)
             if record["operation"] == "call" and record["member"] in {"getScreenDetails", "keys"}]
    assert after == before
    assert [hit["path"] for hit in page.read_watch_api_hits()] == ["window.getScreenDetails", "caches.keys"]


@pytest.mark.parametrize("tracing", [False, True])
def test_metadata_does_not_repeat_coercion_or_swallow_original_exception(page, tracing):
    page.watch_apis(["CSS.escape", "window.webkitCancelAnimationFrame"])
    if tracing:
        page.start_access_trace()
    assert page.eval("""(() => {
        let coercions=0;
        const escaped=CSS.escape({toString(){coercions++;return 'a b';}});
        webkitCancelAnimationFrame({valueOf(){coercions++;return 0;}});
        const sentinel=Error('coercion sentinel');let caught=false;
        try {CSS.escape({toString(){coercions++;throw sentinel;}});} catch(error) {caught=error===sentinel;}
        return escaped==='a\\\\ b' && coercions===3 && caught;
    })()""", name="coercion.js")
    assert [hit["path"] for hit in page.read_watch_api_hits()] == [
        "CSS.escape", "window.webkitCancelAnimationFrame", "CSS.escape",
    ]
    if tracing:
        calls = [record for record in events(page) if record["operation"] == "call" and record["member"] == "escape"]
        assert len(calls) == 2
        assert calls[0]["argument_summaries"] == ["object"] and calls[0]["outcome"] == "returned"
        assert calls[1]["argument_summaries"] == ["object"] and calls[1]["outcome"] == "threw"
        assert "coercion sentinel" in calls[1]["exception_message"]


def test_monitors_preserve_global_shapes_descriptors_and_identities(page):
    names = json.dumps(list(REFERENCE["members"]))
    shape = """JSON.stringify((() => {
        const output={};
        const functionShape=fn => ({name:fn.name,length:fn.length,source:Function.prototype.toString.call(fn),prototype:Object.hasOwn(fn,'prototype')});
        for(const name of """ + names + """) {
            let owner=window;while(owner && !Object.getOwnPropertyDescriptor(owner,name))owner=Object.getPrototypeOf(owner);
            const descriptor=Object.getOwnPropertyDescriptor(owner,name);
            const result={enumerable:descriptor.enumerable,configurable:descriptor.configurable};
            if('value' in descriptor){result.kind='data';result.writable=descriptor.writable;}
            else{result.kind='accessor';result.get=descriptor.get&&functionShape(descriptor.get);result.set=descriptor.set&&functionShape(descriptor.set);}
            if(typeof window[name]==='function')result.function=functionShape(window[name]);
            output[name]=result;
        }
        return output;
    })())"""
    page.eval("globalThis.originalWindowMembers=" + names + ".map(name=>window[name])")
    baseline = json.loads(page.eval(shape))
    page.watch_apis(["window.close", "CSS.escape"])
    page.start_access_trace()
    assert json.loads(page.eval(shape)) == baseline
    assert page.eval(names + ".every((name,index)=>window[name]===originalWindowMembers[index])")
    page.stop_access_trace()
    page.watch_apis([])
    assert json.loads(page.eval(shape)) == baseline
    assert page.eval(names + ".every((name,index)=>window[name]===originalWindowMembers[index])")


def test_filters_capacity_and_stopped_trace_keep_watch_channel_independent(page):
    page.eval("globalThis.cachedCss=CSS")
    page.watch_apis(["CSS.escape", "window.close"])
    page.start_access_trace(max_events=2, object_types=["window"])
    page.eval("credentialless;credentialless;credentialless;credentialless;credentialless;cachedCss.escape('a')")
    report = page.read_access_trace(clear=False)
    assert [(record["member"], record["operation"]) for record in report["events"]] == [
        ("credentialless", "get"), ("credentialless", "get"),
    ]
    assert report["dropped_events"] == 3
    assert [hit["path"] for hit in page.read_watch_api_hits()] == ["CSS.escape"]
    page.stop_access_trace()
    page.eval("close();cachedCss.escape('b')")
    assert events(page) == report["events"]
    assert [hit["path"] for hit in page.read_watch_api_hits()] == ["window.close", "CSS.escape"]
    page.watch_apis([])
    page.eval("close();cachedCss.escape('c')")
    assert page.read_watch_api_hits() == []


def test_monitoring_configuration_survives_load_and_is_isolated_by_page(page):
    page.watch_apis(["window.close", "CSS.escape"])
    page.start_access_trace()
    page.eval("close();CSS.escape('a')", name="first-generation.js")
    first = [record for record in events(page) if record["operation"] == "call" and record["member"] in {"close", "escape"}]
    assert len(first) == 2
    with iv8.Page() as other:
        other.eval("close();CSS.escape('other')")
        assert other.read_watch_api_hits() == [] and other.read_access_trace()["events"] == []
    page.load(HTML, BASE)
    page.eval("close();CSS.escape('b')", name="second-generation.js")
    later = [record for record in events(page) if record["operation"] == "call"
             and record["resource_name"] == "second-generation.js" and record["member"] in {"close", "escape"}]
    assert len(later) == 2
    for member in ["close", "escape"]:
        before = next(record for record in first if record["member"] == member)
        after = next(record for record in later if record["member"] == member)
        assert before["generation"] != after["generation"] and before["object_id"] != after["object_id"]
    assert [hit["path"] for hit in page.read_watch_api_hits()] == [
        "window.close", "CSS.escape", "window.close", "CSS.escape",
    ]


def test_break_on_hit_without_inspector_connection_records_without_pause(page):
    page.watch_apis(["window.close", "CSS.escape"], break_on_hit=True)
    page.start_access_trace()
    page.eval("close();CSS.escape('a')")
    assert [hit["path"] for hit in page.read_watch_api_hits()] == ["window.close", "CSS.escape"]
    assert page._native.devtools_pause_count() == 0
    assert len([record for record in events(page) if record["operation"] == "call" and record["member"] in {"close", "escape"}]) == 2
