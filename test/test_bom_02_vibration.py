"""BOM-02 vibration conversion in the no-trusted-activation Chrome profile."""

import hashlib
import json
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(
    not getattr(iv8, "_v8_linked", False), reason="requires a V8-linked build")
FIXTURES = Path(__file__).parent / "fixtures" / "bom_02"
REFERENCES = {
    mode: json.loads((FIXTURES / name).read_text(encoding="utf-8"))
    for mode, name in (("secure", "chrome.json"), ("insecure", "chrome-insecure.json"))
}
SOURCES = REFERENCES["secure"]["result"]["vibrationSources"]
HTML = "<html><head></head><body></body></html>"


def evaluate(page, body):
    return json.loads(page.eval("JSON.stringify((()=>{" + body + "})())"))


def observe_case(page, name, source):
    return evaluate(page, """
        const name=""" + json.dumps(name) + ";const source=" + json.dumps(source) + """;
        const effects=[];
        const sentinel=name.startsWith('typeerror_sentinel')?new TypeError('original TypeError sentinel'):{sentinel:name};
        try {
            const value=source===null?navigator.vibrate():navigator.vibrate(eval(source));
            return {value,effects};
        }catch(error){return {error:{name:error.name??null,message:error.message??null,same:error===sentinel},effects};}
    """)


def event_keys(records):
    return [(event["object_type"], event["label"], event["member"], event["operation"])
            for event in records]


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, "https://vibration.test/page/")
        yield value


def test_reference_hash_conditions_and_synchronous_profiles_are_consistent():
    digest = hashlib.sha256((FIXTURES / "probe.js").read_bytes()).hexdigest()
    for mode, reference in REFERENCES.items():
        assert reference["probe_sha256"] == digest
        assert "Google Chrome 157." in reference["browser"]
        assert reference["execution"]["profile_path"] == "<temporary-profile>"
        assert reference["result"]["secure"] is (mode == "secure")
        assert reference["result"]["activation"] == {"active": False, "beenActive": False}
        assert reference["result"]["vibrationSources"] == SOURCES
    assert REFERENCES["secure"]["result"]["vibration"] == REFERENCES["insecure"]["result"]["vibration"]


@pytest.mark.parametrize("mode", ["secure", "insecure"])
def test_vibrate_shape_matches_native_browser_descriptor(mode):
    base = ("https" if mode == "secure" else "http") + "://vibration.test/page/"
    with iv8.Page() as value:
        value.load(HTML, base)
        actual = evaluate(value, """
            const descriptor=Object.getOwnPropertyDescriptor(Navigator.prototype,'vibrate');
            const method=descriptor.value;let construct;
            try{Reflect.construct(method,[]);construct='constructed';}catch(error){construct=error.name;}
            return {present:'vibrate'in navigator,own:Object.hasOwn(navigator,'vibrate'),
                descriptor:{enumerable:descriptor.enumerable,configurable:descriptor.configurable,
                    writable:descriptor.writable,kind:'value'in descriptor?'data':'accessor',
                    method:{name:method.name,length:method.length,
                        source:Function.prototype.toString.call(method),
                        prototype:Object.hasOwn(method,'prototype'),construct}}};
        """)
        assert actual == REFERENCES[mode]["result"]["shape"]["vibrate"]


@pytest.mark.parametrize("mode", ["secure", "insecure"])
@pytest.mark.parametrize("name", list(SOURCES))
def test_scalar_and_sequence_conversions_match_browser_effects_and_exception_identity(mode, name):
    base = ("https" if mode == "secure" else "http") + "://vibration.test/page/"
    with iv8.Page() as value:
        value.load(HTML, base)
        assert observe_case(value, name, SOURCES[name]) == REFERENCES[mode]["result"]["vibration"][name]


@pytest.mark.parametrize("receiver", ["{}", "null", "undefined", "17", "Navigator.prototype",
                                     "Object.create(Navigator.prototype)", "new Proxy(navigator,{})"])
def test_receiver_validation_precedes_pattern_lookup_and_number_conversion(page, receiver):
    result = evaluate(page, """
        const effects=[];
        const input={get [Symbol.iterator](){effects.push('iterator');return undefined;},
            valueOf(){effects.push('valueOf');return 1;}};
        try{return {value:navigator.vibrate.call(""" + receiver + """,input),effects};}
        catch(error){return {error:{name:error.name,message:error.message},effects};}
    """)
    assert result == REFERENCES["secure"]["result"]["vibration"]["invalidReceiver"]


def test_extra_arguments_are_never_coerced_or_inspected(page):
    assert page.eval("""(()=>{
        let conversions=0;
        const extra=new Proxy({},{get(){conversions++;throw Error('unused extra');}});
        return navigator.vibrate(1,extra,Symbol('ignored'))===false&&conversions===0;
    })()""")


def test_noncallable_hook_diagnostic_does_not_rewrite_same_spelled_string_hook(page):
    result = evaluate(page, """
        try{navigator.vibrate({[Symbol.toPrimitive]:'[object Object]'});return null;}
        catch(error){return {name:error.name,message:error.message};}
    """)
    assert result == {
        "name": "TypeError",
        "message": "Failed to execute 'vibrate' on 'Navigator': '[object Object]' returned for property "
                   "'Symbol(Symbol.toPrimitive)' of object '#<Object>' is not a function",
    }


def test_noncallable_hook_diagnostics_do_not_read_user_metadata_or_proxy_traps(page):
    assert page.eval("""(()=>{
        const originalConstructor=Object.getOwnPropertyDescriptor(Object.prototype,'constructor');
        const originalTag=Object.getOwnPropertyDescriptor(Object.prototype,Symbol.toStringTag);
        let reads=0;
        Object.defineProperty(Object.prototype,'constructor',{configurable:true,get(){reads++;throw Error('constructor getter');}});
        Object.defineProperty(Object.prototype,Symbol.toStringTag,{configurable:true,get(){reads++;throw Error('tag getter');}});
        const hook=new Proxy({},{get(){reads++;throw Error('proxy get');},
            getPrototypeOf(){reads++;throw Error('proxy prototype');}});
        try {
            try{navigator.vibrate({[Symbol.toPrimitive]:hook});return false;}
            catch(error){return reads===0&&error.name==='TypeError'&&error.message.includes("'#<Object>' returned");}
        } finally {
            Object.defineProperty(Object.prototype,'constructor',originalConstructor);
            if(originalTag)Object.defineProperty(Object.prototype,Symbol.toStringTag,originalTag);
            else delete Object.prototype[Symbol.toStringTag];
        }
    })()""")


def test_prototype_references_and_cached_calls_have_distinct_safe_trace_entries(page):
    page.eval("""globalThis.vibrationNavigator=navigator;globalThis.vibrationPrototype=Navigator.prototype;
        globalThis.vibrationMethod=vibrationPrototype.vibrate;""")
    page.watch_apis(["navigator.vibrate"])
    page.start_access_trace()
    assert page.eval("vibrationPrototype.vibrate===vibrationMethod", name="vibration-reference.js")
    records = page.read_access_trace()["events"]
    assert event_keys(records) == [("host_object", "Navigator.prototype", "vibrate", "get")]
    assert records[0]["outcome"] == "returned" and records[0]["value_summary"] == "function"
    assert page.read_watch_api_hits() == []
    assert page.eval("vibrationMethod.call(vibrationNavigator,[1,2,3])", name="vibration-call.js") is False
    record, = page.read_access_trace()["events"]
    assert event_keys([record]) == [("navigator", "navigator", "vibrate", "call")]
    assert record["outcome"] == "returned" and record["return_summary"] == "boolean:false"
    assert record["argument_count"] == 1 and record["argument_summaries"] == ["array"]
    assert record["resource_name"] == "vibration-call.js" and record["source_kind"] == "eval"
    assert page.read_watch_api_hits() == [{"path": "navigator.vibrate", "type": "call",
                                           "resource_name": "vibration-call.js"}]


@pytest.mark.parametrize("expression,summary", [("vibrationMethod.call(vibrationNavigator)", []),
    ("vibrationMethod.call(vibrationNavigator,Symbol('bad'))", ["symbol"]),
    ("vibrationMethod.call(vibrationNavigator,[1n])", ["array"])])
def test_synchronous_failures_complete_trace_and_still_reach_watch(page, expression, summary):
    page.eval("globalThis.vibrationNavigator=navigator;globalThis.vibrationMethod=Navigator.prototype.vibrate;")
    page.watch_apis(["navigator.vibrate"])
    page.start_access_trace(max_events=1)
    with pytest.raises(iv8.JSError) as caught:
        page.eval(expression, name="vibration-error.js")
    assert caught.value.name == "TypeError"
    record, = page.read_access_trace(clear=False)["events"]
    assert event_keys([record]) == [("navigator", "navigator", "vibrate", "call")]
    assert record["outcome"] == "threw" and record["exception_name"] == "TypeError"
    assert "Failed to execute 'vibrate' on 'Navigator'" in record["exception_message"]
    assert record["argument_summaries"] == summary and "return_summary" not in record
    assert record["resource_name"] == "vibration-error.js"
    assert page.read_access_trace(clear=False)["dropped_events"] == 0
    assert len(page.read_watch_api_hits()) == 1


def test_user_conversion_reentry_is_observed_once_and_original_typeerror_is_not_rewritten(page):
    page.eval("""globalThis.vibrationNavigator=navigator;globalThis.vibrationMethod=Navigator.prototype.vibrate;
        globalThis.vibrationError=new TypeError('original user failure');globalThis.vibrationCount=0;
        globalThis.vibrationInput={valueOf(){vibrationCount++;vibrationNavigator.userAgent;throw vibrationError;}};""")
    page.start_access_trace()
    assert page.eval("""(()=>{try{vibrationMethod.call(vibrationNavigator,vibrationInput);return false;}
        catch(error){return error===vibrationError&&error.message==='original user failure';}})()""")
    records = page.read_access_trace()["events"]
    assert event_keys(records) == [
        ("navigator", "navigator", "vibrate", "call"),
        ("navigator", "navigator", "userAgent", "get"),
    ]
    assert records[0]["outcome"] == "threw" and records[0]["exception_name"] == "TypeError"
    assert "original user failure" in records[0]["exception_message"]
    assert "Failed to execute 'vibrate'" not in records[0]["exception_message"]
    assert records[1]["outcome"] == "returned"
    page.stop_access_trace()
    assert page.eval("vibrationCount") == 1


def test_native_bound_numeric_conversion_remains_a_user_entry(page):
    page.eval("""globalThis.vibrationNavigator=navigator;globalThis.vibrationMethod=Navigator.prototype.vibrate;
        globalThis.vibrationInput={valueOf:navigator.javaEnabled.bind(navigator)};""")
    page.start_access_trace()
    assert page.eval("vibrationMethod.call(vibrationNavigator,vibrationInput)") is False
    records = page.read_access_trace()["events"]
    assert event_keys(records) == [
        ("navigator", "navigator", "vibrate", "call"),
        ("navigator", "navigator", "javaEnabled", "call"),
    ]
    assert all(record["outcome"] == "returned" for record in records)


def test_filters_capacity_stop_and_watch_remain_independent(page):
    page.eval("globalThis.vibrationNavigator=navigator;globalThis.vibrationMethod=Navigator.prototype.vibrate;")
    page.watch_apis(["navigator.vibrate"])
    page.start_access_trace(max_events=1, object_types=["navigator"])
    page.eval("vibrationMethod.call(vibrationNavigator,1);vibrationMethod.call(vibrationNavigator,2);")
    report = page.read_access_trace(clear=False)
    assert event_keys(report["events"]) == [("navigator", "navigator", "vibrate", "call")]
    assert report["dropped_events"] == 1 and len(page.read_watch_api_hits()) == 2
    page.stop_access_trace()
    page.eval("vibrationMethod.call(vibrationNavigator,3)")
    assert page.read_access_trace(clear=False)["events"] == report["events"]
    assert len(page.read_watch_api_hits()) == 1


def test_reload_configuration_isolation_and_no_trusted_activation(page):
    page.watch_apis(["navigator.vibrate"])
    page.start_access_trace()
    previous = page.read_access_trace(clear=False)["generation"]
    page.load(HTML, "https://vibration.test/reloaded/")
    assert page.read_access_trace(clear=False)["events"] == []
    assert page.eval("navigator.vibrate(1)") is False
    records = page.read_access_trace()["events"]
    assert any(record["member"] == "vibrate" and record["operation"] == "call" for record in records)
    assert all(record["generation"] == previous + 1 for record in records)
    assert len(page.read_watch_api_hits()) == 1
    page.stop_access_trace()
    page.eval("window.dispatchEvent(new MouseEvent('click'))")
    assert page.eval("navigator.vibrate(1)===false&&!navigator.userActivation.hasBeenActive")
    with iv8.Page() as other:
        assert other.read_access_trace()["events"] == [] and other.read_watch_api_hits() == []


def test_debug_outputs_native_return_and_original_conversion_failure_and_prod_is_quiet(capsys):
    with iv8.Page() as value:
        value.load(HTML, "https://vibration.test/", mode="debug")
        assert capsys.readouterr().out == ""
        assert value.eval("navigator.vibrate([1,2,3])") is False
        assert capsys.readouterr().out.count("CALL navigator.vibrate(<Array>) -> false") == 1
        with pytest.raises(iv8.JSError):
            value.eval("navigator.vibrate(Symbol('bad'))")
        assert "CALL navigator.vibrate(<Symbol>) !! TypeError" in capsys.readouterr().out
        value.load(HTML, "https://vibration.test/", mode="prod")
        assert value.eval("navigator.vibrate(1)") is False
        assert capsys.readouterr().out == ""
