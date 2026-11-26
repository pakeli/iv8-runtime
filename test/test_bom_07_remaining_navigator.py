"""Native BOM-07 remaining Navigator APIs and DOMRect geometry types pinned to Chrome."""

import hashlib
import json
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(
    not getattr(iv8, "_v8_linked", False), reason="requires a V8-linked build"
)

FIXTURES = Path(__file__).parent / "fixtures" / "bom_07"
INVENTORY = Path(__file__).parent.parent / "docs" / "browser_api_inventory.json"
HTML = "<html><head></head><body></body></html>"
SECURE_BASE = "http://127.0.0.1:59177/reference"
INSECURE_BASE = "http://bom-fixture.invalid:59469/reference"
MEMBERS = (
    "mediaDevices",
    "serviceWorker",
    "storageBuckets",
    "login",
    "managed",
    "ink",
    "devicePosture",
    "virtualKeyboard",
    "windowControlsOverlay",
    "cpuPerformance",
)
DECLARED_INTERFACES = (
    "MediaDevices",
    "MediaDeviceInfo",
    "InputDeviceInfo",
    "ServiceWorkerContainer",
    "ServiceWorker",
    "ServiceWorkerRegistration",
    "StorageBucketManager",
    "StorageBucket",
    "NavigatorLogin",
    "NavigatorManagedData",
    "Ink",
    "DelegatedInkTrailPresenter",
    "DevicePosture",
    "VirtualKeyboard",
    "WindowControlsOverlay",
    "DOMRect",
    "DOMRectReadOnly",
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


@pytest.fixture(scope="module")
def opaque_reference():
    capture = json.loads((FIXTURES / "chrome-opaque.json").read_text(encoding="utf-8"))
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
    if (Array.isArray(value)) return {type: 'array', length: value.length, items: value.map(x => typeof x === 'object' && x ? Object.prototype.toString.call(x) : x)};
    return {type: 'object', tag: Object.prototype.toString.call(value)};
  };
"""


def test_bom07_shapes_globals_and_interfaces_match_chrome(
    secure_reference, insecure_reference, opaque_reference
):
    for url, expected_secure, reference in (
        (SECURE_BASE, True, secure_reference),
        (INSECURE_BASE, False, insecure_reference),
        ("about:blank", False, opaque_reference),
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
                    entry.valueType = typeof value;
                    if (typeof value === 'object' && value !== null) {{
                      entry.stable = value === navigator[name];
                      entry.tag = Object.prototype.toString.call(value);
                      entry.ownKeys = Reflect.ownKeys(value).map(String);
                      entry.protoName = Object.getPrototypeOf(value).constructor.name;
                    }} else {{
                      entry.value = value;
                    }}
                    try {{ Object.getOwnPropertyDescriptor(Navigator.prototype, name).get.call({{}}); entry.badReceiver = 'ok'; }}
                    catch (e) {{ entry.badReceiver = summarizeError(e); }}
                  }}
                  shape[name] = entry;
                }}
                return {{
                  globals, interfaces, shape,
                  inputCtorParentName: Object.getPrototypeOf(InputDeviceInfo).name,
                  domRectCtorParentEq: Object.getPrototypeOf(DOMRect) === DOMRectReadOnly,
                }};
                """,
            )
            assert actual["shape"] == reference["shape"], url
            assert actual["inputCtorParentName"] == "MediaDeviceInfo"
            assert actual["domRectCtorParentEq"] is True
            for g in DECLARED_INTERFACES:
                assert actual["globals"][g] == reference["globals"][g], (url, g)
                if g in reference["interfaces"]:
                    assert actual["interfaces"][g] == reference["interfaces"][g], (url, g)


def test_secure_navigator_reaches_full_70_of_70_inventory_coverage(page):
    inventory = json.loads(INVENTORY.read_text(encoding="utf-8"))
    nav_inv = inventory["objects"]["navigator"]
    expected_names = set(nav_inv["exposed"]) | set(nav_inv["missing"])
    assert len(expected_names) == 70
    present = set(
        evaluate(
            page,
            """
            return Reflect.ownKeys(Navigator.prototype)
              .filter(k => typeof k === 'string' && k !== 'constructor');
            """,
        )
    )
    assert present == expected_names


CALL_MATRIX = [
    ("md_enum", "navigator.mediaDevices", "enumerateDevices", "[]"),
    ("md_gum_empty", "navigator.mediaDevices", "getUserMedia", "[]"),
    ("md_gum_dict", "navigator.mediaDevices", "getUserMedia", "[{}]"),
    ("md_gum_audio", "navigator.mediaDevices", "getUserMedia", "[{audio: true}]"),
    ("md_gum_bad", "navigator.mediaDevices", "getUserMedia", "[17]"),
    ("md_capture_empty", "navigator.mediaDevices", "setCaptureHandleConfig", "[]"),
    (
        "md_capture_ok",
        "navigator.mediaDevices",
        "setCaptureHandleConfig",
        "[{handle: 'abc', exposeOrigin: true}]",
    ),
    (
        "md_capture_long",
        "navigator.mediaDevices",
        "setCaptureHandleConfig",
        "[{handle: 'x'.repeat(1025)}]",
    ),
    ("sw_getReg_same", "navigator.serviceWorker", "getRegistration", "['/']"),
    (
        "sw_getReg_cross",
        "navigator.serviceWorker",
        "getRegistration",
        "['https://example.com/']",
    ),
    ("sw_getRegs", "navigator.serviceWorker", "getRegistrations", "[]"),
    ("sw_reg_missing", "navigator.serviceWorker", "register", "[]"),
    (
        "sw_reg_cross",
        "navigator.serviceWorker",
        "register",
        "['https://example.com/sw.js']",
    ),
    ("sb_keys_initial", "navigator.storageBuckets", "keys", "[]"),
    ("sb_open_missing", "navigator.storageBuckets", "open", "[]"),
    ("sb_open_bad_upper", "navigator.storageBuckets", "open", "['BadName']"),
    ("sb_open_bad_dash", "navigator.storageBuckets", "open", "['-bad']"),
    ("sb_open_ok", "navigator.storageBuckets", "open", "['my-bucket']"),
    ("sb_keys_after", "navigator.storageBuckets", "keys", "[]"),
    ("sb_del_missing", "navigator.storageBuckets", "delete", "[]"),
    ("sb_del_bad", "navigator.storageBuckets", "delete", "['BadName']"),
    ("sb_del_ok", "navigator.storageBuckets", "delete", "['my-bucket']"),
    ("login_missing", "navigator.login", "setStatus", "[]"),
    ("login_bad", "navigator.login", "setStatus", "['bogus']"),
    ("login_in", "navigator.login", "setStatus", "['logged-in']"),
    ("login_out", "navigator.login", "setStatus", "['logged-out']"),
    ("mgd_missing", "navigator.managed", "getManagedConfiguration", "[]"),
    ("mgd_bad", "navigator.managed", "getManagedConfiguration", "[17]"),
    ("mgd_ok", "navigator.managed", "getManagedConfiguration", "[['a','b']]"),
    ("ink_empty", "navigator.ink", "requestPresenter", "[]"),
    ("ink_dict", "navigator.ink", "requestPresenter", "[{}]"),
    ("ink_null_area", "navigator.ink", "requestPresenter", "[{presentationArea: null}]"),
    ("ink_el", "navigator.ink", "requestPresenter", "[{presentationArea: document.body}]"),
    ("ink_bad_area", "navigator.ink", "requestPresenter", "[{presentationArea: 17}]"),
    ("ink_bad", "navigator.ink", "requestPresenter", "[17]"),
]


def test_all_captured_bom07_call_cases_match_chrome(page, secure_reference):
    for case_name, target_expr, method, args_expr in CALL_MATRIX:
        page.eval(
            "(() => { 'use strict';"
            + JS_HELPERS
            + f"""
            globalThis.bom07Record = (function() {{
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
            }})()"""
        )
        page.run_until_idle()
        actual = evaluate(page, "return globalThis.bom07Record;")
        assert actual == secure_reference["calls"][case_name], case_name


def test_dom_rect_media_devices_sw_storage_buckets_and_ink_semantics(
    page, secure_reference
):
    page.eval(
        """
        globalThis.bom07Async = {};
        navigator.mediaDevices.enumerateDevices().then(d => { globalThis.bom07Async.devs = d; });
        navigator.mediaDevices.getDisplayMedia({video: false}).then(
          () => {}, e => { globalThis.bom07Async.gdmEmptyErr = e.name + ':' + e.message; }
        );
        navigator.mediaDevices.getDisplayMedia().then(
          () => {}, e => { globalThis.bom07Async.gdmDenied = e.name; }
        );
        navigator.serviceWorker.register('/missing-sw.js').then(
          () => {}, e => { globalThis.bom07Async.swLocal = e.name + ':' + e.message.startsWith("Failed to register a ServiceWorker for scope ('http://127.0.0.1:59177/') with script ('http://127.0.0.1:59177/missing-sw.js'):"); }
        );
        navigator.serviceWorker.register('/sw.js', {updateViaCache: 'bogus'}).then(
          () => {}, e => { globalThis.bom07Async.swBadUvc = e.name + ':' + e.message; }
        );
        const expTarget = Date.now() + 120000;
        navigator.storageBuckets.open('alpha_1', {quota: 4096, expires: expTarget}).then(async b => {
          const b2 = await navigator.storageBuckets.open('alpha_1');
          const est = await b.estimate();
          const exp1 = await b.expires();
          const persisted = await b.persisted();
          const persist = await b.persist();
          globalThis.bom07Async.bucket = {
            name: b.name,
            distinctWrapper: b !== b2,
            est,
            expMatches: exp1 === Math.trunc(expTarget),
            persisted,
            persist,
            idbTag: Object.prototype.toString.call(b.indexedDB),
            idbStable: b.indexedDB === b.indexedDB && b.indexedDB !== window.indexedDB,
            cachesTag: Object.prototype.toString.call(b.caches),
            cachesStable: b.caches === b.caches && b.caches !== window.caches,
          };
        });
        navigator.ink.requestPresenter({presentationArea: document.body}).then(p => {
          let updErr = null;
          try { p.updateInkTrailStartPoint(new Event('click'), {color: 'red', diameter: 4}); }
          catch (e) { updErr = e.name + ':' + e.message; }
          globalThis.bom07Async.ink = {
            areaIsBody: p.presentationArea === document.body,
            updErr,
          };
        });
        """
    )
    page.run_until_idle()
    details = evaluate(
        page,
        """
        const rNeg = new DOMRect(10, 20, -5, -8);
        const rSpecial = new DOMRect(NaN, Infinity, -Infinity, -0);
        let protoReads = 0;
        const customProto = {custom: true};
        const proxyTarget = new Proxy(function() {}, {
          get(t, p, r) { if (p === 'prototype') { protoReads++; return customProto; } return Reflect.get(t, p, r); }
        });
        const rSub = Reflect.construct(DOMRect, [1, 2, 3, 4], proxyTarget);
        let setRoErr = null;
        try { Object.getOwnPropertyDescriptor(DOMRect.prototype, 'x').set.call(new DOMRectReadOnly(), 5); }
        catch (e) { setRoErr = e.name + ':' + e.message; }
        let capWildcardErr = null;
        try { navigator.mediaDevices.setCaptureHandleConfig({permittedOrigins: ['*', 'https://example.com']}); }
        catch (e) { capWildcardErr = e.name; }
        const vkRect = navigator.virtualKeyboard.boundingRect;
        navigator.virtualKeyboard.overlaysContent = true;
        const vkAfter = navigator.virtualKeyboard.overlaysContent;
        navigator.virtualKeyboard.overlaysContent = false;
        const wcoRect = navigator.windowControlsOverlay.getTitlebarAreaRect();
        return {
          supportedConstraints: navigator.mediaDevices.getSupportedConstraints(),
          devs: globalThis.bom07Async.devs.map(d => ({
            deviceId: d.deviceId, kind: d.kind, label: d.label, groupId: typeof d.groupId,
            tag: Object.prototype.toString.call(d), json: d.toJSON(),
            caps: typeof d.getCapabilities === 'function' ? d.getCapabilities() : null,
          })),
          gdmEmptyErr: globalThis.bom07Async.gdmEmptyErr,
          gdmDenied: globalThis.bom07Async.gdmDenied,
          capWildcardErr,
          sw: {
            controller: navigator.serviceWorker.controller,
            readySame: navigator.serviceWorker.ready === navigator.serviceWorker.ready,
            readyTag: Object.prototype.toString.call(navigator.serviceWorker.ready),
          },
          swLocal: globalThis.bom07Async.swLocal,
          swBadUvc: globalThis.bom07Async.swBadUvc,
          bucket: globalThis.bom07Async.bucket,
          ink: globalThis.bom07Async.ink,
          devicePosture: {type: navigator.devicePosture.type, onchange: navigator.devicePosture.onchange},
          vk: {
            rectTag: Object.prototype.toString.call(vkRect),
            rectSame: vkRect === navigator.virtualKeyboard.boundingRect,
            rectJson: vkRect.toJSON(),
            vkAfter,
          },
          wco: {
            visible: navigator.windowControlsOverlay.visible,
            rectTag: Object.prototype.toString.call(wcoRect),
            rectSame: wcoRect === navigator.windowControlsOverlay.getTitlebarAreaRect(),
            rectJson: wcoRect.toJSON(),
          },
          domRect: {
            negJson: rNeg.toJSON(),
            xNaN: Number.isNaN(rSpecial.x),
            heightNegZero: Object.is(rSpecial.height, -0),
            topInf: rSpecial.top === Infinity,
            leftNaN: Number.isNaN(rSpecial.left),
            fromRect: DOMRect.fromRect({x: 3, y: 4, width: 5, height: 6}).toJSON(),
            roFromRectTag: Object.prototype.toString.call(DOMRectReadOnly.fromRect({width: -3, x: 7})),
            protoReads,
            subProtoOk: Object.getPrototypeOf(rSub) === customProto,
            setRoErr,
          },
        };
        """,
    )
    assert details["supportedConstraints"] == secure_reference["details"]["supportedConstraints"]
    assert [
        {k: v for k, v in d.items() if k != "caps"} for d in details["devs"]
    ] == secure_reference["details"]["enumerateDevices"]
    assert [d["caps"] for d in details["devs"]] == [{}, {}, None]
    assert (
        details["gdmEmptyErr"]
        == "TypeError:Failed to execute 'getDisplayMedia' on 'MediaDevices': either audio or video must be requested"
    )
    assert details["gdmDenied"] == "NotAllowedError"
    assert details["capWildcardErr"] == "NotSupportedError"
    assert details["sw"] == secure_reference["details"]["sw"]
    assert details["swLocal"] == "TypeError:true"
    assert "ServiceWorkerUpdateViaCache" in details["swBadUvc"]
    assert details["bucket"] == {
        "name": "alpha_1",
        "distinctWrapper": True,
        "est": {"quota": 4096, "usage": 0, "usageDetails": {}},
        "expMatches": True,
        "persisted": False,
        "persist": False,
        "idbTag": "[object IDBFactory]",
        "idbStable": True,
        "cachesTag": "[object CacheStorage]",
        "cachesStable": True,
    }
    assert details["ink"] == {
        "areaIsBody": True,
        "updErr": "TypeError:Failed to execute 'updateInkTrailStartPoint' on 'DelegatedInkTrailPresenter': parameter 1 is not of type 'PointerEvent'.",
    }
    assert details["devicePosture"] == secure_reference["details"]["devicePosture"]
    assert details["vk"] == {
        "rectTag": "[object DOMRect]",
        "rectSame": True,
        "rectJson": {"x": 0, "y": 0, "width": 0, "height": 0, "top": 0, "right": 0, "bottom": 0, "left": 0},
        "vkAfter": True,
    }
    assert details["wco"] == secure_reference["details"]["wco"]
    assert details["domRect"] == {
        "negJson": {"x": 10, "y": 20, "width": -5, "height": -8, "top": 12, "right": 10, "bottom": 20, "left": 5},
        "xNaN": True,
        "heightNegZero": True,
        "topInf": True,
        "leftNaN": True,
        "fromRect": {"x": 3, "y": 4, "width": 5, "height": 6, "top": 4, "right": 8, "bottom": 10, "left": 3},
        "roFromRectTag": "[object DOMRectReadOnly]",
        "protoReads": 1,
        "subProtoOk": True,
        "setRoErr": "TypeError:Illegal invocation",
    }


def test_promise_returning_bom07_methods_reject_on_bad_receiver_and_cpu_tier_is_deterministic():
    with iv8.Page(environment={"hardwareConcurrency": 2}) as low_page:
        low_page.load(HTML, SECURE_BASE)
        assert low_page.eval("navigator.cpuPerformance") == 1
    with iv8.Page(environment={"hardwareConcurrency": 4}) as mid_page:
        mid_page.load(HTML, SECURE_BASE)
        assert mid_page.eval("navigator.cpuPerformance") == 2
    with iv8.Page(environment={"hardwareConcurrency": 8}) as high_page:
        high_page.load(HTML, SECURE_BASE)
        assert high_page.eval("navigator.cpuPerformance") == 4
        high_page.eval(
            """
            globalThis.badRecvResults = [];
            for (const [iface, proto, method] of [
              ['MediaDevices', MediaDevices.prototype, 'enumerateDevices'],
              ['MediaDevices', MediaDevices.prototype, 'getUserMedia'],
              ['MediaDevices', MediaDevices.prototype, 'getDisplayMedia'],
              ['ServiceWorkerContainer', ServiceWorkerContainer.prototype, 'getRegistration'],
              ['ServiceWorkerContainer', ServiceWorkerContainer.prototype, 'getRegistrations'],
              ['ServiceWorkerContainer', ServiceWorkerContainer.prototype, 'register'],
              ['ServiceWorkerRegistration', ServiceWorkerRegistration.prototype, 'unregister'],
              ['ServiceWorkerRegistration', ServiceWorkerRegistration.prototype, 'update'],
              ['StorageBucketManager', StorageBucketManager.prototype, 'keys'],
              ['StorageBucketManager', StorageBucketManager.prototype, 'open'],
              ['StorageBucketManager', StorageBucketManager.prototype, 'delete'],
              ['StorageBucket', StorageBucket.prototype, 'estimate'],
              ['StorageBucket', StorageBucket.prototype, 'persist'],
              ['NavigatorLogin', NavigatorLogin.prototype, 'setStatus'],
              ['NavigatorManagedData', NavigatorManagedData.prototype, 'getManagedConfiguration'],
              ['Ink', Ink.prototype, 'requestPresenter'],
            ]) {
              const p = proto[method].call({});
              p.then(
                () => globalThis.badRecvResults.push('resolved'),
                e => globalThis.badRecvResults.push(
                  e.name === 'TypeError' && e.message === `Failed to execute '${method}' on '${iface}': Illegal invocation`
                )
              );
            }
            """
        )
        high_page.run_until_idle()
        results = evaluate(high_page, "return globalThis.badRecvResults;")
        assert results == [True] * 16


def test_bom07_access_trace_and_watch_apis_monitoring(page):
    page.watch_apis([
        "navigator.mediaDevices.getSupportedConstraints",
        "navigator.serviceWorker.startMessages",
        "navigator.storageBuckets.keys",
        "navigator.login.setStatus",
        "navigator.managed.getManagedConfiguration",
        "navigator.ink.requestPresenter",
        "navigator.virtualKeyboard.show",
        "navigator.windowControlsOverlay.getTitlebarAreaRect",
        "DOMRect.fromRect",
    ])
    page.start_access_trace()
    page.eval(
        """
        let fired = 0;
        navigator.devicePosture.onchange = () => { fired++; };
        navigator.devicePosture.dispatchEvent(new Event('change'));
        void navigator.cpuPerformance;
        navigator.mediaDevices.getSupportedConstraints();
        navigator.serviceWorker.startMessages();
        navigator.storageBuckets.keys();
        navigator.login.setStatus('logged-in');
        navigator.managed.getManagedConfiguration(['a']).catch(() => {});
        navigator.ink.requestPresenter();
        void navigator.devicePosture.type;
        void navigator.virtualKeyboard.boundingRect;
        navigator.virtualKeyboard.show();
        navigator.windowControlsOverlay.getTitlebarAreaRect();
        DOMRect.fromRect({x: 1, y: 2, width: 3, height: 4});
        globalThis.postureFired = fired;
        """
    )
    page.run_until_idle(max_steps=20)
    assert page.eval("postureFired") == 1
    hits = [entry["path"] for entry in page.read_watch_api_hits()]
    for expected in (
        "navigator.mediaDevices.getSupportedConstraints",
        "navigator.serviceWorker.startMessages",
        "navigator.storageBuckets.keys",
        "navigator.login.setStatus",
        "navigator.managed.getManagedConfiguration",
        "navigator.ink.requestPresenter",
        "navigator.virtualKeyboard.show",
        "navigator.windowControlsOverlay.getTitlebarAreaRect",
        "DOMRect.fromRect",
    ):
        assert expected in hits, expected
    access_events = page.read_access_trace(clear=True)["events"]
    gets = [(e["label"], e["member"]) for e in access_events if e["operation"] == "get"]
    assert ("navigator", "cpuPerformance") in gets
    assert ("navigator", "mediaDevices") in gets
    assert ("navigator", "serviceWorker") in gets
    assert all(e.get("source_kind") in {"eval", "timer"} for e in access_events)

    old_md = page.eval("navigator.mediaDevices")
    page.load(HTML, INSECURE_BASE)
    assert not old_md.context_alive
    assert page.eval("('ink' in navigator) && ('windowControlsOverlay' in navigator) && !('mediaDevices' in navigator)")


def test_bom07_codex_review_regressions(page):
    import time

    page.eval(
        """
        globalThis.bom07Review = {};
        (async () => {
          try {
            await navigator.mediaDevices.getUserMedia({video: {width: Symbol()}});
          } catch (e) { bom07Review.gumWidthSym = e.name + ':' + e.message; }

          try {
            await navigator.mediaDevices.getUserMedia({video: {advanced: [{width: Symbol()}]}});
          } catch (e) { bom07Review.gumAdvWidthSym = e.name + ':' + e.message; }

          try {
            await navigator.mediaDevices.getDisplayMedia({video: true, selfBrowserSurface: 'bogus'});
          } catch (e) { bom07Review.gdmBadSelf = e.name + ':' + e.message; }

          try {
            await navigator.mediaDevices.getDisplayMedia({video: true, windowAudio: 'bogus'});
          } catch (e) { bom07Review.gdmBadWinAudio = e.name + ':' + e.message; }

          try {
            await navigator.mediaDevices.getDisplayMedia({video: true, controller: 123});
          } catch (e) { bom07Review.gdmBadCtrl = e.name + ':' + e.message; }

          history.pushState({}, '', '/subdir/page.html');
          try {
            await navigator.serviceWorker.register('worker.js');
          } catch (e) { bom07Review.swAfterPush = e.message; }
          history.replaceState({}, '', '/reference');

          try {
            await navigator.storageBuckets.open('qnull', {quota: null});
          } catch (e) { bom07Review.sbQuotaNull = e.name + ':' + e.message; }

          try {
            await navigator.storageBuckets.open('enull', {expires: null});
          } catch (e) { bom07Review.sbExpiresNull = e.name + ':' + e.message; }

          await navigator.storageBuckets.open('shortlived', {quota: 2048, expires: Date.now() + 60});
        })();
        """
    )
    page.run_until_idle(max_steps=20)
    time.sleep(0.1)
    page.eval(
        """
        (async () => {
          bom07Review.keysAfterExpiry = await navigator.storageBuckets.keys();
          const reopened = await navigator.storageBuckets.open('shortlived');
          bom07Review.reopenedQuota = (await reopened.estimate()).quota;
        })();
        """
    )
    page.run_until_idle(max_steps=20)
    review = evaluate(page, "return globalThis.bom07Review;")
    assert "Cannot convert a Symbol value to a number" in review["gumWidthSym"]
    assert "Cannot convert a Symbol value to a number" in review["gumAdvWidthSym"]
    assert "DisplayMediaIncludeOrExclude" in review["gdmBadSelf"]
    assert "DisplayMediaSystemWindowOrExclude" in review["gdmBadWinAudio"]
    assert "Failed to convert value to 'CaptureController'" in review["gdmBadCtrl"]
    assert (
        "scope ('http://127.0.0.1:59177/subdir/') with script ('http://127.0.0.1:59177/subdir/worker.js')"
        in review["swAfterPush"]
    )
    assert review["sbQuotaNull"] == "TypeError:The bucket's quota cannot equal zero."
    assert review["sbExpiresNull"] == "TypeError:The bucket expiration is invalid."
    assert review["keysAfterExpiry"] == []
    assert review["reopenedQuota"] == 10737418240
