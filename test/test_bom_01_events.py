"""BOM-01 native event interfaces against the offline Chrome reference."""

import json
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")
BASE = "https://bom-01-events.test/"
HTML = "<html><head></head><body></body></html>"
REFERENCE = json.loads((Path(__file__).parent / "fixtures/bom_01/chrome.json").read_text())["result"]
TARGETS = [
    ("NetworkInformation", "navigator.connection", "connection", "change"),
    ("ScreenOrientation", "screen.orientation", "orientation", "change"),
    ("VisualViewport", "visualViewport", "visual_viewport", "scrollend"),
]


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, BASE)
        yield value


def evaluate(page, body):
    return json.loads(page.eval("JSON.stringify((() => {" + body + "})())"))


def events(page):
    return page.read_access_trace(clear=False)["events"]


def settled(page, expression):
    page.eval("""globalThis.bom_outcome = null;
        (() => {
            const errorRecord = error => ({name:error.name, message:error.message});
            try {
                const value = (""" + expression + """);
                const promise = value instanceof Promise;
                Promise.resolve(value).then(
                    result => {bom_outcome = {promise, value:result === undefined ? 'undefined' : result};},
                    error => {bom_outcome = {promise, error:errorRecord(error)};});
            } catch (error) {
                bom_outcome = {promise:false, synchronous:true, error:errorRecord(error)};
            }
        })();""")
    page.run_jobs()
    return evaluate(page, "return bom_outcome;")


@pytest.mark.parametrize("interface,expression,object_type,kind", TARGETS)
def test_shared_interface_shape_and_eager_members_match_browser(page, interface, expression, object_type, kind):
    result = evaluate(page, """
        const Constructor = globalThis[""" + json.dumps(interface) + """];
        const prototype = Constructor.prototype;
        const keysBefore = Reflect.ownKeys(prototype).map(String);
        const target = """ + expression + """;
        const attempt = callback => {
            try { callback(); return 'constructed'; } catch (error) { return error.name; }
        };
        const functionShape = method => ({name:method.name, length:method.length,
            source:Function.prototype.toString.call(method), prototype:Object.hasOwn(method,'prototype'),
            construct:attempt(() => Reflect.construct(method, []))});
        const descriptor = key => {
            const entry = Object.getOwnPropertyDescriptor(prototype,key);
            return {enumerable:entry.enumerable, configurable:entry.configurable,
                ...(Object.hasOwn(entry,'value') ? {kind:'data', writable:entry.writable,
                    ...(typeof entry.value === 'function' ? {method:functionShape(entry.value)} : {value:entry.value})} :
                    {kind:'accessor', get:entry.get ? functionShape(entry.get) : null,
                        set:entry.set ? functionShape(entry.set) : null})};
        };
        return {keysBefore, shape:{constructor:functionShape(Constructor),
            parent:Object.getPrototypeOf(prototype) === EventTarget.prototype ? 'EventTarget' : 'Object',
            samePrototype:Object.getPrototypeOf(target) === prototype,
            tag:Object.prototype.toString.call(target), own:Reflect.ownKeys(target).map(String),
            members:Object.fromEntries(Reflect.ownKeys(prototype).map(key => [String(key),descriptor(key)])),
            inheritedEventMethods:['addEventListener','removeEventListener','dispatchEvent'].map(
                key => target[key] === EventTarget.prototype[key])}};
    """)
    assert result["keysBefore"] == list(REFERENCE["shape"][interface]["members"])
    assert result["shape"] == REFERENCE["shape"][interface]


@pytest.mark.parametrize("interface,expression,object_type,kind", TARGETS)
def test_cached_prototype_constructor_keeps_native_get_monitoring(page, interface, expression, object_type, kind):
    page.eval("globalThis.bom_prototype=" + interface + ".prototype;globalThis.bom_constructor=" + interface)
    page.start_access_trace(max_events=1, object_types=["host_object"])
    assert page.eval("bom_prototype.constructor === bom_constructor", name="bom-constructor.js")
    record, = events(page)
    assert (record["label"], record["member"], record["operation"]) == (interface + ".prototype", "constructor", "get")
    assert record["outcome"] == "returned" and record["value_summary"] == "function"


@pytest.mark.parametrize("interface,expression,object_type,kind", TARGETS)
def test_synthetic_event_behavior_matches_browser(page, interface, expression, object_type, kind):
    result = evaluate(page, """
        const target = """ + expression + """;
        const kind = """ + json.dumps(kind) + """;
        const attempt = callback => {
            try { return {value:callback()}; }
            catch (error) { return {error:{name:error.name,message:error.message}}; }
        };
        const seen = [];
        const listener = function(event) {
            seen.push(['listener',this===target,event.target===target,event.currentTarget===target,event.eventPhase]);
        };
        target.addEventListener(kind,listener,{once:true});
        target['on'+kind] = function(event) {
            seen.push(['handler',this===target,event.target===target,event.currentTarget===target,event.eventPhase]);
            event.preventDefault();
        };
        const event = new Event(kind,{cancelable:true});
        const returned = target.dispatchEvent(event);
        const returnedSecond = target.dispatchEvent(new Event(kind));
        target['on'+kind] = 17;
        return {seen,returned,returnedSecond,handlerAfterPrimitive:target['on'+kind],
            finalState:[event.currentTarget===null,event.eventPhase,event.composedPath().length],
            invalidDispatch:attempt(() => target.dispatchEvent({type:kind})),
            invalidReceiver:attempt(() => target.dispatchEvent.call({},event))};
    """)
    assert result == REFERENCE["events"][interface]


@pytest.mark.parametrize("expression,handler,kind", [
    ("navigator.connection", "onchange", "change"),
    ("screen.orientation", "onchange", "change"),
    ("visualViewport", "onresize", "resize"),
    ("visualViewport", "onscroll", "scroll"),
    ("visualViewport", "onscrollend", "scrollend"),
])
def test_selected_handlers_execute_only_on_dispatch_and_clear_nonfunctions(page, expression, handler, kind):
    page.eval("""globalThis.bom_target=""" + expression + """;globalThis.bom_calls=0;
        bom_target[""" + json.dumps(handler) + """] = function(event) {
            if (this===bom_target && event.currentTarget===bom_target) bom_calls++;
        };""")
    page.run_jobs()
    assert page.eval("bom_calls") == 0
    assert page.eval("bom_target.dispatchEvent(new Event(" + json.dumps(kind) + "))")
    assert page.eval("bom_calls") == 1
    assert evaluate(page, """
        return [null,17,{},undefined].every(value => {
            bom_target[""" + json.dumps(handler) + """] = value;
            return bom_target[""" + json.dumps(handler) + """] === null;
        });
    """)
    page.eval("bom_target.dispatchEvent(new Event(" + json.dumps(kind) + "))")
    assert page.eval("bom_calls") == 1


@pytest.mark.parametrize("interface,expression,object_type,kind", TARGETS)
def test_listener_deduplication_removal_once_and_passive(page, interface, expression, object_type, kind):
    assert evaluate(page, """
        const target = """ + expression + """;
        const seen = [];
        const once = () => seen.push('once');
        const removed = () => seen.push('removed');
        target.addEventListener('probe',once,{once:true});
        target.addEventListener('probe',once,{once:true});
        target.addEventListener('probe',removed);
        target.removeEventListener('probe',removed);
        target.addEventListener('probe',event => {seen.push('passive');event.preventDefault();},{passive:true});
        const first = target.dispatchEvent(new Event('probe',{cancelable:true}));
        const second = target.dispatchEvent(new Event('probe',{cancelable:true}));
        return {seen,first,second};
    """) == {"seen": ["once", "passive", "passive"], "first": True, "second": True}


@pytest.mark.parametrize("interface,expression,object_type,kind", TARGETS)
@pytest.mark.parametrize("argument", [
    "({type:'change'})", "Object.create(Event.prototype)", "Object.create(new Event('change'))",
    "new Proxy(new Event('change'),{get:trap,getPrototypeOf:trap,getOwnPropertyDescriptor:trap,has:trap})",
    "null", "undefined",
])
def test_dispatch_requires_own_native_event_brand_without_proxy_traps(page, interface, expression, object_type, kind, argument):
    result = evaluate(page, """
        const target = """ + expression + """;
        let effects = 0;
        const trap = () => {effects++;throw Error('unexpected diagnostic trap');};
        let error;
        try { target.dispatchEvent(""" + argument + """); } catch (caught) { error = caught.name; }
        return {error,effects};
    """)
    assert result == {"error": "TypeError", "effects": 0}


@pytest.mark.parametrize("interface,expression,object_type,kind", TARGETS)
def test_cached_dispatch_failure_has_one_completed_trace_and_watch_hit(page, interface, expression, object_type, kind):
    page.eval("globalThis.bom_target=" + expression + ";globalThis.bom_dispatch=bom_target.dispatchEvent")
    page.watch_apis([expression + ".dispatchEvent"])
    page.start_access_trace(object_types=[object_type])
    assert page.eval("""(() => {
        try {bom_dispatch.call(bom_target,{});} catch(error) {return error.name==='TypeError';}
        return false;
    })()""", name="bom-dispatch.js")
    record, = events(page)
    assert (record["object_type"], record["member"], record["operation"]) == (object_type, "dispatchEvent", "call")
    assert record["outcome"] == "threw" and record["exception_name"] == "TypeError"
    assert record["argument_count"] == 1 and record["resource_name"] == "bom-dispatch.js"
    assert [hit["path"] for hit in page.read_watch_api_hits()] == [expression + ".dispatchEvent"]


@pytest.mark.parametrize("fixture_key,expression", [
    *[(kind, "screen.orientation.lock(" + json.dumps(kind) + ")") for kind in
      ("portrait", "landscape", "any", "natural", "portrait-primary", "bad")],
    ("null", "screen.orientation.lock(null)"),
    ("undefined", "screen.orientation.lock(undefined)"),
    ("noArgument", "screen.orientation.lock()"),
    ("wrongReceiver", "screen.orientation.lock.call({},'portrait')"),
    *[("portrait", "screen.orientation.lock(" + json.dumps(kind) + ")") for kind in
      ("portrait-secondary", "landscape-primary", "landscape-secondary")],
])
def test_orientation_lock_promise_and_error_channels_match_browser(page, fixture_key, expression):
    assert settled(page, expression) == REFERENCE["orientation"][fixture_key]


def test_unlock_keeps_configured_orientation_and_rejects_foreign_receivers_synchronously(page):
    assert evaluate(page, """
        const target = screen.orientation;
        const before = [target.type,target.angle];
        const result = target.unlock({toString(){throw Error('unused');}});
        let error;
        try {target.unlock.call({});} catch(caught) {error={name:caught.name,message:caught.message};}
        return {unchanged:before[0]===target.type&&before[1]===target.angle,
            returned:String(result),error};
    """) == {"unchanged": True, "returned": "undefined", "error": REFERENCE["orientation"]["unlockWrongReceiver"]["error"]}


@pytest.mark.parametrize("expression,label,object_type", [
    ("bom_lock.call(bom_orientation,'portrait')", "screen.orientation", "orientation"),
    ("bom_lock.call(bom_orientation,'bad')", "screen.orientation", "orientation"),
    ("bom_lock.call({},'portrait')", "ScreenOrientation.prototype", "host_object"),
])
def test_lock_trace_captures_promise_return_without_settlement(page, expression, label, object_type):
    page.eval("globalThis.bom_orientation=screen.orientation;globalThis.bom_lock=bom_orientation.lock")
    page.watch_apis(["screen.orientation.lock"])
    page.start_access_trace()
    assert page.eval("(() => {const result=" + expression + ";result.catch(()=>{});return result instanceof Promise;})()", name="bom-lock.js")
    record, = events(page)
    assert (record["object_type"], record["label"], record["member"], record["operation"]) == (object_type, label, "lock", "call")
    assert record["outcome"] == "returned" and record["return_summary"] == "object"
    assert "exception_name" not in record and record["argument_count"] == 1
    assert record["resource_name"] == "bom-lock.js"
    snapshot = page.read_access_trace(clear=False)
    page.run_jobs()
    assert page.read_access_trace(clear=False) == snapshot
    assert [hit["path"] for hit in page.read_watch_api_hits()] == ["screen.orientation.lock"]


def test_lock_conversion_preserves_thrown_identity_and_user_api_reentry(page):
    page.eval("""globalThis.bom_orientation=screen.orientation;globalThis.bom_lock=bom_orientation.lock;
        globalThis.bom_sentinel={message:'conversion'};globalThis.bom_reads=0;globalThis.bom_same=false;
        globalThis.bom_argument={toString(){bom_reads++;navigator.userAgent;throw bom_sentinel;}};""")
    page.start_access_trace(object_types=["orientation", "navigator"])
    assert page.eval("""(() => {
        const result=bom_lock.call(bom_orientation,bom_argument);
        result.catch(error=>{bom_same=error===bom_sentinel;});
        return result instanceof Promise;
    })()""", name="bom-conversion.js")
    page.run_jobs()
    assert page.eval("bom_same && bom_reads===1")
    records = events(page)
    assert [(record["object_type"], record["member"], record["operation"]) for record in records] == [
        ("orientation", "lock", "call"), ("navigator", "userAgent", "get")]
    assert records[0]["outcome"] == "returned" and records[0]["return_summary"] == "object"


def test_lock_symbol_conversion_uses_rejected_promise(page):
    result = settled(page, "screen.orientation.lock(Symbol('orientation'))")
    assert result["promise"] and result["error"]["name"] == "TypeError"
    assert "synchronous" not in result


@pytest.mark.parametrize("interface,expression,object_type,kind", TARGETS)
def test_handlers_and_nested_objects_are_stable_isolated_and_released_on_reload(page, interface, expression, object_type, kind):
    page.eval("globalThis.bom_target=" + expression + ";bom_target.on" + kind + "=()=>{}")
    assert page.eval("bom_target === " + expression + " && typeof bom_target.on" + kind + " === 'function'")
    with iv8.Page() as other:
        other.load(HTML, BASE)
        assert other.eval(expression + ".on" + kind + " === null")
    page.load(HTML, BASE)
    assert page.eval(expression + ".on" + kind + " === null")
    page.eval(expression + ".addEventListener('cleanup',()=>{});" + expression + ".on" + kind + "=()=>{}")
    # The fixture's close also releases listener/handler handles without reload.


@pytest.mark.parametrize("interface,expression,object_type,kind", TARGETS)
def test_handler_get_set_and_dispatch_callback_remain_monitored(page, interface, expression, object_type, kind):
    page.eval("globalThis.bom_target=" + expression + ";globalThis.bom_event=new Event(" + json.dumps(kind) + ");"
              "globalThis.bom_handler=()=>{navigator.userAgent;}")
    page.watch_apis([expression + ".dispatchEvent"])
    page.start_access_trace(object_types=[object_type, "navigator"])
    assert page.eval("bom_target.on" + kind + "=bom_handler;"
                     "bom_target.on" + kind + "===bom_handler && bom_target.dispatchEvent(bom_event)", name="bom-handler.js")
    records = events(page)
    assert [(record["object_type"], record["member"], record["operation"]) for record in records] == [
        (object_type, "on" + kind, "set"), (object_type, "on" + kind, "get"),
        (object_type, "dispatchEvent", "call"), ("navigator", "userAgent", "get")]
    assert all(record["outcome"] == "returned" for record in records)
    assert records[2]["return_summary"] == "boolean:true"
    assert [hit["path"] for hit in page.read_watch_api_hits()] == [expression + ".dispatchEvent"]


@pytest.mark.parametrize("interface,expression,object_type,kind", TARGETS)
def test_debug_prints_selected_operations_and_prod_is_silent(page, capsys, interface, expression, object_type, kind):
    page.load(HTML, BASE, mode="debug")
    capsys.readouterr()
    script = expression + ".on" + kind + "=()=>{};" + expression + ".dispatchEvent(new Event(" + json.dumps(kind) + "))"
    page.eval(script)
    output = capsys.readouterr().out
    assert "dispatchEvent" in output and "on" + kind in output
    page.load(HTML, BASE, mode="prod")
    page.eval(script)
    assert capsys.readouterr().out == ""
