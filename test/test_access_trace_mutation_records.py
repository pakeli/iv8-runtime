"""Observe MutationRecord snapshot fields without changing their plain data shape."""

import json

import pytest

import iv8


pytestmark = pytest.mark.skipif(
    not getattr(iv8, "_v8_linked", False), reason="requires a V8-linked build")
BASE = "https://mutation-fields.test/page/"
HTML = ('<html><head></head><body><div id="target" data-seed="before">'
        '<span id="left">old</span><span id="right">next</span></div>'
        '<aside id="sink"></aside></body></html>')
FIELDS = ["type", "target", "addedNodes", "removedNodes", "previousSibling",
          "nextSibling", "attributeName", "attributeNamespace", "oldValue"]
ATTRIBUTE_VALUES = ["attributes", True, 0, 0, True, True, "data-seed", None, "before"]


def bind_nodes(page):
    page.eval("""globalThis.mutation_target=document.getElementById('target');
        globalThis.mutation_left=document.getElementById('left');
        globalThis.mutation_right=document.getElementById('right');
        globalThis.mutation_sink=document.getElementById('sink');
        globalThis.mutation_text=mutation_left.firstChild;
        globalThis.mutation_navigator=navigator;
        globalThis.mutation_record=null;globalThis.mutation_seen=[];
        globalThis.mutation_delivery_count=0;
        globalThis.mutation_expected_target=mutation_target;
        globalThis.mutation_expected_previous=null;globalThis.mutation_expected_next=null;
        globalThis.mutation_fields=""" + json.dumps(FIELDS) + """;
        globalThis.mutation_snapshot=record=>[record.type,record.target===mutation_expected_target,
            record.addedNodes.length,record.removedNodes.length,
            record.previousSibling===mutation_expected_previous,record.nextSibling===mutation_expected_next,
            record.attributeName,record.attributeNamespace,record.oldValue];""",
              name="mutation-setup.js")


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, BASE)
        bind_nodes(value)
        yield value


def queue_attribute(page, callback="()=>{}", old_value=True):
    page.eval("globalThis.mutation_observer=new MutationObserver(" + callback + ");"
              "mutation_observer.observe(mutation_target,{attributes:true,attributeOldValue:" +
              str(old_value).lower() + "});"
              "globalThis.mutation_take=mutation_observer.takeRecords;"
              "mutation_target.setAttribute('data-seed','after');")


def cache_attribute(page, old_value=True):
    queue_attribute(page, old_value=old_value)
    page.eval("globalThis.mutation_records=mutation_take.call(mutation_observer);"
              "mutation_record=mutation_records[0]")


def record_events(page):
    return [event for event in page.read_access_trace(clear=False)["events"]
            if event["label"] == "MutationRecord"]


def assert_field_gets(page, expected_fields=FIELDS):
    records = record_events(page)
    assert [(event["object_type"], event["member"], event["operation"])
            for event in records] == [("host_object", field, "get") for field in expected_fields]
    assert all(event["outcome"] == "returned" for event in records)
    assert page.read_access_trace(clear=False)["dropped_events"] == 0
    return records


def test_cached_take_records_fields_have_completed_gets_and_one_record_identity(page):
    cache_attribute(page)
    page.watch_apis(["MutationRecord." + field for field in FIELDS])
    page.start_access_trace(max_events=9)
    assert page.eval("mutation_snapshot(mutation_record)", to_py=True,
                     name="mutation-fields.js") == ATTRIBUTE_VALUES
    records = assert_field_gets(page)
    assert len({event["object_id"] for event in records}) == 1
    assert [event["seq"] for event in records] == list(range(1, 10))
    assert [event["value_summary"] for event in records] == [
        "string:10:attributes", "host_object", "array", "array", "null", "null",
        "string:9:data-seed", "null", "string:6:before"]
    assert all(event["source_kind"] == "eval" and event["resource_name"] == "mutation-fields.js"
               for event in records)
    assert page.read_watch_api_hits() == []


def test_all_writable_fields_keep_exact_assigned_values_without_coercion(page):
    cache_attribute(page)
    page.eval("""globalThis.mutation_replacements=['user-type',mutation_sink,[mutation_left],
        [mutation_right],mutation_left,mutation_right,'manual','urn:manual',{marker:1}];""")
    page.watch_apis(["MutationRecord." + field for field in FIELDS])
    page.start_access_trace(max_events=18)
    assert page.eval("""mutation_fields.forEach((field,index)=>{
        mutation_record[field]=mutation_replacements[index];});
        mutation_fields.every((field,index)=>mutation_record[field]===mutation_replacements[index]);
    """, name="mutation-write.js")
    records = record_events(page)
    assert [(event["member"], event["operation"]) for event in records] == [
        *[(field, "set") for field in FIELDS], *[(field, "get") for field in FIELDS]]
    expected = ["string:9:user-type", "host_object", "array", "array", "host_object",
                "host_object", "string:6:manual", "string:10:urn:manual", "object"]
    assert [event["value_summary"] for event in records] == expected * 2
    assert all(event["object_type"] == "host_object" and event["outcome"] == "returned"
               for event in records)
    assert len({event["object_id"] for event in records}) == 1
    assert page.read_access_trace(clear=False)["dropped_events"] == 0
    assert page.read_watch_api_hits() == []
    page.stop_access_trace()
    assert page.eval("mutation_target.getAttribute('data-seed')") == "after"


@pytest.mark.parametrize("kind,setup,expected", [
    ("insert", """globalThis.mutation_inserted=document.createElement('b');
        mutation_expected_previous=mutation_left;mutation_expected_next=mutation_right;
        globalThis.mutation_observer=new MutationObserver(()=>{});
        mutation_observer.observe(mutation_target,{childList:true});
        mutation_target.insertBefore(mutation_inserted,mutation_right);""",
     ["childList", True, 1, 0, True, True, None, None, None]),
    ("remove", """mutation_expected_next=mutation_right;
        globalThis.mutation_observer=new MutationObserver(()=>{});
        mutation_observer.observe(mutation_target,{childList:true});
        mutation_target.removeChild(mutation_left);""",
     ["childList", True, 0, 1, True, True, None, None, None]),
    ("new-attribute", """globalThis.mutation_observer=new MutationObserver(()=>{});
        mutation_observer.observe(mutation_target,{attributes:true,attributeOldValue:true});
        mutation_target.setAttribute('data-new','value');""",
     ["attributes", True, 0, 0, True, True, "data-new", None, None]),
    ("namespace-old-value", """mutation_target.setAttributeNS('urn:mutation','p:seed','before');
        globalThis.mutation_observer=new MutationObserver(()=>{});
        mutation_observer.observe(mutation_target,{attributes:true,attributeOldValue:true});
        mutation_target.setAttributeNS('urn:mutation','q:seed','after');""",
     ["attributes", True, 0, 0, True, True, "seed", "urn:mutation", "before"]),
    ("character-data", """mutation_expected_target=mutation_text;
        globalThis.mutation_observer=new MutationObserver(()=>{});
        mutation_observer.observe(mutation_text,{characterData:true,characterDataOldValue:true});
        mutation_text.data='changed';""",
     ["characterData", True, 0, 0, True, True, None, None, "old"]),
])
def test_record_kind_values_nulls_namespaces_and_old_values_are_preserved(page, kind, setup, expected):
    page.eval(setup + "mutation_record=mutation_observer.takeRecords()[0]")
    page.start_access_trace(max_events=9)
    assert page.eval("mutation_snapshot(mutation_record)", to_py=True) == expected
    records = assert_field_gets(page)
    assert len({event["object_id"] for event in records}) == 1


def test_old_value_stays_null_when_capture_is_not_requested(page):
    cache_attribute(page, old_value=False)
    page.start_access_trace(max_events=1)
    assert page.eval("mutation_record.oldValue") is None
    record, = assert_field_gets(page, ["oldValue"])
    assert record["value_summary"] == "null"


def test_async_observer_field_reads_reenter_user_origin_without_setup_noise(page):
    queue_attribute(page, callback="""records=>{
        mutation_seen=mutation_snapshot(records[0]);
        mutation_seen.push(mutation_navigator.hardwareConcurrency);
    }""")
    page.start_access_trace(max_events=10)
    page.run_jobs()
    records = assert_field_gets(page)
    assert len({event["object_id"] for event in records}) == 1
    report = page.read_access_trace(clear=False)
    assert len(report["events"]) == 10
    assert [(event["label"], event["member"], event["operation"])
            for event in report["events"][-1:]] == [("navigator", "hardwareConcurrency", "get")]
    assert not [event for event in records if event["operation"] == "set"]
    page.stop_access_trace()
    assert page.eval("mutation_seen", to_py=True) == ATTRIBUTE_VALUES + [8]


def test_delivery_without_record_field_use_is_quiet_and_does_not_spend_capacity(page):
    queue_attribute(page, callback="records=>{mutation_record=records[0];mutation_delivery_count++;}")
    page.start_access_trace(max_events=1)
    page.run_jobs()
    before = page.read_access_trace(clear=False)
    assert before["events"] == [] and before["dropped_events"] == 0
    assert page.eval("mutation_record.type") == "attributes"
    record, = assert_field_gets(page, ["type"])
    assert record["seq"] == 1
    page.stop_access_trace()
    assert page.eval("mutation_delivery_count") == 1


def test_take_records_call_keeps_materialization_internal_and_arrays_ordinary(page):
    queue_attribute(page)
    page.start_access_trace(max_events=1)
    value = page.eval("mutation_record=mutation_take.call(mutation_observer)[0]",
                      name="mutation-take.js")
    assert isinstance(value, iv8.JSValue) and value.context_alive
    report = page.read_access_trace(clear=False)
    call, = report["events"]
    assert (call["object_type"], call["label"], call["member"], call["operation"]) == (
        "host_object", "MutationObserver", "takeRecords", "call")
    assert call["outcome"] == "returned" and call["return_summary"] == "array"
    assert call["argument_count"] == 0 and report["dropped_events"] == 0
    page.run_jobs()
    assert page.read_access_trace(clear=False)["events"] == report["events"]
    page.stop_access_trace()
    assert page.eval("Array.isArray(mutation_record.addedNodes) && Array.isArray(mutation_record.removedNodes)")


def test_record_identities_are_distinct_while_target_node_wrappers_stay_canonical(page):
    page.eval("""globalThis.mutation_observer=new MutationObserver(()=>{});
        mutation_observer.observe(mutation_target,{attributes:true});
        mutation_target.setAttribute('data-one','1');mutation_target.setAttribute('data-two','2');
        globalThis.mutation_records=mutation_observer.takeRecords();
        globalThis.mutation_first=mutation_records[0];globalThis.mutation_second=mutation_records[1];""")
    page.start_access_trace(max_events=4)
    assert page.eval("mutation_first.type;mutation_first.target===mutation_target && "
                     "mutation_second.type==='attributes' && mutation_second.target===mutation_target")
    records = assert_field_gets(page, ["type", "target", "type", "target"])
    assert records[0]["object_id"] == records[1]["object_id"]
    assert records[2]["object_id"] == records[3]["object_id"] != records[0]["object_id"]
    page.stop_access_trace()
    assert page.eval("mutation_first!==mutation_second && mutation_first.target===mutation_second.target && "
                     "mutation_first.addedNodes!==mutation_second.addedNodes")


def test_plain_object_json_prototype_descriptors_and_extensibility_are_unchanged(page):
    cache_attribute(page)
    serialized = page.eval("JSON.stringify(mutation_record)")
    shape = """JSON.stringify({keys:Object.keys(mutation_record),
        objectPrototype:Object.getPrototypeOf(mutation_record)===Object.prototype,
        constructor:mutation_record.constructor===Object,extensible:Object.isExtensible(mutation_record),
        frozen:Object.isFrozen(mutation_record),global:typeof MutationRecord,
        descriptors:mutation_fields.map(field=>{const descriptor=Object.getOwnPropertyDescriptor(mutation_record,field);
            return ['value'in descriptor,'get'in descriptor,descriptor.writable,
                descriptor.enumerable,descriptor.configurable];})})"""
    expected = {"keys": FIELDS, "objectPrototype": True, "constructor": True,
                "extensible": True, "frozen": False, "global": "undefined",
                "descriptors": [[True, False, True, True, True]] * 9}
    assert json.loads(page.eval(shape)) == expected
    page.start_access_trace()
    assert json.loads(page.eval(shape)) == expected
    assert page.eval("JSON.stringify(mutation_record)") == serialized
    gets = [event for event in record_events(page) if event["operation"] == "get"]
    assert {event["member"] for event in gets} == set(FIELDS)
    assert all(event["outcome"] == "returned" for event in gets)
    page.stop_access_trace()
    assert json.loads(page.eval(shape)) == expected


def test_delete_and_user_redefinition_preserve_current_data_property_behavior(page):
    cache_attribute(page)
    page.start_access_trace()
    assert page.eval("mutation_record.type='custom';mutation_record.type") == "custom"
    assert [(event["member"], event["operation"]) for event in record_events(page)] == [
        ("type", "set"), ("type", "get")]
    page.read_access_trace(clear=True)
    assert page.eval("""(() => {
        const deleted=delete mutation_record.type;
        Object.defineProperty(mutation_record,'type',{value:'manual',writable:true,
            enumerable:true,configurable:true});
        const descriptor=Object.getOwnPropertyDescriptor(mutation_record,'type');
        return deleted && mutation_record.type==='manual' && descriptor.value==='manual' &&
            descriptor.writable && descriptor.enumerable && descriptor.configurable;
    })()""")
    assert not [event for event in record_events(page) if event["member"] == "type"]
    page.eval("globalThis.mutation_getter_reads=0")
    page.read_access_trace(clear=True)
    page.eval("""Object.defineProperty(mutation_record,'oldValue',{
        get(){mutation_getter_reads++;return 'accessor';},configurable:true});""")
    # Reflection can materialize the previous native data value. Isolate the
    # later user accessor read, whose replacement ends this native field path.
    page.read_access_trace(clear=True)
    assert page.eval("mutation_record.oldValue==='accessor' && mutation_getter_reads===1")
    assert not [event for event in record_events(page) if event["member"] == "oldValue"]


def test_explicit_freeze_keeps_reads_and_rejects_writes_with_original_data_semantics(page):
    cache_attribute(page)
    assert page.eval("Object.freeze(mutation_record);Object.isFrozen(mutation_record)")
    page.start_access_trace(max_events=1)
    assert page.eval("""(() => {
        const before=mutation_record.type;mutation_record.type='ignored';let rejected=false;
        try{(()=>{'use strict';mutation_record.type='ignored';})()}catch(error){rejected=error instanceof TypeError;}
        return before==='attributes' && rejected;
    })()""")
    assert_field_gets(page, ["type"])


@pytest.mark.parametrize("payload_kind", ["proxy", "error"])
def test_value_summaries_do_not_coerce_or_read_proxy_and_error_getters(page, payload_kind):
    cache_attribute(page)
    page.eval("""globalThis.mutation_effects=[];globalThis.mutation_sentinel=Error('must not run');
        globalThis.mutation_trap=()=>{mutation_effects.push('trap');throw mutation_sentinel;};""")
    if payload_kind == "proxy":
        page.eval("""globalThis.mutation_payload=new Proxy({},{get:mutation_trap,
            getPrototypeOf:mutation_trap,getOwnPropertyDescriptor:mutation_trap,
            ownKeys:mutation_trap,has:mutation_trap});""")
    else:
        page.eval("""globalThis.mutation_payload=Error('payload');
            for(const name of ['name','message','stack'])Object.defineProperty(mutation_payload,name,
                {get:mutation_trap,configurable:true});""")
    page.start_access_trace(max_events=2)
    assert page.eval("mutation_record.oldValue=mutation_payload;mutation_record.oldValue===mutation_payload")
    records = record_events(page)
    assert [(event["member"], event["operation"], event["value_summary"])
            for event in records] == [("oldValue", "set", "object"), ("oldValue", "get", "object")]
    assert all(event["outcome"] == "returned" for event in records)
    assert page.read_access_trace(clear=False)["dropped_events"] == 0
    page.stop_access_trace()
    assert page.eval("mutation_effects.length") == 0


def test_filters_stopping_and_capacity_keep_record_and_node_channels_distinct(page):
    cache_attribute(page)
    page.start_access_trace(max_events=2, object_types=["host_object"])
    page.eval("mutation_record.type;mutation_record.attributeName;mutation_record.oldValue")
    before = page.read_access_trace(clear=False)
    assert [(event["member"], event["operation"]) for event in before["events"]] == [
        ("type", "get"), ("attributeName", "get")]
    assert before["dropped_events"] == 1
    page.stop_access_trace()
    page.eval("mutation_record.type='stopped';mutation_record.oldValue")
    after = page.read_access_trace(clear=False)
    assert after["events"] == before["events"] and after["dropped_events"] == 1
    page.start_access_trace(object_types=["element"])
    assert page.eval("mutation_record.type;mutation_record.target.id") == "target"
    assert [(event["object_type"], event["label"], event["member"], event["operation"])
            for event in page.read_access_trace()["events"]] == [("element", "div", "id", "get")]


def test_record_generations_and_pages_keep_buffers_values_and_lifetimes_isolated(page):
    cache_attribute(page)
    old_record = page.eval("mutation_record")
    page.start_access_trace(object_types=["host_object"])
    assert page.eval("mutation_record.type") == "attributes"
    first, = record_events(page)
    with iv8.Page() as other:
        other.load(HTML, BASE)
        bind_nodes(other)
        cache_attribute(other)
        other.start_access_trace(object_types=["host_object"])
        page.eval("mutation_record.type='first page'")
        assert other.read_access_trace()["events"] == []
        assert other.eval("mutation_record.type") == "attributes"
        assert len(record_events(other)) == 1
        page.load(HTML, BASE)
        bind_nodes(page)
        cache_attribute(page)
        page.read_access_trace(clear=True)
        assert page.eval("mutation_record.type") == "attributes"
        second, = record_events(page)
        assert second["generation"] == first["generation"] + 1
        assert second["object_id"] != first["object_id"]
        assert not old_record.context_alive


@pytest.mark.parametrize("configurable", [True, False])
def test_inherited_setter_preserves_actual_own_data_value_and_flags(page, configurable):
    queue_attribute(page)
    page.eval("globalThis.mutation_setter_values=[]")
    page.start_access_trace(max_events=2)
    assert page.eval("""(() => {
        const original=Object.getOwnPropertyDescriptor(Object.prototype,'type');
        Object.defineProperty(Object.prototype,'type',{configurable:true,set(value){
            mutation_setter_values.push(value);Object.defineProperty(this,'type',{
                value:'hooked:'+value,writable:false,enumerable:false,configurable:""" +
                str(configurable).lower() + """});}});
        try{mutation_record=mutation_take.call(mutation_observer)[0];}
        finally{if(original)Object.defineProperty(Object.prototype,'type',original);else delete Object.prototype.type;}
        const descriptor=Object.getOwnPropertyDescriptor(mutation_record,'type');
        return mutation_setter_values.length===1 && mutation_setter_values[0]==='attributes' &&
            mutation_record.type==='hooked:attributes' && 'value'in descriptor && !descriptor.writable &&
            !descriptor.enumerable && descriptor.configurable===""" + str(configurable).lower() + ";})()",
                     name="mutation-inherited.js")
    records = record_events(page)
    if configurable:
        assert [(event["member"], event["operation"], event["value_summary"])
                for event in records] == [("type", "get", "string:17:hooked:attributes")]
    else:
        assert records == []
    calls = [event for event in page.read_access_trace(clear=False)["events"] if event["operation"] == "call"]
    assert [(event["label"], event["member"]) for event in calls] == [("MutationObserver", "takeRecords")]
    assert page.read_access_trace(clear=False)["dropped_events"] == 0


def test_inherited_bound_native_setter_is_user_work_without_diagnostic_getter_reads(page):
    queue_attribute(page)
    page.eval("""globalThis.mutation_diagnostic_reads=0;
        globalThis.mutation_sentinel=Error('inherited getter must not run');
        globalThis.mutation_bound_set=Object.getOwnPropertyDescriptor(Element.prototype,'id').set.bind(mutation_sink);""")
    page.start_access_trace(max_events=2)
    assert page.eval("""(() => {
        const original=Object.getOwnPropertyDescriptor(Object.prototype,'type');
        Object.defineProperty(Object.prototype,'type',{configurable:true,
            get(){mutation_diagnostic_reads++;throw mutation_sentinel;},set:mutation_bound_set});
        try{mutation_record=mutation_take.call(mutation_observer)[0];}
        finally{if(original)Object.defineProperty(Object.prototype,'type',original);else delete Object.prototype.type;}
        return mutation_diagnostic_reads===0 && !Object.hasOwn(mutation_record,'type');
    })()""", name="mutation-bound-setter.js")
    report = page.read_access_trace(clear=False)
    assert [(event["object_type"], event["label"], event["member"], event["operation"])
            for event in report["events"]] == [
        ("host_object", "MutationObserver", "takeRecords", "call"),
        ("element", "aside", "id", "set"),
    ]
    assert report["events"][1]["value_summary"] == "string:10:attributes"
    assert all(event["outcome"] == "returned" for event in report["events"])
    assert report["dropped_events"] == 0
    page.stop_access_trace()
    assert page.eval("mutation_sink.id") == "attributes"


def test_inherited_throwing_setter_preserves_baseline_error_and_later_field_behavior():
    for enabled in (False, True):
        with iv8.Page() as page:
            page.load(HTML, BASE)
            bind_nodes(page)
            queue_attribute(page)
            page.eval("""globalThis.mutation_throw_count=0;globalThis.mutation_late_count=0;
                globalThis.mutation_sentinel=Error('inherited field sentinel');""")
            if enabled:
                page.start_access_trace()
            observed = json.loads(page.eval("""JSON.stringify((() => {
                const oldType=Object.getOwnPropertyDescriptor(Object.prototype,'type');
                const oldTarget=Object.getOwnPropertyDescriptor(Object.prototype,'target');
                Object.defineProperty(Object.prototype,'type',{configurable:true,set(value){
                    mutation_throw_count++;throw mutation_sentinel;}});
                Object.defineProperty(Object.prototype,'target',{configurable:true,set(value){
                    mutation_late_count++;Object.defineProperty(this,'target',{
                        value,writable:true,enumerable:true,configurable:true});}});
                let result,caught=false;
                try{result=mutation_take.call(mutation_observer);}catch(error){caught=error===mutation_sentinel;}
                finally {
                    if(oldType)Object.defineProperty(Object.prototype,'type',oldType);else delete Object.prototype.type;
                    if(oldTarget)Object.defineProperty(Object.prototype,'target',oldTarget);else delete Object.prototype.target;
                }
                return {caught,typeCalls:mutation_throw_count,lateCalls:mutation_late_count,
                    resultLength:result ? result.length : null,
                    sameTarget:result ? result[0].target===mutation_target : null,
                    keys:result ? Object.keys(result[0]) : null};
            })())""", name="mutation-throwing-setter.js"))
            # The prior builder ignores each Set Maybe. Its later field work
            # already suppresses this error when tracing is off; the existing
            # outer traced native-call TryCatch preserves the original when on.
            # This field-hook change must retain both established outcomes.
            assert observed == {"caught": enabled, "typeCalls": 1, "lateCalls": 1,
                                "resultLength": None if enabled else 1,
                                "sameTarget": None if enabled else True,
                                "keys": None if enabled else FIELDS[1:]}
            if enabled:
                call, = page.read_access_trace()["events"]
                assert (call["label"], call["member"], call["operation"]) == (
                    "MutationObserver", "takeRecords", "call")
                assert call["outcome"] == "threw" and call["exception_name"] == "Error"
                assert "inherited field sentinel" in call["exception_message"]
