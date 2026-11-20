"""Secure native related-app inspection with bounded offline task delivery."""

import hashlib
import json
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(
    not getattr(iv8, "_v8_linked", False), reason="requires a V8-linked build")
BASE = "https://bom-related-apps.test/page/"
HTML = "<html><head></head><body></body></html>"
FIXTURES = Path(__file__).parent / "fixtures" / "bom_02"
METHOD = "getInstalledRelatedApps"
PATH = "navigator." + METHOD
SHAPE_HELPERS = """
    const methodShape=method=>{
        let construct;
        try{Reflect.construct(method,[]);construct='constructed';}
        catch(error){construct=error.name;}
        return {name:method.name,length:method.length,
            source:Function.prototype.toString.call(method),
            prototype:Object.hasOwn(method,'prototype'),construct};
    };
    const nativeShape=()=>{
        const descriptor=Object.getOwnPropertyDescriptor(Navigator.prototype,'getInstalledRelatedApps');
        return {present:'getInstalledRelatedApps' in navigator,own:Object.hasOwn(navigator,'getInstalledRelatedApps'),
            descriptor:descriptor?{enumerable:descriptor.enumerable,configurable:descriptor.configurable,
                writable:descriptor.writable,kind:'data',method:methodShape(descriptor.value)}:null};
    };
"""


def evaluate(page, body):
    return json.loads(page.eval("JSON.stringify((()=>{" + body + "})())"))


def events(page):
    return page.read_access_trace(clear=False)["events"]


@pytest.fixture(scope="module")
def browser_reference():
    return json.loads((FIXTURES / "chrome.json").read_text(encoding="utf-8"))["result"]


@pytest.fixture(scope="module")
def insecure_reference():
    return json.loads((FIXTURES / "chrome-insecure.json").read_text(encoding="utf-8"))["result"]


@pytest.fixture(scope="module")
def related_then_reference():
    capture = json.loads((FIXTURES / "chrome-related-then.json").read_text(encoding="utf-8"))
    assert capture["probe_sha256"] == hashlib.sha256((FIXTURES / "related_then_probe.js").read_bytes()).hexdigest()
    return capture["result"]


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, BASE)
        yield value


def install_array_then_probe(page, *, throws=False):
    page.eval("""globalThis.relatedOrder=[];globalThis.relatedThenReads=0;
        globalThis.relatedSentinel={sentinel:'then'};
        globalThis.relatedNavigator=navigator;
        globalThis.relatedOriginalThen=Object.getOwnPropertyDescriptor(Array.prototype,'then');
        globalThis.relatedRestoreThen=()=>{
            if(relatedOriginalThen)Object.defineProperty(Array.prototype,'then',relatedOriginalThen);
            else delete Array.prototype.then;
        };
        Object.defineProperty(Array.prototype,'then',{configurable:true,get(){
            relatedThenReads++;relatedOrder.push('then lookup');relatedNavigator.onLine;
        """ + ("throw relatedSentinel;" if throws else "return undefined;") + """
        }});""", name="related-then-probe.js")


def test_related_method_native_shape_is_installed_before_navigator_access(page, browser_reference):
    assert page.eval("typeof Navigator.prototype.getInstalledRelatedApps==='function'")
    assert evaluate(page, SHAPE_HELPERS + "return nativeShape();") == browser_reference["shape"][METHOD]
    assert page.eval("navigator.getInstalledRelatedApps===Navigator.prototype.getInstalledRelatedApps")


@pytest.mark.parametrize("url,secure", [
    (BASE, True), ("http://localhost/related/", True),
    ("http://127.0.0.1:13452/related/", True), ("file:///related/page.html", True),
    ("http://bom-related-apps.test/related/", False), ("about:blank", False),
])
def test_method_exposure_uses_native_secure_generation_profile(url, secure, browser_reference, insecure_reference):
    reference = browser_reference if secure else insecure_reference
    with iv8.Page() as value:
        value.load(HTML, url)
        assert value.eval("isSecureContext") is secure
        assert evaluate(value, SHAPE_HELPERS + "return nativeShape();") == reference["shape"][METHOD]
        assert value.eval("Object.hasOwn(Navigator.prototype,'getInstalledRelatedApps')") is secure
        # The public readonly flag cannot grant a native API or hide one.
        assert value.eval("""(()=>{
            try{Object.defineProperty(globalThis,'isSecureContext',{value:!isSecureContext});}catch(error){}
            isSecureContext=!isSecureContext;
            return typeof Navigator.prototype.getInstalledRelatedApps==='function';
        })()""") is secure


def test_secure_insecure_reload_and_page_isolation_have_fresh_exposure(page):
    page.eval("globalThis.relatedOld=navigator.getInstalledRelatedApps;")
    old_function = page.eval("relatedOld")
    page.load(HTML, "http://insecure-related.test/")
    assert not old_function.context_alive
    assert page.eval("""!('getInstalledRelatedApps' in navigator)&&
        !Object.hasOwn(Navigator.prototype,'getInstalledRelatedApps')""")
    page.load(HTML, BASE)
    assert page.eval("typeof Navigator.prototype.getInstalledRelatedApps==='function'")
    with iv8.Page() as other:
        other.load(HTML, "http://other-insecure.test/")
        assert other.eval("!('getInstalledRelatedApps' in navigator)")
        assert page.eval("typeof navigator.getInstalledRelatedApps==='function'")


@pytest.mark.parametrize("pump", ["tick", "advance", "idle", "legacy"])
def test_empty_snapshot_promise_is_delivered_by_existing_task_pumps(page, browser_reference, pump):
    assert page.eval("""globalThis.relatedValid={promise:false};
        const pending=navigator.getInstalledRelatedApps();
        relatedValid.promise=pending instanceof Promise;
        pending.then(value=>{relatedValid.value=value;});
        !Object.hasOwn(relatedValid,'value');""")
    page.run_jobs()
    assert page.eval("!Object.hasOwn(relatedValid,'value')")
    if pump == "tick":
        assert page.tick()
    elif pump == "advance":
        assert page.advance(0)
    elif pump == "idle":
        assert page.run_until_idle(max_steps=10)
    else:
        page.run_timers()
        page.run_jobs()
    assert evaluate(page, "return relatedValid;") == browser_reference["related"]["valid"]


def test_each_call_returns_a_fresh_promise_and_mutable_snapshot(page, browser_reference):
    page.eval("""globalThis.relatedLeft=null;globalThis.relatedRight=null;
        globalThis.relatedFirst=navigator.getInstalledRelatedApps();
        globalThis.relatedSecond=navigator.getInstalledRelatedApps();
        relatedFirst.then(value=>{relatedLeft=value;});
        relatedSecond.then(value=>{relatedRight=value;});""")
    assert page.tick()
    assert page.eval("Array.isArray(relatedLeft)&&relatedRight===null")
    assert page.tick()
    actual = evaluate(page, """return {promisesFresh:relatedFirst!==relatedSecond,
        arraysFresh:relatedLeft!==relatedRight,array:Array.isArray(relatedLeft),frozen:Object.isFrozen(relatedLeft)};""")
    assert actual == browser_reference["related"]["identity"]
    assert page.eval("relatedLeft.push('local');relatedLeft.length===1&&relatedRight.length===0")


@pytest.mark.parametrize("kind,receiver", [
    ("object", "{}"), ("undefined", "undefined"), ("null", "null"),
    ("prototype", "Navigator.prototype"), ("proxy", "new Proxy(navigator,{})"),
    ("forged", "Object.create(Navigator.prototype)"),
])
def test_invalid_receiver_returns_rejected_promise_without_argument_inspection(page, browser_reference, kind, receiver):
    page.eval("""globalThis.relatedCall=Navigator.prototype.getInstalledRelatedApps;
        globalThis.relatedResult={};globalThis.relatedArgumentReads=0;
        globalThis.relatedUnused=new Proxy({}, {get(){relatedArgumentReads++;throw Error('unused argument');}});""")
    assert page.eval("""(()=>{
        try{
            const pending=relatedCall.call(""" + receiver + """,relatedUnused);
            relatedResult.promise=pending instanceof Promise;
            pending.then(value=>{relatedResult.value=value;},error=>{
                relatedResult.error={name:error.name,message:error.message};
            });
            return relatedResult.promise;
        }catch(error){relatedResult.synchronous=true;return false;}
    })()""")
    page.run_jobs()
    expected = (browser_reference["related"]["invalidReceiver"] if kind == "object" else
                browser_reference["related"]["invalidReceivers"][kind])
    assert evaluate(page, "return relatedResult;") == expected
    assert page.eval("relatedArgumentReads") == 0
    assert not page.tick()  # Receiver failure creates no host-delivery task.


def test_extra_arguments_and_replaced_global_promise_do_not_change_native_operation(page, browser_reference):
    assert page.eval("""globalThis.relatedResult={};globalThis.relatedArgumentReads=0;
        globalThis.relatedNativePromise=Promise;
        globalThis.relatedUnused=new Proxy({}, {get(){relatedArgumentReads++;throw Error('unused argument');}});
        globalThis.Promise=function(){throw Error('public Promise must not be called');};
        const pending=navigator.getInstalledRelatedApps(relatedUnused,Symbol('ignored'));
        relatedResult.promise=pending instanceof relatedNativePromise;
        pending.then(value=>{relatedResult.value=value;});
        relatedResult.promise;""")
    try:
        assert page.tick()
        assert evaluate(page, "return relatedResult;") == browser_reference["related"]["extraArgument"]
        assert page.eval("relatedArgumentReads") == 0
    finally:
        page.eval("globalThis.Promise=relatedNativePromise")


def test_array_then_lookup_matches_browser_task_timing(page, browser_reference):
    install_array_then_probe(page)
    try:
        page.eval("""const pending=navigator.getInstalledRelatedApps();relatedOrder.push('after call');
            pending.then(()=>relatedOrder.push('resolved'));
            Promise.resolve().then(()=>relatedOrder.push('microtask'));""")
        assert page.eval("relatedThenReads") == 0
        page.run_jobs()
        assert evaluate(page, "return relatedOrder;") == ["after call", "microtask"]
        assert page.tick()
        assert evaluate(page, "return relatedOrder;") == browser_reference["related"]["thenTiming"]
        assert page.eval("relatedThenReads") == 1
    finally:
        page.eval("relatedRestoreThen()")


@pytest.mark.parametrize("depth", [1, 9, 64])
def test_complete_microtask_chain_drains_before_result_then_lookup(page, depth):
    install_array_then_probe(page)
    try:
        page.eval("""const pending=navigator.getInstalledRelatedApps();relatedOrder.push('after call');
            let chain=Promise.resolve();
            for(let index=0;index<""" + str(depth) + """;index++)chain=chain.then(()=>relatedOrder.push('microtask '+index));
            pending.then(()=>relatedOrder.push('resolved'));""")
        page.run_jobs()
        expected = ["after call", *["microtask " + str(index) for index in range(depth)]]
        assert evaluate(page, "return relatedOrder;") == expected
        assert page.eval("relatedThenReads") == 0
        assert page.tick()
        assert evaluate(page, "return relatedOrder;") == [*expected, "then lookup", "resolved"]
        assert page.eval("relatedThenReads") == 1
    finally:
        page.eval("relatedRestoreThen()")


def test_throwing_array_then_rejects_original_value_without_synchronous_throw(page, related_then_reference):
    install_array_then_probe(page, throws=True)
    try:
        assert page.eval("""globalThis.relatedRejected=false;
            globalThis.relatedThenResult={status:'pending',sameSentinel:false,synchronousThrow:false};
            try{
                const pending=navigator.getInstalledRelatedApps();
                relatedThenResult.promise=pending instanceof Promise;
                relatedOrder.push('after call');
                pending.then(()=>{relatedThenResult.status='resolved';},error=>{
                    relatedThenResult.status='rejected';relatedRejected=error===relatedSentinel;
                    relatedThenResult.sameSentinel=relatedRejected;relatedOrder.push('rejected');
                });
            }catch(error){relatedThenResult.synchronousThrow=true;relatedThenResult.status='threw';}
            relatedThenResult.promise;""")
        page.run_jobs()
        assert page.eval("relatedThenReads===0&&!relatedRejected")
        assert page.tick()
        actual = evaluate(page, """return {...relatedThenResult,reads:relatedThenReads,order:relatedOrder};""")
        assert actual == {name: related_then_reference[name] for name in (
            "promise", "status", "sameSentinel", "synchronousThrow", "reads", "order")}
    finally:
        page.eval("relatedRestoreThen()")


def test_public_timer_cancellation_cannot_cancel_private_result_delivery(page):
    page.eval("""globalThis.relatedSettled=false;globalThis.relatedUserTimerFired=false;
        navigator.getInstalledRelatedApps().then(()=>{relatedSettled=true;});
        const userTimer=setTimeout(()=>{relatedUserTimerFired=true;},0);
        for(let candidate=-5;candidate<=userTimer+5;candidate++){
            clearTimeout(candidate);clearInterval(candidate);
        }""")
    assert page.run_until_idle(max_steps=10)
    assert page.eval("relatedSettled&&!relatedUserTimerFired")


def test_pending_delivery_is_released_on_reload_and_disposal(page):
    page.eval("""globalThis.relatedPending=navigator.getInstalledRelatedApps();
        relatedPending.then(()=>{globalThis.relatedOldDelivery=true;});""")
    saved_promise = page.eval("relatedPending")
    page.load(HTML, BASE)
    assert not saved_promise.context_alive
    assert not page.tick()
    assert page.eval("typeof relatedOldDelivery==='undefined'")
    pending = page.eval("navigator.getInstalledRelatedApps()")
    page.dispose()
    page.dispose()
    assert not pending.context_alive


def test_cached_valid_and_invalid_calls_trace_promise_return_with_watch_hits(page):
    page.eval("globalThis.relatedNavigator=navigator;globalThis.relatedCall=Navigator.prototype.getInstalledRelatedApps;")
    page.watch_apis([PATH])
    page.start_access_trace()
    page.eval("relatedCall.call(relatedNavigator);relatedCall.call({}).catch(()=>{});", name="related-calls.js")
    records = events(page)
    assert [(record["label"], record["member"], record["operation"], record.get("outcome"))
            for record in records] == [
        ("navigator", METHOD, "call", "returned"),
        ("Navigator.prototype", METHOD, "call", "returned"),
    ]
    assert all(record["return_summary"] == "object" and record["argument_count"] == 0
               and "exception_name" not in record for record in records)
    assert [hit["path"] for hit in page.read_watch_api_hits()] == [PATH, PATH]
    assert page.run_until_idle(max_steps=10)
    assert events(page) == records  # Neither native delivery nor rejection invents entries.


def test_method_reference_and_callback_reentry_have_exact_events_without_extra_reads(page):
    page.eval("globalThis.relatedNavigator=navigator;globalThis.relatedCall=Navigator.prototype.getInstalledRelatedApps;")
    install_array_then_probe(page)
    page.start_access_trace()
    try:
        page.eval("relatedCall.call(relatedNavigator).then(()=>relatedNavigator.userAgent);")
        assert [(record["member"], record["operation"]) for record in events(page)] == [(METHOD, "call")]
        assert page.tick()
        records = events(page)
        assert [(record["member"], record["operation"]) for record in records] == [
            (METHOD, "call"), ("onLine", "get"), ("userAgent", "get"),
        ]
        assert all(record["outcome"] == "returned" for record in records)
        page.stop_access_trace()
        assert page.eval("relatedThenReads") == 1
    finally:
        page.eval("relatedRestoreThen()")


def test_capacity_filter_and_stop_do_not_disable_independent_watch_channel(page):
    page.eval("globalThis.relatedNavigator=navigator;globalThis.relatedCall=Navigator.prototype.getInstalledRelatedApps;")
    page.watch_apis([PATH])
    page.start_access_trace(max_events=1, object_types=["navigator"])
    page.eval("relatedCall.call(relatedNavigator);relatedCall.call(relatedNavigator);")
    report = page.read_access_trace(clear=False)
    assert len(report["events"]) == 1 and report["events"][0]["return_summary"] == "object"
    assert report["dropped_events"] == 1
    assert len(page.read_watch_api_hits()) == 2
    page.stop_access_trace()
    assert page.run_until_idle(max_steps=10)
    page.eval("relatedCall.call(relatedNavigator);")
    assert page.read_access_trace(clear=False)["events"] == report["events"]
    assert len(page.read_watch_api_hits()) == 1


def test_native_method_remains_writable_and_deletable_without_reinstallation(page):
    page.eval("globalThis.relatedNavigator=navigator;globalThis.relatedCall=Navigator.prototype.getInstalledRelatedApps;")
    assert page.eval("""(()=>{
        const replacement=()=>17;
        Navigator.prototype.getInstalledRelatedApps=replacement;
        if(navigator.getInstalledRelatedApps!==replacement||navigator.getInstalledRelatedApps()!==17)return false;
        return delete Navigator.prototype.getInstalledRelatedApps&&!('getInstalledRelatedApps' in navigator);
    })()""")
    assert page.eval("relatedCall.call(relatedNavigator) instanceof Promise")
    assert page.tick()
    assert page.eval("!('getInstalledRelatedApps' in navigator)")


def test_debug_outputs_completed_call_and_prod_stays_quiet(capsys):
    with iv8.Page() as value:
        value.load(HTML, BASE, mode="debug")
        assert capsys.readouterr().out == ""
        value.eval("navigator.getInstalledRelatedApps()")
        output = capsys.readouterr().out
        assert output.count("CALL navigator.getInstalledRelatedApps() -> <Object>") == 1
        assert value.tick()
        assert capsys.readouterr().out == ""
        value.load(HTML, BASE, mode="prod")
        value.eval("navigator.getInstalledRelatedApps()")
        assert value.tick()
        assert capsys.readouterr().out == ""
        assert value.read_access_trace()["events"] == []
