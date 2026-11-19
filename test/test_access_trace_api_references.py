"""Known host API references are observable; ordinary JS aliases/data are not.

Only the generic native constructor registry is required here. Manual/JS Event,
Blob and other independently installed constructors have separate coverage.
"""

import json

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")
BASE = "https://api-references.test/"
HTML = "<html><head></head><body></body></html>"
METHODS = [("CSS", "escape"), ("indexedDB", "open"), ("caches", "keys")]
CONSTANTS = [("GPUBufferUsage", "MAP_READ", 1), ("NodeFilter", "SHOW_ALL", 4294967295)]
CONSTRUCTORS = ["Document", "EventTarget", "Text", "DOMParser", "Navigator"]


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, BASE)
        yield value


def events(page):
    return page.read_access_trace(clear=False)["events"]


def method_reads(page, owner, member):
    return [event for event in events(page) if event["object_type"] == "window_shell" and event["label"] == owner
            and event["member"] == member and event["operation"] == "get"]


def constructor_events(page, name, operation):
    return [event for event in events(page) if event["object_type"] == "window" and event["label"] == "window"
            and event["member"] == name and event["operation"] == operation]


@pytest.mark.parametrize("owner,member", METHODS, ids=[owner+"."+member for owner, member in METHODS])
def test_known_method_reference_reads_record_get_without_a_call(page, owner, member):
    page.eval("globalThis.apiOwner=window[" + json.dumps(owner) + "]")
    page.watch_apis([owner + "." + member])
    page.start_access_trace()
    handle = page.eval("apiOwner[" + json.dumps(member) + "]", name="api-reference.js")
    assert isinstance(handle, iv8.JSValue) and handle.context_alive
    record, = method_reads(page, owner, member)
    assert record["outcome"] == "returned" and record["value_summary"] == "function"
    assert record["source_kind"] == "eval" and record["resource_name"] == "api-reference.js"
    assert not [event for event in events(page) if event["operation"] == "call"]
    assert page.read_watch_api_hits() == []


@pytest.mark.parametrize("owner,member,expected", CONSTANTS, ids=[owner+"."+member for owner, member, _ in CONSTANTS])
def test_static_api_constant_reads_record_their_namespace(page, owner, member, expected):
    page.eval("globalThis.apiOwner=window[" + json.dumps(owner) + "]")
    page.watch_apis([owner + "." + member])
    page.start_access_trace()
    assert page.eval("apiOwner[" + json.dumps(member) + "]", name="api-constant.js") == expected
    record, = method_reads(page, owner, member)
    assert record["outcome"] == "returned" and record["value_summary"] == "number:" + str(expected)
    assert record["resource_name"] == "api-constant.js"
    assert page.read_watch_api_hits() == []


def test_chrome_app_is_a_known_api_entry_but_its_user_fields_are_not(page):
    page.eval("globalThis.apiOwner=chrome")
    page.watch_apis(["chrome.app"])
    page.start_access_trace()
    value = page.eval("apiOwner.app", name="chrome-app.js")
    assert value.context_alive
    record, = method_reads(page, "chrome", "app")
    assert record["outcome"] == "returned" and record["value_summary"] == "object"
    page.eval("globalThis.ordinaryApp=apiOwner.app")
    page.read_access_trace(clear=True)
    assert page.eval("ordinaryApp.userValue=7;ordinaryApp.userValue") == 7
    assert events(page) == [] and page.read_watch_api_hits() == []


def test_reference_tracking_preserves_method_descriptors_native_shapes_and_identity(page):
    references = json.dumps(METHODS + [(owner, member) for owner, member, _ in CONSTANTS] + [("chrome", "app")])
    page.eval("globalThis.savedApiValues=" + references + ".map(([owner,member])=>window[owner][member])")
    snapshot = """JSON.stringify(""" + references + """.map(([owner,member])=>{
        const value=window[owner][member];let holder=window[owner];
        while(holder && !Object.getOwnPropertyDescriptor(holder,member))holder=Object.getPrototypeOf(holder);
        const descriptor=Object.getOwnPropertyDescriptor(holder,member);
        const result={data:'value'in descriptor,writable:descriptor.writable,enumerable:descriptor.enumerable,
            configurable:descriptor.configurable,prototype:Object.getPrototypeOf(value === null ? {} : Object(value)) === Function.prototype};
        if(typeof value==='function')Object.assign(result,{name:value.name,length:value.length,
            source:Function.prototype.toString.call(value),ownPrototype:Object.hasOwn(value,'prototype')});
        return result;
    }))"""
    before = json.loads(page.eval(snapshot))
    assert all(item["data"] for item in before)
    page.watch_apis([owner + "." + member for owner, member in METHODS])
    page.start_access_trace()
    assert json.loads(page.eval(snapshot)) == before
    assert page.eval(references + ".every(([owner,member],index)=>window[owner][member]===savedApiValues[index])")
    page.stop_access_trace()
    page.watch_apis([])
    assert json.loads(page.eval(snapshot)) == before
    assert page.eval(references + ".every(([owner,member],index)=>window[owner][member]===savedApiValues[index])")
    assert page.read_watch_api_hits() == []


def test_reference_constants_keep_readonly_and_enumeration_attributes(page):
    page.start_access_trace()
    for owner, member, expected in CONSTANTS:
        result = json.loads(page.eval("""JSON.stringify((()=>{
            const owner=window[""" + json.dumps(owner) + """],name=""" + json.dumps(member) + """;
            const descriptor=Object.getOwnPropertyDescriptor(owner,name);
            owner[name]=0;let rejected=false;
            try{(function(){'use strict';owner[name]=0;})()}catch(error){rejected=error instanceof TypeError;}
            return {data:'value'in descriptor,writable:descriptor.writable,enumerable:descriptor.enumerable,
                configurable:descriptor.configurable,value:owner[name],rejected};
        })())"""))
        assert result == {"data": True, "writable": False, "enumerable": True,
                          "configurable": False, "value": expected, "rejected": True}
        assert not [event for event in events(page) if event["label"] == owner
                    and event["member"] == member and event["operation"] == "set"]


@pytest.mark.parametrize("name", CONSTRUCTORS)
def test_generic_constructor_global_reads_do_not_invoke_the_constructor(page, name):
    page.watch_apis(["window." + name])
    page.start_access_trace()
    value = page.eval("window[" + json.dumps(name) + "]", name="constructor-reference.js")
    assert value.context_alive
    record, = constructor_events(page, name, "get")
    assert record["outcome"] == "returned" and record["value_summary"] == "function"
    assert not constructor_events(page, name, "call")
    assert page.read_watch_api_hits() == []


@pytest.mark.parametrize("name,arguments,count,check", [
    ("Document", "", 0, "constructedValue.nodeType===9 && constructedValue.ownerDocument===null"),
    ("EventTarget", "", 0, "typeof constructedValue.addEventListener==='function'"),
    ("Text", "'payload'", 1, "constructedValue.nodeType===3 && constructedValue.data==='payload'"),
    ("DOMParser", "", 0, "typeof constructedValue.parseFromString==='function'"),
])
def test_cached_generic_constructors_record_actual_creation_without_a_global_reread(page, name, arguments, count, check):
    page.eval("globalThis.cachedCtor=window[" + json.dumps(name) + "]")
    page.watch_apis(["window." + name])
    page.start_access_trace()
    page.eval("globalThis.constructedValue=new cachedCtor(" + arguments + ")", name="generic-construct.js")
    assert page.eval("constructedValue instanceof cachedCtor && (" + check + ")")
    assert constructor_events(page, name, "get") == []
    record, = constructor_events(page, name, "call")
    assert record["outcome"] == "returned" and record["return_summary"] == "host_object"
    assert record["argument_count"] == count and len(record["argument_summaries"]) == count
    assert record["resource_name"] == "generic-construct.js"
    assert page.read_watch_api_hits() == [{"path": "window." + name, "resource_name": "generic-construct.js", "type": "call"}]


@pytest.mark.parametrize("name,expression", [
    ("Navigator", "new cachedCtor()"), ("Document", "cachedCtor()"), ("Text", "cachedCtor('payload')"),
])
def test_generic_constructor_errors_are_completed_and_preserved(page, name, expression):
    page.eval("globalThis.cachedCtor=window[" + json.dumps(name) + "]")
    page.watch_apis(["window." + name])
    page.start_access_trace()
    with pytest.raises(iv8.JSError) as error:
        page.eval(expression, name="generic-construct-error.js")
    assert error.value.name == "TypeError"
    record, = constructor_events(page, name, "call")
    assert record["outcome"] == "threw" and record["exception_name"] == "TypeError"
    assert record["exception_message"] and record["exception_stack"]
    assert "return_summary" not in record
    assert constructor_events(page, name, "get") == []
    assert [hit["path"] for hit in page.read_watch_api_hits()] == ["window." + name]


def test_constructor_get_and_construct_are_independent_monitored_operations(page):
    page.eval("globalThis.cachedCtor=Document")
    page.watch_apis(["window.Document"])
    page.start_access_trace()
    page.eval("new cachedCtor()", name="independent-construct.js")
    assert len(constructor_events(page, "Document", "call")) == 1
    assert constructor_events(page, "Document", "get") == []
    page.eval("window.Document", name="independent-get.js")
    assert len(constructor_events(page, "Document", "get")) == 1
    assert len(constructor_events(page, "Document", "call")) == 1
    assert len(page.read_watch_api_hits()) == 1


def test_local_alias_reads_and_returned_user_data_do_not_become_host_api_gets(page):
    page.eval("globalThis.cachedEscape=CSS.escape;globalThis.returnedData=structuredClone({value:7})")
    page.watch_apis(["CSS.escape"])
    page.start_access_trace()
    assert page.eval("typeof cachedEscape==='function' && returnedData.value===7")
    assert events(page) == [] and page.read_watch_api_hits() == []
    assert page.eval("cachedEscape('a b')") == "a\\ b"
    recorded = events(page)
    assert not [event for event in recorded if event["member"] == "escape" and event["operation"] == "get"]
    assert len([event for event in recorded if event["label"] == "CSS" and event["member"] == "escape" and event["operation"] == "call"]) == 1
    assert [hit["path"] for hit in page.read_watch_api_hits()] == ["CSS.escape"]
