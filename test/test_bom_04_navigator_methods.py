"""Native BOM-04 Navigator methods, ClearKey EME, and DOMException pinned to Chrome fixtures."""

import hashlib
import json
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(
    not getattr(iv8, "_v8_linked", False), reason="requires a V8-linked build"
)

FIXTURES = Path(__file__).parent / "fixtures" / "bom_04"
HTML = "<html><head></head><body></body></html>"
SECURE_BASE = "http://127.0.0.1:51790/reference"
INSECURE_BASE = "http://bom-fixture.invalid:51791/reference"
METHODS = (
    "canShare",
    "share",
    "setAppBadge",
    "clearAppBadge",
    "registerProtocolHandler",
    "unregisterProtocolHandler",
    "getUserMedia",
    "webkitGetUserMedia",
    "requestMIDIAccess",
    "requestMediaKeySystemAccess",
)


def evaluate(page, body):
    return json.loads(page.eval("JSON.stringify((()=>{" + body + "})())"))


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


@pytest.fixture(scope="module")
def media_keys_reference():
    capture = json.loads((FIXTURES / "chrome-media-keys.json").read_text(encoding="utf-8"))
    assert capture["probe_sha256"] == hashlib.sha256(
        (FIXTURES / "media_keys_probe.js").read_bytes()
    ).hexdigest()
    return capture["result"]


@pytest.fixture(scope="module")
def dom_exception_reference():
    capture = json.loads((FIXTURES / "chrome-dom-exception.json").read_text(encoding="utf-8"))
    assert capture["probe_sha256"] == hashlib.sha256(
        (FIXTURES / "dom_exception_probe.js").read_bytes()
    ).hexdigest()
    return capture["result"]


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, SECURE_BASE)
        yield value


JS_HELPERS = """
  const summarizeError = error => (error && typeof error === 'object')
    ? {name: error.name, message: error.message, tag: Object.prototype.toString.call(error),
       domException: typeof DOMException === 'function' && error instanceof DOMException}
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
        value: typeof item.value === 'function' ? functionShape(item.value) : item.value}
        : {kind: 'accessor', get: item.get ? functionShape(item.get) : null, set: item.set ? functionShape(item.set) : null})};
  };
  const plain = value => {
    if (value === undefined) return {type: 'undefined'};
    if (value === null || typeof value !== 'object') return {type: typeof value, value};
    return {type: 'object', tag: Object.prototype.toString.call(value)};
  };
"""


def run_observed_call(page, case_name, method, source):
    page.eval(
        "(() => { 'use strict';"
        + JS_HELPERS
        + f"""
        globalThis.bom04Case = (function() {{
          const effects = [], sentinel = {{sentinel: {json.dumps(case_name)}}};
          const success = function (...values) {{
            effects.push({{kind: 'success', thisUndefined: this === undefined, values: values.map(plain)}});
          }};
          const failure = function (error) {{
            effects.push({{kind: 'failure', thisUndefined: this === undefined, error: summarizeError(error)}});
          }};
          const record = {{effects, source: {json.dumps(source)}}};
          setTimeout(() => effects.push('task'), 0);
          try {{
            const args = eval({json.dumps(source)});
            const result = Reflect.apply(navigator[{json.dumps(method)}], navigator, args);
            record.returned = plain(result);
            if (result instanceof Promise) {{
              result.then(
                value => {{ effects.push({{kind: 'resolved', value: plain(value)}}); }},
                error => {{ effects.push({{kind: 'rejected', error: summarizeError(error), same: error === sentinel}}); }}
              );
            }}
          }} catch (error) {{
            record.error = {{...summarizeError(error), same: error === sentinel}};
          }}
          effects.push('after call');
          Promise.resolve().then(() => effects.push('microtask'));
          return record;
        }})();
        }})();"""
    )
    page.run_until_idle(max_steps=20)
    return json.loads(page.eval("JSON.stringify(bom04Case)"))


def test_secure_and_insecure_method_shapes_match_chrome_reference(
    secure_reference, insecure_reference
):
    for url, expected_secure, reference in (
        (SECURE_BASE, True, secure_reference),
        ("https://bom04-secure.test/page", True, secure_reference),
        (INSECURE_BASE, False, insecure_reference),
        ("about:blank", False, insecure_reference),
    ):
        with iv8.Page() as current:
            current.load(HTML, url)
            assert current.eval("isSecureContext") is expected_secure
            actual = evaluate(
                current,
                JS_HELPERS
                + """
                const shape = {};
                for (const name of """
                + json.dumps(list(METHODS))
                + """) {
                  shape[name] = {
                    present: name in navigator,
                    own: Object.hasOwn(navigator, name),
                    descriptor: descriptor(Navigator.prototype, name),
                  };
                }
                shape.sameGetUserMedia = navigator.getUserMedia === navigator.webkitGetUserMedia;
                return shape;
                """,
            )
            if expected_secure:
                assert actual == reference["shape"]
            else:
                for method in METHODS:
                    assert actual[method] == reference["shape"][method]


def test_receiver_brand_rejections_match_chrome_channels(page, secure_reference):
    page.eval(
        JS_HELPERS
        + """
        globalThis.bom04Receivers = {};
        for (const method of """
        + json.dumps(list(METHODS))
        + """) {
          try {
            const result = navigator[method].call({});
            bom04Receivers[method] = {returned: plain(result)};
            if (result instanceof Promise) {
              result.then(
                value => { bom04Receivers[method].resolved = plain(value); },
                error => { bom04Receivers[method].rejected = summarizeError(error); }
              );
            }
          } catch (error) {
            bom04Receivers[method] = {error: summarizeError(error)};
          }
        }
        """
    )
    page.run_jobs()
    assert json.loads(page.eval("JSON.stringify(bom04Receivers)")) == secure_reference["receivers"]


def test_all_captured_bom04_call_cases_match_chrome_reference(page, secure_reference):
    method_by_prefix = [
        ("canShare_", "canShare"),
        ("share_", "share"),
        ("setAppBadge_", "setAppBadge"),
        ("clearAppBadge_", "clearAppBadge"),
        ("register_", "registerProtocolHandler"),
        ("unregister_", "unregisterProtocolHandler"),
        ("getUserMedia_", "getUserMedia"),
        ("webkitGetUserMedia_", "webkitGetUserMedia"),
        ("midi_", "requestMIDIAccess"),
        ("mksa_", "requestMediaKeySystemAccess"),
    ]
    checked = 0
    for case_name, expected in secure_reference["calls"].items():
        method = next(
            target for prefix, target in method_by_prefix if case_name.startswith(prefix)
        )
        actual = run_observed_call(page, case_name, method, expected["source"])
        assert actual == expected, f"Mismatch in {case_name}"
        checked += 1
    assert checked == len(secure_reference["calls"])


def test_can_share_rejects_blob_and_non_sequence_files(page):
    actual = evaluate(
        page,
        """
        const out = {};
        try { navigator.canShare({files: [new Blob(['a'])]}); }
        catch (e) { out.blob = e.name + ': ' + e.message; }
        try { navigator.canShare({files: 17}); }
        catch (e) { out.nonSeq = e.name + ': ' + e.message; }
        return out;
        """,
    )
    assert actual == {
        "blob": (
            "TypeError: Failed to execute 'canShare' on 'Navigator': "
            "Failed to read the 'files' property from 'ShareData': "
            "Failed to convert value to 'File'."
        ),
        "nonSeq": (
            "TypeError: Failed to execute 'canShare' on 'Navigator': "
            "Failed to read the 'files' property from 'ShareData': "
            "The provided value cannot be converted to a sequence."
        ),
    }


def test_dom_exception_constructor_constants_prototype_and_stack_match_chrome(
    page, dom_exception_reference
):
    actual = evaluate(
        page,
        """
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
        const proto = DOMException.prototype;
        const summarize = value => ({
          name: value.name, message: value.message, code: value.code,
          ownKeys: Reflect.ownKeys(value).map(String),
          tag: Object.prototype.toString.call(value),
          instanceOfError: value instanceof Error,
          string: String(value),
          stackType: typeof value.stack,
          stackOwn: descriptor(value, 'stack') ? descriptor(value, 'stack').kind : null,
        });
        const sources = {
          none: 'new DOMException()', message: "new DOMException('m')",
          named: "new DOMException('m','NotAllowedError')",
          custom: "new DOMException('m','CustomName')",
          undefined_args: 'new DOMException(undefined, undefined)',
          null_args: 'new DOMException(null, null)',
          number_args: 'new DOMException(1, 2)',
          legacy: "new DOMException('m','IndexSizeError')",
          data_clone: "new DOMException('m','DataCloneError')",
          quota: "new DOMException('m','QuotaExceededError')",
          security: "new DOMException('m','SecurityError')",
          syntax: "new DOMException('m','SyntaxError')",
          not_supported: "new DOMException('m','NotSupportedError')",
          abort: "new DOMException('m','AbortError')",
          timeout: "new DOMException('m','TimeoutError')",
          invalid_state: "new DOMException('m','InvalidStateError')",
          not_found: "new DOMException('m','NotFoundError')",
          encoding: "new DOMException('m','EncodingError')",
          operation: "new DOMException('m','OperationError')",
          call: "DOMException('m')",
          symbol: "new DOMException(Symbol('m'))",
        };
        const cases = {};
        for (const [k, src] of Object.entries(sources)) {
          try { cases[k] = summarize(eval(src)); }
          catch (e) { cases[k] = {error: e.name, message: e.message}; }
        }
        const receivers = {}, prototypeAccess = {};
        for (const key of ['name', 'message', 'code']) {
          try { Object.getOwnPropertyDescriptor(proto, key).get.call({}); receivers[key] = 'returned'; }
          catch (e) { receivers[key] = e.name + ': ' + e.message; }
          try { prototypeAccess[key] = proto[key]; }
          catch (e) { prototypeAccess[key] = e.name + ': ' + e.message; }
        }
        const conversionOrder = [];
        new DOMException(
          {toString() { conversionOrder.push('message'); return 'm'; }},
          {toString() { conversionOrder.push('name'); return 'n'; }}
        );
        let protoReads = 0;
        const childProto = Object.create(proto);
        const target = new Proxy(function(){}, {
          get(t, k, r) { if (k === 'prototype') { protoReads++; return childProto; } return Reflect.get(t, k, r); }
        });
        const constructed = Reflect.construct(DOMException, ['msg', 'AbortError'], target);
        class Sub extends DOMException { constructor() { super('sub', 'AbortError'); } }
        const sub = new Sub();
        return {
          global: descriptor(globalThis, 'DOMException'),
          constructor: functionShape(DOMException),
          constructorParent: Object.getPrototypeOf(DOMException) === Function.prototype ? 'Function.prototype' : 'other',
          constructorKeys: Reflect.ownKeys(DOMException).map(String),
          constructorMembers: Object.fromEntries(Reflect.ownKeys(DOMException).map(k => [String(k), descriptor(DOMException, k)])),
          prototypeKeys: Reflect.ownKeys(proto).map(String),
          prototypeParentIsError: Object.getPrototypeOf(proto) === Error.prototype,
          prototypeMembers: Object.fromEntries(Reflect.ownKeys(proto).map(k => [String(k), descriptor(proto, k)])),
          cases,
          receivers,
          prototypeAccess,
          conversionOrder,
          subclass: {proto: Object.getPrototypeOf(sub) === Sub.prototype, name: sub.name, code: sub.code, tag: Object.prototype.toString.call(sub)},
          reflectConstruct: {protoReads, match: Object.getPrototypeOf(constructed) === childProto, code: constructed.code},
        };
        """,
    )
    for key in (
        "global",
        "constructor",
        "constructorParent",
        "constructorKeys",
        "constructorMembers",
        "prototypeKeys",
        "prototypeParentIsError",
        "prototypeMembers",
        "cases",
        "receivers",
        "prototypeAccess",
        "conversionOrder",
        "subclass",
    ):
        assert actual[key] == dom_exception_reference[key], key
    assert actual["reflectConstruct"] == {"protoReads": 1, "match": True, "code": 20}

    page.eval("globalThis.platformShareError = null; navigator.share({}).catch(e => { platformShareError = e; });")
    page.run_jobs()
    platform_err = evaluate(
        page,
        """
        const e = platformShareError;
        return {
          name: e.name, message: e.message, code: e.code,
          ownKeys: Reflect.ownKeys(e).map(String),
          tag: Object.prototype.toString.call(e),
          instanceOfError: e instanceof Error,
          string: String(e),
          stackType: typeof e.stack,
          stackOwn: Object.getOwnPropertyDescriptor(e, 'stack').get ? 'accessor' : 'data',
        };
        """,
    )
    assert platform_err == dom_exception_reference["platform"]

    with pytest.raises(iv8.JSError) as caught:
        page.eval("navigator.registerProtocolHandler('http', '/%s')")
    assert caught.value.name == "SecurityError"
    assert "scheme allowlist" in caught.value.message


def test_clearkey_media_key_system_access_and_media_keys_match_chrome(
    page, media_keys_reference
):
    page.eval(
        """
        globalThis.emeOut = {configs: {}};
        (async () => {
          const summarizeError = e => ({name: e.name, message: e.message, tag: Object.prototype.toString.call(e)});
          const vp = 'video/mp4; codecs="avc1.42E01E"';
          const cases = {
            empty: [{}],
            init_only: [{initDataTypes: ['cenc']}],
            video_mp4: [{videoCapabilities: [{contentType: vp}]}],
            video_mp4_init: [{initDataTypes: ['cenc'], videoCapabilities: [{contentType: vp}]}],
            video_webm_vp9: [{videoCapabilities: [{contentType: 'video/webm; codecs="vp9"'}]}],
            video_webm_vp8: [{videoCapabilities: [{contentType: 'video/webm; codecs="vp8"'}]}],
            audio_mp4: [{audioCapabilities: [{contentType: 'audio/mp4; codecs="mp4a.40.2"'}]}],
            audio_webm_opus: [{audioCapabilities: [{contentType: 'audio/webm; codecs="opus"'}]}],
            audio_mp4_flac: [{audioCapabilities: [{contentType: 'audio/mp4; codecs="flac"'}]}],
            video_no_codecs: [{videoCapabilities: [{contentType: 'video/mp4'}]}],
            video_bad_mime: [{videoCapabilities: [{contentType: 'video/x-unknown; codecs="foo"'}]}],
            video_empty_type: [{videoCapabilities: [{contentType: ''}]}],
            video_hevc: [{videoCapabilities: [{contentType: 'video/mp4; codecs="hvc1.1.6.L93.B0"'}]}],
            mixed: [{videoCapabilities: [{contentType: 'video/x-unknown'}, {contentType: vp, robustness: ''}]}],
            robustness: [{videoCapabilities: [{contentType: vp, robustness: 'SW_SECURE_CRYPTO'}]}],
            init_webm: [{initDataTypes: ['webm'], videoCapabilities: [{contentType: vp}]}],
            init_keyids: [{initDataTypes: ['keyids'], videoCapabilities: [{contentType: vp}]}],
            init_bad: [{initDataTypes: ['bogus'], videoCapabilities: [{contentType: vp}]}],
            init_mixed: [{initDataTypes: ['bogus', 'cenc', 'keyids'], videoCapabilities: [{contentType: vp}]}],
            distinctive_required: [{distinctiveIdentifier: 'required', videoCapabilities: [{contentType: vp}]}],
            distinctive_not_allowed: [{distinctiveIdentifier: 'not-allowed', videoCapabilities: [{contentType: vp}]}],
            persistent_required: [{persistentState: 'required', videoCapabilities: [{contentType: vp}]}],
            persistent_not_allowed: [{persistentState: 'not-allowed', videoCapabilities: [{contentType: vp}]}],
            session_temporary: [{sessionTypes: ['temporary'], videoCapabilities: [{contentType: vp}]}],
            session_persistent: [{sessionTypes: ['persistent-license'], videoCapabilities: [{contentType: vp}]}],
            label: [{label: 'first', videoCapabilities: [{contentType: vp}]}],
            second_ok: [{initDataTypes: ['bogus']}, {label: 'second', videoCapabilities: [{contentType: vp}]}],
            encryption_scheme: [{videoCapabilities: [{contentType: vp, encryptionScheme: 'cenc'}]}],
            encryption_cbcs: [{videoCapabilities: [{contentType: vp, encryptionScheme: 'cbcs'}]}],
            encryption_bad: [{videoCapabilities: [{contentType: vp, encryptionScheme: 'bogus'}]}],
            audio_video: [{audioCapabilities: [{contentType: 'audio/mp4; codecs="mp4a.40.2"'}], videoCapabilities: [{contentType: vp}]}],
          };
          for (const [name, configs] of Object.entries(cases)) {
            try {
              const access = await navigator.requestMediaKeySystemAccess('org.w3.clearkey', configs);
              emeOut.configs[name] = {resolved: true, keySystem: access.keySystem, configuration: access.getConfiguration()};
            } catch (e) { emeOut.configs[name] = {rejected: summarizeError(e)}; }
          }
          for (const system of ['com.widevine.alpha', 'com.microsoft.playready', 'com.apple.fps',
                                'org.w3.clearkey.extra', 'ORG.W3.CLEARKEY', 'x-org.w3.clearkey']) {
            try {
              await navigator.requestMediaKeySystemAccess(system, cases.video_mp4);
              emeOut.configs['system_' + system] = {resolved: true};
            } catch (e) { emeOut.configs['system_' + system] = {rejected: summarizeError(e)}; }
          }
          const access = await navigator.requestMediaKeySystemAccess('org.w3.clearkey', cases.video_mp4_init);
          const order = [];
          setTimeout(() => order.push('task'), 0);
          const pending = access.createMediaKeys();
          pending.then(() => order.push('resolved'));
          Promise.resolve().then(() => order.push('microtask'));
          const keys = await pending;
          emeOut.createOrder = order;
          emeOut.sameConfiguration = access.getConfiguration() === access.getConfiguration();
          emeOut.createDistinct = keys !== await access.createMediaKeys();
          emeOut.accessProtoKeys = Reflect.ownKeys(MediaKeySystemAccess.prototype).map(String);
          emeOut.keysProtoKeys = Reflect.ownKeys(MediaKeys.prototype).map(String);
          const session = keys.createSession();
          emeOut.sessionProtoKeys = Reflect.ownKeys(MediaKeySession.prototype).map(String);
          emeOut.statusMapProtoKeys = Reflect.ownKeys(MediaKeyStatusMap.prototype).map(String);
          emeOut.statusCert = await keys.setServerCertificate(new Uint8Array([1, 2, 3]));
          emeOut.policyUsable = await keys.getStatusForPolicy({minHdcpVersion: '1.0'});
          emeOut.sessionState = {
            id: session.sessionId,
            expNaN: Number.isNaN(session.expiration),
            closedSame: session.closed === session.closed,
            statusesSame: session.keyStatuses === session.keyStatuses,
            size: session.keyStatuses.size,
            has: session.keyStatuses.has(new Uint8Array([1])),
            entriesLen: [...session.keyStatuses].length,
          };
        })();
        """
    )
    page.run_until_idle(max_steps=30)
    actual = json.loads(page.eval("JSON.stringify(emeOut)"))
    assert actual["configs"] == media_keys_reference["configs"]
    assert actual["createOrder"] == ["microtask", "task", "resolved"]
    assert actual["sameConfiguration"] is False
    assert actual["createDistinct"] is True
    assert actual["accessProtoKeys"] == media_keys_reference["access"]["prototypeKeys"]
    assert actual["keysProtoKeys"] == media_keys_reference["mediaKeys"]["prototypeKeys"]
    assert actual["sessionProtoKeys"] == media_keys_reference["mediaKeys"]["calls"]["session_default"]["sessionKeys"]
    assert actual["statusMapProtoKeys"] == [
        "size", "get", "has", "entries", "forEach", "keys", "values",
        "constructor", "Symbol(Symbol.toStringTag)", "Symbol(Symbol.iterator)",
    ]
    assert actual["statusCert"] is False
    assert actual["policyUsable"] == "usable"
    assert actual["sessionState"] == {
        "id": "",
        "expNaN": True,
        "closedSame": True,
        "statusesSame": True,
        "size": 0,
        "has": False,
        "entriesLen": 0,
    }


def test_eme_global_exposure_and_reload_lifecycle():
    with iv8.Page() as current:
        current.load(HTML, SECURE_BASE)
        assert current.eval(
            "['DOMException','MediaKeySystemAccess','MediaKeys','MediaKeySession','MediaKeyStatusMap']"
            ".every(k => typeof globalThis[k] === 'function')"
        )
        old_fn = current.eval("navigator.share")
        current.load(HTML, INSECURE_BASE)
        assert not old_fn.context_alive
        assert current.eval("typeof DOMException === 'function'")
        assert current.eval(
            "['MediaKeySystemAccess','MediaKeys','MediaKeySession','MediaKeyStatusMap']"
            ".every(k => typeof globalThis[k] === 'undefined')"
        )
        assert current.eval(json.dumps(list(METHODS)) + ".every(m => !(m in navigator))")


def test_access_tracing_and_watch_apis_cover_bom04_methods_and_eme(page):
    page.watch_apis([
        "navigator.canShare",
        "navigator.share",
        "navigator.setAppBadge",
        "navigator.clearAppBadge",
        "navigator.registerProtocolHandler",
        "navigator.unregisterProtocolHandler",
        "navigator.getUserMedia",
        "navigator.webkitGetUserMedia",
        "navigator.requestMIDIAccess",
        "navigator.requestMediaKeySystemAccess",
        "MediaKeySystemAccess.getConfiguration",
        "MediaKeySystemAccess.createMediaKeys",
        "MediaKeys.createSession",
        "window.DOMException",
    ])
    page.start_access_trace()
    page.eval(
        """
        navigator.canShare({text: 'hello'});
        navigator.share({text: 'hello'}).catch(() => {});
        navigator.setAppBadge(4);
        navigator.clearAppBadge();
        navigator.registerProtocolHandler('web+demo', '/?q=%s');
        navigator.unregisterProtocolHandler('web+demo', '/?q=%s');
        navigator.getUserMedia({audio: true}, () => {}, () => {});
        navigator.webkitGetUserMedia({video: true}, () => {}, () => {});
        navigator.requestMIDIAccess().catch(() => {});
        navigator.requestMediaKeySystemAccess(
          'org.w3.clearkey',
          [{videoCapabilities: [{contentType: 'video/mp4; codecs="avc1.42E01E"'}]}]
        ).then(async access => {
          access.getConfiguration();
          const keys = await access.createMediaKeys();
          keys.createSession();
        });
        new DOMException('custom', 'AbortError');
        try { navigator.canShare.call({}); } catch (_) {}
        navigator.share.call({}).catch(() => {});
        """,
        name="bom04-trace.js",
    )
    page.run_until_idle(max_steps=20)
    hits = [entry["path"] for entry in page.read_watch_api_hits()]
    for expected_api in (
        "navigator.canShare",
        "navigator.share",
        "navigator.setAppBadge",
        "navigator.clearAppBadge",
        "navigator.registerProtocolHandler",
        "navigator.unregisterProtocolHandler",
        "navigator.getUserMedia",
        "navigator.webkitGetUserMedia",
        "navigator.requestMIDIAccess",
        "navigator.requestMediaKeySystemAccess",
        "MediaKeySystemAccess.getConfiguration",
        "MediaKeySystemAccess.createMediaKeys",
        "MediaKeys.createSession",
        "window.DOMException",
    ):
        assert expected_api in hits, expected_api
    trace_events = page.read_access_trace(clear=True)["events"]
    calls = [
        (e["label"], e["member"], e["outcome"])
        for e in trace_events
        if e["operation"] == "call"
    ]
    assert ("navigator", "canShare", "returned") in calls
    assert ("navigator", "share", "returned") in calls
    assert ("Navigator.prototype", "canShare", "threw") in calls
    assert ("Navigator.prototype", "share", "returned") in calls
    assert ("MediaKeySystemAccess", "getConfiguration", "returned") in calls
    assert ("MediaKeys", "createSession", "returned") in calls


def test_blink_iterator_value_before_done_track_constraints_and_codec_syntax(page):
    actual = evaluate(
        page,
        """
        const iterOrder = [];
        const terminalThrowingIterable = {
          [Symbol.iterator]() {
            return {
              next() {
                return {
                  get done() { iterOrder.push('done'); return true; },
                  get value() { iterOrder.push('value'); throw new Error('boom'); },
                };
              },
            };
          },
        };
        let canShareErr = null;
        try { navigator.canShare({files: terminalThrowingIterable}); }
        catch (e) { canShareErr = e.name + ': ' + e.message; }

        const gumOrder = [];
        const sentinel = {sentinel: 'track'};
        let gumSame = false;
        try {
          navigator.getUserMedia(
            {audio: {get deviceId() { gumOrder.push('deviceId'); throw sentinel; }}},
            17,
            17
          );
        } catch (e) { gumSame = e === sentinel; }

        const readKeys = [];
        const proxy = new Proxy({}, {
          get(t, k, r) { readKeys.push(String(k)); return Reflect.get(t, k, r); }
        });
        navigator.getUserMedia({audio: proxy}, () => {}, () => {});

        return {iterOrder, canShareErr, gumOrder, gumSame, firstKey: readKeys[0], lastKey: readKeys.at(-1), keyCount: readKeys.length};
        """,
    )
    assert actual == {
        "iterOrder": ["value"],
        "canShareErr": "Error: boom",
        "gumOrder": ["deviceId"],
        "gumSame": True,
        "firstKey": "aspectRatio",
        "lastKey": "advanced",
        "keyCount": 39,
    }

    page.eval(
        """
        globalThis.codecMatrix = {};
        (async () => {
          for (const ct of [
            'video/mp4; codecs="avc1.42E01E"',
            'video/mp4; codecs="avc1.garbage"',
            'video/mp4; codecs="avc1.12345"',
            'video/mp4; codecs="avc1.42E01EZ"',
            'video/mp4; codecs="avc3.64001f"',
            'video/mp4; codecs="avc3.garbage"',
            'video/mp4; codecs="vp09.00.10.08"',
            'video/mp4; codecs="vp09.garbage"',
            'video/mp4; codecs="av01.0.04M.08"',
            'video/mp4; codecs="av01.garbage"',
            'audio/mp4; codecs="mp4a.40.2"',
            'audio/mp4; codecs="mp4a.garbage"',
            'audio/mp4; codecs="mp4a.40.999"',
          ]) {
            try {
              await navigator.requestMediaKeySystemAccess('org.w3.clearkey', [{
                videoCapabilities: ct.startsWith('video') ? [{contentType: ct}] : undefined,
                audioCapabilities: ct.startsWith('audio') ? [{contentType: ct}] : undefined,
              }]);
              codecMatrix[ct] = true;
            } catch (_) {
              codecMatrix[ct] = false;
            }
          }
        })();
        """
    )
    page.run_until_idle(max_steps=20)
    assert json.loads(page.eval("JSON.stringify(codecMatrix)")) == {
        'video/mp4; codecs="avc1.42E01E"': True,
        'video/mp4; codecs="avc1.garbage"': False,
        'video/mp4; codecs="avc1.12345"': False,
        'video/mp4; codecs="avc1.42E01EZ"': False,
        'video/mp4; codecs="avc3.64001f"': True,
        'video/mp4; codecs="avc3.garbage"': False,
        'video/mp4; codecs="vp09.00.10.08"': True,
        'video/mp4; codecs="vp09.garbage"': False,
        'video/mp4; codecs="av01.0.04M.08"': True,
        'video/mp4; codecs="av01.garbage"': False,
        'audio/mp4; codecs="mp4a.40.2"': True,
        'audio/mp4; codecs="mp4a.garbage"': False,
        'audio/mp4; codecs="mp4a.40.999"': False,
    }
