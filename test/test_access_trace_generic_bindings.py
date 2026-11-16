"""Generic defining-prototype references and rejected Web IDL receivers."""

import json

import pytest

import iv8


pytestmark = pytest.mark.skipif(
    not getattr(iv8, "_v8_linked", False), reason="requires a V8-linked build")
BASE = "https://generic-bindings.test/page/"
HTML = """<html><head></head><body>
<div id="target"><span id="child"></span></div><form id="other"></form>
</body></html>"""
METHODS = [
    ("Element", "getAttribute", 1),
    ("Node", "contains", 1),
    ("Document", "createElement", 1),
    ("Navigator", "javaEnabled", 0),
    ("NavigatorUAData", "toJSON", 0),
    ("EventTarget", "addEventListener", 2),
]
METHOD_REJECTIONS = [
    ("Element", "getAttribute", "generic_argument", ["object"]),
    ("Node", "contains", "generic_element", ["host_object"]),
    ("Document", "createElement", "generic_argument", ["object"]),
    ("Navigator", "javaEnabled", "", []),
    ("NavigatorUAData", "toJSON", "", []),
    ("EventTarget", "addEventListener", "generic_argument,generic_listener", ["object", "function"]),
]
ATTRIBUTE_REJECTIONS = [
    ("Element", "id", "get"), ("Element", "id", "set"),
    ("Node", "nodeType", "get"), ("Node", "textContent", "set"),
    ("Document", "title", "get"), ("Document", "title", "set"),
    ("Navigator", "userAgent", "get"), ("NavigatorUAData", "platform", "get"),
]


def bind_objects(page):
    page.eval("""globalThis.generic_element=document.getElementById('target');
        globalThis.generic_other=document.getElementById('other');
        globalThis.generic_document=document;globalThis.generic_navigator=navigator;
        globalThis.generic_ua=navigator.userAgentData;
        globalThis.generic_target=new EventTarget();
        globalThis.generic_prototypes={Element:Element.prototype,Node:Node.prototype,
            Document:Document.prototype,Navigator:Navigator.prototype,
            NavigatorUAData:NavigatorUAData.prototype,EventTarget:EventTarget.prototype,
            HTMLElement:HTMLElement.prototype};
        globalThis.generic_listener=()=>{};globalThis.generic_bad_receiver={};
        globalThis.generic_coercions=0;globalThis.generic_sentinel=Error('unexpected coercion');
        globalThis.generic_argument={toString(){generic_coercions++;throw generic_sentinel;},
            valueOf(){generic_coercions++;throw generic_sentinel;}};""")


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, BASE)
        bind_objects(value)
        yield value


def events(page):
    return page.read_access_trace(clear=False)["events"]


def event_keys(records):
    return [(record["object_type"], record["label"], record["member"], record["operation"])
            for record in records]


def prototype_expression(interface):
    return "generic_prototypes[" + json.dumps(interface) + "]"


def cache_method(page, interface, member):
    page.eval("globalThis.generic_method=" + prototype_expression(interface) + "[" + json.dumps(member) + "]")


def failed_entry(page, interface, member, operation):
    report = page.read_access_trace(clear=False)
    assert report["dropped_events"] == 0
    record, = report["events"]
    assert event_keys([record]) == [("host_object", interface + ".prototype", member, operation)]
    assert record["outcome"] == "threw" and record["exception_name"] == "TypeError"
    assert "Illegal invocation" in record["exception_message"]
    assert record["resource_name"] == "generic-brand.js" and record["source_kind"] == "eval"
    assert "return_summary" not in record
    return record


@pytest.mark.parametrize("interface,member,length,defining", [
    *[(interface, member, length, interface) for interface, member, length in METHODS],
    ("HTMLElement", "getAttribute", 1, "Element"),
])
def test_defining_prototype_method_reference_is_completed_without_calling(
        page, interface, member, length, defining):
    cache_method(page, interface, member)
    page.watch_apis(["div.getAttribute", "document.createElement", "navigator.javaEnabled",
                     "NavigatorUAData.toJSON", "EventTarget.addEventListener"])
    page.start_access_trace(max_events=1)
    assert page.eval(prototype_expression(interface) + "[" + json.dumps(member) + "]===generic_method",
                     name="generic-reference.js")
    record, = events(page)
    assert event_keys([record]) == [("host_object", defining + ".prototype", member, "get")]
    assert record["outcome"] == "returned" and record["value_summary"] == "function"
    assert record["resource_name"] == "generic-reference.js" and record["source_kind"] == "eval"
    assert "argument_count" not in record and "return_summary" not in record
    assert page.read_access_trace(clear=False)["dropped_events"] == 0
    assert page.read_watch_api_hits() == []
    page.stop_access_trace()
    assert page.eval("generic_method.name") == member
    assert page.eval("generic_method.length") == length


@pytest.mark.parametrize("interface,member,arguments,summaries", METHOD_REJECTIONS)
def test_cached_native_method_rejection_is_recorded_before_argument_coercion(
        page, interface, member, arguments, summaries):
    cache_method(page, interface, member)
    page.start_access_trace(max_events=1)
    expression = "generic_method.call(generic_bad_receiver" + ("," + arguments if arguments else "") + ")"
    with pytest.raises(iv8.JSError) as caught:
        page.eval(expression, name="generic-brand.js")
    assert caught.value.name == "TypeError" and caught.value.message == "Illegal invocation"
    record = failed_entry(page, interface, member, "call")
    assert record["argument_count"] == len(summaries)
    assert record["argument_summaries"] == summaries
    page.stop_access_trace()
    assert page.eval("generic_coercions") == 0


@pytest.mark.parametrize("interface,member,operation", ATTRIBUTE_REJECTIONS)
def test_invalid_idl_accessor_records_operation_before_the_original_brand_error(
        page, interface, member, operation):
    page.eval("globalThis.generic_accessor=Object.getOwnPropertyDescriptor(" +
              prototype_expression(interface) + "," + json.dumps(member) + ")." + operation)
    page.start_access_trace(max_events=1)
    arguments = ",generic_argument" if operation == "set" else ""
    with pytest.raises(iv8.JSError) as caught:
        page.eval("generic_accessor.call(generic_bad_receiver" + arguments + ")", name="generic-brand.js")
    assert caught.value.name == "TypeError" and caught.value.message == "Illegal invocation"
    record = failed_entry(page, interface, member, operation)
    if operation == "set":
        assert record["value_summary"] == "object"
    page.stop_access_trace()
    assert page.eval("generic_coercions") == 0


@pytest.mark.parametrize("receiver", ["null", "17", "Object.create(generic_prototypes.Element)"])
def test_primitive_and_forged_receivers_keep_prototype_rejection_identity(page, receiver):
    cache_method(page, "Element", "getAttribute")
    page.eval("globalThis.generic_receiver=" + receiver)
    page.start_access_trace(max_events=1)
    with pytest.raises(iv8.JSError) as caught:
        page.eval("generic_method.call(generic_receiver,generic_argument)", name="generic-brand.js")
    assert caught.value.name == "TypeError" and caught.value.message == "Illegal invocation"
    record = failed_entry(page, "Element", "getAttribute", "call")
    assert record["argument_summaries"] == ["object"]
    page.stop_access_trace()
    assert page.eval("generic_coercions") == 0


def test_wrong_native_brands_use_defining_prototype_identities(page):
    cache_method(page, "Element", "getAttribute")
    page.eval("globalThis.generic_get_ua=Object.getOwnPropertyDescriptor(generic_prototypes.Navigator,'userAgent').get")
    page.start_access_trace(max_events=4)
    page.eval("generic_prototypes.Element.getAttribute;generic_prototypes.Navigator.javaEnabled")
    for expression in ("generic_method.call(generic_document,generic_argument)",
                       "generic_get_ua.call(generic_element)"):
        with pytest.raises(iv8.JSError) as caught:
            page.eval(expression, name="generic-brand.js")
        assert caught.value.name == "TypeError" and caught.value.message == "Illegal invocation"
    records = events(page)
    assert event_keys(records) == [
        ("host_object", "Element.prototype", "getAttribute", "get"),
        ("host_object", "Navigator.prototype", "javaEnabled", "get"),
        ("host_object", "Element.prototype", "getAttribute", "call"),
        ("host_object", "Navigator.prototype", "userAgent", "get"),
    ]
    assert records[0]["object_id"] == records[2]["object_id"]
    assert records[1]["object_id"] == records[3]["object_id"] != records[0]["object_id"]
    assert all(record["outcome"] == "threw" for record in records[2:])
    assert records[2]["argument_summaries"] == ["object"]
    assert page.read_access_trace(clear=False)["dropped_events"] == 0
    page.stop_access_trace()
    assert page.eval("generic_coercions") == 0


def test_rejection_diagnostics_do_not_run_proxy_or_error_prototype_getters(page):
    cache_method(page, "Element", "getAttribute")
    page.eval("""globalThis.generic_get_id=Object.getOwnPropertyDescriptor(generic_prototypes.Element,'id').get;
        globalThis.generic_set_id=Object.getOwnPropertyDescriptor(generic_prototypes.Element,'id').set;
        globalThis.generic_effects=[];
        const trap=()=>{generic_effects.push('proxy');throw generic_sentinel;};
        globalThis.generic_proxy=new Proxy({},{get:trap,getPrototypeOf:trap,
            getOwnPropertyDescriptor:trap,has:trap,ownKeys:trap});
        globalThis.generic_payload=new Proxy({},{get:trap,getPrototypeOf:trap,
            getOwnPropertyDescriptor:trap,has:trap,ownKeys:trap});""")
    page.start_access_trace(max_events=3)
    assert page.eval("""(() => {
        const original=Object.getOwnPropertyDescriptor(TypeError.prototype,'name');
        Object.defineProperty(TypeError.prototype,'name',{configurable:true,
            get(){generic_effects.push('error name');throw generic_sentinel;}});
        const caught=[];
        try {
            for(const invoke of [()=>generic_method.call(generic_proxy,generic_payload),
                    ()=>generic_get_id.call(generic_proxy),
                    ()=>generic_set_id.call(generic_proxy,generic_payload)]) {
                try{invoke();}catch(error){caught.push(error instanceof TypeError &&
                    Object.getOwnPropertyDescriptor(error,'message').value==='Illegal invocation');}
            }
        } finally {Object.defineProperty(TypeError.prototype,'name',original);}
        return caught.length===3 && caught.every(Boolean) && generic_effects.length===0;
    })()""", name="generic-proxy.js")
    records = events(page)
    assert event_keys(records) == [
        ("host_object", "Element.prototype", "getAttribute", "call"),
        ("host_object", "Element.prototype", "id", "get"),
        ("host_object", "Element.prototype", "id", "set"),
    ]
    assert all(record["outcome"] == "threw" for record in records)
    assert records[0]["argument_summaries"] == ["object"]
    assert records[2]["value_summary"] == "object"
    assert len({record["object_id"] for record in records}) == 1
    assert page.read_access_trace(clear=False)["dropped_events"] == 0


def test_valid_instance_references_keep_shared_identity_and_adjacent_call_folding(page):
    cache_method(page, "Element", "getAttribute")
    page.watch_apis(["div.getAttribute"])
    page.start_access_trace()
    assert page.eval("""(() => {
        const first=generic_element.getAttribute,second=generic_other.getAttribute;
        return first===second && first===generic_method && generic_element.getAttribute('id')==='target';
    })()""", name="generic-valid.js")
    records = events(page)
    assert event_keys(records) == [
        ("element", "div", "getAttribute", "get_method"),
        ("element", "form", "getAttribute", "get_method"),
        ("element", "div", "getAttribute", "call"),
    ]
    assert records[0]["object_id"] == records[2]["object_id"] != records[1]["object_id"]
    assert records[2]["outcome"] == "returned" and records[2]["return_summary"] == "string:6:target"
    assert records[2]["argument_summaries"] == ["string:2:id"]
    assert [hit["path"] for hit in page.read_watch_api_hits()] == ["div.getAttribute"]


def test_cached_borrowed_and_base_interface_methods_use_actual_native_receivers(page):
    page.eval("""globalThis.generic_get_attribute=generic_prototypes.Element.getAttribute;
        globalThis.generic_contains=generic_prototypes.Node.contains;
        globalThis.generic_add_listener=generic_prototypes.EventTarget.addEventListener;
        globalThis.generic_dispatch=generic_prototypes.EventTarget.dispatchEvent;
        globalThis.generic_event=new Event('generic');globalThis.generic_count=0;
        globalThis.generic_derived=new(class extends EventTarget {})();
        globalThis.generic_count_listener=()=>generic_count++;""")
    page.start_access_trace()
    assert page.eval("""(() => {
        const id=generic_get_attribute.call(generic_other,'id');
        const contains=generic_contains.call(generic_document,generic_element);
        generic_add_listener.call(generic_derived,'generic',generic_count_listener);
        const dispatched=generic_dispatch.call(generic_derived,generic_event);
        return id==='other' && contains && dispatched && generic_count===1;
    })()""", name="generic-borrowed.js")
    records = events(page)
    assert event_keys(records) == [
        ("element", "form", "getAttribute", "call"),
        ("document", "document", "contains", "call"),
        ("event_target", "", "addEventListener", "call"),
        ("event_target", "", "dispatchEvent", "call"),
    ]
    assert records[2]["object_id"] == records[3]["object_id"]
    assert all(record["outcome"] == "returned" for record in records)
    assert records[0]["return_summary"] == "string:5:other"


def test_valid_borrowed_idl_accessors_keep_receiver_state_and_native_shape(page):
    page.eval("""globalThis.generic_id_descriptor=Object.getOwnPropertyDescriptor(generic_prototypes.Element,'id');
        globalThis.generic_node_type=Object.getOwnPropertyDescriptor(generic_prototypes.Node,'nodeType').get;""")
    page.start_access_trace()
    assert page.eval("""(() => {
        const before=generic_id_descriptor.get.call(generic_other);
        generic_id_descriptor.set.call(generic_other,'renamed');
        return before==='other' && generic_id_descriptor.get.call(generic_other)==='renamed' &&
            generic_node_type.call(generic_other)===1;
    })()""")
    records = events(page)
    assert event_keys(records) == [
        ("element", "form", "id", "get"), ("element", "form", "id", "set"),
        ("element", "form", "id", "get"), ("element", "form", "nodeType", "get"),
    ]
    assert len({record["object_id"] for record in records}) == 1
    assert records[1]["value_summary"] == "string:7:renamed"
    page.stop_access_trace()
    assert page.eval("""(() => {
        const getter=generic_id_descriptor.get,setter=generic_id_descriptor.set;
        return getter.name==='get id' && getter.length===0 && setter.name==='set id' && setter.length===1 &&
            !Object.hasOwn(getter,'prototype') && !Object.hasOwn(setter,'prototype') &&
            String(getter)==='function get id() { [native code] }' &&
            String(setter)==='function set id() { [native code] }';
    })()""")


def test_prototype_method_monitoring_preserves_native_function_and_data_descriptors(page):
    entries = json.dumps(METHODS)
    snapshot = """JSON.stringify(""" + entries + """.map(([owner,member])=>{
        const prototype=generic_prototypes[owner],method=prototype[member];
        const descriptor=Object.getOwnPropertyDescriptor(prototype,member);
        let rejected=false;try{Reflect.construct(method,[]);}catch(error){rejected=error instanceof TypeError;}
        return {name:method.name,length:method.length,source:String(method),
            ownPrototype:Object.hasOwn(method,'prototype'),ownNames:Object.getOwnPropertyNames(method).sort(),
            data:'value'in descriptor,same:descriptor.value===method,writable:descriptor.writable,
            enumerable:descriptor.enumerable,configurable:descriptor.configurable,rejected};
    }))"""
    expected = [{"name": member, "length": length,
                 "source": "function " + member + "() { [native code] }",
                 "ownPrototype": False, "ownNames": ["length", "name"], "data": True,
                 "same": True, "writable": True, "enumerable": True, "configurable": True,
                 "rejected": True} for _, member, length in METHODS]
    assert json.loads(page.eval(snapshot)) == expected
    page.start_access_trace()
    assert json.loads(page.eval(snapshot)) == expected
    records = events(page)
    assert event_keys(records) == [entry for interface, member, _ in METHODS for entry in (
        ("host_object", interface + ".prototype", member, "get"),
        ("host_object", "Function.prototype", "toString", "call"),
    )]
    assert all(record["outcome"] == "returned" and record["value_summary"] == "function"
               for record in records[::2])
    for record, (_, member, _) in zip(records[1::2], METHODS):
        source = "function " + member + "() { [native code] }"
        assert record["outcome"] == "returned"
        assert record["argument_count"] == 0 and record["argument_summaries"] == []
        assert record["return_summary"] == "string:" + str(len(source)) + ":" + source
    page.stop_access_trace()
    assert json.loads(page.eval(snapshot)) == expected


def test_writable_delete_and_redefinition_keep_cached_native_calls_and_js_replacements(page):
    cache_method(page, "Element", "getAttribute")
    page.watch_apis(["div.getAttribute"])
    page.start_access_trace()
    assert page.eval("""(() => {
        const prototype=generic_prototypes.Element,replacement=()=> 'replacement';
        prototype.getAttribute=replacement;
        const replaced=prototype.getAttribute===replacement && generic_element.getAttribute('id')==='replacement';
        const deleted=delete prototype.getAttribute;
        const absent=generic_element.getAttribute===undefined;
        const cached=generic_method.call(generic_element,'id')==='target';
        Object.defineProperty(prototype,'getAttribute',{value:generic_method,writable:true,
            enumerable:true,configurable:true});
        const descriptor=Object.getOwnPropertyDescriptor(prototype,'getAttribute');
        const restored=descriptor.value===generic_method && descriptor.writable &&
            descriptor.enumerable && descriptor.configurable && generic_element.getAttribute('id')==='target';
        Object.defineProperty(prototype,'getAttribute',{get(){return replacement;},configurable:true});
        const accessor=generic_element.getAttribute('id')==='replacement';
        return replaced && deleted && absent && cached && restored && accessor;
    })()""")
    calls = [record for record in events(page) if record["operation"] == "call"]
    assert event_keys(calls) == [("element", "div", "getAttribute", "call")] * 2
    assert all(record["argument_summaries"] == ["string:2:id"] and
               record["return_summary"] == "string:6:target" for record in calls)
    assert [hit["path"] for hit in page.read_watch_api_hits()] == ["div.getAttribute"] * 2


def test_prototype_reference_filters_capacity_and_stop_preserve_instance_channel(page):
    cache_method(page, "Element", "getAttribute")
    page.start_access_trace(max_events=2, object_types=["host_object"])
    page.eval("generic_prototypes.Element.getAttribute;generic_prototypes.Element.getAttribute;"
              "generic_prototypes.Element.getAttribute")
    before = page.read_access_trace(clear=False)
    assert event_keys(before["events"]) == [("host_object", "Element.prototype", "getAttribute", "get")] * 2
    assert before["dropped_events"] == 1
    assert len({record["object_id"] for record in before["events"]}) == 1
    page.stop_access_trace()
    page.eval("generic_prototypes.Element.getAttribute")
    after = page.read_access_trace(clear=False)
    assert after["events"] == before["events"] and after["dropped_events"] == 1

    page.start_access_trace(object_types=["element"])
    page.eval("generic_prototypes.Element.getAttribute;generic_element.getAttribute")
    assert event_keys(events(page)) == [("element", "div", "getAttribute", "get_method")]
    with pytest.raises(iv8.JSError) as caught:
        page.eval("generic_method.call({})")
    assert caught.value.message == "Illegal invocation"
    assert event_keys(events(page)) == [("element", "div", "getAttribute", "get_method")]


def test_prototype_generation_lifetime_and_page_isolation(page):
    page.start_access_trace(object_types=["host_object"])
    old_reference = page.eval("generic_prototypes.Element.getAttribute")
    first, = events(page)
    with iv8.Page() as other:
        other.load(HTML, BASE)
        bind_objects(other)
        other.start_access_trace(object_types=["host_object"])
        assert other.read_access_trace()["events"] == []
        page.eval("generic_prototypes.Element.getAttribute=()=> 'changed'")
        assert other.eval("generic_element.getAttribute('id')") == "target"
        assert other.read_access_trace()["events"] == []
        page.load(HTML, BASE)
        bind_objects(page)
        page.read_access_trace(clear=True)
        restored = page.eval("generic_prototypes.Element.getAttribute")
        second, = events(page)
        assert second["generation"] == first["generation"] + 1
        assert second["object_id"] != first["object_id"]
        assert not old_reference.context_alive and restored.context_alive
        page.stop_access_trace()
        assert page.eval("generic_element.getAttribute('id')") == "target"


def test_debug_remains_quiet_for_load_and_exact_for_direct_location_access(capsys):
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        assert capsys.readouterr().out == ""
        assert page.eval("window.location.href") == BASE
        assert capsys.readouterr().out.splitlines() == [
            "GET window.window -> <Object>", "GET window.location -> <HostObject>",
            'GET location.href -> "' + BASE + '"',
        ]


def test_own_location_bindings_keep_owner_identity_without_prototype_methods(page):
    page.eval("""globalThis.genericLocation=location;globalThis.genericAssign=location.assign;
        globalThis.genericHref=Object.getOwnPropertyDescriptor(location,'href').get;
        globalThis.genericCoercions=0;globalThis.genericInput={toString(){genericCoercions++;return '/next';}};""")
    page.start_access_trace(max_events=3, object_types=["location"])
    page.eval("""genericLocation.href;
        try{genericAssign.call({},genericInput)}catch(error){}
        try{genericHref.call({})}catch(error){}""", name="own-location-brand.js")
    report=page.read_access_trace()
    assert [(event["label"],event["member"],event["operation"]) for event in report["events"]] == [
        ("location","href","get"),("location","assign","call"),("location","href","get")]
    assert len({event["object_id"] for event in report["events"]}) == 1
    assert report["dropped_events"] == 0
    assert all(event["outcome"] == "threw" and event["exception_name"] == "TypeError"
               for event in report["events"][1:])
    assert page.eval("genericCoercions") == 0
    assert page.eval("!Object.hasOwn(Location.prototype,'assign')")
    assert page.read_navigation_requests()["requests"] == []


def test_late_generic_registration_does_not_invoke_array_prototype_setters(page):
    assert page.eval("""(() => {
        let intercepted=0;
        Object.defineProperty(Array.prototype,'2',{set(){intercepted++;},configurable:true});
        let input;
        try {input=document.createElement('input');}
        finally {delete Array.prototype[2];}
        return intercepted===0 && typeof input.checkValidity==='function';
    })()""")
