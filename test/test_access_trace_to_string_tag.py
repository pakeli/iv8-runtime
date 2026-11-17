"""Observe existing browser-owned toStringTag data without instrumenting user symbols."""

import json
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")
BASE = "https://to-string-tag.test/page/"
HTML = '<html><head></head><body><div id="tag-target" style="color:red"></div></body></html>'
SOURCES = [
    ("tag_doc", "tag_doc_proto", "HTMLDocument", "host_object", "HTMLDocument.prototype"),
    ("tag_nav", "tag_nav_proto", "Navigator", "host_object", "Navigator.prototype"),
    ("tag_window_properties", "tag_window_properties", "WindowProperties", "host_object", "WindowProperties.prototype"),
    ("tag_collection", "tag_collection_proto", "HTMLCollection", "host_object", "HTMLCollection.prototype"),
    ("tag_all", "tag_all_proto", "HTMLAllCollection", "host_object", "HTMLAllCollection.prototype"),
    ("tag_css", "tag_css", "CSS", "window_shell", "CSS"),
    ("tag_style", "tag_style_proto", "CSSStyleDeclaration", "host_object", "CSSStyleDeclaration.prototype"),
    ("tag_battery", "tag_battery", "BatteryManager", "host_object", "BatteryManager"),
    ("tag_quota", "tag_quota", "DeprecatedStorageQuota", "host_object", "DeprecatedStorageQuota"),
]


@pytest.fixture(scope="module")
def browser_reference():
    fixture = Path(__file__).parent / "fixtures" / "symbol_to_string_tag" / "chrome.json"
    return json.loads(fixture.read_text())["result"]


def bind_tags(page):
    page.eval("""globalThis.tag_key=Symbol.toStringTag;globalThis.tag_doc=document;
        globalThis.tag_doc_proto=Object.getPrototypeOf(tag_doc);globalThis.tag_nav=navigator;
        globalThis.tag_nav_proto=Navigator.prototype;
        globalThis.tag_window_properties=Object.getPrototypeOf(Window.prototype);
        globalThis.tag_collection=document.getElementsByTagName('div');
        globalThis.tag_collection_proto=Object.getPrototypeOf(tag_collection);
        globalThis.tag_all=document.all;globalThis.tag_all_proto=Object.getPrototypeOf(tag_all);
        globalThis.tag_css=CSS;globalThis.tag_style=document.getElementById('tag-target').style;
        globalThis.tag_style_proto=Object.getPrototypeOf(tag_style);
        globalThis.tag_battery=null;navigator.getBattery().then(value=>{tag_battery=value;});
        globalThis.tag_quota=navigator.webkitPersistentStorage;
        globalThis.tag_stringify=Object.prototype.toString;
        globalThis.tag_original_descriptor=Object.getOwnPropertyDescriptor;
        globalThis.tag_reflect_get=Reflect.get;globalThis.tag_effects=[];
        globalThis.tag_sentinel=Error('must not inspect metadata');""")
    page.run_jobs()


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, BASE)
        bind_tags(value)
        yield value


def events(page):
    return page.read_access_trace(clear=False)["events"]


def tag_events(page):
    return [event for event in events(page) if event.get("member_kind") == "symbol" and
            event["member"] == "Symbol.toStringTag"]


def assert_tag(event, value, object_type="host_object", label="HTMLDocument.prototype"):
    assert (event["object_type"], event["label"], event["member"], event["member_kind"], event["operation"]) == (
        object_type, label, "Symbol.toStringTag", "symbol", "get")
    assert event["outcome"] == "returned"
    assert event["value_summary"] == "string:" + str(len(value.encode("utf-8"))) + ":" + value


@pytest.mark.parametrize("source,owner,value,object_type,label", SOURCES, ids=[row[2] for row in SOURCES])
@pytest.mark.parametrize("form", ["direct", "reflect"])
def test_cached_browser_objects_direct_and_reflect_tag_reads_record_one_completed_get(
        page, source, owner, value, object_type, label, form):
    page.watch_apis([label + ".Symbol.toStringTag"])
    page.start_access_trace(max_events=1)
    expression = source + "[tag_key]" if form == "direct" else "tag_reflect_get(" + source + ",tag_key)"
    assert page.eval(expression, name="tag-user-read.js") == value
    record, = events(page)
    assert_tag(record, value, object_type, label)
    assert record["source_kind"] == "eval" and record["resource_name"] == "tag-user-read.js"
    assert page.read_access_trace(clear=False)["dropped_events"] == 0
    assert page.read_watch_api_hits() == []


def test_data_descriptors_flags_and_own_key_order_survive_monitoring_and_readonly_attempts(page, browser_reference):
    owners = list(dict.fromkeys(row[1] for row in SOURCES))
    expression = """JSON.stringify([""" + ",".join(owners) + """].map(owner=>{
        const descriptor=Object.getOwnPropertyDescriptor(owner,tag_key);
        return {value:descriptor.value,data:'value'in descriptor,get:'get'in descriptor,
            writable:descriptor.writable,enumerable:descriptor.enumerable,configurable:descriptor.configurable,
            keys:Reflect.ownKeys(owner).map(String)};
    }))"""
    before = json.loads(page.eval(expression))
    assert all(row["data"] and not row["get"] and not row["writable"] and not row["enumerable"] for row in before)
    for row, source in zip(before, SOURCES):
        reference_owner = "WindowProperties" if source[2] == "WindowProperties" else source[4]
        reference = browser_reference["owners"].get(reference_owner)
        if reference is None:
            # Bootstrap Battery/Quota keep their existing flat profile; CSSStyle
            # shape preservation is checked against this runtime's baseline.
            continue
        descriptor = {name: row[name] for name in ("value", "writable", "enumerable", "configurable")}
        assert dict(kind="data", **descriptor) == reference["descriptor"]
        assert [key for key in row["keys"] if key.startswith("Symbol(")] == reference["ownSymbols"]
    page.start_access_trace()
    assert json.loads(page.eval(expression)) == before
    page.read_access_trace(clear=True)
    assert page.eval("""(() => {
        tag_nav[tag_key]='ignored';tag_nav_proto[tag_key]='ignored';let rejected=0;
        for(const owner of [tag_nav,tag_nav_proto]){
            try{(function(){'use strict';owner[tag_key]='ignored';})()}catch(error){if(error instanceof TypeError)rejected++;}
        }
        return rejected===2 && tag_nav[tag_key]==='Navigator';
    })()""")
    record, = tag_events(page)
    assert_tag(record, "Navigator", label="Navigator.prototype")
    assert not [event for event in tag_events(page) if event["operation"] == "set"]
    page.stop_access_trace()
    assert json.loads(page.eval(expression)) == before


def test_native_descriptor_materialization_retains_cached_original_and_wrapped_query_behavior(page):
    page.start_access_trace()
    assert page.eval("tag_original_descriptor(tag_nav_proto,tag_key).value") == "Navigator"
    record, = tag_events(page)
    assert_tag(record, "Navigator", label="Navigator.prototype")
    page.read_access_trace(clear=True)
    assert page.eval("""(() => {
        const first=Object.getOwnPropertyDescriptor(tag_nav_proto,tag_key);
        const second=Reflect.getOwnPropertyDescriptor(tag_nav_proto,tag_key);
        return first.value==='Navigator' && second.value===first.value &&
            !first.writable && !first.enumerable && first.configurable;
    })()""")
    assert tag_events(page) == []


def test_inherited_and_different_reflect_receivers_keep_the_defining_prototype_identity(page):
    page.eval("""globalThis.tag_inherited=Object.create(tag_nav_proto);
        globalThis.tag_receiver_proxy=new Proxy({},{get(){throw tag_sentinel;},getPrototypeOf(){throw tag_sentinel;}});""")
    page.start_access_trace(max_events=5)
    assert page.eval("""tag_inherited[tag_key]==='Navigator' &&
        tag_reflect_get(tag_nav_proto,tag_key,null)==='Navigator' &&
        tag_reflect_get(tag_nav_proto,tag_key,17)==='Navigator' &&
        tag_reflect_get(tag_nav_proto,tag_key,tag_receiver_proxy)==='Navigator' &&
        tag_nav_proto[tag_key]==='Navigator';""")
    records = tag_events(page)
    assert len(records) == 5
    assert len({event["object_id"] for event in records}) == 1
    for record in records:
        assert_tag(record, "Navigator", label="Navigator.prototype")
    assert page.read_access_trace(clear=False)["dropped_events"] == 0
    page.start_access_trace(max_events=2)
    assert page.eval("tag_style[tag_key]===tag_style_proto[tag_key]")
    records = tag_events(page)
    assert len(records) == 2 and records[0]["object_id"] == records[1]["object_id"]
    for record in records:
        assert_tag(record, "CSSStyleDeclaration", label="CSSStyleDeclaration.prototype")
    page.stop_access_trace()
    page.eval("""globalThis.tag_original_symbol=Symbol;globalThis.tag_global_symbol_reads=0;
        globalThis.Symbol={get toStringTag(){tag_global_symbol_reads++;throw tag_sentinel;}};""")
    try:
        page.start_access_trace(max_events=1)
        assert page.eval("tag_style[tag_key]") == "CSSStyleDeclaration"
        record, = events(page)
        assert_tag(record, "CSSStyleDeclaration", label="CSSStyleDeclaration.prototype")
        page.stop_access_trace()
        assert page.eval("tag_global_symbol_reads") == 0
    finally:
        page.stop_access_trace()
        page.eval("globalThis.Symbol=tag_original_symbol")


def test_user_proxy_forwarding_reads_the_native_tag_once_without_diagnostic_traps(page):
    page.eval("""globalThis.tag_proxy=new Proxy(tag_doc,{
        get(target,key,receiver){tag_effects.push(String(key));return Reflect.get(target,key,receiver);},
        getPrototypeOf(){throw tag_sentinel;},ownKeys(){throw tag_sentinel;},
        getOwnPropertyDescriptor(){throw tag_sentinel;}});""")
    page.start_access_trace(max_events=1)
    assert page.eval("tag_reflect_get(tag_proxy,tag_key)") == "HTMLDocument"
    record, = events(page)
    assert_tag(record, "HTMLDocument")
    page.stop_access_trace()
    assert page.eval("tag_effects", to_py=True) == ["Symbol(Symbol.toStringTag)"]


def test_user_own_data_and_getter_shadows_are_not_wrapped_or_reinstalled(page):
    page.eval("Object.defineProperty(tag_nav,tag_key,{value:'OwnTag',writable:true,enumerable:true,configurable:true})")
    page.start_access_trace()
    assert page.eval("tag_nav[tag_key]") == "OwnTag"
    assert tag_events(page) == []
    page.stop_access_trace()
    page.eval("""Object.defineProperty(tag_nav,tag_key,{get(){tag_effects.push('getter');return 'UserTag';},
        enumerable:false,configurable:true});""")
    page.start_access_trace()
    assert page.eval("tag_reflect_get(tag_nav,tag_key)") == "UserTag"
    assert tag_events(page) == []
    page.stop_access_trace()
    assert page.eval("tag_effects", to_py=True) == ["getter"]
    assert page.eval("""(()=>{const descriptor=Object.getOwnPropertyDescriptor(tag_nav,tag_key);
        return typeof descriptor.get==='function' && !('value'in descriptor) && !descriptor.enumerable && descriptor.configurable;})()""")


def test_value_only_redefinition_and_delete_keep_original_javascript_value_and_flag_semantics(page):
    page.start_access_trace()
    assert page.eval("""(() => {
        Object.defineProperty(tag_nav_proto,tag_key,{value:'ChangedTag',writable:true,enumerable:true,configurable:true});
        const descriptor=Object.getOwnPropertyDescriptor(tag_nav_proto,tag_key);
        if(descriptor.value!=='ChangedTag' || !descriptor.writable || !descriptor.enumerable || !descriptor.configurable)return false;
        tag_nav_proto[tag_key]='WrittenTag';
        return tag_nav[tag_key]==='WrittenTag' && tag_reflect_get(tag_nav_proto,tag_key)==='WrittenTag';
    })()""")
    # V8 may preserve the native data callback for a value-only definition;
    # this test constrains values and descriptors, rather than callback counts.
    page.stop_access_trace()
    assert page.eval("delete tag_nav_proto[tag_key]")
    page.start_access_trace()
    assert page.eval("tag_nav[tag_key]") is iv8.JSUndefined
    assert tag_events(page) == []


def test_other_symbol_identities_and_objects_without_runtime_tags_are_not_added_or_traced(page):
    page.eval("globalThis.tag_other_symbol=Symbol('Symbol.toStringTag');globalThis.tag_url=new URL('https://tag.test/')")
    page.start_access_trace()
    assert page.eval("""tag_nav[tag_other_symbol]===undefined && tag_nav_proto[tag_other_symbol]===undefined &&
        Object.getOwnPropertyDescriptor(Object.prototype,tag_key)===undefined &&
        Object.getOwnPropertyDescriptor(Function.prototype,tag_key)===undefined &&
        Object.getOwnPropertyDescriptor(URL.prototype,tag_key)===undefined && tag_url[tag_key]===undefined;""")
    assert tag_events(page) == []


def test_object_to_string_automatic_native_tag_reads_stay_quiet_before_capacity_accounting(page):
    page.eval("globalThis.tag_no_trap_proxy=new Proxy(tag_doc,{})")
    for source, value in (("tag_doc", "HTMLDocument"), ("tag_collection", "HTMLCollection"),
                          ("tag_all", "HTMLAllCollection"), ("tag_style", "CSSStyleDeclaration"),
                          ("tag_no_trap_proxy", "HTMLDocument")):
        page.start_access_trace(max_events=1)
        assert page.eval("tag_stringify.call(" + source + ")") == "[object " + value + "]"
        record, = events(page)
        assert (record["label"], record["member"], record["operation"]) == ("Object.prototype", "toString", "call")
        assert tag_events(page) == []
        assert page.read_access_trace(clear=False)["dropped_events"] == 0


def test_user_js_getter_bound_native_getter_and_proxy_trap_keep_real_browser_symbol_reads(page):
    cases = [
        ("""globalThis.tag_custom={get [tag_key](){return tag_doc[tag_key];}};""",
         "HTMLDocument", "HTMLDocument.prototype"),
        ("""globalThis.tag_custom={};Object.defineProperty(tag_custom,tag_key,
            {get:tag_reflect_get.bind(null,tag_doc,tag_key),configurable:true});""",
         "HTMLDocument", "HTMLDocument.prototype"),
        ("""globalThis.tag_custom=new Proxy(tag_doc,{get(target,key,receiver){return Reflect.get(target,key,receiver);}});""",
         "HTMLDocument", "HTMLDocument.prototype"),
        ("""globalThis.tag_saved_css_descriptor=Object.getOwnPropertyDescriptor(tag_style_proto,tag_key);
            Object.defineProperty(tag_style_proto,tag_key,
                {get:tag_reflect_get.bind(Reflect,tag_nav_proto,tag_key),configurable:true});
            globalThis.tag_custom=tag_style;""",
         "Navigator", "Navigator.prototype"),
    ]
    for setup, value, label in cases:
        page.stop_access_trace()
        page.eval(setup)
        page.start_access_trace(max_events=2)
        try:
            assert page.eval("tag_stringify.call(tag_custom)") == "[object " + value + "]"
            report = page.read_access_trace(clear=False)
            assert len(report["events"]) == 2 and report["events"][0]["member"] == "toString"
            record, = tag_events(page)
            assert_tag(record, value, label=label)
            assert report["dropped_events"] == 0
        finally:
            if label == "Navigator.prototype":
                page.stop_access_trace()
                page.eval("Object.defineProperty(tag_style_proto,tag_key,tag_saved_css_descriptor)")


def test_throwing_user_tag_getter_keeps_original_error_and_restores_later_native_reads(page):
    page.eval("""globalThis.tag_throwing={get [tag_key](){tag_effects.push('getter');
        tag_doc[tag_key];throw tag_sentinel;}};""")
    page.start_access_trace(max_events=3)
    assert page.eval("""(()=>{let caught=false;try{tag_stringify.call(tag_throwing);}
        catch(error){caught=error===tag_sentinel;}return caught && tag_nav[tag_key]==='Navigator';})()""")
    report = page.read_access_trace(clear=False)
    assert len(report["events"]) == 3 and report["events"][0]["outcome"] == "threw"
    records = tag_events(page)
    assert len(records) == 2
    assert_tag(records[0], "HTMLDocument")
    assert_tag(records[1], "Navigator", label="Navigator.prototype")
    assert report["dropped_events"] == 0
    page.stop_access_trace()
    assert page.eval("tag_effects", to_py=True) == ["getter"]


def test_tag_filters_capacity_stopping_and_clear_use_the_declared_owner_channels(page):
    page.start_access_trace(max_events=2, object_types=["host_object"])
    page.eval("tag_css[tag_key];tag_doc[tag_key];tag_nav[tag_key];tag_doc[tag_key]")
    before = page.read_access_trace(clear=False)
    assert len(before["events"]) == 2 and before["dropped_events"] == 1
    assert_tag(before["events"][0], "HTMLDocument")
    assert_tag(before["events"][1], "Navigator", label="Navigator.prototype")
    page.stop_access_trace()
    page.eval("tag_doc[tag_key]")
    assert page.read_access_trace(clear=False)["events"] == before["events"]
    assert page.read_access_trace(clear=False)["dropped_events"] == 1
    page.read_access_trace(clear=True)
    assert page.read_access_trace(clear=False)["events"] == []
    page.start_access_trace(object_types=["window_shell"])
    page.eval("tag_doc[tag_key];tag_css[tag_key]")
    record, = events(page)
    assert_tag(record, "CSS", "window_shell", "CSS")


def test_reload_page_isolation_and_cached_prototypes_keep_generation_owned_tag_identities(page):
    old_owner = page.eval("tag_doc_proto")
    page.start_access_trace(object_types=["host_object"])
    page.eval("tag_doc[tag_key];tag_doc_proto[tag_key]")
    before = tag_events(page)
    assert len(before) == 2 and before[0]["object_id"] == before[1]["object_id"]
    with iv8.Page() as other:
        other.load(HTML, BASE)
        bind_tags(other)
        other.eval("tag_doc[tag_key]")
        assert other.read_access_trace()["events"] == []
        page.load(HTML, BASE)
        bind_tags(page)
        page.read_access_trace(clear=True)
        page.eval("tag_doc[tag_key]")
        after, = tag_events(page)
        assert after["generation"] == before[0]["generation"] + 1
        assert after["object_id"] != before[0]["object_id"]
        assert not old_owner.context_alive


def test_debug_symbol_path_and_prod_output_preserve_values_without_transport(capsys):
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        assert capsys.readouterr().out == ""
        bind_tags(page)
        capsys.readouterr()
        assert page.eval("tag_doc[tag_key]") == "HTMLDocument"
        assert capsys.readouterr().out.splitlines() == [
            'GET HTMLDocument.prototype[Symbol.toStringTag] -> "HTMLDocument"']
        page.load(HTML, BASE, mode="prod")
        bind_tags(page)
        assert page.eval("tag_reflect_get(tag_doc,tag_key)") == "HTMLDocument"
        assert capsys.readouterr().out == ""


def test_tag_metadata_does_not_read_user_name_or_constructor_getters(page):
    page.eval("""for(const [owner,name] of [[tag_doc,'name'],[tag_doc_proto,'constructor']])
        Object.defineProperty(owner,name,{configurable:true,get(){tag_effects.push(name);throw tag_sentinel;}});""")
    page.start_access_trace(max_events=1)
    assert page.eval("tag_reflect_get(tag_doc,tag_key)") == "HTMLDocument"
    record, = events(page)
    assert_tag(record, "HTMLDocument")
    page.stop_access_trace()
    assert page.eval("tag_effects", to_py=True) == []
