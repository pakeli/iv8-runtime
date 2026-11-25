"""Native BOM-06 Navigator device, WebXR, WebGPU, and Presentation objects pinned to Chrome."""

import hashlib
import json
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(
    not getattr(iv8, "_v8_linked", False), reason="requires a V8-linked build"
)

FIXTURES = Path(__file__).parent / "fixtures" / "bom_06"
HTML = "<html><head></head><body></body></html>"
SECURE_BASE = "http://127.0.0.1:53000/reference"
INSECURE_BASE = "http://bom-fixture.invalid:53001/reference"
MEMBERS = ("bluetooth", "hid", "serial", "usb", "xr", "gpu", "presentation")
DECLARED_INTERFACES = (
    "Bluetooth",
    "HID",
    "Serial",
    "USB",
    "XRSystem",
    "GPU",
    "WGSLLanguageFeatures",
    "Presentation",
    "PresentationRequest",
    "PresentationReceiver",
)


def evaluate(page, body):
    return json.loads(page.eval("JSON.stringify((() => { 'use strict'; " + body + " })())"))


@pytest.fixture(scope="module")
def secure_reference():
    capture = json.loads((FIXTURES / "chrome.json").read_text(encoding="utf-8"))
    assert capture["probe_sha256"] == hashlib.sha256(
        (FIXTURES / "probe.js").read_bytes()
    ).hexdigest()
    return capture["result"]


@pytest.fixture(scope="module")
def insecure_reference():
    capture = json.loads((FIXTURES / "chrome-insecure.json").read_text(encoding="utf-8"))
    assert capture["probe_sha256"] == hashlib.sha256(
        (FIXTURES / "probe.js").read_bytes()
    ).hexdigest()
    return capture["result"]


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, SECURE_BASE)
        yield value


JS_HELPERS = """
  const summarizeError = error => (error && typeof error === 'object')
    ? {name: error.name, message: error.message, code: 'code' in error ? error.code : undefined,
       tag: Object.prototype.toString.call(error), domException: typeof DOMException === 'function' && error instanceof DOMException}
    : {thrown: typeof error, value: String(error)};
  const functionShape = fn => {
    let construct;
    try { Reflect.construct(fn, []); construct = 'constructed'; } catch (error) { construct = error.name; }
    return {name: fn.name, length: fn.length, source: Function.prototype.toString.call(fn),
      prototype: Object.hasOwn(fn, 'prototype'), construct};
  };
  const descriptor = (object, key) => {
    const item = Object.getOwnPropertyDescriptor(object, key); if (!item) return null;
    return {enumerable: item.enumerable, configurable: item.configurable,
      ...('value' in item ? {kind: 'data', writable: item.writable,
        value: typeof item.value === 'function' ? functionShape(item.value) : (typeof item.value === 'object' && item.value !== null ? '[object]' : item.value)}
        : {kind: 'accessor', get: item.get ? functionShape(item.get) : null, set: item.set ? functionShape(item.set) : null})};
  };
  const plain = value => {
    if (value === undefined) return {type: 'undefined'};
    if (value === null || typeof value !== 'object') return {type: typeof value, value};
    if (Array.isArray(value)) return {type: 'array', length: value.length};
    return {type: 'object', tag: Object.prototype.toString.call(value)};
  };
"""


def test_bom06_shapes_globals_and_interfaces_match_chrome(
    secure_reference, insecure_reference
):
    for url, expected_secure, reference in (
        (SECURE_BASE, True, secure_reference),
        (INSECURE_BASE, False, insecure_reference),
        ("about:blank", False, insecure_reference),
    ):
        with iv8.Page() as current:
            current.load(HTML, url)
            assert current.eval("isSecureContext") is expected_secure
            actual = evaluate(
                current,
                JS_HELPERS
                + f"""
                const globals = {{}}, interfaces = {{}}, shape = {{}};
                for (const g of {json.dumps(list(DECLARED_INTERFACES))}) {{
                  globals[g] = {{present: g in globalThis, descriptor: descriptor(globalThis, g)}};
                  if (typeof globalThis[g] === 'function' && globalThis[g].prototype) {{
                    const proto = globalThis[g].prototype;
                    const parent = Object.getPrototypeOf(proto);
                    interfaces[g] = {{
                      constructor: functionShape(globalThis[g]),
                      constructorKeys: Reflect.ownKeys(globalThis[g]).map(String),
                      prototypeKeys: Reflect.ownKeys(proto).map(String),
                      parent: parent === Object.prototype ? 'Object.prototype' : (parent === EventTarget.prototype ? 'EventTarget.prototype' : (parent && parent.constructor ? parent.constructor.name : String(parent))),
                      members: Object.fromEntries(Reflect.ownKeys(proto).map(k => [String(k), descriptor(proto, k)])),
                    }};
                  }}
                }}
                for (const name of {json.dumps(list(MEMBERS))}) {{
                  const entry = {{present: name in navigator, own: Object.hasOwn(navigator, name), descriptor: descriptor(Navigator.prototype, name)}};
                  if (entry.present) {{
                    const value = navigator[name];
                    entry.stable = value === navigator[name];
                    entry.tag = Object.prototype.toString.call(value);
                    entry.ownKeys = Reflect.ownKeys(value).map(String);
                    entry.protoName = Object.getPrototypeOf(value).constructor.name;
                    try {{ Object.getOwnPropertyDescriptor(Navigator.prototype, name).get.call({{}}); entry.badReceiver = 'ok'; }}
                    catch (e) {{ entry.badReceiver = summarizeError(e); }}
                  }}
                  shape[name] = entry;
                }}
                return {{globals, interfaces, shape}};
                """,
            )
            assert actual["shape"] == reference["shape"], url
            for g in DECLARED_INTERFACES:
                assert actual["globals"][g] == reference["globals"][g], (url, g)
                if expected_secure:
                    assert actual["interfaces"][g] == reference["interfaces"][g], g


CALL_MATRIX = [
    ("bt_avail", "navigator.bluetooth", "getAvailability", "[]"),
    ("bt_req_empty", "navigator.bluetooth", "requestDevice", "[]"),
    ("bt_req_dict", "navigator.bluetooth", "requestDevice", "[{}]"),
    ("bt_req_all", "navigator.bluetooth", "requestDevice", "[{acceptAllDevices: true}]"),
    ("bt_req_bad", "navigator.bluetooth", "requestDevice", "[17]"),
    ("hid_get", "navigator.hid", "getDevices", "[]"),
    ("hid_req_missing", "navigator.hid", "requestDevice", "[]"),
    ("hid_req_empty", "navigator.hid", "requestDevice", "[{}]"),
    ("hid_req_filters", "navigator.hid", "requestDevice", "[{filters: []}]"),
    ("ser_get", "navigator.serial", "getPorts", "[]"),
    ("ser_req_empty", "navigator.serial", "requestPort", "[]"),
    ("ser_req_dict", "navigator.serial", "requestPort", "[{}]"),
    ("ser_req_bad", "navigator.serial", "requestPort", "[17]"),
    ("usb_get", "navigator.usb", "getDevices", "[]"),
    ("usb_req_missing", "navigator.usb", "requestDevice", "[]"),
    ("usb_req_empty", "navigator.usb", "requestDevice", "[{}]"),
    ("usb_req_filters", "navigator.usb", "requestDevice", "[{filters: []}]"),
    ("xr_sup_missing", "navigator.xr", "isSessionSupported", "[]"),
    ("xr_sup_inline", "navigator.xr", "isSessionSupported", "['inline']"),
    ("xr_sup_vr", "navigator.xr", "isSessionSupported", "['immersive-vr']"),
    ("xr_sup_ar", "navigator.xr", "isSessionSupported", "['immersive-ar']"),
    ("xr_sup_bad", "navigator.xr", "isSessionSupported", "['bogus']"),
    ("xr_req_missing", "navigator.xr", "requestSession", "[]"),
    ("xr_req_vr", "navigator.xr", "requestSession", "['immersive-vr']"),
    ("xr_req_bad", "navigator.xr", "requestSession", "['bogus']"),
    ("gpu_req_empty", "navigator.gpu", "requestAdapter", "[]"),
    ("gpu_req_dict", "navigator.gpu", "requestAdapter", "[{}]"),
    ("gpu_req_power", "navigator.gpu", "requestAdapter", "[{powerPreference: 'high-performance'}]"),
    ("gpu_req_bad_power", "navigator.gpu", "requestAdapter", "[{powerPreference: 'bogus'}]"),
    ("gpu_req_bad", "navigator.gpu", "requestAdapter", "[17]"),
]


def test_all_captured_bom06_call_cases_match_chrome(page, secure_reference):
    for case_name, target_expr, method, args_expr in CALL_MATRIX:
        page.eval(
            "(() => { 'use strict';"
            + JS_HELPERS
            + f"""
            globalThis.bom06Record = (function() {{
              const effects = [], sentinel = {{sentinel: {json.dumps(case_name)}}};
              const record = {{effects}};
              setTimeout(() => effects.push('task'), 0);
              try {{
                const target = eval({json.dumps(target_expr)});
                const args = eval({json.dumps(args_expr)});
                const result = Reflect.apply(target[{json.dumps(method)}], target, args);
                record.returned = plain(result);
                if (result instanceof Promise) {{
                  result.then(
                    v => {{ effects.push({{kind: 'resolved', value: plain(v)}}); }},
                    e => {{ effects.push({{kind: 'rejected', error: summarizeError(e), same: e === sentinel}}); }}
                  );
                }}
              }} catch (e) {{
                record.error = {{...summarizeError(e), same: e === sentinel}};
              }}
              effects.push('after call');
              Promise.resolve().then(() => effects.push('microtask'));
              return record;
            }})();
            }})();"""
        )
        page.run_until_idle(max_steps=20)
        actual = json.loads(page.eval("JSON.stringify(bom06Record)"))
        assert actual == secure_reference["calls"][case_name], case_name


def test_gpu_wgsl_features_and_presentation_request_match_chrome(page, secure_reference):
    details = evaluate(
        page,
        """
        const out = {};
        out.gpuFormat = navigator.gpu.getPreferredCanvasFormat();
        const f = navigator.gpu.wgslLanguageFeatures;
        out.wgsl = {
          same: f === navigator.gpu.wgslLanguageFeatures,
          tag: Object.prototype.toString.call(f),
          size: f.size,
          values: [...f.values()],
        };
        out.wgslHas = [f.has('subgroup_id'), f.has('missing_feature')];
        out.presentation = {
          defaultRequest: navigator.presentation.defaultRequest,
          receiver: navigator.presentation.receiver,
        };
        const pr = new PresentationRequest('https://example.com/present');
        navigator.presentation.defaultRequest = pr;
        out.presentationAfter = {
          same: navigator.presentation.defaultRequest === pr,
          tag: Object.prototype.toString.call(pr),
        };
        navigator.presentation.defaultRequest = null;
        try { new PresentationRequest([]); }
        catch (e) { out.emptySeqErr = e.name; }
        try { new PresentationRequest('javascript:void(0)'); }
        catch (e) { out.badSchemeErr = e.name; }
        return out;
        """,
    )
    assert details["gpuFormat"] == secure_reference["details"]["gpuFormat"]
    assert details["wgsl"] == secure_reference["details"]["wgsl"]
    assert details["wgslHas"] == [True, False]
    assert details["presentation"] == secure_reference["details"]["presentation"]
    assert details["presentationAfter"] == secure_reference["details"]["presentationAfter"]
    assert details["emptySeqErr"] == "NotSupportedError"
    assert details["badSchemeErr"] == "NotSupportedError"


def test_bom06_events_tracing_watch_apis_and_reload_lifecycle(page):
    page.watch_apis([
        "navigator.bluetooth.getAvailability",
        "navigator.hid.getDevices",
        "navigator.serial.getPorts",
        "navigator.usb.getDevices",
        "navigator.xr.isSessionSupported",
        "navigator.gpu.getPreferredCanvasFormat",
        "navigator.gpu.requestAdapter",
        "PresentationRequest.start",
    ])
    page.start_access_trace()
    page.eval(
        """
        let fired = 0;
        navigator.hid.onconnect = () => { fired++; };
        navigator.hid.dispatchEvent(new Event('connect'));
        navigator.bluetooth.getAvailability();
        navigator.hid.getDevices();
        navigator.serial.getPorts();
        navigator.usb.getDevices();
        navigator.xr.isSessionSupported('inline');
        navigator.gpu.getPreferredCanvasFormat();
        navigator.gpu.requestAdapter();
        const pr = new PresentationRequest('/cast');
        pr.start().catch(() => {});
        navigator.usb.getDevices.call({}).catch(() => {});
        globalThis.hidFired = fired;
        """
    )
    page.run_until_idle(max_steps=20)
    assert page.eval("hidFired") == 1
    hits = [entry["path"] for entry in page.read_watch_api_hits()]
    for expected in (
        "navigator.bluetooth.getAvailability",
        "navigator.hid.getDevices",
        "navigator.serial.getPorts",
        "navigator.usb.getDevices",
        "navigator.xr.isSessionSupported",
        "navigator.gpu.getPreferredCanvasFormat",
        "navigator.gpu.requestAdapter",
        "PresentationRequest.start",
    ):
        assert expected in hits, expected
    trace_events = page.read_access_trace(clear=True)["events"]
    gets = [(e["label"], e["member"]) for e in trace_events if e["operation"] == "get"]
    assert ("navigator", "gpu") in gets
    assert ("navigator", "usb") in gets

    old_gpu = page.eval("navigator.gpu")
    page.load(HTML, INSECURE_BASE)
    assert not old_gpu.context_alive
    assert page.eval(json.dumps(list(MEMBERS)) + ".every(k => !(k in navigator))")


def test_bom06_review_regressions_filter_members_xr_features_and_wgsl_iterator(page):
    page.eval(
        """
        globalThis.bom06Review = {};
        (async () => {
          const sentinel = {sentinel: 'usb'};
          try {
            await navigator.usb.requestDevice({filters: [{get vendorId() { throw sentinel; }}]});
          } catch (e) { bom06Review.usbThrowSame = e === sentinel; }

          try {
            await navigator.xr.requestSession('immersive-vr', {requiredFeatures: [Symbol('bad')]});
          } catch (e) { bom06Review.xrFeatErr = e.name; }

          let iterReads = 0;
          const seqObj = {};
          Object.defineProperty(seqObj, Symbol.iterator, {
            get() { iterReads++; return () => ['https://example.com/'][Symbol.iterator](); }
          });
          new PresentationRequest(seqObj);
          bom06Review.presentationIterReads = iterReads;

          const saved = Array.prototype[Symbol.iterator];
          delete Array.prototype[Symbol.iterator];
          try {
            bom06Review.wgslCount = [...navigator.gpu.wgslLanguageFeatures.values()].length;
          } finally {
            Array.prototype[Symbol.iterator] = saved;
          }
        })();
        """
    )
    page.run_until_idle(max_steps=20)
    assert json.loads(page.eval("JSON.stringify(bom06Review)")) == {
        "usbThrowSame": True,
        "xrFeatErr": "TypeError",
        "presentationIterReads": 2,
        "wgslCount": 13,
    }
