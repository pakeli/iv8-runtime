"""Presence-01: bounded BOM identities, synthetic events and native on* slots.

Handler names come from the committed Chrome inventory. Identity/frozen-array
and Promise-channel expectations were captured from isolated Chrome 157 on
2026-11-15; synthetic events do not imply trusted input or device events.
"""

import json
import logging
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")
BASE = "https://presence-01.test/"
HTML = '<html><body><div id="parent"><span id="child"></span></div></body></html>'
INVENTORY = Path(__file__).resolve().parents[1] / "docs" / "browser_api_inventory.json"
ELEMENT_HANDLERS = {
    "onbeforecopy", "onbeforecut", "onbeforepaste", "onfullscreenchange",
    "onfullscreenerror", "onsearch", "onwebkitfullscreenchange", "onwebkitfullscreenerror",
}
CONSOLE_METHODS = (
    "clear", "countReset", "dirxml", "groupCollapsed", "profile", "profileEnd", "timeLog", "timeStamp",
)
UA_DATA = {
    "brands": [{"brand": "Chromium", "version": "157"}, {"brand": "Google Chrome", "version": "157"}],
    "mobile": False, "platform": "macOS", "architecture": "arm", "bitness": "64",
    "model": "", "platformVersion": "15.0.0", "uaFullVersion": "157.0.8103.51",
    "fullVersionList": [{"brand": "Chromium", "version": "157.0.8103.51"}], "wow64": False,
}


@pytest.mark.parametrize("tracing", [False, True])
def test_native_invalid_event_has_active_path_and_dispatch_guard(tracing):
    with iv8.Page() as page:
        page.load('<html><body><input required></body></html>', BASE)
        if tracing:
            page.start_access_trace()
        result = evaluate(page, """
            const input = document.querySelector('input');
            let event, activePath = false, unchangedType = false, nestedError;
            input.addEventListener('invalid', current => {
                event = current;
                activePath = current.composedPath().length === 1 && current.composedPath()[0] === input;
                current.initEvent('changed', true, false);
                unchangedType = current.type === 'invalid';
                try { input.dispatchEvent(current); }
                catch (error) { nestedError = error.name; }
            });
            const valid = input.checkValidity();
            return {valid, activePath, unchangedType, nestedError,
                finished:event.currentTarget === null && event.composedPath().length === 0};
        """)
        assert result == {'valid': False, 'activePath': True, 'unchangedType': True,
                          'nestedError': 'InvalidStateError', 'finished': True}


def evaluate(page, body):
    return json.loads(page.eval("JSON.stringify((() => {" + body + "})())"))


@pytest.fixture
def page():
    with iv8.Page() as value:
        value.load(HTML, BASE)
        yield value


@pytest.mark.parametrize("nested", [False, True])
def test_navigator_configuration_and_high_entropy_promise(nested):
    metadata = {
        "userAgent": "Mozilla/5.0 (Macintosh; Intel Mac OS X 15_0) Chrome/157.0.8103.51 Safari/537.36",
        "platform": "MacIntel", "appVersion": "configured app version",
        "deviceMemory": 16.0, "userAgentData": UA_DATA,
    }
    environment = {"navigator": metadata} if nested else metadata
    with iv8.Page(environment=environment) as value:
        value.load(HTML, BASE)
        assert value.eval("navigator.appVersion") == metadata["appVersion"]
        assert value.eval("navigator.deviceMemory") == 16.0
        assert evaluate(value, "return navigator.userAgentData.toJSON();") == {
            "brands": UA_DATA["brands"], "mobile": False, "platform": "macOS",
        }
        assert value.eval("""(() => {
            const promise = navigator.userAgentData.getHighEntropyValues(
                ['architecture', 'bitness', 'model', 'platformVersion', 'uaFullVersion',
                 'fullVersionList', 'wow64', 'unknown-hint', 'architecture']);
            promise.then(result => {globalThis.entropyResult = result;});
            return promise instanceof Promise;
        })()""")
        value.run_jobs()
        expected = dict(UA_DATA)
        assert evaluate(value, "return entropyResult;") == expected


def test_navigator_default_derivation_and_identity_prototypes(page):
    assert page.eval("navigator.appVersion === navigator.userAgent.replace(/^Mozilla\\//, '')")
    assert page.eval("""(() => {
        const data = navigator.userAgentData;
        const activation = navigator.userActivation;
        return data === navigator.userAgentData && activation === navigator.userActivation &&
            Object.getPrototypeOf(data) === NavigatorUAData.prototype &&
            Object.getPrototypeOf(activation) === UserActivation.prototype &&
            !Object.hasOwn(navigator, 'appVersion') && !Object.hasOwn(navigator, 'deviceMemory') &&
            !Object.hasOwn(navigator, 'userAgentData') && !Object.hasOwn(navigator, 'userActivation') &&
            ['appVersion','deviceMemory','userAgentData','userActivation'].every(
                name => typeof Object.getOwnPropertyDescriptor(Navigator.prototype, name).get === 'function') &&
            !activation.isActive && !activation.hasBeenActive;
    })()""")
    page.eval("window.dispatchEvent(new MouseEvent('click'))")
    assert page.eval("!navigator.userActivation.isActive && !navigator.userActivation.hasBeenActive")


def test_ua_brands_are_fresh_frozen_arrays_with_independent_records():
    with iv8.Page(environment={"userAgentData": UA_DATA}) as value:
        assert value.eval("""(() => {
            const data = navigator.userAgentData;
            const first = data.brands;
            if (first === data.brands || !Object.isFrozen(first) || Object.isFrozen(first[0])) return false;
            first[0].brand = 'local mutation';
            const json = data.toJSON();
            json.brands[0].brand = 'JSON mutation';
            return data.brands[0].brand === 'Chromium';
        })()""")


@pytest.mark.parametrize("argument,expected_keys,error_name", [
    ("undefined", None, "TypeError"),
    ("'architecture'", None, "TypeError"),
    ("[]", ["brands", "mobile", "platform"], None),
    ("new Set(['architecture'])", ["architecture", "brands", "mobile", "platform"], None),
])
def test_high_entropy_argument_outcomes_use_promise_channel(page, argument, expected_keys, error_name):
    result = page.eval("""(() => {
        try {
            const result = navigator.userAgentData.getHighEntropyValues(""" + argument + """);
            result.then(value => {globalThis.entropyOutcome = {keys:Object.keys(value).sort()};},
                        error => {globalThis.entropyOutcome = {error:error.name};});
            return result instanceof Promise;
        } catch (error) { return false; }
    })()""")
    assert result is True
    page.run_jobs()
    expected = {"error": error_name} if error_name else {"keys": expected_keys}
    assert evaluate(page, "return entropyOutcome;") == expected


@pytest.mark.parametrize("metadata", [
    {"deviceMemory": 0}, {"deviceMemory": float("nan")},
    {"userAgentData": {"mobile": 1}},
    {"userAgentData": {"brands": [{"brand": "Chromium", "version": 157}]}},
])
def test_invalid_new_environment_metadata_is_rejected(metadata):
    with pytest.raises(TypeError):
        iv8.Page(environment=metadata)


def test_ancestor_origins_is_a_stable_empty_dom_string_list(page):
    assert page.eval("""(() => {
        const origins = location.ancestorOrigins;
        history.pushState(null, '', '/changed');
        return origins === location.ancestorOrigins && origins instanceof DOMStringList &&
            Object.getPrototypeOf(origins) === DOMStringList.prototype && origins.length === 0 &&
            origins.item(0) === null && !origins.contains('https://parent.test') &&
            typeof Object.getOwnPropertyDescriptor(DOMStringList.prototype, 'length').get === 'function' &&
            Object.hasOwn(DOMStringList.prototype, 'item') && Object.hasOwn(DOMStringList.prototype, 'contains');
    })()""")


@pytest.mark.parametrize("target_name,count", [("window", 125), ("document", 117), ("element", 110)])
def test_reference_handlers_have_correct_owner_and_execute_synthetically(page, target_name, count):
    reference = json.loads(INVENTORY.read_text(encoding="utf-8"))["objects"][target_name]
    names = sorted(name for name in set(reference["exposed"] + reference["missing"]) if name.startswith("on"))
    assert len(names) == count
    result = evaluate(page, """
        const kind = """ + json.dumps(target_name) + "; const names = " + json.dumps(names) + ";" + """
        const elementHandlers = new Set(""" + json.dumps(sorted(ELEMENT_HANDLERS)) + """);
        const target = kind === 'window' ? window : kind === 'document' ? document : document.createElement('div');
        const errors = [];
        for (const name of names) {
            const owner = kind === 'window' ? window : kind === 'document' ? Document.prototype :
                elementHandlers.has(name) ? Element.prototype : HTMLElement.prototype;
            const descriptor = Object.getOwnPropertyDescriptor(owner, name);
            if (!descriptor || typeof descriptor.get !== 'function' || typeof descriptor.set !== 'function' ||
                Object.hasOwn(target, name) !== (kind === 'window') || target[name] !== null) {
                errors.push(name + ':owner'); continue;
            }
            let calls = 0;
            const callback = function(event) {
                if (this === target && event.currentTarget === target && event.eventPhase === 2) calls += 1;
            };
            target[name] = callback;
            if (target[name] !== callback) errors.push(name + ':identity');
            target.dispatchEvent(new Event(name.slice(2)));
            target[name] = null;
            target.dispatchEvent(new Event(name.slice(2)));
            if (calls !== 1 || target[name] !== null) errors.push(name + ':dispatch');
        }
        return errors;
    """)
    assert result == []


def test_handler_path_cancellation_and_dispatch_cleanup(page):
    assert page.eval("""(() => {
        const parent = document.getElementById('parent'), child = document.getElementById('child');
        const order = [];
        let validPath = true;
        for (const [target,label] of [[child,'child'],[parent,'parent'],[document,'document'],[window,'window']]) {
            target.onclick = function(event) {
                const path = event.composedPath();
                validPath = validPath && path[0] === child && path.includes(parent) &&
                    path.includes(document) && path[path.length - 1] === window && event.srcElement === child;
                path.length = 0;
                validPath = validPath && event.composedPath().length > 0;
                order.push(label);
                if (target === document) return false;
            };
        }
        const event = new Event('click', {bubbles:true, composed:true, cancelable:true});
        const accepted = child.dispatchEvent(event);
        return validPath && order.join(',') === 'child,parent,document,window' && !accepted &&
            event.defaultPrevented && !event.returnValue && event.composed && event.srcElement === child &&
            event.currentTarget === null && event.eventPhase === Event.NONE &&
            event.composedPath().length === 0 && !event.cancelBubble;
    })()""")


@pytest.mark.parametrize("target", ["window", "document", "document.getElementById('child')"])
def test_stop_immediate_skips_handlers_but_stop_propagation_does_not(page, target):
    assert page.eval("""(() => {
        const target = """ + target + """;
        let calls = 0;
        target.onclick = () => {calls += 1;};
        const immediate = event => event.stopImmediatePropagation();
        target.addEventListener('click', immediate);
        target.dispatchEvent(new Event('click'));
        target.removeEventListener('click', immediate);
        target.addEventListener('click', event => event.stopPropagation());
        target.dispatchEvent(new Event('click'));
        return calls === 1;
    })()""")


def test_event_members_constants_timestamp_and_legacy_initialization(page):
    assert page.eval("""(() => {
        const names = ['composed','timeStamp','srcElement','cancelBubble','returnValue','initEvent','composedPath'];
        if (!names.every(name => Object.hasOwn(Event.prototype, name))) return false;
        for (const [name,value] of [['NONE',0],['CAPTURING_PHASE',1],['AT_TARGET',2],['BUBBLING_PHASE',3]]) {
            if (Event[name] !== value || Event.prototype[name] !== value) return false;
        }
        const event = new Event('original', {composed:true, cancelable:true});
        if (event.isTrusted || !Object.hasOwn(event, 'isTrusted') || !Number.isFinite(event.timeStamp)) return false;
        event.returnValue = false;
        if (!event.defaultPrevented || event.returnValue) return false;
        event.initEvent('changed', true, false);
        return event.type === 'changed' && event.bubbles && !event.cancelable &&
            !event.defaultPrevented && event.returnValue && event.composed;
    })()""")
    page.eval("globalThis.timestampEvent = new Event('timestamp'); globalThis.timestampValue = timestampEvent.timeStamp")
    page.advance(10, max_steps=10)
    assert page.eval("timestampEvent.timeStamp === timestampValue && new Event('later').timeStamp >= timestampValue")


def test_pre_stopped_events_and_initialization_during_dispatch(page):
    assert page.eval("""(() => {
        let calls = 0;
        const event = new Event('click');
        window.onclick = function(current) {
            calls += 1; current.initEvent('changed', true, true);
        };
        event.cancelBubble = true;
        window.dispatchEvent(event);
        if (calls !== 0 || event.cancelBubble) return false;
        window.dispatchEvent(event);
        return calls === 1 && event.type === 'click' && !event.bubbles && !event.cancelable &&
            event.currentTarget === null && event.composedPath().length === 0;
    })()""")


def test_ui_event_inheritance_and_initialization(page):
    assert page.eval("""(() => {
        if (Object.getPrototypeOf(UIEvent.prototype) !== Event.prototype ||
            Object.getPrototypeOf(UIEvent) !== Event) return false;
        for (const constructor of [MouseEvent,KeyboardEvent,FocusEvent]) {
            if (Object.getPrototypeOf(constructor.prototype) !== UIEvent.prototype ||
                Object.getPrototypeOf(constructor) !== UIEvent ||
                !(new constructor('probe') instanceof UIEvent)) return false;
        }
        for (const constructor of [CustomEvent,MessageEvent]) {
            if (Object.getPrototypeOf(constructor.prototype) !== Event.prototype ||
                Object.getPrototypeOf(constructor) !== Event) return false;
        }
        const event = new UIEvent('probe', {view:window, detail:5});
        if (!(event instanceof Event) || event.view !== window || event.detail !== 5 ||
            event.which !== 0 || event.sourceCapabilities !== null) return false;
        event.initUIEvent('changed', true, true, window, 9);
        return event.type === 'changed' && event.bubbles && event.cancelable && event.view === window && event.detail === 9;
    })()""")


def test_new_host_objects_and_handlers_reset_without_cross_page_state(page):
    page.eval("""navigator.userAgentData.localMarker = 1;
        navigator.userActivation.localMarker = 1; location.ancestorOrigins.localMarker = 1;
        window.onclick = () => {}; document.onclick = () => {};
        document.getElementById('child').onclick = () => {};""")
    with iv8.Page() as other:
        assert other.eval("""navigator.userAgentData.localMarker === undefined &&
            navigator.userActivation.localMarker === undefined && location.ancestorOrigins.localMarker === undefined &&
            window.onclick === null && document.onclick === null""")
    page.load(HTML, BASE)
    assert page.eval("""navigator.userAgentData.localMarker === undefined &&
        navigator.userActivation.localMarker === undefined && location.ancestorOrigins.localMarker === undefined &&
        window.onclick === null && document.onclick === null && document.getElementById('child').onclick === null""")


@pytest.mark.parametrize("name", CONSOLE_METHODS)
def test_new_console_methods_execute_without_return_value(page, name):
    assert page.eval("typeof console." + name) == "function"
    assert page.eval("console." + name + "('presence')") is iv8.JSUndefined


def test_console_count_reset_preserves_counting_behavior(page, caplog):
    with caplog.at_level(logging.INFO, logger="iv8.console"):
        assert page.eval("""(() => {
            let conversions = 0;
            const ignored = {toString() {conversions += 1; return 'extra';}};
            console.count('presence', ignored); console.count('presence');
            console.countReset('presence', ignored); console.count('presence');
            return conversions === 0;
        })()""")
    assert [record.getMessage() for record in caplog.records if record.name == "iv8.console"] == [
        "presence: 1", "presence: 2", "presence: 1",
    ]


def test_console_undefined_label_uses_default_counter(page, caplog):
    with caplog.at_level(logging.INFO, logger="iv8.console"):
        page.eval("console.count(); console.count(undefined); console.countReset(undefined); console.count()")
    assert [record.getMessage() for record in caplog.records if record.name == "iv8.console"] == [
        "default: 1", "default: 2", "default: 1",
    ]


def test_console_time_log_uses_virtual_clock_and_preserves_extra_data(page, caplog):
    with caplog.at_level(logging.INFO, logger="iv8.console"):
        page.eval("console.time('timer')")
        page.advance(12, max_steps=10)
        assert page.eval("console.timeLog('timer', 'detail', 7)") is iv8.JSUndefined
        page.eval("console.time('timer')")
        page.advance(5, max_steps=10)
        page.eval("console.timeLog('timer'); console.timeEnd('timer', 'ignored'); console.timeLog('timer')")
        page.eval("console.time(undefined)")
        page.advance(3, max_steps=10)
        page.eval("console.timeLog(undefined, 'default detail'); console.timeEnd()")
        page.load(HTML, BASE)
        page.eval("console.timeLog('timer'); console.timeLog()")
    assert [record.getMessage() for record in caplog.records if record.name == "iv8.console"] == [
        "Timer started: timer", "timer: 12 ms detail 7", "Timer started: timer",
        "timer: 17 ms", "Timer ended: timer", "Timer started: default",
        "default: 3 ms default detail", "Timer ended: default",
    ]


def test_screen_is_extended_is_a_read_only_screen_property(page):
    assert page.eval("""(() => {
        const descriptor = Object.getOwnPropertyDescriptor(Screen.prototype, 'isExtended');
        return screen.isExtended === false && !Object.hasOwn(screen, 'isExtended') &&
            typeof descriptor.get === 'function' && descriptor.set === undefined;
    })()""")


def test_screen_change_handler_only_runs_on_synthetic_dispatch(page):
    assert page.eval("""(() => {
        const descriptor = Object.getOwnPropertyDescriptor(Screen.prototype, 'onchange');
        if (!descriptor || typeof descriptor.get !== 'function' || typeof descriptor.set !== 'function' ||
            screen.onchange !== null) return false;
        let calls = 0;
        screen.onchange = function(event) {
            if (this === screen && event.currentTarget === screen && event.type === 'change') calls += 1;
            return false;
        };
        const event = new Event('change', {cancelable:true});
        const accepted = screen.dispatchEvent(event);
        screen.onchange = null;
        screen.dispatchEvent(new Event('change'));
        return !accepted && event.defaultPrevented && calls === 1 && screen.isExtended === false;
    })()""")


def test_base_event_requires_new_without_writing_to_window(page):
    assert page.eval("""(() => {
        const before = JSON.stringify(Object.getOwnPropertyNames(window));
        let name;
        try { Event('probe'); } catch (error) { name = error.name; }
        return name === 'TypeError' && JSON.stringify(Object.getOwnPropertyNames(window)) === before;
    })()""")


def test_native_load_event_uses_event_prototype_and_clears_dispatch_path(page):
    page.load('''<html><body><script>
        window.onload = function(event) {
            const path = event.composedPath();
            globalThis.nativeLoadEvent = event;
            globalThis.nativeLoadShape = {
                instance:event instanceof Event,
                prototype:Object.getPrototypeOf(event) === Event.prototype,
                type:event.type, target:event.target === window,
                currentTarget:event.currentTarget === window,
                phase:event.eventPhase, path:path.length === 1 && path[0] === window,
                trusted:event.isTrusted, timestamp:Number.isFinite(event.timeStamp)
            };
        };
    </script></body></html>''', BASE)
    assert evaluate(page, "return nativeLoadShape;") == {
        "instance": True, "prototype": True, "type": "load", "target": True,
        "currentTarget": True, "phase": 2, "path": True, "trusted": False, "timestamp": True,
    }
    assert page.eval("nativeLoadEvent.currentTarget === null && nativeLoadEvent.eventPhase === 0 && nativeLoadEvent.composedPath().length === 0")
