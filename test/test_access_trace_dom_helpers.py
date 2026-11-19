"""Monitor direct DOMTokenList and live collection native entries."""

import json

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")
BASE = "https://dom-helper-trace.test/"
HTML = """<html><head></head><body>
<div id="first" name="alias" class="group a"></div>
<div id="second" class="group b"></div></body></html>"""


def prepare(page):
    page.eval("""globalThis.firstNode=document.getElementById('first');
        globalThis.secondNode=document.getElementById('second');
        globalThis.tokens=firstNode.classList;globalThis.otherTokens=secondNode.classList;
        globalThis.collection=document.getElementsByTagName('div');
        globalThis.otherCollection=document.getElementsByClassName('group');
        globalThis.allCollection=document.all;""")


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, BASE)
        prepare(value)
        yield value


def events(page, **matches):
    return [event for event in page.read_access_trace(clear=False)["events"]
            if all(event.get(key) == value for key, value in matches.items())]


def test_class_list_methods_and_value_access_have_the_binding_identity(page):
    names = ["add", "contains", "toggle", "replace", "remove", "item"]
    page.watch_apis(["DOMTokenList." + name for name in names])
    page.start_access_trace(object_types=["host_object"])
    assert page.eval("""(()=>{
        const initial=tokens.length===2 && tokens.value==='group a';
        tokens.add('c');const contains=tokens.contains('c');
        const toggled=tokens.toggle('d'),replaced=tokens.replace('d','e');
        tokens.remove('a');const item=tokens.item(1);
        tokens.value='x y';
        return initial && contains && toggled && replaced && item==='c' &&
            tokens.value==='x y' && tokens.length===2;
    })()""", name="class-list-native.js")
    recorded = events(page, object_type="host_object", label="DOMTokenList")
    calls = [event for event in recorded if event["operation"] == "call"]
    assert [event["member"] for event in calls] == names
    assert all(event["outcome"] == "returned" for event in recorded)
    assert len({event["object_id"] for event in recorded}) == 1
    assert all(event["source_kind"] == "eval" and event["resource_name"] == "class-list-native.js"
               for event in recorded)
    for name in names:
        reference, = [event for event in recorded if event["operation"] == "get" and event["member"] == name]
        assert reference["value_summary"] == "function"
    write, = events(page, label="DOMTokenList", member="value", operation="set")
    assert write["value_summary"] == "string:3:x y"
    assert [event["value_summary"] for event in recorded
            if event["operation"] == "get" and event["member"] == "length"] == ["number:2", "number:2"]
    assert [hit["path"] for hit in page.read_watch_api_hits()] == ["DOMTokenList." + name for name in names]
    assert page.eval("firstNode.className") == "x y"


def test_class_list_call_and_boolean_setter_completions_preserve_errors(page):
    page.watch_apis(["DOMTokenList.add"])
    page.start_access_trace(object_types=["host_object"])
    assert page.eval("""(()=>{
        let methodError=false,coercions=0;
        try{tokens.add('');}catch(error){methodError=error instanceof TypeError;}
        const sentinel=Error('class value sentinel');let preserved=false;
        try{tokens.value={toString(){coercions++;throw sentinel;}};}
        catch(error){preserved=error===sentinel;}
        return methodError && preserved && coercions===1 && tokens.value==='group a';
    })()""", name="class-list-error.js")
    call, = events(page, label="DOMTokenList", member="add", operation="call")
    write, = events(page, label="DOMTokenList", member="value", operation="set")
    assert call["outcome"] == "threw" and call["exception_name"] == "TypeError"
    assert write["outcome"] == "threw" and write["exception_name"] == "Error"
    assert write["value_summary"] == "object"
    assert "class value sentinel" in write["exception_message"]
    assert [hit["path"] for hit in page.read_watch_api_hits()] == ["DOMTokenList.add"]


def test_collection_length_item_index_name_and_probe_entries_are_distinct(page):
    page.watch_apis(["HTMLCollection.item", "HTMLCollection.namedItem"])
    page.start_access_trace(object_types=["host_object"])
    assert page.eval("""collection.length===2 && collection.item(0)===firstNode &&
        collection[1]===secondNode && collection.alias===firstNode &&
        collection.namedItem('alias')===firstNode && collection[9]===undefined &&
        collection.missingName===undefined""", name="collection-native.js")
    recorded = events(page, object_type="host_object", label="HTMLCollection")
    length, = [event for event in recorded if event["member"] == "length" and event["operation"] == "get"]
    assert length["value_summary"] == "number:2" and length["outcome"] == "returned"
    for member in ["1", "alias"]:
        read, = [event for event in recorded if event["member"] == member and event["operation"] == "get"]
        assert read["outcome"] == "returned" and read["value_summary"] == "host_object"
    for member in ["item", "namedItem"]:
        reference, = events(page, label="HTMLCollection.prototype", member=member, operation="get")
        call, = [event for event in recorded if event["member"] == member and event["operation"] == "call"]
        assert reference["value_summary"] == "function"
        assert call["outcome"] == "returned" and call["return_summary"] == "host_object"
    for member in ["9", "missingName"]:
        probes = [event for event in recorded if event["member"] == member and event["operation"] == "get_probe"]
        assert probes and all(event["resolution"] == "missing" for event in probes)
        assert not [event for event in recorded if event["member"] == member and event["operation"] == "get"]
    assert len({event["object_id"] for event in recorded}) == 1
    assert [hit["path"] for hit in page.read_watch_api_hits()] == ["HTMLCollection.item", "HTMLCollection.namedItem"]
    page.read_access_trace(clear=True)
    assert page.eval("1 in collection")
    assert events(page, label="HTMLCollection", member="1", operation="has")
    assert json.loads(page.eval("JSON.stringify(Object.keys(collection))")) == ["0", "1"]
    assert events(page, label="HTMLCollection", member="", operation="own_keys")


@pytest.mark.parametrize("label,expression", [
    ("DOMTokenList", "tokens.length;otherTokens.length"),
    ("HTMLCollection", "collection.length;otherCollection.length"),
])
def test_two_dom_helper_bindings_and_reload_never_share_trace_identity(page, label, expression):
    page.start_access_trace(object_types=["host_object"])
    page.eval(expression)
    before = events(page, label=label, member="length", operation="get")
    assert len(before) == 2 and before[0]["object_id"] != before[1]["object_id"]
    page.read_access_trace(clear=True)
    page.load(HTML, BASE)
    prepare(page)
    page.eval(expression)
    after = events(page, label=label, member="length", operation="get")
    assert len(after) == 2 and after[0]["object_id"] != after[1]["object_id"]
    assert {event["object_id"] for event in before}.isdisjoint(event["object_id"] for event in after)
    assert after[0]["generation"] == before[0]["generation"] + 1


def test_shared_collection_method_data_keeps_shape_identity_and_receiver_assignment(page):
    snapshot = """JSON.stringify((()=>{
        const prototype=Object.getPrototypeOf(collection),descriptor=Object.getOwnPropertyDescriptor(prototype,'item');
        const method=prototype.item;
        return {data:'value'in descriptor,writable:descriptor.writable,enumerable:descriptor.enumerable,
            configurable:descriptor.configurable,name:method.name,length:method.length,
            source:Function.prototype.toString.call(method),ownPrototype:Object.hasOwn(method,'prototype')};
    })())"""
    before = page.eval(snapshot)
    page.eval("globalThis.originalItem=collection.item")
    page.start_access_trace(object_types=["host_object"])
    assert page.eval(snapshot) == before
    page.read_access_trace(clear=True)
    page.eval("Object.getPrototypeOf(collection).item")
    reference, = events(page, label="HTMLCollection.prototype", member="item", operation="get")
    assert reference["outcome"] == "returned" and reference["value_summary"] == "function"
    assert page.eval("""(()=>{
        const override=()=>7;collection.item=override;
        return Object.hasOwn(collection,'item') && collection.item===override &&
            otherCollection.item===originalItem && Object.getPrototypeOf(collection).item===originalItem;
    })()""")


def test_html_all_collection_callable_entry_uses_its_binding_label(page):
    page.watch_apis(["HTMLAllCollection.[[Call]]"])
    page.start_access_trace(object_types=["host_object"])
    assert page.eval("allCollection('alias')===firstNode && allCollection.length>0",
                     name="all-collection-native.js")
    call, = events(page, label="HTMLAllCollection", member="[[Call]]", operation="call")
    length, = events(page, label="HTMLAllCollection", member="length", operation="get")
    assert call["outcome"] == "returned" and call["return_summary"] == "host_object"
    assert call["argument_summaries"] == ["string:5:alias"]
    assert call["object_id"] == length["object_id"]
    assert [hit["path"] for hit in page.read_watch_api_hits()] == ["HTMLAllCollection.[[Call]]"]
