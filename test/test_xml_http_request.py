"""XHR-01 browser-derived shape with explicitly inert native request methods."""

import json
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")
BASE = "https://xml-http-request.test/page/"
HTML = "<html><head></head><body></body></html>"
HANDLERS = ["onloadstart", "onprogress", "onabort", "onerror", "onload", "ontimeout", "onloadend"]
METHODS = [
    ("abort", "", "undefined"),
    ("getAllResponseHeaders", "", ""),
    ("getResponseHeader", "'X-Probe'", None),
    ("open", "'POST','https://never-request.invalid/data',false", "undefined"),
    ("overrideMimeType", "'application/json'", "undefined"),
    ("send", "'payload'", "undefined"),
    ("setRequestHeader", "'X-Probe','value'", "undefined"),
    ("setAttributionReporting", "{}", "undefined"),
    ("setPrivateToken", "{}", "undefined"),
]
SHAPE_HELPERS = r"""
    const functionShape=value=>{
        let constructable=true;
        try{Reflect.construct(function(){},[],value);}catch{constructable=false;}
        return {name:value.name,length:value.length,source:Function.prototype.toString.call(value),
            ownPrototype:Object.hasOwn(value,'prototype'),constructable,ownKeys:Reflect.ownKeys(value).map(String)};
    };
    const primitive=value=>value===undefined?'<undefined>':Array.isArray(value)?value.map(primitive):
        value!==null && typeof value==='object'?{tag:Object.prototype.toString.call(value)}:
        typeof value==='symbol'?String(value):value;
    const descriptorShape=descriptor=>{
        const result={enumerable:descriptor.enumerable,configurable:descriptor.configurable};
        if('value'in descriptor)Object.assign(result,{kind:'data',writable:descriptor.writable,
            value:typeof descriptor.value==='function'?functionShape(descriptor.value):primitive(descriptor.value)});
        else Object.assign(result,{kind:'accessor',get:descriptor.get?functionShape(descriptor.get):null,
            set:descriptor.set?functionShape(descriptor.set):null});
        return result;
    };
    const ownSurface=value=>Object.fromEntries(Reflect.ownKeys(value).map(key=>
        [String(key),descriptorShape(Object.getOwnPropertyDescriptor(value,key))]));
    const outcome=operation=>{
        try{return {returned:primitive(operation())};}
        catch(error){return {name:error.name,message:error.message,code:error.code};}
    };
"""


@pytest.fixture(scope="module")
def chrome_reference():
    root = Path(__file__).parent / "fixtures" / "xml_http_request"
    return {"primary": json.loads((root / "chrome.json").read_text()),
            "additional": json.loads((root / "chrome_additional.json").read_text())}


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, BASE)
        yield value


@pytest.fixture
def xhr_page(page):
    page.eval("globalThis.xhr=new XMLHttpRequest();globalThis.upload=xhr.upload")
    return page


def evaluate(page, expression):
    return json.loads(page.eval("JSON.stringify(" + expression + ")"))


@pytest.mark.parametrize("name", ["XMLHttpRequest", "XMLHttpRequestEventTarget", "XMLHttpRequestUpload"])
def test_constructor_and_eager_prototype_surface_match_chrome(page, chrome_reference, name):
    expected = chrome_reference["primary"]["result"]["constructors"][name]
    expression = """(() => {""" + SHAPE_HELPERS + """
        const constructor=globalThis[""" + json.dumps(name) + """];
        return {function:functionShape(constructor),
            globalDescriptor:descriptorShape(Object.getOwnPropertyDescriptor(globalThis,""" + json.dumps(name) + """)),
            ownSurface:ownSurface(constructor),prototypeSurface:ownSurface(constructor.prototype),
            constructorParent:Object.getPrototypeOf(constructor).name,
            prototypeKeys:Reflect.ownKeys(constructor.prototype).map(String)};
    })()"""
    projection = {key: expected[key] for key in (
        "function", "globalDescriptor", "ownSurface", "prototypeSurface", "constructorParent")}
    projection["prototypeKeys"] = list(expected["prototypeSurface"])
    assert evaluate(page, expression) == projection
    page.eval("new XMLHttpRequest()")
    assert evaluate(page, expression) == projection
    assert evaluate(page, "(() => {" + SHAPE_HELPERS + "return outcome(()=>globalThis[" +
                    json.dumps(name) + "]());})()") == expected["callWithoutNew"]
    assert evaluate(page, "(() => {" + SHAPE_HELPERS + "return outcome(()=>Object.prototype.toString.call(new globalThis[" +
                    json.dumps(name) + "]()));})()") == expected["construct"]


def test_xhr_upload_inheritance_identity_and_subclass_brands(xhr_page, chrome_reference):
    page = xhr_page
    actual = evaluate(page, """(() => {
        const chain=value=>{const rows=[];for(let current=value;current;current=Object.getPrototypeOf(current)){
            const descriptor=Object.getOwnPropertyDescriptor(current,'constructor');
            rows.push({tag:Object.prototype.toString.call(current),ownConstructor:
                descriptor && typeof descriptor.value==='function'?descriptor.value.name:null});
        }return rows;};
        class DerivedRequest extends XMLHttpRequest{}
        const derived=new DerivedRequest(),second=new XMLHttpRequest();
        return {xhr:chain(xhr),upload:chain(upload),uploadSameObject:xhr.upload===upload,
            uploadSeparate:second.upload!==upload,xhrOwn:Reflect.ownKeys(xhr).map(String),
            uploadOwn:Reflect.ownKeys(upload).map(String),
            derived:[derived instanceof DerivedRequest,derived instanceof XMLHttpRequest,derived.upload===derived.upload],
            shared:xhr.addEventListener===EventTarget.prototype.addEventListener &&
                upload.dispatchEvent===EventTarget.prototype.dispatchEvent,
            instances:[upload instanceof XMLHttpRequestUpload,upload instanceof XMLHttpRequestEventTarget,
                upload instanceof EventTarget,xhr instanceof XMLHttpRequestEventTarget,xhr instanceof EventTarget]};
    })()""")
    reference = chrome_reference["primary"]["result"]
    for key in ("xhr", "upload"):
        assert actual[key] == [{"tag": row["tag"], "ownConstructor": row["ownConstructor"]}
                               for row in reference["prototypeChains"][key]]
    assert actual["uploadSameObject"] and actual["uploadSeparate"] and actual["shared"]
    assert actual["xhrOwn"] == actual["uploadOwn"] == []
    assert actual["derived"] == reference["localErrors"]["subclass"]["returned"]
    assert actual["instances"] == [True] * 5
    for name in ("XMLHttpRequest", "XMLHttpRequestEventTarget", "XMLHttpRequestUpload"):
        assert not hasattr(iv8, name)


def test_fresh_fields_and_handler_defaults_match_chrome(xhr_page, chrome_reference):
    expected = chrome_reference["primary"]["result"]["defaults"]
    actual = evaluate(xhr_page, "Object.fromEntries(" + json.dumps(list(expected)) + ".map(field=>[field,{returned:"
                      "field==='upload'?Object.prototype.toString.call(xhr[field]):xhr[field]}]))")
    assert actual == expected
    assert xhr_page.eval(json.dumps(HANDLERS) + ".every(name=>upload[name]===null)")


def test_readonly_fields_and_constants_preserve_browser_flags_and_assignment_rules(xhr_page):
    assert xhr_page.eval("""(() => {
        for(const name of ['readyState','upload','responseURL','status','statusText','response','responseText','responseXML']){
            const original=xhr[name];xhr[name]=42;let rejected=false;
            try{(function(){'use strict';xhr[name]=42;})()}catch(error){rejected=error instanceof TypeError;}
            if(!rejected || xhr[name]!==original)return false;
        }
        for(const [name,value] of [['UNSENT',0],['OPENED',1],['HEADERS_RECEIVED',2],['LOADING',3],['DONE',4]]){
            for(const owner of [XMLHttpRequest,XMLHttpRequest.prototype]){
                const descriptor=Object.getOwnPropertyDescriptor(owner,name);
                if(descriptor.value!==value || descriptor.writable || !descriptor.enumerable || descriptor.configurable)return false;
                owner[name]=99;if(owner[name]!==value || Reflect.deleteProperty(owner,name))return false;
            }
            if(xhr[name]!==value)return false;
        }
        return true;
    })()""")


@pytest.mark.parametrize("method,arguments,expected", METHODS, ids=[row[0] for row in METHODS])
def test_native_request_methods_are_inert_placeholders(xhr_page, method, arguments, expected):
    result = xhr_page.eval("xhr[" + json.dumps(method) + "](" + arguments + ")")
    if expected == "undefined":
        assert result is iv8.JSUndefined
    else:
        assert result == expected
    assert evaluate(xhr_page, "[xhr.readyState,xhr.status,xhr.statusText,xhr.responseURL,xhr.response,xhr.responseText,xhr.responseXML]") == [
        0, 0, "", "", "", "", None]


def test_required_arity_errors_match_chrome_without_backend_state_checks(xhr_page, chrome_reference):
    actual = evaluate(xhr_page, """(() => {""" + SHAPE_HELPERS + """
        const samples=[['open',[]],['open',['GET']],['setRequestHeader',[]],['setRequestHeader',['X-Probe']],
            ['getResponseHeader',[]],['overrideMimeType',[]],['setAttributionReporting',[]],['setPrivateToken',[]]];
        return Object.fromEntries(samples.map(([name,args])=>[name+':'+args.length,
            outcome(()=>XMLHttpRequest.prototype[name].apply(xhr,args))]));
    })()""")
    assert actual == chrome_reference["additional"]["result"]["arity"]


@pytest.mark.parametrize("response_type", ["", "text", "json", "document", "arraybuffer", "blob"])
def test_response_type_storage_and_unreceived_response_guards_match_chrome(
        xhr_page, chrome_reference, response_type):
    actual = evaluate(xhr_page, """(() => {""" + SHAPE_HELPERS + """
        xhr.responseType='text';const result={set:outcome(()=>{xhr.responseType=""" + json.dumps(response_type) + """;
            return xhr.responseType;})};
        for(const name of ['response','responseText','responseXML'])result[name]=outcome(()=>xhr[name]);
        return result;
    })()""")
    assert actual == chrome_reference["additional"]["result"]["responseTypes"][response_type]
    assert xhr_page.eval("xhr.readyState") == 0


def test_invalid_response_type_values_keep_the_previous_enum_value(xhr_page, chrome_reference):
    for label, value in (("madeup", "'madeup'"), ("null", "null"), ("undefined", "undefined")):
        actual = evaluate(xhr_page, """(() => {""" + SHAPE_HELPERS + """
            xhr.responseType='text';const result={set:outcome(()=>{xhr.responseType=""" + value + """;return xhr.responseType;})};
            for(const name of ['response','responseText','responseXML'])result[name]=outcome(()=>xhr[name]);return result;
        })()""")
        assert actual == chrome_reference["additional"]["result"]["responseTypes"][label]


def test_timeout_uint32_conversions_and_single_object_coercion(xhr_page, chrome_reference):
    actual = evaluate(xhr_page, """(() => {""" + SHAPE_HELPERS + """
        const samples=[['negative',-1],['wrap',4294967296],['fraction',1.9],['nan',NaN],['infinity',Infinity],
            ['undefined',undefined],['null',null],['string','12'],['bigint',1n]];
        const results=Object.fromEntries(samples.map(([name,value])=>[name,outcome(()=>{xhr.timeout=value;return xhr.timeout;})]));
        let coercions=0;results.object=outcome(()=>{xhr.timeout={valueOf(){coercions++;return 3.9;}};return xhr.timeout;});
        results.object.coercions=coercions;return results;
    })()""")
    expected = chrome_reference["additional"]["result"]["timeout"]
    for key, value in expected.items():
        if "returned" in value:
            assert actual[key] == value
        else:
            assert actual[key]["name"] == value["name"]
            assert "Cannot convert a BigInt value to a number" in actual[key]["message"]


def test_with_credentials_boolean_conversion_does_not_coerce_objects(xhr_page):
    assert xhr_page.eval("""(() => {
        let reads=0;const object=new Proxy({},{get(){reads++;throw Error('must not coerce');}});
        for(const value of [false,0,'',null,undefined]){xhr.withCredentials=value;if(xhr.withCredentials!==false)return false;}
        for(const value of [true,1,'yes',object]){xhr.withCredentials=value;if(xhr.withCredentials!==true)return false;}
        return reads===0;
    })()""")


@pytest.mark.parametrize("field,converter", [("timeout", "valueOf"), ("responseType", "toString")])
def test_configuration_coercion_throws_the_original_value_once(xhr_page, field, converter):
    assert xhr_page.eval("""(() => {
        const sentinel=Error('setter sentinel');let conversions=0,caught=false;
        const value={""" + converter + """(){conversions++;throw sentinel;}};
        try{xhr[""" + json.dumps(field) + """]=value;}catch(error){caught=error===sentinel;}
        return caught && conversions===1;
    })()""")
    assert xhr_page.eval("xhr[" + json.dumps(field) + "]") == (0 if field == "timeout" else "")


@pytest.mark.parametrize("target", ["xhr", "upload"])
def test_handler_storage_retains_object_identity_but_only_calls_functions(xhr_page, chrome_reference, target):
    actual = evaluate(xhr_page, """(() => {
        const target=""" + target + """,results={};
        for(const [name,value] of [['object',{}],['array',[]],['number',42],['string','callback'],['null',null],['undefined',undefined]]){
            target.onload=value;results[name]={same:target.onload===value,isNull:target.onload===null,type:typeof target.onload};
        }
        return results;
    })()""")
    assert actual == chrome_reference["additional"]["result"]["handlerValues"]
    assert xhr_page.eval("""(() => {
        const target=""" + target + """;let handled=0,listened=0,objectCalls=0;
        const listener=()=>listened++;target.addEventListener('progress',listener);
        target.onprogress={handleEvent(){objectCalls++;}};
        target.dispatchEvent(new Event('progress'));
        target.onprogress=event=>{handled++;return false;};
        const event=new Event('progress',{cancelable:true}),returned=target.dispatchEvent(event);
        target.removeEventListener('progress',listener);target.onprogress=42;
        target.dispatchEvent(new Event('progress'));
        return objectCalls===0 && handled===1 && listened===2 && !returned && event.defaultPrevented && target.onprogress===null;
    })()""")


def test_wrong_receiver_checks_precede_stub_arity_and_argument_side_effects(xhr_page):
    assert xhr_page.eval("""(() => {
        let conversions=0,traps=0;const sentinel=Error('must not run');
        const argument={toString(){conversions++;throw sentinel;},valueOf(){conversions++;throw sentinel;}};
        const proxy=new Proxy(xhr,{get(){traps++;throw sentinel;},getPrototypeOf(){traps++;throw sentinel;}});
        for(const receiver of [{},XMLHttpRequest.prototype,Object.create(XMLHttpRequest.prototype),proxy,null]){
            for(const name of ['open','send','abort','getResponseHeader','getAllResponseHeaders','overrideMimeType',
                    'setRequestHeader','setAttributionReporting','setPrivateToken']){
                let rejected=false;try{XMLHttpRequest.prototype[name].call(receiver,argument,argument);}
                catch(error){rejected=error instanceof TypeError && error.message==='Illegal invocation';}
                if(!rejected)return false;
            }
        }
        return conversions===0 && traps===0;
    })()""")


def test_stub_parameters_are_not_parsed_converted_or_observed(xhr_page):
    assert xhr_page.eval("""(() => {
        let effects=0;const trap=()=>{effects++;throw Error('placeholder inspected arguments');};
        const value=new Proxy({},{get:trap,getPrototypeOf:trap,getOwnPropertyDescriptor:trap,ownKeys:trap});
        for(const name of ['open','send','abort','overrideMimeType','setRequestHeader',
                'setAttributionReporting','setPrivateToken'])if(xhr[name](value,value)!==undefined)return false;
        return xhr.getResponseHeader(value)===null && xhr.getAllResponseHeaders(value)==='' && effects===0 && xhr.readyState===0;
    })()""")


def test_method_writable_delete_redefine_and_cached_native_identity(xhr_page):
    assert xhr_page.eval("""(() => {
        const prototype=XMLHttpRequest.prototype,original=prototype.send,other=new XMLHttpRequest();
        xhr.send=()=>17;if(xhr.send()!==17 || other.send!==original)return false;
        delete xhr.send;if(xhr.send!==original)return false;
        const deleted=delete prototype.send,missing=xhr.send===undefined;
        const cached=original.call(xhr,'data')===undefined;
        Object.defineProperty(prototype,'send',{value:original,writable:true,enumerable:true,configurable:true});
        return deleted && missing && cached && xhr.send===original && xhr.send('data')===undefined;
    })()""")


def test_no_method_changes_state_produces_response_or_schedules_transport_events(xhr_page):
    page = xhr_page
    page.eval("""globalThis.xhr_events=[];
        for(const target of [xhr,upload])for(const name of ['readystatechange','loadstart','progress','abort','error','load','timeout','loadend'])
            target.addEventListener(name,()=>xhr_events.push(name));
        xhr.open('POST','https://never-request.invalid/data');xhr.setRequestHeader('X-Probe','value');
        xhr.send('payload');xhr.abort();xhr.overrideMimeType('text/plain');
        xhr.setAttributionReporting({});xhr.setPrivateToken({});""")
    page.run_jobs()
    page.run_timers()
    page.run_animation_frame()
    page.run_idle_callbacks()
    assert page.tick() is False and page.advance(1000) and page.run_until_idle()
    assert evaluate(page, "[xhr_events,xhr.readyState,xhr.status,xhr.statusText,xhr.responseURL,xhr.response,xhr.responseText,xhr.responseXML]") == [
        [], 0, 0, "", "", "", "", None]


def test_insecure_profile_does_not_expose_secure_only_extra_methods(page, chrome_reference):
    page.load(HTML, "http://xhr-fixture.invalid/")
    reference = chrome_reference["primary"]["additional_profiles"]["insecure"]["result"]["constructors"]["XMLHttpRequest"]
    assert evaluate(page, "Reflect.ownKeys(XMLHttpRequest.prototype).map(String)") == list(reference["prototypeSurface"])
    assert page.eval("const request=new XMLHttpRequest();request.setAttributionReporting===undefined && request.setPrivateToken===undefined")


def test_reload_dispose_and_pages_keep_configuration_handlers_and_upload_isolated(xhr_page):
    page = xhr_page
    xhr_handle, upload_handle = page.eval("xhr"), page.eval("upload")
    page.eval("xhr.timeout=17;xhr.withCredentials=true;xhr.onload=()=>{};upload.onprogress={};")
    with iv8.Page() as other:
        other.load(HTML, BASE)
        assert evaluate(other, "(()=>{const request=new XMLHttpRequest();return [request.timeout,request.withCredentials,request.onload,request.upload.onprogress]})()") == [0, False, None, None]
        page.load(HTML, BASE)
        assert not xhr_handle.context_alive and not upload_handle.context_alive
        assert evaluate(page, "(()=>{const request=new XMLHttpRequest();return [request.timeout,request.withCredentials,request.onload,request.upload.onprogress]})()") == [0, False, None, None]
    page.dispose()
    page.dispose()
    with pytest.raises(iv8.JSContextDisposedError):
        page.eval("new XMLHttpRequest()")
