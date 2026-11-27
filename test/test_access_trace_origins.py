"""Trace user browser API entries across native work and callback reentry."""

import json

import pytest

import iv8


pytestmark = pytest.mark.skipif(
    not getattr(iv8, "_v8_linked", False), reason="requires a V8-linked build")
BASE = "https://trace-origins.test/page/"
HTML = '<html><head></head><body><div id="origin"></div></body></html>'


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, BASE)
        yield value


def event_keys(events):
    return [(event["label"], event["member"], event["operation"])
            for event in events]


def retained_events(page, count):
    report = page.read_access_trace(clear=False)
    assert len(report["events"]) == count
    assert report["dropped_events"] == 0
    assert [event["seq"] for event in report["events"]] == list(range(1, count + 1))
    return report["events"]


def test_debug_location_read_prints_exactly_the_three_user_gets(capsys):
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        assert capsys.readouterr().out == ""
        assert page.eval("window.location.href", name="origin-location.js") == BASE
        assert capsys.readouterr().out.splitlines() == [
            "GET window.window -> <Object>",
            "GET window.location -> <HostObject>",
            'GET location.href -> "' + BASE + '"',
        ]
        assert page.read_access_trace()["events"] == []


def test_raw_reload_installation_and_lifecycle_do_not_consume_capacity(page):
    page.start_access_trace(max_events=3)
    previous_generation = page.read_access_trace()["generation"]
    page.load(HTML, BASE)
    report = page.read_access_trace(clear=False)
    assert report["generation"] == previous_generation + 1
    assert report["enabled"] and report["max_events"] == 3
    assert report["events"] == [] and report["dropped_events"] == 0
    assert page.eval("window.location.href", name="origin-after-load.js") == BASE
    events = retained_events(page, 3)
    assert event_keys(events) == [
        ("window", "window", "get"),
        ("window", "location", "get"),
        ("location", "href", "get"),
    ]
    assert all(event["resource_name"] == "origin-after-load.js" for event in events)


def test_user_event_constructor_records_call_and_read_without_native_field_writes(page):
    page.eval("globalThis.originCtor=Event;globalThis.originEvent=null")
    page.start_access_trace(max_events=2)
    assert page.eval("originEvent=new originCtor('probe');originEvent.type",
                     name="origin-constructor.js") == "probe"
    events = retained_events(page, 2)
    assert event_keys(events) == [("window", "Event", "call"), ("Event", "type", "get")]
    assert events[0]["outcome"] == "returned" and events[0]["return_summary"] == "object"
    assert events[1]["value_summary"] == "string:5:probe"


def test_inline_external_and_host_scripts_keep_user_accesses_and_source_order(page):
    html = """<html><head><script>navigator.hardwareConcurrency;</script>
        <script src="external.js"></script></head><body></body></html>"""
    page.start_access_trace(max_events=6)
    page.load(html, BASE, resources={BASE + "external.js": "screen.width;"},
              scripts=[{"name": "origin-host.js", "code": "history.length;"}])
    events = retained_events(page, 6)
    assert event_keys(events) == [
        ("window", "navigator", "get"), ("navigator", "hardwareConcurrency", "get"),
        ("window", "screen", "get"), ("screen", "width", "get"),
        ("window", "history", "get"), ("history", "length", "get"),
    ]
    assert [event["resource_name"] for event in events] == [
        BASE + "#inline-script-1", BASE + "#inline-script-1",
        BASE + "external.js", BASE + "external.js", "origin-host.js", "origin-host.js",
    ]


def test_lifecycle_user_handlers_are_observed_without_synthetic_event_entries(page):
    html = """<script>
        globalThis.originSeen=[];
        document.addEventListener('DOMContentLoaded', event=>{
            originSeen.push(event.type,navigator.hardwareConcurrency);
        });
        addEventListener('load', event=>{
            originSeen.push(event.type,navigator.hardwareConcurrency);
        });
        </script>"""
    page.start_access_trace()
    page.load(html, BASE)
    page.stop_access_trace()
    events = page.read_access_trace()["events"]
    assert [event["value_summary"] for event in events
            if event["label"] == "Event" and event["member"] == "type"] == [
                "string:16:DOMContentLoaded", "string:4:load"]
    assert len([event for event in events if event["label"] == "navigator" and
                event["member"] == "hardwareConcurrency"]) == 2
    assert not [event for event in events if event["label"] == "window" and
                event["member"] == "Event" and event["operation"] == "call"]
    assert not [event for event in events if event["label"] == "Event" and
                event["operation"] == "set"]
    assert json.loads(page.eval("JSON.stringify(originSeen)")) == [
        "DOMContentLoaded", 8, "load", 8]


def test_nested_dispatch_keeps_user_reads_and_hides_event_bookkeeping(page):
    page.eval("""globalThis.originNavigator=navigator;globalThis.originDeliver=dispatchEvent;
        globalThis.originOuter=new Event('outer');globalThis.originInner=new Event('inner');
        globalThis.originSeen=[];
        addEventListener('inner', event=>{
            originSeen.push(event.type);originNavigator.hardwareConcurrency;
        });
        addEventListener('outer', event=>{
            originSeen.push(event.type);originDeliver(originInner);originNavigator.language;
        });""")
    page.start_access_trace(max_events=6)
    assert page.eval("originDeliver(originOuter)", name="origin-nested-dispatch.js")
    events = retained_events(page, 6)
    assert event_keys(events) == [
        ("window", "dispatchEvent", "call"), ("Event", "type", "get"),
        ("window", "dispatchEvent", "call"), ("Event", "type", "get"),
        ("navigator", "hardwareConcurrency", "get"), ("navigator", "language", "get"),
    ]
    page.stop_access_trace()
    assert json.loads(page.eval("JSON.stringify(originSeen)")) == ["outer", "inner"]


@pytest.mark.parametrize("pump,schedule", [
    ("run_timers", "setTimeout(originCallback,0)"),
    ("run_jobs", "Promise.resolve().then(originCallback)"),
    ("run_animation_frame", "requestAnimationFrame(originCallback)"),
    ("run_idle_callbacks", "requestIdleCallback(originCallback)"),
], ids=["timer", "promise", "animation-frame", "idle"])
def test_user_callback_reentry_is_observed_with_one_event_capacity(page, pump, schedule):
    page.eval("""globalThis.originNavigator=navigator;globalThis.originSeen=[];
        globalThis.originCallback=()=>originSeen.push(originNavigator.hardwareConcurrency);"""
              + schedule, name="origin-callback-setup.js")
    page.start_access_trace(max_events=1)
    getattr(page, pump)()
    events = retained_events(page, 1)
    assert event_keys(events) == [("navigator", "hardwareConcurrency", "get")]
    assert events[0]["outcome"] == "returned" and events[0]["value_summary"] == "number:8"
    page.stop_access_trace()
    assert page.eval("originSeen.join(',')") == "8"


def test_mutation_delivery_preserves_user_callback_origin(page):
    page.eval("""globalThis.originNavigator=navigator;globalThis.originSeen=[];
        globalThis.originElement=document.getElementById('origin');
        globalThis.originObserver=new MutationObserver(records=>{
            originSeen.push(records.length,originNavigator.hardwareConcurrency);
        });
        originObserver.observe(originElement,{attributes:true});
        originElement.setAttribute('data-origin','one');""", name="origin-observer.js")
    page.start_access_trace(max_events=1)
    page.run_jobs()
    assert event_keys(retained_events(page, 1)) == [("navigator", "hardwareConcurrency", "get")]
    page.stop_access_trace()
    assert page.eval("originSeen.join(',')") == "1,8"


def test_message_delivery_observes_user_fields_without_dispatch_maintenance(page):
    page.eval("""globalThis.originNavigator=navigator;globalThis.originSeen=[];
        addEventListener('message', event=>{
            originSeen.push(event.type,originNavigator.hardwareConcurrency);
        });postMessage('payload','*');""", name="origin-message.js")
    page.start_access_trace(max_events=2)
    page.run_timers()
    assert event_keys(retained_events(page, 2)) == [
        ("Event", "type", "get"), ("navigator", "hardwareConcurrency", "get")]
    page.stop_access_trace()
    assert page.eval("originSeen.join(',')") == "message,8"


def test_file_reader_delivery_hides_native_state_updates_but_observes_callback_reads(page):
    page.eval("""globalThis.originNavigator=navigator;globalThis.originSeen=[];
        globalThis.originReader=new FileReader();globalThis.originBlob=new Blob(['payload']);
        originReader.addEventListener('load', event=>{
            originSeen.push(event.type,originReader.result,originNavigator.hardwareConcurrency);
        });originReader.readAsText(originBlob);""", name="origin-file-reader.js")
    page.start_access_trace(max_events=3)
    page.run_jobs()
    assert event_keys(retained_events(page, 3)) == [
        ("Event", "type", "get"), ("FileReader", "result", "get"),
        ("navigator", "hardwareConcurrency", "get")]
    page.stop_access_trace()
    assert page.eval("originSeen.join(',')") == "load,payload,8"


@pytest.mark.parametrize("kind", ["coercion", "option-getter", "inherited-setter"])
def test_throwing_user_reentry_preserves_exception_and_restores_origin(page, kind):
    page.eval("""globalThis.originNavigator=navigator;globalThis.originLocation=location;
        globalThis.originError=Error('origin sentinel');globalThis.originCtor=Event;
        globalThis.originCaught=false;""")
    if kind == "coercion":
        operation = "originLocation.assign({toString(){originNavigator.language;throw originError;}})"
        expected_call = ("location", "assign", "call")
    elif kind == "option-getter":
        operation = "new originCtor('probe',{get bubbles(){originNavigator.language;throw originError;}})"
        expected_call = ("window", "Event", "call")
    else:
        page.eval("""globalThis.originCtor=class extends Event {
            set type(value){originNavigator.language;throw originError;}
        };""")
        operation = "new originCtor('probe')"
        expected_call = ("window", "Event", "call")
    page.start_access_trace(max_events=3)
    assert page.eval("try{" + operation + ";}catch(error){originCaught=error===originError;}"
                     "originNavigator.hardwareConcurrency", name="origin-throw.js") == 8
    events = retained_events(page, 3)
    assert event_keys(events) == [expected_call, ("navigator", "language", "get"),
                                  ("navigator", "hardwareConcurrency", "get")]
    assert events[0]["outcome"] == "threw" and "return_summary" not in events[0]
    assert all(event["outcome"] == "returned" for event in events[1:])
    page.stop_access_trace()
    assert page.eval("originCaught")


@pytest.mark.parametrize("setup,arguments,label,member", [
    ("new Blob(['abc'])", "0,1", "Blob", "slice"),
    ("new URL('https://trace-origins.test/value')", "", "URL", "toJSON"),
], ids=["internal-constructor", "internal-property-read"])
def test_native_api_implementation_does_not_add_internal_calls_or_reads(
        page, setup, arguments, label, member):
    page.eval("globalThis.originTarget=" + setup + ";globalThis.originMethod=originTarget." + member)
    page.start_access_trace(max_events=1)
    result = page.eval("originMethod.call(originTarget" + ("," + arguments if arguments else "") + ")",
                       name="origin-native-body.js")
    events = retained_events(page, 1)
    assert event_keys(events) == [(label, member, "call")]
    assert events[0]["outcome"] == "returned"
    if label == "Blob":
        assert isinstance(result, iv8.JSValue) and result.context_alive
    else:
        assert result == "https://trace-origins.test/value"


@pytest.mark.parametrize("pump,schedule", [
    ("run_timers", "setTimeout(originBound,0)"),
    ("run_jobs", "queueMicrotask(originBound)"),
    ("run_jobs", "Promise.resolve().then(originBound)"),
    ("run_animation_frame", "requestAnimationFrame(originBound)"),
    ("run_idle_callbacks", "requestIdleCallback(originBound)"),
], ids=["timer", "microtask", "promise", "frame", "idle"])
def test_bound_native_callbacks_remain_user_entries_without_a_js_frame(page, pump, schedule):
    page.eval("globalThis.originBound=navigator.javaEnabled.bind(navigator);" + schedule)
    page.start_access_trace(max_events=1)
    getattr(page, pump)()
    events = retained_events(page, 1)
    assert event_keys(events) == [("navigator", "javaEnabled", "call")]
    assert events[0]["outcome"] == "returned"


def test_bound_native_lifecycle_callback_remains_visible(page):
    page.start_access_trace()
    page.load("""<script>document.addEventListener('DOMContentLoaded',
        navigator.javaEnabled.bind(navigator));</script>""", BASE)
    events = page.read_access_trace()["events"]
    assert len([event for event in events if event["label"] == "navigator" and
                event["member"] == "javaEnabled" and event["operation"] == "call"]) == 1
    assert not [event for event in events if event["label"] == "Event" and
                event["operation"] == "set"]


@pytest.mark.parametrize("api", ["btoa", "matchMedia"])
def test_bound_native_coercion_remains_user_code_in_native_or_script_api(page, api):
    page.eval("globalThis.originCall=" + api + ";globalThis.originInput={toString:location.toString.bind(location)}")
    page.start_access_trace(max_events=2)
    page.eval("originCall(originInput)")
    events = retained_events(page, 2)
    assert event_keys(events) == [("window", api, "call"), ("location", "toString", "call")]
    assert all(event["outcome"] == "returned" for event in events)


def test_bound_native_event_option_getter_remains_visible(page):
    page.eval("""globalThis.originCtor=Event;globalThis.originOptions={};
        Object.defineProperty(originOptions,'bubbles',{get:navigator.javaEnabled.bind(navigator)});""")
    page.start_access_trace(max_events=2)
    page.eval("new originCtor('probe',originOptions)")
    assert event_keys(retained_events(page, 2)) == [
        ("window", "Event", "call"), ("navigator", "javaEnabled", "call")]


@pytest.mark.parametrize("constructor,field", [("Event", "type"), ("FileReader", "readyState")])
def test_bound_native_inherited_field_setter_remains_visible(page, constructor, field):
    page.eval("""globalThis.originCtor=class extends """ + constructor + """ {};
        Object.defineProperty(originCtor.prototype,""" + json.dumps(field) + """,
            {set:navigator.javaEnabled.bind(navigator),configurable:true});""")
    page.start_access_trace(max_events=2)
    page.eval("new originCtor(" + ("'probe'" if constructor == "Event" else "") + ")")
    assert event_keys(retained_events(page, 2)) == [
        ("window", constructor, "call"), ("navigator", "javaEnabled", "call")]


def test_bound_native_scheduler_option_getter_preserves_original_read_count(page):
    page.eval("""globalThis.originCall=scheduler.postTask.bind(scheduler);globalThis.originOptions={};
        Object.defineProperty(originOptions,'delay',{get:performance.now.bind(performance)});""")
    page.start_access_trace(max_events=2)
    page.eval("originCall(()=>7,originOptions)")
    assert event_keys(retained_events(page, 2)) == [
        ("scheduler", "postTask", "call"), ("performance", "now", "call")]


def test_filtered_script_bridge_restores_user_origin_after_throw(page):
    page.eval("globalThis.originCall=postMessage;globalThis.originNavigator=navigator")
    page.start_access_trace(max_events=1, object_types=["navigator"])
    assert page.eval("try{originCall()}catch(error){} originNavigator.hardwareConcurrency") == 8
    events = retained_events(page, 1)
    assert event_keys(events) == [("navigator", "hardwareConcurrency", "get")]
    assert events[0]["outcome"] == "returned"


def test_bound_native_clone_getter_is_user_code_even_with_internal_resource_name(page):
    page.eval("""globalThis.originCall=structuredClone;globalThis.originInput={};
        Object.defineProperty(originInput,'value',{
            get:navigator.javaEnabled.bind(navigator),enumerable:true});""")
    page.start_access_trace(max_events=2)
    assert page.eval("originCall(originInput)", to_py=True, name="internal") == {"value": False}
    events = retained_events(page, 2)
    assert event_keys(events) == [
        ("window", "structuredClone", "call"), ("navigator", "javaEnabled", "call")]
    assert all(event["resource_name"] == "internal" and event["source_kind"] == "eval"
               for event in events)


@pytest.mark.parametrize("writing", [False, True], ids=["getter", "setter"])
def test_css_custom_bound_accessor_is_user_code_and_keeps_proxy_receiver(page, writing):
    page.eval("""globalThis.originNavigator=navigator;
        globalThis.originStyle=document.getElementById('origin').style;
        const descriptor={configurable:true};
        descriptor[""" + ("'set'" if writing else "'get'") + """]=navigator.javaEnabled.bind(navigator);
        Object.defineProperty(originStyle,'userProperty',descriptor);""")
    page.start_access_trace(max_events=3)
    operation = "originStyle.userProperty=7" if writing else "originStyle.userProperty"
    assert page.eval(operation + ";originNavigator.hardwareConcurrency") == 8
    events = retained_events(page, 3)
    assert event_keys(events) == [
        ("CSSStyleDeclaration", "userProperty", "set" if writing else "get"),
        ("navigator", "javaEnabled", "call"), ("navigator", "hardwareConcurrency", "get")]
    assert all(event["outcome"] == "returned" for event in events)
    page.stop_access_trace()
    assert page.eval("""(() => {
        Object.defineProperty(originStyle,'receiverCheck',{get(){return this===originStyle;}});
        return originStyle.receiverCheck;
    })()""")


@pytest.mark.parametrize("accessor", [False, True], ids=["method-replacement", "getter-replacement"])
def test_css_backend_bound_native_override_is_not_hidden(page, accessor):
    descriptor = "{get:navigator.javaEnabled.bind(navigator),configurable:true}" if accessor else \
        "{value:navigator.javaEnabled.bind(navigator),writable:true,configurable:true}"
    page.eval("""globalThis.originNavigator=navigator;
        globalThis.originNode=document.getElementById('origin');globalThis.originStyle=originNode.style;
        Object.defineProperty(originNode,'getAttribute',""" + descriptor + ");")
    page.start_access_trace(max_events=3)
    assert page.eval("try{originStyle.length}catch(error){} originNavigator.hardwareConcurrency") == 8
    events = retained_events(page, 3)
    assert event_keys(events) == [
        ("CSSStyleDeclaration", "length", "get"), ("navigator", "javaEnabled", "call"),
        ("navigator", "hardwareConcurrency", "get")]
    assert events[0]["outcome"] == ("threw" if accessor else "returned")
    assert all(event["outcome"] == "returned" for event in events[1:])


@pytest.mark.parametrize("expression,member,operation,expected", [
    ("originStyle.length", "length", "get", 0),
    ("originStyle.cssText", "cssText", "get", ""),
    ("originStyle.cssText='color: red;'", "cssText", "set", "color: red;"),
], ids=["length", "css-text-get", "css-text-set"])
def test_css_default_backend_is_internal_and_does_not_consume_capacity(
        page, expression, member, operation, expected):
    page.eval("globalThis.originStyle=document.getElementById('origin').style")
    page.start_access_trace(max_events=1)
    assert page.eval(expression) == expected
    events = retained_events(page, 1)
    assert event_keys(events) == [("CSSStyleDeclaration", member, operation)]
    assert events[0]["outcome"] == "returned"


def test_bound_native_global_string_keeps_both_original_match_media_conversions(page):
    page.eval("globalThis.originCall=matchMedia;globalThis.String=location.toString.bind(location)")
    page.start_access_trace(max_events=3)
    page.eval("originCall('ignored')")
    events = retained_events(page, 3)
    assert event_keys(events) == [
        ("window", "matchMedia", "call"), ("location", "toString", "call"),
        ("location", "toString", "call")]
    assert all(event["outcome"] == "returned" for event in events)
