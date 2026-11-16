"""Native value-object monitoring preserves instance state and coercion paths."""

import json

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")
BASE = "https://value-objects.test/"
HTML = "<html><head></head><body></body></html>"
CASES = [
    ("URL", "new URL('https://value-objects.test/a?q=1')", "URL", "toJSON()",
     "string:32:https://value-objects.test/a?q=1", ("href", "https://value-objects.test/a?q=1")),
    ("URLSearchParams", "new URLSearchParams('x=one')", "URLSearchParams", "get('x')",
     "string:3:one", None),
    ("TextEncoder", "new TextEncoder()", "TextEncoder", "encode('abc')", "object", None),
    ("TextDecoder", "new TextDecoder('utf-8',{fatal:true})", "TextDecoder", "decode()",
     "string:0:", ("fatal", True)),
    ("Blob", "new Blob(['abc'])", "Blob", "text()", "object", ("size", 3)),
    ("File", "new File(['abc'],'sample.txt')", "Blob", "arrayBuffer()", "object", ("name", "sample.txt")),
    ("FileReader", "new FileReader()", "FileReader", "readAsText(sourceBlob)", "undefined", ("readyState", 0)),
]


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, BASE)
        yield value


def events(page, **matches):
    return [event for event in page.read_access_trace(clear=False)["events"]
            if all(event.get(key) == value for key, value in matches.items())]


@pytest.mark.parametrize("constructor,expression,label,method,summary,field", CASES,
                         ids=[case[0] for case in CASES])
def test_value_constructor_fields_and_calls_complete(page, constructor, expression, label, method, summary, field):
    page.eval("globalThis.sourceBlob=new Blob(['abc'])")
    member = method.partition("(")[0]
    page.watch_apis(["window." + constructor, label + "." + member])
    page.start_access_trace()
    page.eval("globalThis.valueObject=" + expression, name="value-construct.js")
    record, = events(page, object_type="window", member=constructor, operation="call")
    assert record["outcome"] == "returned" and record["return_summary"] == "object"
    assert record["resource_name"] == "value-construct.js"
    if field is not None:
        name, expected = field
        assert page.eval("valueObject[" + json.dumps(name) + "]", name="value-get.js") == expected
        read, = events(page, object_type="host_object", label=constructor, member=name, operation="get")
        assert read["outcome"] == "returned" and read["resource_name"] == "value-get.js"
    page.eval("valueObject." + method, name="value-call.js")
    call, = events(page, object_type="host_object", label=label, member=member, operation="call")
    assert call["outcome"] == "returned" and call["return_summary"] == summary
    assert call["resource_name"] == "value-call.js" and call["source_kind"] == "eval"
    assert [hit["path"] for hit in page.read_watch_api_hits()] == [
        "window." + constructor, label + "." + member,
    ]
    if constructor == "FileReader":
        assert page.eval("valueObject.readyState") == 1
        page.run_jobs()
        assert page.eval("valueObject.result") == "abc"


@pytest.mark.parametrize("label,expression,field,method", [
    ("URL", "new URL('https://value-objects.test/a')", "href", "toJSON()"),
    ("TextDecoder", "new TextDecoder()", "fatal", "decode()"),
    ("Blob", "new Blob(['abc'])", "size", "arrayBuffer()"),
])
def test_native_instance_identity_matches_fields_and_calls_and_resets(page, label, expression, field, method):
    def construct_pair():
        page.eval("globalThis.firstValue=" + expression + ";globalThis.secondValue=" + expression)

    def exercise_pair():
        page.eval("firstValue." + field + ";firstValue." + method +
                  ";secondValue." + field + ";secondValue." + method, name="value-identities.js")
        calls = events(page, object_type="host_object", label=label,
                       member=method.partition("(")[0], operation="call")
        assert len(calls) == 2
        assert calls[0]["object_id"] != calls[1]["object_id"]
        reads = events(page, object_type="host_object", label=label, member=field, operation="get")
        assert {event["object_id"] for event in reads} == {event["object_id"] for event in calls}
        assert all(event["outcome"] == "returned" for event in calls + reads)
        return calls

    construct_pair()
    page.start_access_trace(object_types=["host_object"])
    before = exercise_pair()
    page.read_access_trace(clear=True)
    page.load(HTML, BASE)
    construct_pair()
    after = exercise_pair()
    assert {event["object_id"] for event in before}.isdisjoint(event["object_id"] for event in after)
    assert after[0]["generation"] == before[0]["generation"] + 1


@pytest.mark.parametrize("tracing", [False, True])
def test_file_reader_inherited_setters_and_thrown_identity_are_preserved(page, tracing):
    if tracing:
        page.start_access_trace()
    result = json.loads(page.eval("""JSON.stringify((()=>{
        const hits=[];
        class Reader extends FileReader {
            set readyState(value){hits.push(['readyState',value]);}
            set result(value){
                hits.push(['result',value]);
                Object.defineProperty(this,'result',{value:'from setter',writable:false,
                    enumerable:false,configurable:true});
            }
            set error(value){hits.push(['error',value]);}
        }
        const reader=new Reader(),descriptor=Object.getOwnPropertyDescriptor(reader,'result');
        const sentinel=Error('reader setter sentinel');let caught=false,throws=0;
        class ThrowingReader extends FileReader {set readyState(value){throws++;throw sentinel;}}
        try{new ThrowingReader();}catch(error){caught=error===sentinel;}
        return {hits,caught,throws,ownReady:Object.hasOwn(reader,'readyState'),
            ownError:Object.hasOwn(reader,'error'),result:reader.result,
            descriptor:{data:'value'in descriptor,writable:descriptor.writable,
                enumerable:descriptor.enumerable,configurable:descriptor.configurable}};
    })())""", name="reader-subclass.js"))
    assert result == {
        "hits": [["readyState", 0], ["result", None], ["error", None]],
        "caught": True, "throws": 1, "ownReady": False, "ownError": False,
        "result": "from setter", "descriptor": {"data": True, "writable": False,
                                                  "enumerable": False, "configurable": True},
    }
    if tracing:
        constructors = events(page, object_type="window", member="FileReader", operation="call")
        assert [event["outcome"] for event in constructors] == ["returned", "threw"]
        assert constructors[1]["exception_name"] == "Error"
        assert "reader setter sentinel" in constructors[1]["exception_message"]
        assert not events(page, object_type="host_object", label="FileReader", member="readyState")
        assert not events(page, object_type="host_object", label="FileReader", member="error")


def test_native_constructor_and_method_errors_keep_single_coercion_and_exception(page):
    page.eval("globalThis.encoder=new TextEncoder();globalThis.coercions=0")
    page.watch_apis(["window.URL", "TextEncoder.encode"])
    page.start_access_trace()
    assert page.eval("""(()=>{
        const sentinel=Error('value coercion sentinel');
        const value={toString(){coercions++;throw sentinel;}};
        let constructorCaught=false,methodCaught=false;
        try{new URL(value);}catch(error){constructorCaught=error===sentinel;}
        try{encoder.encode(value);}catch(error){methodCaught=error===sentinel;}
        return constructorCaught && methodCaught && coercions===2;
    })()""", name="value-error.js")
    constructor, = events(page, object_type="window", member="URL", operation="call")
    method, = events(page, object_type="host_object", label="TextEncoder", member="encode", operation="call")
    for record in (constructor, method):
        assert record["outcome"] == "threw" and record["exception_name"] == "Error"
        assert record["argument_summaries"] == ["object"]
        assert "value coercion sentinel" in record["exception_message"]
        assert "return_summary" not in record
    assert [hit["path"] for hit in page.read_watch_api_hits()] == ["window.URL", "TextEncoder.encode"]


@pytest.mark.parametrize("tracing", [False, True])
@pytest.mark.parametrize("mode", ["begin", "abort", "delivery"])
def test_file_reader_field_failure_stops_later_callbacks(page, tracing, mode):
    page.eval("""globalThis.reader=new FileReader();globalThis.payload=new Blob(['abc']);
        globalThis.readerSentinel=Error('file reader field sentinel');
        globalThis.readerEffects=[];globalThis.readerCaught=false;
        for(const type of ['abort','load','loadend'])
            reader.addEventListener(type,()=>readerEffects.push(type));""")
    if mode != "begin":
        page.eval("reader.readAsText(payload)")
    page.eval("""Object.defineProperty(reader,'readyState',{configurable:true,
        set(value){readerEffects.push('readyState');throw readerSentinel;}});
        Object.defineProperty(reader,'result',{configurable:true,
            set(value){readerEffects.push('result');}});""")
    page.capture_async_errors()
    if tracing:
        page.start_access_trace()
    if mode == "delivery":
        page.run_jobs()
        error, = page.read_async_errors()["errors"]
        assert error["kind"] == "microtask" and error["name"] == "Error"
        assert "file reader field sentinel" in error["message"]
        assert error["stack"] == page.eval("readerSentinel.stack")
    else:
        operation = "reader.readAsText(payload)" if mode == "begin" else "reader.abort()"
        assert page.eval("""(()=>{try{""" + operation + """;}catch(error){
            readerCaught=error===readerSentinel;}return readerCaught;})()""")
        page.run_jobs()
        assert page.read_async_errors()["errors"] == []
    assert json.loads(page.eval("JSON.stringify(readerEffects)")) == ["readyState"]
