"""Native legacy quota aliases and DOMError, pinned to offline Chrome fixtures."""

import hashlib
import json
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")
FIXTURES = Path(__file__).parent / "fixtures" / "bom_03"
REFERENCES = {
    mode: json.loads((FIXTURES / filename).read_text(encoding="utf-8"))
    for mode, filename in [("secure", "chrome.json"), ("insecure", "chrome-insecure.json"),
                           ("opaque", "chrome-opaque.json")]
}
URLS = {"secure": "https://quota.test/", "insecure": "http://quota.test/", "opaque": "about:blank"}
QUOTA = 10 * 1024**3
SHAPE_HELPERS = """
    const functionShape=fn=>{
        let construct;try{Reflect.construct(fn,[]);construct='constructed';}catch(error){construct=error.name;}
        return {name:fn.name,length:fn.length,source:Function.prototype.toString.call(fn),
            prototype:Object.hasOwn(fn,'prototype'),construct};
    };
    const descriptor=(object,key)=>{
        const item=Object.getOwnPropertyDescriptor(object,key);if(!item)return null;
        return {enumerable:item.enumerable,configurable:item.configurable,
            ...('value'in item?{kind:'data',writable:item.writable,
                value:typeof item.value==='function'?functionShape(item.value):item.value}:
                {kind:'accessor',get:item.get?functionShape(item.get):null,set:item.set?functionShape(item.set):null})};
    };
"""


def evaluate(page, body):
    return json.loads(page.eval("JSON.stringify((()=>{" + body + "})())"))


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load("<html><body></body></html>", URLS["secure"])
        yield value


@pytest.mark.parametrize("mode", list(URLS))
def test_reference_hash_and_native_alias_shape(mode):
    reference = REFERENCES[mode]
    assert hashlib.sha256((FIXTURES / "probe.js").read_bytes()).hexdigest() == reference["probe_sha256"]
    assert reference["execution"]["profile_path"] == "<temporary-profile>"
    with iv8.Page() as value:
        value.load("<html></html>", URLS[mode])
        for name in ["webkitTemporaryStorage", "webkitPersistentStorage"]:
            actual = evaluate(value, SHAPE_HELPERS + "const name=" + json.dumps(name) + """;
                return {present:name in navigator,own:Object.hasOwn(navigator,name),
                    descriptor:descriptor(Navigator.prototype,name)};
            """)
            assert actual == reference["result"]["shape"][name]
        actual = evaluate(value, SHAPE_HELPERS + """
            const quota=navigator.webkitTemporaryStorage,proto=Object.getPrototypeOf(quota);
            return {sameAliases:quota===navigator.webkitPersistentStorage,stable:quota===navigator.webkitTemporaryStorage,
                tag:Object.prototype.toString.call(quota),ownKeys:Reflect.ownKeys(quota).map(String),
                prototypeKeys:Reflect.ownKeys(proto).map(String),prototypeParentIsObject:Object.getPrototypeOf(proto)===Object.prototype,
                globalType:typeof DeprecatedStorageQuota,constructorIsObject:quota.constructor===Object,
                methods:Object.fromEntries(['queryUsageAndQuota','requestQuota'].map(name=>[name,descriptor(proto,name)])),
                tagDescriptor:descriptor(proto,Symbol.toStringTag)};
        """)
        assert actual == reference["result"]["shape"]["quota"]


@pytest.mark.parametrize("mode", list(URLS))
@pytest.mark.parametrize("case", list(REFERENCES["secure"]["result"]["sources"]))
def test_quota_completion_conversion_and_callback_order_match_browser(mode, case):
    reference = REFERENCES[mode]["result"]
    item = reference["sources"][case]
    with iv8.Page() as value:
        value.load("<html></html>", URLS[mode])
        value.eval("""globalThis.quotaObserved=(()=>{'use strict';
            const effects=[],sentinel={sentinel:""" + json.dumps(case) + """};
            const record={effects},quota=navigator.webkitTemporaryStorage;
            const success=function(...values){effects.push({kind:'success',thisUndefined:this===undefined,values});};
            const failure=function(error){effects.push({kind:'failure',thisUndefined:this===undefined,
                error:{name:error.name,message:error.message},tag:Object.prototype.toString.call(error),
                instanceOfError:error instanceof Error,domError:error instanceof DOMError,ownKeys:Reflect.ownKeys(error).map(String)});};
            try{const args=eval(""" + json.dumps(item["source"]) + ");const returned=Reflect.apply(quota[" +
                   json.dumps(item["method"]) + """],quota,args);record.returned=returned===undefined?'undefined':typeof returned;}
            catch(error){record.error={name:error.name,message:error.message,same:error===sentinel};}
            effects.push('after call');Promise.resolve().then(()=>effects.push('microtask'));return record;
        })();""", name="quota-case.js")
        assert value.run_until_idle(max_steps=20)
        actual = json.loads(value.eval("JSON.stringify(quotaObserved)"))
        expected = dict(reference["calls"][case])
        # The bounded browser timer proves no callback for optional error=null;
        # the offline task queue is fully drained instead of sleeping three seconds.
        expected.pop("timeout", None)
        assert actual == expected


def test_dom_error_shape_constructor_and_subclass_prototype_match_browser(page):
    expected = dict(REFERENCES["secure"]["result"]["shape"]["domError"])
    expected.pop("reflectConstruct")
    actual = evaluate(page, SHAPE_HELPERS + """
        const instance=new DOMError('CustomError','custom message');
        return {globalType:typeof DOMError,constructor:functionShape(DOMError),
            ownKeys:Reflect.ownKeys(instance).map(String),prototypeKeys:Reflect.ownKeys(DOMError.prototype).map(String),
            prototypeParentIsObject:Object.getPrototypeOf(DOMError.prototype)===Object.prototype,
            constructorParentIsFunction:Object.getPrototypeOf(DOMError)===Function.prototype,
            instanceOfError:instance instanceof Error,tag:Object.prototype.toString.call(instance),name:instance.name,message:instance.message,
            members:Object.fromEntries(['name','message','constructor'].map(name=>[name,descriptor(DOMError.prototype,name)])),
            tagDescriptor:descriptor(DOMError.prototype,Symbol.toStringTag)};
    """)
    assert actual == expected
    actual = evaluate(page, """
        let prototypeReads=0;const childPrototype=Object.create(DOMError.prototype);
        const target=new Proxy(function(){},{get(target,key,receiver){
            if(key==='prototype'){prototypeReads++;return childPrototype;}return Reflect.get(target,key,receiver);}});
        const constructed=Reflect.construct(DOMError,['CustomError'],target);
        return {prototypeReads,prototypeMatches:Object.getPrototypeOf(constructed)===childPrototype,
            name:constructed.name,message:constructed.message};
    """)
    assert actual == REFERENCES["secure"]["result"]["shape"]["domError"]["reflectConstruct"]


@pytest.mark.parametrize("name,expression", [
    ("missing", "new DOMError()"), ("undefined", "new DOMError(undefined)"),
    ("null", "new DOMError(null,null)"), ("omitted_message", "new DOMError('Name')"),
    ("undefined_message", "new DOMError('Name',undefined)"), ("known_name", "new DOMError('AbortError')"),
    ("call", "DOMError('Name')"), ("symbol", "new DOMError(Symbol('name'))"),
    ("symbol_message", "new DOMError('Name',Symbol('message'))"),
])
def test_dom_error_constructor_results_and_errors_match_browser(page, name, expression):
    actual = evaluate(page, "try{const value=" + expression + ";return {name:value.name,message:value.message};}"
                      "catch(error){return {error:{name:error.name,message:error.message}};}")
    assert actual == REFERENCES["secure"]["result"]["errors"][name]


def test_dom_error_preserves_domstring_units_and_user_conversion_failures(page):
    assert page.eval(r"""(()=>{
        const error=new DOMError('a\u0000b\ud800','\udfff\u0000');
        if(error.name.length!==4||error.name.charCodeAt(3)!==0xd800||error.message.length!==2)return false;
        const sentinel=new TypeError('original');let calls=0,kept=false;
        try{new DOMError({toString(){calls++;throw sentinel;}},{toString(){throw Error('unused');}});}
        catch(error){kept=error===sentinel;}
        const effects=[];const custom=new DOMError({[Symbol.toPrimitive](hint){effects.push(hint);return 'N';}},
            {toString(){effects.push('message');return 'M';}});
        return kept&&calls===1&&effects.join(',')==='string,message'&&custom.name==='N'&&custom.message==='M';
    })()""")


@pytest.mark.parametrize("method", ["queryUsageAndQuota", "requestQuota"])
def test_invalid_receiver_precedes_parameter_coercion(page, method):
    actual = evaluate(page, "const quota=navigator.webkitTemporaryStorage;try{quota[" + json.dumps(method) +
                      "].call({});return {returned:true};}catch(error){return {error:{name:error.name,message:error.message}};}")
    assert actual == REFERENCES["secure"]["result"]["errors"][method + "Receiver"]
    assert page.eval("""(()=>{let reads=0;
        const value={valueOf(){reads++;throw Error('must not convert');}};
        try{navigator.webkitTemporaryStorage.requestQuota.call({},value);}catch(error){return error.name==='TypeError'&&reads===0;}
    })()""")


def test_alias_identity_survives_parent_overrides_and_readonly_assignment(page):
    assert page.eval("""(()=>{const quota=navigator.webkitTemporaryStorage;
        navigator.webkitTemporaryStorage={};navigator.webkitPersistentStorage={};
        if(navigator.webkitTemporaryStorage!==quota||navigator.webkitPersistentStorage!==quota)return false;
        Object.defineProperty(navigator,'webkitTemporaryStorage',{value:'user',configurable:true});
        return navigator.webkitTemporaryStorage==='user'&&navigator.webkitPersistentStorage===quota&&
            delete navigator.webkitTemporaryStorage&&navigator.webkitTemporaryStorage===quota;
    })()""")


def test_quota_profile_matches_storage_manager_without_calling_user_replacement(page):
    page.eval("""globalThis.quotaValues=null;globalThis.estimateValue=null;
        navigator.storage.estimate().then(value=>estimateValue=value);
        const quota=navigator.webkitTemporaryStorage;
        quota.queryUsageAndQuota((usage,total)=>quotaValues=[usage,total]);
        navigator.storage.estimate=()=>{throw Error('user replacement must not be called');};""")
    assert page.run_until_idle()
    assert evaluate(page, "return [quotaValues,estimateValue.usage,estimateValue.quota];") == [[0, QUOTA], 0, QUOTA]


@pytest.mark.parametrize("expression,expected", [("-0.5", 0), ("-18446744073709551616", 0),
                                                 ("18446744073709555712", 4096)])
def test_unsigned_conversion_edges_and_positive_zero(page, expression, expected):
    page.eval("globalThis.granted=null;navigator.webkitTemporaryStorage.requestQuota(" + expression + ",value=>granted=value);")
    assert page.tick()
    assert page.eval("granted") == expected
    assert page.eval("!Object.is(granted,-0)")


def test_private_delivery_is_not_user_cancelable_and_reloads_release_it(page):
    page.eval("""globalThis.granted=null;
        navigator.webkitTemporaryStorage.requestQuota(7,value=>granted=value);
        for(let id=0;id<20;id++){clearTimeout(id);clearInterval(id);}""")
    page.run_jobs()
    assert page.eval("granted") is None
    assert page.tick() and page.eval("granted") == 7
    old = page.eval("navigator.webkitTemporaryStorage")
    page.eval("navigator.webkitTemporaryStorage.requestQuota(9,()=>{throw Error('old task');})")
    page.load("<html></html>", URLS["opaque"])
    assert not page.tick()
    with pytest.raises(iv8.JSContextDisposedError):
        old.to_py()
    page.dispose()
    page.dispose()


def test_callback_exception_is_captured_and_does_not_abort_other_delivery(page):
    page.capture_async_errors()
    page.eval("""globalThis.progress=0;
        navigator.webkitTemporaryStorage.requestQuota(1,()=>{throw new RangeError('quota callback');});
        navigator.webkitPersistentStorage.requestQuota(2,value=>progress=value);
    """, name="quota-callback.js")
    assert page.run_until_idle()
    assert page.eval("progress") == 2
    error, = page.read_async_errors()["errors"]
    assert (error["kind"], error["name"]) == ("storage_quota", "RangeError")
    assert "quota-callback.js" in error["stack"]


def test_trace_watch_aliases_and_callback_reentry_without_internal_noise(page):
    page.eval("""globalThis.quota=navigator.webkitTemporaryStorage;
        globalThis.quotaMethod=quota.requestQuota;globalThis.cachedNavigator=navigator;""")
    paths = ["navigator.webkitTemporaryStorage.requestQuota", "navigator.webkitPersistentStorage.requestQuota"]
    page.watch_apis(paths)
    page.start_access_trace()
    page.eval("quotaMethod.call(quota,17,()=>cachedNavigator.userAgent)", name="quota-user.js")
    record, = page.read_access_trace(clear=False)["events"]
    assert (record["label"], record["member"], record["operation"], record["outcome"], record["return_summary"]) == (
        "DeprecatedStorageQuota", "requestQuota", "call", "returned", "undefined")
    assert [hit["path"] for hit in page.read_watch_api_hits()] == paths
    assert page.tick()
    records = page.read_access_trace()["events"]
    assert [(record["member"], record["operation"]) for record in records] == [("requestQuota", "call"), ("userAgent", "get")]


@pytest.mark.parametrize("method", ["queryUsageAndQuota", "requestQuota"])
def test_rejected_receiver_keeps_legacy_watch_and_canonical_failed_trace(page, method):
    page.eval("globalThis.quotaMethod=navigator.webkitPersistentStorage[" + json.dumps(method) + "];")
    paths = [prefix + "." + method for prefix in ["DeprecatedStorageQuota", "navigator.webkitTemporaryStorage", "navigator.webkitPersistentStorage"]]
    page.watch_apis(paths)
    page.start_access_trace()
    assert page.eval("(()=>{try{quotaMethod.call({},1)}catch(error){return error.name}})()") == "TypeError"
    assert [hit["path"] for hit in page.read_watch_api_hits()] == paths
    record, = page.read_access_trace()["events"]
    assert (record["member"], record["operation"], record["outcome"], record["exception_name"]) == (
        method, "call", "threw", "TypeError")


def test_opaque_error_constructor_override_and_field_monitoring(page):
    page.load("<html></html>", URLS["opaque"])
    page.eval("""globalThis.NativeDOMError=DOMError;globalThis.quota=navigator.webkitTemporaryStorage;
        globalThis.observed=null;globalThis.DOMError=function(){throw Error('user constructor');};""")
    page.start_access_trace()
    page.eval("quota.requestQuota(1,null,error=>{observed=[error instanceof NativeDOMError,error.name,error.message]})")
    assert evaluate(page, "return observed;") == [True, "AbortError", "The user aborted a request."]
    records = page.read_access_trace()["events"]
    assert [(record["label"], record["member"], record["operation"]) for record in records] == [
        ("DeprecatedStorageQuota", "requestQuota", "call"), ("DOMError", "name", "get"), ("DOMError", "message", "get")]


def test_debug_and_prod_modes(page, capsys):
    page.load("<html></html>", URLS["secure"], mode="debug")
    assert capsys.readouterr().out == ""
    page.eval("navigator.webkitTemporaryStorage.requestQuota(1,()=>{})")
    assert "CALL DeprecatedStorageQuota.requestQuota(" in capsys.readouterr().out
    assert page.tick()
    assert capsys.readouterr().out == ""
    page.load("<html></html>", URLS["secure"], mode="prod")
    page.eval("navigator.webkitPersistentStorage.requestQuota(1,()=>{})")
    assert page.tick()
    assert capsys.readouterr().out == ""
    assert page.read_access_trace()["events"] == []
