"""Legacy Window root entry reads, preserving existing object semantics."""

import json

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")
ROOTS = (
    "history", "location", "navigator", "document", "screen", "console",
    "performance", "crypto", "localStorage", "sessionStorage", "visualViewport", "scheduler",
)
DATA_ROOTS = set(ROOTS) - {"history", "location", "crypto"}
READONLY_DATA_ROOTS = {
    "performance", "localStorage", "sessionStorage", "visualViewport", "scheduler",
}
BASE = "https://legacy-roots.test/start?seed=1"
HTML = "<html><head></head><body></body></html>"


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, BASE)
        yield value


def events(page):
    return page.read_access_trace(clear=False)["events"]


def root_reads(records, name):
    return [record for record in records if record["object_type"] == "window" and record["label"] == "window"
            and record["member"] == name and record["operation"] == "get"]


@pytest.mark.parametrize("name", ROOTS)
def test_each_root_read_is_completed_and_remains_readable_after_stop(page, name):
    page.watch_apis(["window." + name])
    page.start_access_trace()
    handle = page.eval("window[" + json.dumps(name) + "]", name=name + "-entry.js")
    assert isinstance(handle, iv8.JSValue) and handle.context_alive
    page.stop_access_trace()
    recorded = events(page)
    assert [(record["object_type"], record["label"], record["member"], record["operation"]) for record in recorded] == [
        ("window", "window", "window", "get"), ("window", "window", name, "get"),
    ]
    assert recorded[0]["outcome"] == "returned" and recorded[0]["value_summary"] == "object"
    record, = root_reads(recorded, name)
    assert record["outcome"] == "returned"
    assert record["value_summary"] in {"host_object", "object"}
    assert record["source_kind"] == "eval" and record["resource_name"] == name + "-entry.js"
    text = page.read_access_trace(clear=False, format="text")
    lines = text.splitlines()
    assert len(lines) == 2
    assert lines[0].startswith("GET window.window -> ")
    assert lines[1].startswith("GET window." + name + " -> ")
    page.eval("window[" + json.dumps(name) + "]")
    assert events(page) == recorded
    assert page.read_watch_api_hits() == []


def test_root_and_child_accesses_keep_distinct_native_categories(page):
    cases = [
        ("history", "history.length", "history", "length", "get"),
        ("location", "location.href", "location", "href", "get"),
        ("navigator", "navigator.userAgent", "navigator", "userAgent", "get"),
        ("document", "document.readyState", "document", "readyState", "get"),
        ("screen", "screen.width", "screen", "width", "get"),
        ("console", "console.log('legacy')", "console", "log", "call"),
        ("performance", "performance.now()", "performance", "now", "call"),
        ("crypto", "crypto.randomUUID()", "crypto", "randomUUID", "call"),
        ("localStorage", "localStorage.length", "storage", "length", "get"),
        ("sessionStorage", "sessionStorage.length", "storage", "length", "get"),
        ("visualViewport", "visualViewport.width", "visual_viewport", "width", "get"),
    ]
    page.start_access_trace()
    for root, expression, category, member, operation in cases:
        page.read_access_trace(clear=True)
        page.eval(expression, name="root-child.js")
        recorded = events(page)
        parent, = root_reads(recorded, root)
        children = [record for record in recorded if record["object_type"] == category
                    and record["member"] == member and record["operation"] == operation]
        assert len(children) == 1, root
        child = children[0]
        assert parent["outcome"] == child["outcome"] == "returned"
        assert parent["seq"] < child["seq"] and parent["object_id"] != child["object_id"]
        assert child["resource_name"] == "root-child.js" and child["source_kind"] == "eval"


def test_scheduler_root_tracing_preserves_existing_javascript_task_facade(page):
    page.watch_apis(["window.scheduler"])
    page.start_access_trace(object_types=["window"])
    assert page.eval("""(() => {
        const promise=scheduler.postTask(()=>7);
        promise.then(value=>{globalThis.schedulerResult=value;});
        return promise instanceof Promise;
    })()""")
    assert len(root_reads(events(page), "scheduler")) == 1
    page.run_timers()
    page.run_jobs()
    assert page.eval("schedulerResult") == 7
    assert page.read_watch_api_hits() == []


def test_descriptors_readonly_flags_and_root_identities_are_preserved(page):
    names = json.dumps(ROOTS)
    snapshot = """JSON.stringify((() => {
        const result={};
        const shape=fn=>({name:fn.name,length:fn.length,source:Function.prototype.toString.call(fn),prototype:Object.hasOwn(fn,'prototype')});
        for(const name of """ + names + """) {
            const descriptor=Object.getOwnPropertyDescriptor(window,name);
            const item={enumerable:descriptor.enumerable,configurable:descriptor.configurable};
            if('value' in descriptor){item.kind='data';item.writable=descriptor.writable;}
            else{item.kind='accessor';item.get=descriptor.get&&shape(descriptor.get);item.set=descriptor.set&&shape(descriptor.set);}
            result[name]=item;
        }
        return result;
    })())"""
    page.eval("globalThis.originalRootValues=" + names + ".map(name=>window[name])")
    before = json.loads(page.eval(snapshot))
    for name in DATA_ROOTS:
        assert before[name] == {
            "kind": "data", "writable": name not in READONLY_DATA_ROOTS,
            "enumerable": name != "console", "configurable": name not in READONLY_DATA_ROOTS,
        }, name
    assert before["history"]["kind"] == before["location"]["kind"] == "accessor"
    assert before["crypto"] == {
        "kind": "accessor", "enumerable": True, "configurable": True,
        "get": {"name": "get crypto", "length": 0,
                "source": "function get crypto() { [native code] }", "prototype": False},
    }
    assert before["history"]["configurable"] is True and "set" not in before["history"]
    assert before["location"]["configurable"] is False
    for kind, length in [("get", 0), ("set", 1)]:
        assert before["location"][kind] == {
            "name": kind + " location", "length": length,
            "source": "function " + kind + " location() { [native code] }", "prototype": False,
        }
    page.watch_apis(["window." + name for name in ROOTS])
    page.start_access_trace()
    assert json.loads(page.eval(snapshot)) == before
    assert page.eval(names + ".every((name,index)=>window[name]===originalRootValues[index])")
    page.stop_access_trace()
    assert json.loads(page.eval(snapshot)) == before
    assert page.eval(names + ".every((name,index)=>window[name]===originalRootValues[index])")
    assert page.read_watch_api_hits() == []


def test_readonly_roots_reject_writes_without_fake_set_events(page):
    page.start_access_trace()
    for name in sorted(READONLY_DATA_ROOTS | {"crypto"}):
        page.read_access_trace(clear=True)
        assert page.eval("""(() => {
            const name=""" + json.dumps(name) + """, original=window[name];
            window[name]=null;
            let rejected=false;
            try {(function(){'use strict';window[name]=null;})()} catch(error){rejected=error instanceof TypeError;}
            return rejected && window[name]===original;
        })()""")
        assert not [record for record in events(page) if record["member"] == name and record["operation"] == "set"], name


def test_location_assignment_records_user_set_without_native_forwarding_with_one_conversion(page):
    page.watch_apis(["window.location", "location.href"])
    page.start_access_trace()
    assert page.eval("""(() => {
        const original=window.location;let conversions=0;
        window.location={toString(){conversions++;return 'next?q=2#section';}};
        return conversions===1 && window.location===original && original.href==='""" + BASE + """';
    })()""", name="assign-location.js")
    writes = [record for record in events(page) if record["operation"] == "set"]
    assert [(record["object_type"], record["label"], record["member"]) for record in writes] == [
        ("window", "window", "location"),
    ]
    assert all(record["outcome"] == "returned" and record["value_summary"] == "object" for record in writes)
    assert all(record["resource_name"] == "assign-location.js" for record in writes)
    intents = page.read_navigation_requests()["requests"]
    assert [(item["operation"], item["url"]) for item in intents] == [
        ("assign", "https://legacy-roots.test/next?q=2#section"),
    ]
    assert page.read_watch_api_hits() == []

    # The forwarded href assignment above belongs to the implementation. A
    # direct user href write must still record its own set and conversion.
    page.start_access_trace()
    assert page.eval("""(() => {
        const original=location;let conversions=0;
        original.href={toString(){conversions++;return 'href-next';}};
        return conversions===1 && original.href==='""" + BASE + """';
    })()""", name="assign-href.js")
    writes = [record for record in events(page) if record["operation"] == "set"]
    assert [(record["object_type"], record["label"], record["member"]) for record in writes] == [
        ("location", "location", "href"),
    ]
    assert writes[0]["outcome"] == "returned" and writes[0]["value_summary"] == "object"
    assert writes[0]["resource_name"] == "assign-href.js"
    assert [(item["operation"], item["url"]) for item in page.read_navigation_requests()["requests"]] == [
        ("assign", "https://legacy-roots.test/href-next"),
    ]
    assert page.read_watch_api_hits() == []


def test_location_throw_preserves_identity_and_failed_user_set_completion(page):
    for target, object_type, member in (
        ("window.location", "window", "location"),
        ("location.href", "location", "href"),
    ):
        page.start_access_trace()
        assert page.eval("""(() => {
            const sentinel=Error('location coercion sentinel');let conversions=0,caught=false;
            try {""" + target + """={toString(){conversions++;throw sentinel;}};}
            catch(error){caught=error===sentinel;}
            return conversions===1 && caught;
        })()""", name="location-throw.js")
        writes = [record for record in events(page) if record["operation"] == "set"]
        assert [(record["object_type"], record["member"]) for record in writes] == [(object_type, member)]
        assert writes[0]["outcome"] == "threw"
        if object_type == "window":
            assert writes[0]["value_summary"] == "object"
        else:
            # Generic HostObject setters publish the value summary only after
            # success; their original synchronous exception evidence remains.
            assert "value_summary" not in writes[0]
        assert "location coercion sentinel" in writes[0]["exception_message"]
        assert page.read_navigation_requests()["requests"] == []


def test_root_filters_and_stop_do_not_turn_property_reads_into_watch_hits(page):
    page.watch_apis(["window.history", "window.navigator"])
    page.start_access_trace(object_types=["window"])
    page.eval("history.length;navigator.userAgent")
    assert [(record["member"], record["operation"]) for record in events(page)] == [("history", "get"), ("navigator", "get")]
    page.start_access_trace(object_types=["history", "navigator"])
    page.eval("history.length;navigator.userAgent")
    before = events(page)
    assert [(record["object_type"], record["member"]) for record in before] == [("history", "length"), ("navigator", "userAgent")]
    page.stop_access_trace()
    page.eval("history.length;navigator.userAgent")
    assert events(page) == before
    assert page.read_watch_api_hits() == []


def test_root_configuration_survives_reload_and_is_independent_between_pages(page):
    page.watch_apis(["window." + name for name in ROOTS])
    page.start_access_trace(object_types=["window"])
    page.eval("navigator", name="first-root.js")
    first, = root_reads(events(page), "navigator")
    with iv8.Page() as other:
        other.eval("navigator;history;location")
        assert other.read_access_trace()["events"] == [] and other.read_watch_api_hits() == []
    page.load(HTML, BASE)
    page.read_access_trace(clear=True)
    page.eval("navigator", name="second-root.js")
    second, = root_reads(events(page), "navigator")
    assert second["generation"] == first["generation"] + 1
    assert second["object_id"] != first["object_id"]
    assert second["resource_name"] == "second-root.js"
    assert page.read_watch_api_hits() == []
