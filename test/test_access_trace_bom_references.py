"""Remaining independent BOM references, data fields, and rejected receivers."""

import json

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")
BASE = "https://bom-references.test/"
HTML = "<html><head></head><body></body></html>"
REFERENCES = [
    ("performancePrototype", "host_object", "Performance.prototype", name, "Performance." + name, True)
    for name in ("now", "mark", "measure", "getEntries", "getEntriesByName", "getEntriesByType", "clearMarks", "clearMeasures")
] + [
    ("cryptoOwner", "crypto", "crypto", name, "crypto." + name, False)
    for name in ("getRandomValues", "randomUUID")
] + [
    ("storagePrototype", "host_object", "Storage.prototype", name, "Storage." + name, True)
    for name in ("getItem", "setItem", "removeItem", "clear", "key")
] + [
    ("managerOwner", "storage_manager", "navigator.storage", name, "navigator.storage." + name, False)
    for name in ("estimate", "persisted", "persist")
] + [("permissionsOwner", "permissions", "navigator.permissions", "query", "navigator.permissions.query", False)] + [
    ("statusOwner", "permission_status", "permissionStatus", name, "permissionStatus." + name, False)
    for name in ("addEventListener", "removeEventListener", "dispatchEvent")
]


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, BASE)
        value.eval("""globalThis.performanceOwner=performance;globalThis.performancePrototype=Object.getPrototypeOf(performance);
            globalThis.cryptoOwner=crypto;
            globalThis.localOwner=localStorage;globalThis.storagePrototype=Object.getPrototypeOf(localStorage);
            globalThis.managerOwner=navigator.storage;globalThis.permissionsOwner=navigator.permissions;
            permissionsOwner.query({name:'geolocation'}).then(value=>{globalThis.statusOwner=value;});
            globalThis.queryOne=matchMedia('(min-width: 0px)');globalThis.queryTwo=matchMedia('(min-width: 0px)');
            globalThis.syntheticEvent=new Event('change');""")
        value.run_jobs()
        yield value


def events(page, **matches):
    return [event for event in page.read_access_trace(clear=False)["events"]
            if all(event.get(key) == value for key, value in matches.items())]


def test_all_22_bom_method_references_record_get_with_original_data_flags(page):
    assert len(REFERENCES) == 22
    page.watch_apis([entry[4] for entry in REFERENCES])
    page.start_access_trace()
    for owner, object_type, label, member, _, mutable in REFERENCES:
        page.read_access_trace(clear=True)
        result = page.eval(owner + "[" + json.dumps(member) + "]", name="bom-reference.js")
        assert isinstance(result, iv8.JSValue) and result.context_alive, (label, member)
        record, = events(page, object_type=object_type, label=label, member=member, operation="get")
        assert record["outcome"] == "returned" and record["value_summary"] == "function", (label, member)
        assert record["source_kind"] == "eval" and record["resource_name"] == "bom-reference.js"
        assert not events(page, operation="call")
        descriptor = json.loads(page.eval("""JSON.stringify((()=>{
            const descriptor=Object.getOwnPropertyDescriptor(""" + owner + "," + json.dumps(member) + """);
            return {data:'value'in descriptor,writable:descriptor.writable,
                enumerable:descriptor.enumerable,configurable:descriptor.configurable};
        })())"""))
        assert descriptor == {"data": True, "writable": mutable, "enumerable": True, "configurable": mutable}
    assert page.read_watch_api_hits() == []


def test_new_bom_watch_paths_do_not_duplicate_existing_structured_calls(page):
    paths = ["navigator.storage.estimate", "navigator.storage.persisted", "navigator.storage.persist",
             "navigator.permissions.query", "permissionStatus.addEventListener",
             "permissionStatus.removeEventListener", "permissionStatus.dispatchEvent",
             "Storage.setItem", "Storage.getItem", "Storage.removeItem", "Storage.key", "Storage.clear"]
    page.watch_apis(paths)
    page.start_access_trace()
    assert page.eval("""(()=>{
        managerOwner.estimate();managerOwner.persisted();managerOwner.persist();
        permissionsOwner.query({name:'geolocation'});
        const listener=()=>{};statusOwner.addEventListener('change',listener);
        statusOwner.removeEventListener('change',listener);
        const dispatched=statusOwner.dispatchEvent(syntheticEvent);
        localOwner.setItem('key','value');const value=localOwner.getItem('key');
        localOwner.removeItem('key');const key=localOwner.key(0);localOwner.clear();
        return dispatched===false && value==='value' && key===null;
    })()""", name="bom-calls.js")
    expected = [("storage_manager", "estimate"), ("storage_manager", "persisted"), ("storage_manager", "persist"),
                ("permissions", "query"), ("permission_status", "addEventListener"),
                ("permission_status", "removeEventListener"), ("permission_status", "dispatchEvent"),
                ("storage", "setItem"), ("storage", "getItem"), ("storage", "removeItem"),
                ("storage", "key"), ("storage", "clear")]
    recorded = [event for event in events(page, operation="call")
                if event["object_type"] in {"storage_manager", "permissions", "permission_status", "storage"}]
    assert [(event["object_type"], event["member"]) for event in recorded] == expected
    assert all(event["outcome"] == "returned" and event["resource_name"] == "bom-calls.js" for event in recorded)
    assert [hit["path"] for hit in page.read_watch_api_hits()] == paths


def test_status_and_media_query_onchange_keep_per_wrapper_data_and_log_reads_writes(page):
    page.start_access_trace()
    assert page.eval("""(()=>{
        const callback=()=>{};
        const initial=statusOwner.onchange===null && queryOne.onchange===null;
        statusOwner.onchange=callback;queryOne.onchange=callback;
        return initial && statusOwner.onchange===callback && queryOne.onchange===callback &&
            queryTwo.onchange===null && queryOne!==queryTwo;
    })()""", name="bom-onchange.js")
    for object_type, label, get_count in [("permission_status", "permissionStatus", 2), ("media_query_list", "matchMedia", 3)]:
        reads = events(page, object_type=object_type, label=label, member="onchange", operation="get")
        writes = events(page, object_type=object_type, label=label, member="onchange", operation="set")
        assert len(reads) == get_count and len(writes) == 1
        assert writes[0]["value_summary"] == "function" and writes[0]["outcome"] == "returned"
        assert all(event["outcome"] == "returned" for event in reads)
    assert page.eval("""[statusOwner,queryOne].every(owner=>{
        const descriptor=Object.getOwnPropertyDescriptor(owner,'onchange');
        return 'value'in descriptor && descriptor.writable && descriptor.enumerable && descriptor.configurable;
    })""")


def test_performance_entry_fields_have_separate_stable_identity_and_stay_readonly(page):
    page.eval("""performanceOwner.mark('sample');
        performanceOwner.mark('other');
        globalThis.firstEntry=performanceOwner.getEntries()[0];
        globalThis.secondEntry=performanceOwner.getEntries()[1];
        globalThis.sameFirstEntry=performanceOwner.getEntries()[0];""")
    assert page.eval("firstEntry === sameFirstEntry && firstEntry instanceof PerformanceMark")
    page.start_access_trace(object_types=["host_object"])
    assert json.loads(page.eval("""JSON.stringify([
        [firstEntry.name,firstEntry.entryType,firstEntry.startTime,firstEntry.duration],
        [secondEntry.name,secondEntry.entryType,secondEntry.startTime,secondEntry.duration]])""")) == [
            ["sample", "mark", 0, 0], ["other", "mark", 0, 0],
        ]
    reads = events(page, label="PerformanceEntry", operation="get")
    assert [event["member"] for event in reads] == ["name", "entryType", "startTime", "duration"] * 2
    assert all(event["outcome"] == "returned" for event in reads)
    assert len({event["object_id"] for event in reads[:4]}) == len({event["object_id"] for event in reads[4:]}) == 1
    assert reads[0]["object_id"] != reads[4]["object_id"]
    assert page.eval("""(()=>{
        const descriptor=Object.getOwnPropertyDescriptor(PerformanceEntry.prototype,'name');let rejected=false;
        try{(function(){'use strict';firstEntry.name='changed';})()}catch(error){rejected=error instanceof TypeError;}
        return !Object.hasOwn(firstEntry,'name') && typeof descriptor.get==='function' &&
            descriptor.set===undefined && descriptor.enumerable &&
            descriptor.configurable && rejected && firstEntry.name==='sample';
    })()""")
    assert not events(page, label="PerformanceEntry", operation="set")


@pytest.mark.parametrize("member,arguments", [
    ("getItem", "'key'"), ("setItem", "'key','value'"), ("removeItem", "'key'"),
    ("clear", ""), ("key", "0"), ("length", None),
])
def test_invalid_storage_receiver_is_logged_without_changing_valid_identity(page, member, arguments):
    page.eval("localOwner.setItem('key','value')")
    if arguments is None:
        page.eval("globalThis.storageOperation=Object.getOwnPropertyDescriptor(storagePrototype,'length').get")
    else:
        page.eval("globalThis.storageOperation=storagePrototype[" + json.dumps(member) + "]")
        page.watch_apis(["Storage." + member])
    page.start_access_trace()
    operation = "get" if arguments is None else "call"
    valid_call = "storageOperation.call(localOwner" + ("," + arguments if arguments else "") + ")"
    invalid_call = "storageOperation.call({}" + ("," + arguments if arguments else "") + ")"
    page.eval(valid_call, name="storage-valid.js")
    assert page.eval("""(()=>{try{""" + invalid_call + """;return false;}
        catch(error){return error instanceof TypeError && error.message==='Illegal invocation';}})()""",
        name="storage-invalid.js")
    page.eval(valid_call, name="storage-valid.js")
    valid = events(page, object_type="storage", member=member, operation=operation)
    rejected, = events(page, object_type="host_object", label="Storage", member=member, operation=operation)
    assert len(valid) == 2 and valid[0]["object_id"] == valid[1]["object_id"]
    assert all(event["outcome"] == "returned" for event in valid)
    assert rejected["outcome"] == "threw" and rejected["exception_name"] == "TypeError"
    assert rejected["exception_message"] == "Uncaught TypeError: Illegal invocation" and rejected["exception_stack"]
    assert rejected["resource_name"] == "storage-invalid.js"
    assert rejected["object_id"] != valid[0]["object_id"]
    if arguments is not None:
        assert [hit["path"] for hit in page.read_watch_api_hits()] == ["Storage." + member] * 3
