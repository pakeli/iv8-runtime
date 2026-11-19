"""Observe the two toString intrinsics while preserving strict receivers and native shape."""

import json
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")
BASE = "https://to-string-monitoring.test/page/"
HTML = "<html><head></head><body></body></html>"
APIS = ["Function.prototype.toString", "Object.prototype.toString"]


@pytest.fixture(scope="module")
def browser_reference():
    root = Path(__file__).parent / "fixtures" / "to_string_monitoring"
    data = json.loads((root / "chrome.json").read_text())["result"]
    source = (root / "probe.js").read_text()
    # Reuse the browser probe's receiver factories and side-effect observer,
    # independently of the native hook implementation.
    samples = source[source.index("  function state(receiver)"):source.index("  const receivers = {};")]
    shapes = source[source.index("  function functionShape(value)"):source.index("  const shapes = {};")]
    return {"result": data, "samples": samples, "shapes": shapes}


def bind_methods(page):
    page.eval("""globalThis.ts_methods={
        'Function.prototype.toString':Function.prototype.toString,
        'Object.prototype.toString':Object.prototype.toString};
        globalThis.ts_navigator=navigator;globalThis.ts_result=null;""")


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, BASE)
        bind_methods(value)
        yield value


def sample(page, browser_reference, label):
    page.eval("globalThis.ts_sample=(()=>{" + browser_reference["samples"] +
              "return factories[" + json.dumps(label) + "]();})()")


def observe(page, browser_reference, expression, name="to-string-call.js"):
    return json.loads(page.eval("(()=>{" + browser_reference["samples"] +
                                "return JSON.stringify(observe(()=>" + expression + ",ts_sample));})()", name=name))


def events(page):
    return page.read_access_trace(clear=False)["events"]


def entries(page):
    return [event for event in events(page) if event["label"] in ("Function.prototype", "Object.prototype")
            and event["member"] == "toString"]


def assert_entry(event, api, outcome, arguments=(), receiver_summary=None, receiver_label=None):
    assert (event["object_type"], event["label"], event["member"], event["operation"]) == (
        "host_object", api.rsplit(".", 1)[0], "toString", "call")
    assert event["argument_count"] == len(arguments)
    assert event["argument_summaries"] == list(arguments)
    assert isinstance(event["receiver_summary"], str) and event["receiver_summary"]
    if receiver_summary is not None:
        assert event["receiver_summary"] == receiver_summary
    if receiver_label is None:
        assert "receiver_label" not in event
    else:
        assert event["receiver_label"] == receiver_label
    if "returned" in outcome:
        assert event["outcome"] == "returned"
        assert event["return_summary"].startswith("string:" + str(len(outcome["returned"].encode("utf-8"))) + ":")
    else:
        assert event["outcome"] == "threw" and "return_summary" not in event
        if "name" in outcome:
            assert event["exception_name"] == outcome["name"]
            assert outcome["message"] in event["exception_message"]


@pytest.mark.parametrize("api", APIS)
def test_intrinsic_native_shape_descriptors_identity_and_self_inspection_match_chrome(page, browser_reference, api):
    expression = """(() => {
        const functionToString=ts_methods['Function.prototype.toString'],apply=Reflect.apply;
        """ + browser_reference["shapes"] + """
        const method=ts_methods[""" + json.dumps(api) + """],owner=""" + api.rsplit(".", 1)[0] + """;
        return JSON.stringify({function:functionShape(method),
            descriptor:descriptorShape(Object.getOwnPropertyDescriptor(owner,'toString')),
            ownDescriptors:{name:descriptorShape(Object.getOwnPropertyDescriptor(method,'name')),
                length:descriptorShape(Object.getOwnPropertyDescriptor(method,'length'))},
            repeatedIdentity:method===owner.toString});
    })()"""
    expected = browser_reference["result"]["shapes"][api]
    assert json.loads(page.eval(expression)) == expected
    page.start_access_trace()
    assert json.loads(page.eval(expression)) == expected
    records = entries(page)
    assert len(records) == 2
    assert all(record["label"] == "Function.prototype" and record["outcome"] == "returned" for record in records)
    assert all(record["receiver_summary"] == "function" and "receiver_label" not in record for record in records)
    page.stop_access_trace()
    assert json.loads(page.eval(expression)) == expected
    assert not hasattr(iv8.Page, "watch_function_to_string")


@pytest.mark.parametrize("api", APIS)
@pytest.mark.parametrize("form", ["call", "apply", "bind", "reflectApply"])
def test_cached_before_start_intrinsic_and_bound_invocations_record_once(page, browser_reference, api, form):
    label = "ordinaryFunction" if api.startswith("Function") else "customTag"
    sample(page, browser_reference, label)
    method = "ts_methods[" + json.dumps(api) + "]"
    page.eval("globalThis.ts_bound=" + method + ".bind(ts_sample.receiver)")
    expressions = {"call": method + ".call(ts_sample.receiver)",
                   "apply": method + ".apply(ts_sample.receiver,[])",
                   "bind": "ts_bound()", "reflectApply": "Reflect.apply(" + method + ",ts_sample.receiver,[])"}
    page.start_access_trace(max_events=1)
    actual = observe(page, browser_reference, expressions[form])
    expected = browser_reference["result"]["forms"][api][label][form]
    assert actual == expected
    record, = events(page)
    assert_entry(record, api, expected["outcome"], receiver_summary="function" if api.startswith("Function") else "object")
    assert record["source_kind"] == "eval" and record["resource_name"] == "to-string-call.js"
    assert page.read_access_trace(clear=False)["dropped_events"] == 0


@pytest.mark.parametrize("api", APIS)
def test_detached_prototype_member_and_natural_member_calls_preserve_receivers(page, browser_reference, api):
    reference = browser_reference["result"]["direct"]
    is_function = api.startswith("Function")
    label = "ordinaryFunction" if is_function else "customTag"
    natural_key = "functionNaturalMember" if is_function else "objectNaturalMember"
    prototype_key = "functionPrototypeMember" if is_function else "objectPrototypeMember"
    expressions = [("ts_detached()", api), (api + "()", prototype_key),
                   ("ts_sample.receiver.toString()", natural_key)]
    page.eval("globalThis.ts_detached=ts_methods[" + json.dumps(api) + "]")
    for expression, key in expressions:
        sample(page, browser_reference, label)
        page.start_access_trace(max_events=1)
        actual = observe(page, browser_reference, expression)
        assert actual == reference[key]
        record, = events(page)
        assert_entry(record, api, reference[key]["outcome"])


@pytest.mark.parametrize("label", ["undefined", "null", "boolean", "number", "string", "bigint", "symbol"])
def test_strict_null_undefined_and_primitive_receivers_match_both_chrome_intrinsics(page, browser_reference, label):
    expected = browser_reference["result"]["receivers"][label]
    records = []
    for api in APIS:
        sample(page, browser_reference, label)
        page.start_access_trace(max_events=1)
        actual = observe(page, browser_reference, "Reflect.apply(ts_methods[" + json.dumps(api) + "],ts_sample.receiver,[])")
        assert actual == expected[api]
        record, = events(page)
        receiver_summary = {"undefined": "undefined", "null": "null", "boolean": "boolean:true",
                            "number": "number:17", "string": "string:6:sample", "bigint": "bigint",
                            "symbol": "symbol"}[label]
        assert_entry(record, api, expected[api]["outcome"], receiver_summary=receiver_summary)
        records.append(record)
    assert records[0]["object_id"] != records[1]["object_id"]


@pytest.mark.parametrize("label", ["callableProxy", "objectProxy", "revokedCallableProxy", "revokedObjectProxy"])
def test_proxy_and_revoked_proxy_results_errors_and_traps_match_chrome(page, browser_reference, label):
    for api in APIS:
        sample(page, browser_reference, label)
        page.start_access_trace(max_events=1)
        actual = observe(page, browser_reference, "Reflect.apply(ts_methods[" + json.dumps(api) + "],ts_sample.receiver,[])")
        expected = browser_reference["result"]["receivers"][label][api]
        assert actual == expected
        record, = events(page)
        assert_entry(record, api, expected["outcome"],
                     receiver_summary="function" if "Callable" in label or label == "callableProxy" else "object")
        assert page.read_access_trace(clear=False)["dropped_events"] == 0


def test_tag_getters_are_called_once_and_nonstring_tags_are_not_coerced(page, browser_reference):
    for label in ("customTag", "emptyTag", "numericTag", "boxedTag", "callableCustomTag"):
        for api in APIS:
            sample(page, browser_reference, label)
            page.start_access_trace(max_events=1)
            actual = observe(page, browser_reference, "Reflect.apply(ts_methods[" + json.dumps(api) + "],ts_sample.receiver,[])")
            expected = browser_reference["result"]["receivers"][label][api]
            assert actual == expected
            record, = events(page)
            assert_entry(record, api, expected["outcome"])


def test_throwing_tag_getters_preserve_object_string_and_undefined_sentinel_identity(page, browser_reference):
    for label in ("throwingObjectTag", "throwingStringTag", "throwingUndefinedTag"):
        for api in APIS:
            expected = browser_reference["result"]["receivers"][label][api]
            for enabled in (False, True):
                page.stop_access_trace()
                sample(page, browser_reference, label)
                page.read_access_trace(clear=True)
                if enabled:
                    page.start_access_trace(max_events=1)
                actual = observe(page, browser_reference, "Reflect.apply(ts_methods[" + json.dumps(api) + "],ts_sample.receiver,[])")
                assert actual == expected
                if enabled:
                    record, = events(page)
                    assert_entry(record, api, expected["outcome"])
                else:
                    assert events(page) == []


def test_observation_never_reads_function_name_or_ordinary_conversion_properties(page, browser_reference):
    for label in ("poisonedFunctionName", "poisonedOrdinaryProperties"):
        for api in APIS:
            sample(page, browser_reference, label)
            page.start_access_trace(max_events=1)
            actual = observe(page, browser_reference, "Reflect.apply(ts_methods[" + json.dumps(api) + "],ts_sample.receiver,[])")
            expected = browser_reference["result"]["receivers"][label][api]
            assert actual == expected
            record, = events(page)
            assert_entry(record, api, expected["outcome"])


@pytest.mark.parametrize("api", APIS)
def test_actual_extra_arguments_and_complete_source_are_captured_without_inspecting_values(page, api):
    source = "function LongSample(){/*" + "汉😀\n" * 1000 + "*/return 1;}"
    page.eval("""globalThis.ts_effects=0;const trap=()=>{ts_effects++;throw Error('must not inspect');};
        globalThis.ts_poison=new Proxy({},{get:trap,getPrototypeOf:trap,getOwnPropertyDescriptor:trap});
        globalThis.ts_error=Error('argument');for(const name of ['name','message','stack'])
            Object.defineProperty(ts_error,name,{get:trap,configurable:true});
        globalThis.ts_arguments=[0,1,2,3,4,5,6,7,8,9,ts_poison,ts_error];""")
    receiver = "(" + source + ")" if api.startswith("Function") else "({})"
    page.eval("globalThis.ts_receiver=" + receiver)
    page.eval("globalThis.ts_bound_extra=ts_methods[" + json.dumps(api) + "].bind(ts_receiver,0)")
    page.start_access_trace(max_events=1)
    expected = source if api.startswith("Function") else "[object Object]"
    assert page.eval("ts_bound_extra(...ts_arguments.slice(1))", name="to-string-args.js") == expected
    record, = events(page)
    assert_entry(record, api, {"returned": expected},
                 [*["number:" + str(index) for index in range(10)], "object", "object"],
                 receiver_summary="function" if api.startswith("Function") else "object")
    assert page.eval("ts_effects") == 0
    rendered = page.read_access_trace(clear=False, format="text")
    assert len(rendered.splitlines()) == 1
    rendered_receiver = "<Function>" if api.startswith("Function") else "<Object>"
    assert rendered.startswith("CALL " + api + ".call(" + rendered_receiver +
                               ", 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, <Object>, <Object>) -> ")
    returned = rendered.split(" -> ", 1)[1]
    assert json.loads(returned) == expected


@pytest.mark.parametrize("api", APIS)
def test_debug_outputs_one_intrinsic_call_and_prod_preserves_behavior_without_output(capsys, browser_reference, api):
    label = "ordinaryFunction" if api.startswith("Function") else "customTag"
    expected = browser_reference["result"]["receivers"][label][api]["outcome"]["returned"]
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        assert capsys.readouterr().out == ""
        bind_methods(page)
        sample(page, browser_reference, label)
        capsys.readouterr()
        expression = "ts_methods[" + json.dumps(api) + "].call(ts_sample.receiver)"
        assert page.eval(expression) == expected
        receiver = "<Function>" if api.startswith("Function") else "<Object>"
        assert capsys.readouterr().out.splitlines() == ["CALL " + api + ".call(" + receiver + ") -> " + json.dumps(expected)]
        assert page.read_access_trace()["events"] == []
        page.load(HTML, BASE, mode="prod")
        bind_methods(page)
        sample(page, browser_reference, label)
        assert page.eval(expression) == expected
        assert capsys.readouterr().out == ""
        assert not page.read_access_trace()["enabled"]


def test_nested_tag_getter_reenters_user_origin_and_keeps_outer_inner_completions(page):
    page.eval("""globalThis.ts_subject=function Subject(){return 7;};globalThis.ts_tag_reads=0;
        globalThis.ts_tagged={get [Symbol.toStringTag](){
            ts_tag_reads++;ts_navigator.hardwareConcurrency;
            return ts_methods['Function.prototype.toString'].call(ts_subject);
        }};""")
    page.start_access_trace(max_events=3)
    expected_source = "function Subject(){return 7;}"
    assert page.eval("ts_methods['Object.prototype.toString'].call(ts_tagged)") == "[object " + expected_source + "]"
    records = events(page)
    assert [(event["object_type"], event["label"], event["member"], event["operation"])
            for event in records] == [
        ("host_object", "Object.prototype", "toString", "call"),
        ("navigator", "navigator", "hardwareConcurrency", "get"),
        ("host_object", "Function.prototype", "toString", "call"),
    ]
    assert all(event["outcome"] == "returned" for event in records)
    assert records[0]["object_id"] != records[2]["object_id"]
    page.stop_access_trace()
    assert page.eval("ts_tag_reads") == 1


def test_runtime_load_stays_quiet_and_user_load_scripts_and_callbacks_remain_observable(page):
    page.start_access_trace(max_events=2, object_types=["host_object"])
    page.load(HTML, BASE)
    assert page.read_access_trace(clear=False)["events"] == []
    assert page.read_access_trace(clear=False)["dropped_events"] == 0
    page.load("<script>Object.prototype.toString.call(null)</script>", BASE,
              scripts=[{"name": "to-string-host.js", "code": "Function.prototype.toString.call(function Host(){})"}])
    records = entries(page)
    assert [event["label"] for event in records] == ["Object.prototype", "Function.prototype"]
    assert [event["resource_name"] for event in records] == [BASE + "#inline-script-1", "to-string-host.js"]
    assert [event["source_kind"] for event in records] == ["html_script", "eval"]
    assert page.read_access_trace(clear=False)["dropped_events"] == 0
    page.stop_access_trace()
    bind_methods(page)
    page.eval("""setTimeout(()=>ts_methods['Function.prototype.toString'].call(function Timer(){}),0);
        Promise.resolve().then(()=>ts_methods['Object.prototype.toString'].call(undefined));""")
    page.start_access_trace(max_events=2, object_types=["host_object"])
    page.run_timers()
    page.run_jobs()
    assert sorted(event["label"] for event in entries(page)) == ["Function.prototype", "Object.prototype"]
    assert all(event["outcome"] == "returned" for event in entries(page))
    assert page.read_access_trace(clear=False)["dropped_events"] == 0


def test_filters_capacity_stopping_and_clear_apply_to_fixed_entrypoint_tokens(page):
    page.start_access_trace(max_events=2, object_types=["host_object"])
    page.eval("ts_methods['Function.prototype.toString'].call(function A(){});"
              "ts_methods['Object.prototype.toString'].call({});"
              "ts_methods['Function.prototype.toString'].call(function B(){})")
    before = page.read_access_trace(clear=False)
    assert [event["label"] for event in before["events"]] == ["Function.prototype", "Object.prototype"]
    assert len({event["object_id"] for event in before["events"]}) == 2
    assert before["dropped_events"] == 1
    page.stop_access_trace()
    page.eval("ts_methods['Object.prototype.toString'].call({})")
    assert page.read_access_trace(clear=False)["events"] == before["events"]
    assert page.read_access_trace(clear=False)["dropped_events"] == 1
    page.read_access_trace(clear=True)
    assert page.read_access_trace(clear=False)["events"] == []
    page.start_access_trace(object_types=["navigator"])
    page.eval("ts_methods['Object.prototype.toString'].call({});ts_navigator.userAgent")
    assert [(event["object_type"], event["member"]) for event in events(page)] == [("navigator", "userAgent")]


def test_reload_and_page_isolation_keep_generation_owned_intrinsic_identities(page):
    old_method = page.eval("ts_methods['Object.prototype.toString']")
    page.start_access_trace(object_types=["host_object"])
    page.eval("ts_methods['Function.prototype.toString'].call(function A(){});ts_methods['Object.prototype.toString'].call({})")
    before = entries(page)
    with iv8.Page() as other:
        other.load(HTML, BASE)
        bind_methods(other)
        other.eval("ts_methods['Object.prototype.toString'].call({})")
        assert other.read_access_trace()["events"] == []
        page.load(HTML, BASE)
        bind_methods(page)
        page.read_access_trace(clear=True)
        page.eval("ts_methods['Function.prototype.toString'].call(function B(){});ts_methods['Object.prototype.toString'].call({})")
        after = entries(page)
        assert len(before) == len(after) == 2
        assert [event["label"] for event in after] == [event["label"] for event in before]
        assert [event["receiver_summary"] for event in before] == ["function", "object"]
        assert [event["receiver_summary"] for event in after] == ["function", "object"]
        assert {event["generation"] for event in after} == {before[0]["generation"] + 1}
        assert {event["object_id"] for event in before}.isdisjoint(event["object_id"] for event in after)
        assert not old_method.context_alive


def test_user_overrides_delete_and_redefinition_do_not_fake_native_calls_but_caches_still_work(page):
    page.start_access_trace(object_types=["host_object"])
    assert page.eval("""(() => {
        const fn=function User(){return 1;},object={};
        const fp=Object.getOwnPropertyDescriptor(Function.prototype,'toString');
        const op=Object.getOwnPropertyDescriptor(Object.prototype,'toString');
        try {
            Function.prototype.toString=()=> 'user function';
            Object.prototype.toString=()=> 'user object';
            if(fn.toString()!=='user function' || object.toString()!=='user object')return false;
            if(!delete Function.prototype.toString || !delete Object.prototype.toString)return false;
            if(Object.hasOwn(Function.prototype,'toString') || Object.hasOwn(Object.prototype,'toString'))return false;
            const functionSource=ts_methods['Function.prototype.toString'].call(fn);
            const objectSource=ts_methods['Object.prototype.toString'].call(object);
            return functionSource==='function User(){return 1;}' && objectSource==='[object Object]';
        } finally {
            Object.defineProperty(Function.prototype,'toString',fp);
            Object.defineProperty(Object.prototype,'toString',op);
        }
    })()""")
    records = entries(page)
    assert [event["label"] for event in records] == ["Function.prototype", "Object.prototype"]
    assert all(event["outcome"] == "returned" for event in records)
    page.stop_access_trace()
    assert page.eval("Function.prototype.toString===ts_methods['Function.prototype.toString'] && "
                     "Object.prototype.toString===ts_methods['Object.prototype.toString']")


def test_receiver_fields_distinguish_prototypes_native_methods_documents_and_observer_functions(page):
    page.eval("""globalThis.ts_document=document;globalThis.ts_native_method=document.createElement;
        globalThis.ts_name_reads=0;Object.defineProperty(ts_native_method,'name',{
            configurable:true,get(){ts_name_reads++;throw Error('must not read native function name');}});""")
    samples = [
        ("Function.prototype.toString", "Function.prototype", "function", None,
         "function () { [native code] }"),
        ("Object.prototype.toString", "Object.prototype", "object", None, "[object Object]"),
        ("Function.prototype.toString", "ts_native_method", "function", None,
         "function createElement() { [native code] }"),
        ("Object.prototype.toString", "ts_document", "host_object", "document", "[object HTMLDocument]"),
        ("Function.prototype.toString", "ts_methods['Function.prototype.toString']", "function", None,
         "function toString() { [native code] }"),
        ("Function.prototype.toString", "ts_methods['Object.prototype.toString']", "function", None,
         "function toString() { [native code] }"),
    ]
    for api, receiver, summary, label, expected in samples:
        page.start_access_trace(max_events=1)
        assert page.eval("ts_methods[" + json.dumps(api) + "].call(" + receiver + ")") == expected
        record, = events(page)
        assert_entry(record, api, {"returned": expected}, receiver_summary=summary, receiver_label=label)
        assert record["argument_count"] == 0 and record["argument_summaries"] == []
        assert page.read_access_trace(clear=False)["dropped_events"] == 0
    page.stop_access_trace()
    assert page.eval("ts_name_reads") == 0


def test_proxy_wrapping_a_host_receiver_is_not_unwrapped_or_given_a_native_receiver_label(page):
    page.eval("""globalThis.ts_host_gets=[];globalThis.ts_host_proxy=new Proxy(document,{
        get(target,key,receiver){ts_host_gets.push(String(key));return Reflect.get(target,key,receiver);},
        getPrototypeOf(){throw Error('must not inspect receiver prototype');},
        getOwnPropertyDescriptor(){throw Error('must not inspect receiver descriptor');}
    });""")
    page.start_access_trace(max_events=1)
    expected = "[object HTMLDocument]"
    assert page.eval("ts_methods['Object.prototype.toString'].call(ts_host_proxy)") == expected
    record, = events(page)
    assert_entry(record, "Object.prototype.toString", {"returned": expected}, receiver_summary="object")
    page.stop_access_trace()
    assert page.eval("ts_host_gets", to_py=True) == ["Symbol(Symbol.toStringTag)"]


def test_debug_document_receiver_label_uses_native_metadata_and_prod_stays_silent(capsys):
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        assert capsys.readouterr().out == ""
        bind_methods(page)
        setup = """globalThis.ts_document=document;globalThis.ts_label_reads=0;
            for(const name of ['name','constructor'])Object.defineProperty(ts_document,name,{
                configurable:true,get(){ts_label_reads++;throw Error('diagnostics must not read '+name);}});"""
        page.eval(setup)
        capsys.readouterr()
        expression = "ts_methods['Object.prototype.toString'].call(ts_document)"
        assert page.eval(expression) == "[object HTMLDocument]"
        assert capsys.readouterr().out.splitlines() == [
            'CALL Object.prototype.toString.call(<HostObject: document>) -> "[object HTMLDocument]"']
        assert page.eval("ts_label_reads") == 0
        assert capsys.readouterr().out == ""
        page.load(HTML, BASE, mode="prod")
        bind_methods(page)
        page.eval(setup)
        assert page.eval(expression) == "[object HTMLDocument]"
        assert page.eval("ts_label_reads") == 0
        assert capsys.readouterr().out == ""
