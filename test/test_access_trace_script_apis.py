"""JS-hosted APIs record real entries without inspecting application objects."""

import json

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")
BASE = "https://script-apis.test/"
HTML = '<html><head></head><body><div id="d" style="color: red; width: 20px !important"></div></body></html>'


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, BASE)
        value.eval("globalThis.postAlias=postMessage;globalThis.taskAlias=scheduler.postTask;globalThis.mediaAlias=matchMedia")
        yield value


def events(page):
    return page.read_access_trace(clear=False)["events"]


def records(page, label, member, operation="call"):
    return [event for event in events(page) if event["label"] == label and event["member"] == member
            and event["operation"] == operation]


def begin(page, *paths):
    page.watch_apis(list(paths))
    page.start_access_trace()


def watch_paths(page):
    return [hit["path"] for hit in page.read_watch_api_hits()]


def test_cached_post_message_records_once_and_preserves_async_clone_delivery(page):
    page.eval("globalThis.received=[];addEventListener('message',event=>received.push(event.data))")
    begin(page, "window.postMessage")
    assert page.eval("const message={value:1};postAlias(message,'*');message.value=2;received.length", name="script-api.js") == 0
    record, = records(page, "window", "postMessage")
    assert record["argument_count"] == 2 and record["argument_summaries"] == ["object", "string:1:*"]
    assert record["outcome"] == "returned" and record["return_summary"] == "undefined"
    assert watch_paths(page) == ["window.postMessage"]
    page.run_timers()
    assert page.eval("received.length===1 && received[0].value===1")
    assert records(page, "window", "postMessage") == [record]


@pytest.mark.parametrize("expression,count", [
    ("postAlias()", 0), ("postAlias('x','*',[{}])", 3), ("postAlias(()=>{},'*')", 2),
])
def test_post_message_validation_errors_have_one_failed_entry(page, expression, count):
    begin(page, "window.postMessage")
    with pytest.raises(iv8.JSError) as error:
        page.eval(expression, name="script-api.js")
    assert error.value.name == "TypeError"
    record, = records(page, "window", "postMessage")
    assert record["outcome"] == "threw" and record["argument_count"] == count
    assert "return_summary" not in record
    assert watch_paths(page) == ["window.postMessage"]


def test_post_message_clone_getter_keeps_original_error_identity(page):
    begin(page, "window.postMessage")
    assert page.eval("""(() => {
        const original=Error('clone getter error');let reads=0,caught=false;
        try{postAlias({get value(){reads++;throw original;}},'*');}
        catch(error){caught=error===original;}
        return caught && reads===1;
    })()""", name="script-api.js")
    record, = records(page, "window", "postMessage")
    assert record["outcome"] == "threw" and "return_summary" not in record
    assert record["argument_summaries"] == ["object", "string:1:*"]
    assert watch_paths(page) == ["window.postMessage"]


def test_cached_scheduler_returns_promise_and_call_record_does_not_track_settlement(page):
    page.eval("globalThis.taskResult=null")
    begin(page, "scheduler.postTask")
    assert page.eval("""(() => {
        const result=taskAlias(()=>17,{delay:5});result.then(value=>{taskResult=value;});
        return result instanceof Promise;
    })()""", name="script-api.js")
    record, = records(page, "scheduler", "postTask")
    assert record["argument_count"] == 2 and record["argument_summaries"] == ["function", "object"]
    assert record["outcome"] == "returned" and record["return_summary"] == "object"
    assert watch_paths(page) == ["scheduler.postTask"]
    assert page.eval("taskResult") is None
    page.advance(4, max_steps=10)
    assert page.eval("taskResult") is None
    page.advance(1, max_steps=10)
    assert page.eval("taskResult") == 17
    assert records(page, "scheduler", "postTask") == [record]


@pytest.mark.parametrize("kind", ["callback", "delay", "undefined", "error"])
def test_scheduler_sync_errors_keep_undefined_and_error_identity(page, kind):
    begin(page, "scheduler.postTask")
    if kind in {"callback", "delay"}:
        expression = "taskAlias(1)" if kind == "callback" else "taskAlias(()=>7,{delay:-1})"
        with pytest.raises(iv8.JSError) as error:
            page.eval(expression, name="script-api.js")
        assert error.value.name == "TypeError"
    else:
        thrown = "undefined" if kind == "undefined" else "Error('original scheduler error')"
        assert page.eval("""(() => {
            const original=""" + thrown + """;let reads=0,caught=false;
            try{taskAlias(()=>7,{get delay(){reads++;throw original;}});}
            catch(error){caught=error===original;}
            return caught && reads===1;
        })()""", name="script-api.js")
    record, = records(page, "scheduler", "postTask")
    assert record["outcome"] == "threw" and "return_summary" not in record
    if kind == "undefined":
        assert record["value_summary"] == "undefined"
    assert watch_paths(page) == ["scheduler.postTask"]


@pytest.mark.parametrize("throws", [False, True])
def test_match_media_conversion_is_once_and_entry_is_not_duplicated(page, throws):
    begin(page, "window.matchMedia")
    assert page.eval("""(() => {
        const original=Error('media conversion');let converted=0,caught=false,result;
        try{result=mediaAlias({toString(){converted++;""" + ("throw original;" if throws else "return '(min-width: 1px)';") + """}});}
        catch(error){caught=error===original;}
        return converted===1 && """ + ("caught" if throws else "result.matches===true") + ";})()", name="script-api.js")
    record, = records(page, "window", "matchMedia")
    assert record["outcome"] == ("threw" if throws else "returned")
    assert record["argument_summaries"] == ["object"]
    assert watch_paths(page) == ["window.matchMedia"]


@pytest.mark.parametrize("member", ["sendBeacon", "getBattery", "queryUsageAndQuota", "requestQuota"])
def test_navigator_and_quota_entries_have_real_completed_calls(page, member):
    page.eval("globalThis.nav=navigator;globalThis.quota=nav.webkitPersistentStorage")
    label = "navigator" if member in {"sendBeacon", "getBattery"} else "DeprecatedStorageQuota"
    begin(page, label + "." + member)
    if member == "sendBeacon":
        assert page.eval("nav.sendBeacon('/never-sent','data')", name="script-api.js") is False
        expected = "boolean:false"
    elif member == "getBattery":
        assert page.eval("nav.getBattery().then(value=>{globalThis.batteryResult=value;}) instanceof Promise", name="script-api.js")
        expected = "object"
    elif member == "queryUsageAndQuota":
        page.eval("quota.queryUsageAndQuota((used,total)=>{globalThis.quotaResult=[used,total];},()=>{})", name="script-api.js")
        expected = "undefined"
    else:
        page.eval("quota.requestQuota(1,value=>{globalThis.quotaGranted=value;},error=>{globalThis.quotaFailure=error.name;})", name="script-api.js")
        expected = "undefined"
    record, = records(page, label, member)
    assert record["outcome"] == "returned" and record["return_summary"] == expected
    assert watch_paths(page) == [label + "." + member]
    page.run_jobs()
    if member in {"queryUsageAndQuota", "requestQuota"}:
        assert page.run_until_idle()
    if member == "getBattery":
        assert page.eval("batteryResult.charging===true && batteryResult.level===1")
    elif member == "queryUsageAndQuota":
        assert json.loads(page.eval("JSON.stringify(quotaResult)")) == [0, 10 * 1024**3]
    elif member == "requestQuota":
        assert page.eval("quotaGranted===1 && typeof quotaFailure==='undefined'")
    assert records(page, label, member) == [record]


def test_battery_fields_and_three_event_methods_are_traced(page):
    page.eval("navigator.getBattery().then(value=>{globalThis.battery=value;})")
    page.run_jobs()
    begin(page, "BatteryManager.addEventListener", "BatteryManager.removeEventListener", "BatteryManager.dispatchEvent")
    assert page.eval("""(() => {
        const values=[battery.charging,battery.chargingTime,battery.dischargingTime,battery.level];
        let calls=0;const callback=()=>{calls++;};
        battery.addEventListener('levelchange',callback);
        battery.dispatchEvent(new Event('levelchange'));
        battery.removeEventListener('levelchange',callback);
        battery.dispatchEvent(new Event('levelchange'));
        return values[0]===true && values[1]===0 && values[2]===Infinity && values[3]===1 && calls===1;
    })()""", name="script-api.js")
    for member, summary in [("charging", "boolean:true"), ("chargingTime", "number:0"),
                            ("dischargingTime", "number:inf"), ("level", "number:1")]:
        record, = records(page, "BatteryManager", member, "get")
        assert record["outcome"] == "returned" and record["value_summary"] == summary
    assert [(record["member"], record["outcome"]) for record in events(page)
            if record["label"] == "BatteryManager" and record["operation"] == "call"] == [
        ("addEventListener", "returned"), ("dispatchEvent", "returned"),
        ("removeEventListener", "returned"), ("dispatchEvent", "returned"),
    ]
    assert watch_paths(page) == ["BatteryManager.addEventListener", "BatteryManager.dispatchEvent",
                                 "BatteryManager.removeEventListener", "BatteryManager.dispatchEvent"]


def test_style_proxy_properties_index_css_text_and_length_have_real_records(page):
    page.eval("globalThis.style=document.getElementById('d').style")
    begin(page)
    assert page.eval("""(() => {
        style.color='blue';const color=style.color;style.cssText='height: 3px;';
        const text=style.cssText,length=style.length,index=style[0],keys=Array.from(style);
        return color==='blue' && text==='height: 3px;' && length===1 && index==='height' && keys.join(',')==='height';
    })()""", name="script-api.js")
    for member, operation in [("color", "set"), ("color", "get"), ("cssText", "set"), ("cssText", "get")]:
        record, = records(page, "CSSStyleDeclaration", member, operation)
        assert record["outcome"] == "returned" and record["value_summary"].startswith("string:")
    assert records(page, "CSSStyleDeclaration", "length", "get")
    assert records(page, "CSSStyleDeclaration", "0", "get")
    assert records(page, "CSSStyleDeclaration.prototype", "[Symbol.iterator]", "get")


@pytest.mark.parametrize("member,expression,count,summary", [
    ("getPropertyValue", "style.getPropertyValue('color')", 1, "string:3:red"),
    ("getPropertyPriority", "style.getPropertyPriority('width')", 1, "string:9:important"),
    ("setProperty", "style.setProperty('height','3px','important')", 3, "undefined"),
    ("removeProperty", "style.removeProperty('color')", 1, "string:3:red"),
    ("item", "style.item(0)", 1, "string:5:color"),
])
def test_style_methods_capture_real_arguments_and_return_values(page, member, expression, count, summary):
    page.eval("globalThis.style=document.getElementById('d').style")
    begin(page, "CSSStyleDeclaration." + member)
    page.eval(expression, name="script-api.js")
    record, = records(page, "CSSStyleDeclaration", member)
    assert record["outcome"] == "returned" and record["argument_count"] == count
    assert record["return_summary"] == summary
    assert watch_paths(page) == ["CSSStyleDeclaration." + member]


def test_style_constructor_and_defining_prototype_method_references_are_visible(page):
    page.eval("globalThis.style=document.getElementById('d').style;globalThis.styleCtor=CSSStyleDeclaration")
    begin(page, "window.CSSStyleDeclaration")
    page.eval("style.constructor;style.getPropertyValue", name="style-references.js")
    for member in ["constructor", "getPropertyValue"]:
        record, = records(page, "CSSStyleDeclaration.prototype", member, "get")
        assert record["outcome"] == "returned" and record["value_summary"] == "function"
    assert watch_paths(page) == []
    with pytest.raises(iv8.JSError) as error:
        page.eval("new styleCtor()", name="script-api.js")
    assert error.value.name == "TypeError"
    record, = records(page, "window", "CSSStyleDeclaration")
    assert record["outcome"] == "threw" and "return_summary" not in record
    assert watch_paths(page) == ["window.CSSStyleDeclaration"]


@pytest.mark.parametrize("kind", ["object", "proxy", "error_getters", "prepare_stack"])
def test_thrown_value_metadata_does_not_inspect_application_values(page, kind):
    for tracing in [False, True]:
        page.watch_apis(["scheduler.postTask"])
        if tracing:
            page.start_access_trace()
        else:
            page.stop_access_trace()
        assert page.eval("""(() => {
            let touched=0;const kind=""" + json.dumps(kind) + """;
            const saved=Error.prepareStackTrace;
            let original;
            if(kind==='object')original={toString(){touched++;return 'unsafe';}};
            else if(kind==='proxy')original=new Proxy({}, {
                get(){touched++;return ()=>{};},getPrototypeOf(){touched++;return null;},
                ownKeys(){touched++;return [];},getOwnPropertyDescriptor(){touched++;return undefined;}
            });
            else {
                original=Error('safe');
                if(kind==='error_getters')for(const name of ['name','message','stack'])
                    Object.defineProperty(original,name,{configurable:true,get(){touched++;return 'unsafe';}});
                else Error.prepareStackTrace=()=>{touched++;return 'unsafe';};
            }
            let caught=false;
            try{taskAlias(()=>{}, {get delay(){throw original;}});}catch(error){caught=error===original;}
            finally{Error.prepareStackTrace=saved;}
            return caught && touched===0;
        })()""", name="script-api.js")
        assert watch_paths(page) == ["scheduler.postTask"]
        if tracing:
            record, = records(page, "scheduler", "postTask")
            assert record["outcome"] == "threw" and record["value_summary"] == "object"
            assert "return_summary" not in record


def test_script_api_shapes_identities_and_conversion_behavior_do_not_change_on_toggle(page):
    page.eval("""globalThis.apiFunctions=[postMessage,scheduler.postTask,matchMedia,navigator.sendBeacon,navigator.getBattery];
        globalThis.style=document.getElementById('d').style;
        globalThis.styleMethod=style.setProperty;""")
    snapshot = """JSON.stringify((()=>{
        const functions=[postMessage,scheduler.postTask,matchMedia,navigator.sendBeacon,navigator.getBattery,style.setProperty];
        const shapes=functions.map(fn=>({name:fn.name,length:fn.length,source:Function.prototype.toString.call(fn),
            ownPrototype:Object.hasOwn(fn,'prototype'),prototype:Object.getPrototypeOf(fn)===Function.prototype}));
        const task=Object.getOwnPropertyDescriptor(scheduler,'postTask');
        return {shapes,task:{data:'value'in task,writable:task.writable,enumerable:task.enumerable,configurable:task.configurable}};
    })())"""
    before = json.loads(page.eval(snapshot))
    assert before["task"] == {"data": True, "writable": False, "enumerable": True, "configurable": False}
    begin(page, "window.postMessage", "scheduler.postTask", "window.matchMedia", "navigator.sendBeacon")
    assert json.loads(page.eval(snapshot)) == before
    assert page.eval("""[postMessage,scheduler.postTask,matchMedia,navigator.sendBeacon,navigator.getBattery]
        .every((fn,index)=>fn===apiFunctions[index]) && style.setProperty===styleMethod""")
    assert page.eval("""(()=>{let converted=0;const query={toString(){converted++;return 'screen';}};
        mediaAlias(query);navigator.sendBeacon({toString(){converted++;return '/never-sent';}});
        return converted===2;})()""")
    page.stop_access_trace()
    page.watch_apis([])
    assert json.loads(page.eval(snapshot)) == before
    assert page.eval("typeof __iv8HostCallBegin==='undefined' && typeof __iv8HostCallEnd==='undefined'")
