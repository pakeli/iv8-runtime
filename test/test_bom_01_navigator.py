"""BOM-01 native Navigator capabilities against the captured Chrome profile."""

import json
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(
    not getattr(iv8, "_v8_linked", False), reason="requires a V8-linked build")
BASE = "https://bom-capabilities.test/page/"
HTML = "<html><head></head><body></body></html>"
REFERENCE = json.loads((Path(__file__).parent / "fixtures" / "bom_01" /
                        "chrome.json").read_text(encoding="utf-8"))["result"]
SHAPE_HELPERS = r"""
    const attempt = callback => {
        try {const value=callback();return {value:value===undefined?'undefined':value};}
        catch(error){return {error:{name:error.name,message:error.message}};}
    };
    const functionShape = method => ({name:method.name,length:method.length,
        source:Function.prototype.toString.call(method),
        prototype:Object.hasOwn(method,'prototype'),
        construct:attempt(()=>Reflect.construct(method,[])).error?.name??'constructed'});
    const descriptor = (owner,key) => {
        const value=Object.getOwnPropertyDescriptor(owner,key);
        if(!value)return null;
        return {enumerable:value.enumerable,configurable:value.configurable,
            ...(Object.hasOwn(value,'value')?{kind:'data',writable:value.writable,
                ...(typeof value.value==='function'?{method:functionShape(value.value)}:{value:value.value})}:
                {kind:'accessor',get:value.get?functionShape(value.get):null,
                    set:value.set?functionShape(value.set):null})};
    };
"""


def evaluate(page, body):
    return json.loads(page.eval("JSON.stringify((()=>{" + body + "})())"))


def events(page):
    return page.read_access_trace(clear=False)["events"]


def event_keys(records):
    return [(event["object_type"], event["label"], event["member"], event["operation"])
            for event in records]


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, BASE)
        yield value


@pytest.mark.parametrize("member", ["getGamepads", "scheduling"])
def test_navigator_member_native_shape_matches_chrome_before_nested_getter(page, member):
    actual = evaluate(page, SHAPE_HELPERS +
                      "return descriptor(Navigator.prototype," + json.dumps(member) + ");")
    assert actual == REFERENCE["shape"]["Navigator"][member]
    assert page.eval("!Object.hasOwn(navigator," + json.dumps(member) + ")")


def test_scheduling_interface_is_eager_and_matches_browser_native_shape(page):
    # The defining operation must exist before navigator.scheduling is evaluated.
    assert page.eval("typeof Scheduling.prototype.isInputPending === 'function'")
    actual = evaluate(page, SHAPE_HELPERS + """
        const target=navigator.scheduling,prototype=Object.getPrototypeOf(target);
        return {constructor:functionShape(Scheduling),
            parent:Object.getPrototypeOf(prototype)===Object.prototype?'Object':'other',
            samePrototype:prototype===Scheduling.prototype,
            tag:Object.prototype.toString.call(target),own:Reflect.ownKeys(target).map(String),
            members:Object.fromEntries(Reflect.ownKeys(prototype).map(key=>[String(key),descriptor(prototype,key)])),
            inheritedEventMethods:['addEventListener','removeEventListener','dispatchEvent'].map(
                key=>target[key]===EventTarget.prototype[key])};
    """)
    assert actual == REFERENCE["shape"]["Scheduling"]
    assert evaluate(page, "return Reflect.ownKeys(Scheduling.prototype).map(String);") == list(
        REFERENCE["shape"]["Scheduling"]["members"])
    assert page.eval("""(()=>{
        try {Scheduling();return false;} catch(error){return error.name==='TypeError';}
    })()""")


def test_gamepad_snapshot_matches_empty_device_chrome_profile(page):
    actual = evaluate(page, SHAPE_HELPERS + """
        const first=navigator.getGamepads(),second=navigator.getGamepads();
        return {array:Array.isArray(first),value:first,frozen:Object.isFrozen(first),fresh:first!==second,
            wrongReceiver:attempt(()=>navigator.getGamepads.call({})),
            ignoredArgument:attempt(()=>navigator.getGamepads({get valueOf(){throw Error('unused');}}))};
    """)
    assert actual == REFERENCE["capabilities"]["gamepads"]
    assert page.eval("""(()=>{
        const first=navigator.getGamepads();first[0]='local';first.push('more');
        const second=navigator.getGamepads();
        return second.length===8&&second.every(value=>value===null)&&
            Object.keys(second).join(',')==='0,1,2,3,4,5,6,7';
    })()""")


def test_gamepad_sequence_creation_does_not_run_inherited_array_setters(page):
    assert page.eval("""(()=>{
        let calls=0;
        Object.defineProperty(Array.prototype,'0',{configurable:true,set(){calls++;throw Error('index setter');}});
        try {
            const snapshot=navigator.getGamepads();
            return calls===0&&Object.hasOwn(snapshot,'0')&&snapshot[0]===null&&snapshot.length===8;
        } finally {delete Array.prototype[0];}
    })()""")


def test_optional_dictionary_results_errors_and_single_boolean_get_match_chrome(page):
    actual = evaluate(page, SHAPE_HELPERS + """
        const options=[],pending=argument=>attempt(()=>navigator.scheduling.isInputPending(argument));
        return {stable:navigator.scheduling===navigator.scheduling,
            results:[pending(undefined),pending(null),pending({}),pending({includeContinuous:true}),
                pending({get includeContinuous(){options.push('read');return {valueOf(){throw Error('no coercion');}};}}),
                pending(1),pending('x'),pending(Symbol('options'))],options,
            wrongReceiver:attempt(()=>navigator.scheduling.isInputPending.call({})),
            getterWrongReceiver:attempt(()=>Object.getOwnPropertyDescriptor(Navigator.prototype,'scheduling').get.call({}))};
    """)
    assert actual == REFERENCE["capabilities"]["scheduling"]


@pytest.mark.parametrize("argument", ["false", "0n", "true", "''"])
def test_other_primitive_options_use_browser_dictionary_type_error(page, argument):
    result = evaluate(page, SHAPE_HELPERS +
                      "return attempt(()=>navigator.scheduling.isInputPending(" + argument + ")); ")
    assert result == REFERENCE["capabilities"]["scheduling"]["results"][5]


def test_dictionary_uses_actual_receiver_inherited_getters_and_no_boolean_coercion(page):
    assert page.eval("""(()=>{
        const seen=[],options=Object.create({get includeContinuous(){
            seen.push(this===options);return {[Symbol.toPrimitive](){throw Error('boolean coercion');}};
        }});
        const callable=function(){};
        Object.defineProperty(callable,'includeContinuous',{get(){seen.push('function');return true;}});
        const proxy=new Proxy({includeContinuous:true},{get(target,key,receiver){
            seen.push(key);return Reflect.get(target,key,receiver);
        }});
        const ignored=new Proxy({},{get(){throw Error('extra argument');}});
        return navigator.scheduling.isInputPending(options,ignored)===false&&
            navigator.scheduling.isInputPending(callable)===false&&
            navigator.scheduling.isInputPending(proxy)===false&&
            seen.length===3&&seen[0]===true&&seen[1]==='function'&&seen[2]==='includeContinuous';
    })()""")


@pytest.mark.parametrize("kind", ["getter", "proxy", "revoked"])
def test_dictionary_errors_preserve_original_identity_and_leave_scheduling_usable(page, kind):
    assert page.eval("""(()=>{
        const kind=""" + json.dumps(kind) + """,sentinel={kind};let options,expected=sentinel,calls=0;
        if(kind==='getter')options={get includeContinuous(){calls++;throw sentinel;}};
        if(kind==='proxy')options=new Proxy({},{get(){calls++;throw sentinel;}});
        if(kind==='revoked'){
            const pair=Proxy.revocable({},{});pair.revoke();options=pair.proxy;expected=null;
        }
        try {navigator.scheduling.isInputPending(options);return false;}
        catch(error){if(expected?error!==expected:error.name!=='TypeError')return false;}
        return calls===(kind==='revoked'?0:1)&&navigator.scheduling.isInputPending()===false;
    })()""")


@pytest.mark.parametrize("receiver", ["{}", "null", "17", "Object.create(Scheduling.prototype)"])
def test_scheduling_brand_check_precedes_dictionary_side_effects(page, receiver):
    assert page.eval("""(()=>{
        let reads=0;const options={get includeContinuous(){reads++;throw Error('unexpected options');}};
        try {Scheduling.prototype.isInputPending.call(""" + receiver + """,options);return false;}
        catch(error){return error.name==='TypeError'&&error.message==='Illegal invocation'&&reads===0;}
    })()""")


def test_synthetic_event_and_js_task_queues_do_not_become_trusted_input(page):
    assert page.eval("""(()=>{
        setTimeout(()=>{},0);requestAnimationFrame(()=>{});queueMicrotask(()=>{});
        window.dispatchEvent(new MouseEvent('mousemove'));
        return navigator.scheduling.isInputPending()===false&&
            navigator.scheduling.isInputPending({includeContinuous:true})===false;
    })()""")
    page.run_until_idle(max_steps=20)
    assert page.eval("navigator.scheduling.isInputPending()") is False


def test_cached_calls_record_safe_results_and_separate_watch_hits(page):
    page.eval("""globalThis.bomNavigator=navigator;globalThis.bomScheduling=navigator.scheduling;
        globalThis.bomGamepads=Navigator.prototype.getGamepads;
        globalThis.bomPending=Scheduling.prototype.isInputPending;""")
    page.watch_apis(["navigator.getGamepads", "navigator.scheduling.isInputPending"])
    page.start_access_trace()
    page.eval("bomGamepads.call(bomNavigator);bomPending.call(bomScheduling,{includeContinuous:true});",
              name="bom-capabilities.js")
    records = events(page)
    assert event_keys(records) == [
        ("navigator", "navigator", "getGamepads", "call"),
        ("host_object", "navigator.scheduling", "isInputPending", "call"),
    ]
    assert [record["argument_summaries"] for record in records] == [[], ["object"]]
    assert [record["return_summary"] for record in records] == ["array", "boolean:false"]
    assert all(record["outcome"] == "returned" and record["resource_name"] == "bom-capabilities.js"
               and record["source_kind"] == "eval" for record in records)
    assert [hit["path"] for hit in page.read_watch_api_hits()] == [
        "navigator.getGamepads", "navigator.scheduling.isInputPending"]
    assert page.read_watch_api_hits() == []


def test_parent_get_and_adjacent_method_calls_have_one_completed_entry_each(page):
    page.eval("globalThis.bomNavigator=navigator;")
    page.start_access_trace()
    page.eval("bomNavigator.getGamepads();bomNavigator.scheduling.isInputPending();")
    records = events(page)
    assert event_keys(records) == [
        ("navigator", "navigator", "getGamepads", "call"),
        ("navigator", "navigator", "scheduling", "get"),
        ("host_object", "navigator.scheduling", "isInputPending", "call"),
    ]
    assert records[1]["value_summary"] == "host_object"
    assert all(record["outcome"] == "returned" for record in records)


def test_options_user_getter_reentry_is_visible_without_diagnostic_reads(page):
    page.eval("""globalThis.bomScheduling=navigator.scheduling;globalThis.bomNavigator=navigator;
        globalThis.bomPending=Scheduling.prototype.isInputPending;globalThis.bomReads=0;
        globalThis.bomOptions={get includeContinuous(){bomReads++;return bomNavigator.onLine;}};""")
    page.start_access_trace()
    assert page.eval("bomPending.call(bomScheduling,bomOptions)") is False
    records = events(page)
    assert event_keys(records) == [
        ("host_object", "navigator.scheduling", "isInputPending", "call"),
        ("navigator", "navigator", "onLine", "get"),
    ]
    assert all(record["outcome"] == "returned" for record in records)
    page.stop_access_trace()
    assert page.eval("bomReads") == 1


@pytest.mark.parametrize("operation", ["dictionary", "receiver", "getter"])
def test_failed_native_entries_have_completed_exception_trace(page, operation):
    page.eval("""globalThis.bomScheduling=navigator.scheduling;
        globalThis.bomPending=Scheduling.prototype.isInputPending;
        globalThis.bomGetter=Object.getOwnPropertyDescriptor(Navigator.prototype,'scheduling').get;""")
    expressions = {"dictionary": "bomPending.call(bomScheduling,17)",
                   "receiver": "bomPending.call({}, {})", "getter": "bomGetter.call({})"}
    page.start_access_trace(max_events=1)
    with pytest.raises(iv8.JSError) as caught:
        page.eval(expressions[operation], name="bom-error.js")
    assert caught.value.name == "TypeError"
    record, = events(page)
    assert record["outcome"] == "threw" and record["exception_name"] == "TypeError"
    assert record["resource_name"] == "bom-error.js" and record["source_kind"] == "eval"
    assert record["member"] == ("scheduling" if operation == "getter" else "isInputPending")
    assert record["operation"] == ("get" if operation == "getter" else "call")
    assert record["label"] == {"dictionary": "navigator.scheduling",
                               "receiver": "Scheduling.prototype", "getter": "Navigator.prototype"}[operation]
    assert "return_summary" not in record and page.read_access_trace(clear=False)["dropped_events"] == 0


def test_method_references_filters_capacity_and_stop_keep_watch_independent(page):
    page.eval("globalThis.bomScheduling=navigator.scheduling;globalThis.bomNavigator=navigator;")
    page.watch_apis(["navigator.getGamepads", "navigator.scheduling.isInputPending"])
    page.start_access_trace(max_events=1, object_types=["navigator"])
    page.eval("bomScheduling.isInputPending();bomNavigator.getGamepads();bomNavigator.getGamepads();")
    report = page.read_access_trace(clear=False)
    assert event_keys(report["events"]) == [("navigator", "navigator", "getGamepads", "call")]
    assert report["dropped_events"] == 2  # Later method read and its call exceed capacity.
    assert len(page.read_watch_api_hits()) == 3
    page.stop_access_trace()
    page.eval("bomNavigator.getGamepads();bomScheduling.isInputPending();")
    assert events(page) == report["events"]
    assert len(page.read_watch_api_hits()) == 2


def test_nested_wrappers_reset_on_reload_and_are_separate_between_pages(page):
    saved = page.eval("navigator.scheduling")
    page.eval("navigator.scheduling.localMarker=1;navigator.connection.localMarker=1;")
    assert page.eval("navigator.scheduling===navigator.scheduling&&navigator.connection===navigator.connection")
    page.watch_apis(["navigator.getGamepads", "navigator.scheduling.isInputPending"])
    page.start_access_trace()
    previous_generation = page.read_access_trace(clear=False)["generation"]
    page.load(HTML, BASE)
    assert not saved.context_alive
    assert events(page) == []
    page.eval("navigator.getGamepads();navigator.scheduling.isInputPending();")
    records = events(page)
    assert all(record["generation"] == previous_generation + 1 for record in records)
    assert len(page.read_watch_api_hits()) == 2
    page.stop_access_trace()
    assert page.eval("navigator.scheduling.localMarker===undefined&&navigator.connection.localMarker===undefined")
    page.eval("navigator.scheduling.localMarker=2;")
    with iv8.Page() as other:
        assert other.eval("navigator.scheduling.localMarker===undefined")
        assert other.read_access_trace()["events"] == [] and other.read_watch_api_hits() == []


def test_debug_prints_completed_calls_and_prod_preserves_quiet_behavior(capsys):
    with iv8.Page() as value:
        value.load(HTML, BASE, mode="debug")
        assert capsys.readouterr().out == ""
        value.eval("navigator.getGamepads();navigator.scheduling.isInputPending({includeContinuous:true});")
        output = capsys.readouterr().out
        assert output.count("CALL navigator.getGamepads() -> <Array>") == 1
        assert output.count("CALL navigator.scheduling.isInputPending(<Object>) -> false") == 1
        assert "GET navigator.scheduling -> <HostObject>" in output
        assert value.read_access_trace()["events"] == []
        value.load(HTML, BASE, mode="prod")
        assert value.eval("navigator.scheduling.isInputPending()===false&&navigator.getGamepads().length===8")
        assert capsys.readouterr().out == ""
