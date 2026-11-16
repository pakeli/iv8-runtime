"""Event API entries and Window identities remain observable across aliases."""

import json

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load('<html><body><div id="namedElement"></div></body></html>', "https://event-trace.test/")
        yield value


def records(page, label, member, operation):
    return [event for event in page.read_access_trace(clear=False)["events"]
            if event["label"] == label and event["member"] == member and event["operation"] == operation]


def test_all_window_entry_paths_use_the_same_identity(page):
    page.start_access_trace()
    page.eval("window;self;top;parent;frames;opener;name;status;innerWidth;history;location;navigator;namedElement")
    events = page.read_access_trace()["events"]
    assert {event["member"] for event in events} == {
        "window", "self", "top", "parent", "frames", "opener", "name", "status",
        "innerWidth", "history", "location", "navigator", "namedElement"}
    assert all(event["object_type"] == "window" and event["label"] == "window" for event in events)
    assert len({event["object_id"] for event in events}) == 1


def test_window_event_handler_reads_and_writes_have_property_completion(page):
    page.eval("globalThis.listener=function(){}")
    page.start_access_trace()
    assert page.eval("onload=listener;onload===listener")
    assert page.eval("onload=7;onload===null")
    events = records(page, "window", "onload", "set")
    assert [event["value_summary"] for event in events] == ["function", "number:7"]
    assert all(event["outcome"] == "returned" for event in events)
    assert [event["value_summary"] for event in records(page, "window", "onload", "get")] == ["function", "null"]
    assert len({event["object_id"] for event in page.read_access_trace()["events"]}) == 1


@pytest.mark.parametrize("constructor", ["Event", "UIEvent", "CustomEvent", "MouseEvent", "KeyboardEvent", "FocusEvent", "MessageEvent"])
def test_cached_event_constructors_and_instance_fields_are_recorded(page, constructor):
    page.eval("globalThis.CachedEvent=" + constructor)
    page.watch_apis(["window." + constructor])
    page.start_access_trace()
    assert page.eval("globalThis.eventValue=new CachedEvent('probe');eventValue.type") == "probe"
    call, = records(page, "window", constructor, "call")
    assert call["outcome"] == "returned" and call["return_summary"] == "object"
    assert call["argument_count"] == 1 and call["argument_summaries"] == ["string:5:probe"]
    field, = records(page, "Event", "type", "get")
    assert field["value_summary"] == "string:5:probe" and field["outcome"] == "returned"
    assert [hit["path"] for hit in page.read_watch_api_hits()] == ["window." + constructor]


def test_event_methods_properties_and_readonly_constants_are_recorded(page):
    page.eval("globalThis.eventValue=new Event('probe',{cancelable:true})")
    page.watch_apis(["Event.preventDefault", "Event.composedPath"])
    page.start_access_trace()
    assert page.eval("eventValue.preventDefault();eventValue.returnValue=false;eventValue.defaultPrevented")
    page.eval("eventValue.composedPath();Event.AT_TARGET;eventValue.CAPTURING_PHASE")
    call, = records(page, "Event", "preventDefault", "call")
    assert call["outcome"] == "returned" and call["return_summary"] == "undefined"
    assert records(page, "Event", "returnValue", "set")
    assert records(page, "Event", "defaultPrevented", "get")
    assert records(page, "Event", "AT_TARGET", "get")
    assert records(page, "Event.prototype", "CAPTURING_PHASE", "get")
    assert [hit["path"] for hit in page.read_watch_api_hits()] == ["Event.preventDefault", "Event.composedPath"]


@pytest.mark.parametrize("tracing", [False, True])
def test_event_constructor_preserves_inherited_setter_exception(page, tracing):
    if tracing:
        page.start_access_trace()
    result = json.loads(page.eval("""JSON.stringify((()=>{
        const sentinel=Error('event field sentinel'),effects=[];
        class ThrowingEvent extends Event {
            set type(value){effects.push('type');throw sentinel;}
            set bubbles(value){effects.push('bubbles');}
        }
        let caught=false;try{new ThrowingEvent('probe');}catch(error){caught=error===sentinel;}
        return {caught,effects};
    })())"""))
    assert result == {"caught": True, "effects": ["type"]}
    if tracing:
        call, = records(page, "window", "Event", "call")
        assert call["outcome"] == "threw" and call["exception_name"] == "Error"
        assert "event field sentinel" in call["exception_message"]


def test_idle_deadline_fields_references_and_cached_calls_are_recorded(page):
    page.eval("requestIdleCallback(value=>{globalThis.deadline=value;globalThis.remaining=value.timeRemaining;})")
    page.run_idle_callbacks()
    page.watch_apis(["IdleDeadline.timeRemaining"])
    page.start_access_trace()
    assert page.eval("deadline.didTimeout===false && deadline.timeRemaining===remaining && remaining.call(deadline)===50")
    timeout, = records(page, "IdleDeadline", "didTimeout", "get")
    reference, = records(page, "IdleDeadline", "timeRemaining", "get")
    call, = records(page, "IdleDeadline", "timeRemaining", "call")
    assert timeout["value_summary"] == "boolean:false" and timeout["outcome"] == "returned"
    assert reference["value_summary"] == "function" and reference["outcome"] == "returned"
    assert call["return_summary"] == "number:50" and call["outcome"] == "returned"
    assert len({event["object_id"] for event in (timeout, reference, call)}) == 1
    assert [hit["path"] for hit in page.read_watch_api_hits()] == ["IdleDeadline.timeRemaining"]
