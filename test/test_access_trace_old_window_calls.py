"""Actual get/call records for the older native Window operation families."""

import json

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")
BASE = "https://old-window-calls.test/"
HTML = "<html><head></head><body></body></html>"
CALLS = [
    ("setTimeout", "setTimeout(()=>callbackHits++,0)", 2, "number"),
    ("clearTimeout", "clearTimeout(activeHandle)", 1, "undefined"),
    ("setInterval", "setInterval(()=>callbackHits++,0)", 2, "number"),
    ("clearInterval", "clearInterval(activeHandle)", 1, "undefined"),
    ("queueMicrotask", "queueMicrotask(()=>callbackHits++)", 1, "undefined"),
    ("requestAnimationFrame", "requestAnimationFrame(()=>callbackHits++)", 1, "number"),
    ("cancelAnimationFrame", "cancelAnimationFrame(activeHandle)", 1, "undefined"),
    ("requestIdleCallback", "requestIdleCallback(()=>callbackHits++,{timeout:0})", 2, "number"),
    ("cancelIdleCallback", "cancelIdleCallback(activeHandle)", 1, "undefined"),
    ("alert", "alert('old callback')", 1, "undefined"),
    ("confirm", "confirm('old callback')", 1, "boolean:false"),
    ("prompt", "prompt('old callback','default')", 2, "null"),
    ("print", "print()", 0, "undefined"),
    ("scroll", "scroll(1,2)", 2, "undefined"),
    ("scrollTo", "scrollTo(1,2)", 2, "undefined"),
    ("scrollBy", "scrollBy(1,2)", 2, "undefined"),
    ("focus", "focus()", 0, "undefined"),
    ("blur", "blur()", 0, "undefined"),
    ("moveBy", "moveBy(1,2)", 2, "undefined"),
    ("moveTo", "moveTo(1,2)", 2, "undefined"),
    ("resizeBy", "resizeBy(1,2)", 2, "undefined"),
    ("resizeTo", "resizeTo(1,2)", 2, "undefined"),
    ("stop", "stop()", 0, "undefined"),
    ("structuredClone", "structuredClone(cloneSource)", 1, "object"),
    ("atob", "atob('YWJj')", 1, "string:3:abc"),
    ("btoa", "btoa('abc')", 1, "string:4:YWJj"),
    ("fetch", "fetch('https://old-window-calls.test/never-sent')", 1, "object"),
]
NAMES = tuple(case[0] for case in CALLS)
SETUP = {
    "clearTimeout": "globalThis.activeHandle=setTimeout(()=>callbackHits++,0)",
    "clearInterval": "globalThis.activeHandle=setInterval(()=>callbackHits++,0)",
    "cancelAnimationFrame": "globalThis.activeHandle=requestAnimationFrame(()=>callbackHits++)",
    "cancelIdleCallback": "globalThis.activeHandle=requestIdleCallback(()=>callbackHits++)",
}
SCHEDULED = {
    "setTimeout": ("run_timers", 1), "clearTimeout": ("run_timers", 0),
    "setInterval": ("run_timers", 1), "clearInterval": ("run_timers", 0),
    "queueMicrotask": ("run_jobs", 1),
    "requestAnimationFrame": ("run_animation_frame", 1), "cancelAnimationFrame": ("run_animation_frame", 0),
    "requestIdleCallback": ("run_idle_callbacks", 1), "cancelIdleCallback": ("run_idle_callbacks", 0),
}


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, BASE)
        value.eval("globalThis.callbackHits=0;globalThis.cloneSource={nested:{value:1}}")
        yield value


def events(page):
    return page.read_access_trace(clear=False)["events"]


def calls(page, member):
    return [event for event in events(page) if event["object_type"] == "window" and event["label"] == "window"
            and event["member"] == member and event["operation"] == "call"]


def test_each_old_function_reference_read_is_observable_without_calling_it(page):
    assert len(set(NAMES)) == len(NAMES) == 27
    page.watch_apis(["window." + name for name in NAMES])
    page.start_access_trace()
    for name in NAMES:
        page.read_access_trace(clear=True)
        value = page.eval("window[" + json.dumps(name) + "]", name="old-window-read.js")
        assert isinstance(value, iv8.JSValue) and value.context_alive
        recorded = [event for event in events(page) if event["object_type"] == "window" and event["label"] == "window"
                    and event["member"] == name and event["operation"] == "get"]
        assert len(recorded) == 1, name
        assert recorded[0]["outcome"] == "returned" and recorded[0]["value_summary"] == "function", name
        assert recorded[0]["resource_name"] == "old-window-read.js" and recorded[0]["source_kind"] == "eval", name
        assert not calls(page, name)
    assert page.read_watch_api_hits() == []
    assert page.eval("callbackHits") == 0


@pytest.mark.parametrize("name,expression,count,summary", CALLS, ids=NAMES)
def test_each_old_callback_has_one_completed_call_and_preserves_behavior(page, name, expression, count, summary):
    if name in SETUP:
        page.eval(SETUP[name])
    page.watch_apis(["window." + name])
    page.start_access_trace()
    page.eval("""globalThis.oldCallResult=(""" + expression + """);
        if(oldCallResult instanceof Promise)oldCallResult.catch(error=>{globalThis.oldCallRejection=error.name;});""",
              name="old-window-call.js")
    assert page.eval("callbackHits") == 0
    record, = calls(page, name)
    assert record["outcome"] == "returned"
    assert record["argument_count"] == count and len(record["argument_summaries"]) == count
    assert record["resource_name"] == "old-window-call.js" and record["source_kind"] == "eval"
    if summary == "number":
        assert record["return_summary"].startswith("number:")
        assert page.eval("typeof oldCallResult==='number' && oldCallResult>0")
    else:
        assert record["return_summary"] == summary
    assert page.read_watch_api_hits() == [{"path": "window." + name, "resource_name": "old-window-call.js", "type": "call"}]
    if name in SCHEDULED:
        pump, expected = SCHEDULED[name]
        getattr(page, pump)()
        assert page.eval("callbackHits") == expected
    if name == "structuredClone":
        assert page.eval("oldCallResult!==cloneSource && oldCallResult.nested!==cloneSource.nested && oldCallResult.nested.value===1")
        page.eval("oldCallResult.nested.value=9")
        assert page.eval("cloneSource.nested.value") == 1
    elif name == "atob":
        assert page.eval("oldCallResult") == "abc"
    elif name == "btoa":
        assert page.eval("oldCallResult") == "YWJj"
    elif name == "confirm":
        assert page.eval("oldCallResult") is False
    elif name == "prompt":
        assert page.eval("oldCallResult") is None
    elif name == "fetch":
        assert page.eval("oldCallResult instanceof Promise")
        before = dict(record)
        page.run_jobs()
        assert page.eval("oldCallRejection") == "NotSupportedError"
        assert calls(page, name) == [before]
    page.stop_access_trace()
    if name == "setInterval":
        page.eval("clearInterval(oldCallResult)")


@pytest.mark.parametrize("name,expression,count", [
    ("atob", "atob('*')", 1), ("btoa", "btoa('\\u0100')", 1),
    ("structuredClone", "structuredClone(()=>{})", 1),
    ("queueMicrotask", "queueMicrotask(1)", 1),
    ("requestIdleCallback", "requestIdleCallback(()=>callbackHits++,{timeout:-1})", 2),
])
def test_old_callback_errors_are_recorded_without_being_swallowed(page, name, expression, count):
    page.watch_apis(["window." + name])
    page.start_access_trace()
    with pytest.raises(iv8.JSError) as error:
        page.eval(expression, name="old-window-error.js")
    assert error.value.name == "TypeError"
    record, = calls(page, name)
    assert record["outcome"] == "threw" and record["exception_name"] == "TypeError"
    assert record["argument_count"] == count and len(record["argument_summaries"]) == count
    assert record["exception_message"] and record["exception_stack"]
    assert "return_summary" not in record
    assert page.read_watch_api_hits() == [{"path": "window." + name, "resource_name": "old-window-error.js", "type": "call"}]
    assert page.eval("callbackHits") == 0


@pytest.mark.parametrize("tracing", [False, True])
def test_new_records_do_not_repeat_conversion_or_clone_getters(page, tracing):
    page.watch_apis(["window.btoa", "window.structuredClone", "window.alert", "window.atob"])
    if tracing:
        page.start_access_trace()
    assert page.eval("""(() => {
        let conversions=0;
        const encoded=btoa({toString(){conversions++;return 'abc';}});
        const clone=structuredClone({get value(){conversions++;return 7;}});
        alert({toString(){throw Error('offline alert must not coerce');}});
        const sentinel=Error('base64 sentinel');let preserved=false;
        try{atob({toString(){conversions++;throw sentinel;}});}catch(error){preserved=error===sentinel;}
        return encoded==='YWJj' && clone.value===7 && conversions===3 && preserved;
    })()""", name="old-window-coercion.js")
    assert [hit["path"] for hit in page.read_watch_api_hits()] == [
        "window.btoa", "window.structuredClone", "window.alert", "window.atob",
    ]
    if tracing:
        for name in ["btoa", "structuredClone", "alert", "atob"]:
            record, = calls(page, name)
            assert record["argument_summaries"] == ["object"]
            assert record["outcome"] == ("threw" if name == "atob" else "returned")
        assert "base64 sentinel" in calls(page, "atob")[0]["exception_message"]


def test_fetch_missing_input_remains_a_rejected_promise_not_a_sync_throw(page):
    page.watch_apis(["window.fetch"])
    page.start_access_trace()
    assert page.eval("""(() => {
        const result=fetch();result.catch(error=>{globalThis.missingFetchError=error.name;});
        return result instanceof Promise;
    })()""", name="old-window-fetch.js")
    record, = calls(page, "fetch")
    assert record["outcome"] == "returned" and record["return_summary"] == "object"
    assert record["argument_count"] == 0 and record["argument_summaries"] == []
    assert "exception_name" not in record
    page.run_jobs()
    assert page.eval("missingFetchError") == "TypeError"
    assert calls(page, "fetch") == [record]


def test_recording_toggles_preserve_function_descriptors_and_identities(page):
    names = json.dumps(NAMES)
    page.eval("globalThis.oldFunctions=" + names + ".map(name=>window[name])")
    shape = """JSON.stringify(Object.fromEntries(""" + names + """.map(name=>{
        const fn=window[name],descriptor=Object.getOwnPropertyDescriptor(window,name);
        return [name,{name:fn.name,length:fn.length,source:Function.prototype.toString.call(fn),
            ownPrototype:Object.hasOwn(fn,'prototype'),data:'value'in descriptor,writable:descriptor.writable,
            enumerable:descriptor.enumerable,configurable:descriptor.configurable}];
    })))"""
    before = json.loads(page.eval(shape))
    assert all(value["data"] for value in before.values())
    page.watch_apis(["window." + name for name in NAMES])
    page.start_access_trace()
    assert json.loads(page.eval(shape)) == before
    assert page.eval(names + ".every((name,index)=>window[name]===oldFunctions[index])")
    page.stop_access_trace()
    page.watch_apis([])
    assert json.loads(page.eval(shape)) == before
    assert page.eval(names + ".every((name,index)=>window[name]===oldFunctions[index])")
    assert page.read_watch_api_hits() == []
