"""Regression checks for native callback shapes observed by the EPUB logger."""
import json
from pathlib import Path

import pytest
import iv8

pytestmark = pytest.mark.skipif(not getattr(iv8, '_v8_linked', False), reason='requires V8')

EXPRESSIONS = [
    ('Object.getOwnPropertyDescriptor', 'getOwnPropertyDescriptor', 2),
    ('Object.hasOwn', 'hasOwn', 2),
    ('Object.prototype.hasOwnProperty', 'hasOwnProperty', 1),
    ('Object.keys', 'keys', 1),
    ('Reflect.get', 'get', 2),
    ('Reflect.has', 'has', 2),
    ('Reflect.ownKeys', 'ownKeys', 1),
    ('Reflect.getOwnPropertyDescriptor', 'getOwnPropertyDescriptor', 2),
]


def shape(page, expression):
    return json.loads(page.eval('''JSON.stringify((f => ({
        name:f.name, length:f.length, source:Function.prototype.toString.call(f),
        prototype:Object.hasOwn(f, 'prototype')
    }))(''' + expression + '))'))


def expected(name, length, prototype=False):
    return {'name':name, 'length':length,
            'source':f'function {name}() {{ [native code] }}', 'prototype':prototype}


@pytest.mark.parametrize('mode', ['trace', 'get', 'has', 'ownkeys', 'keys', 'all'])
@pytest.mark.parametrize('before_load', [False, True])
def test_monitoring_preserves_builtin_shapes(mode, before_load):
    with iv8.Page() as page:
        def enable():
            if mode in ('trace', 'all'): page.start_access_trace()
            if mode in ('get', 'all'): page.watch_reflect_get(True)
            if mode in ('has', 'all'): page.watch_reflect_has(True)
            if mode in ('ownkeys', 'all'): page.watch_reflect_ownkeys(True)
            if mode in ('keys', 'all'): page.watch_object_keys(True)
        if before_load: enable()
        page.load('<html><body></body></html>', 'https://epub.test/')
        if not before_load: enable()
        for expression, name, length in EXPRESSIONS:
            assert shape(page, expression) == expected(name, length), expression
            assert page.eval(f'''(() => {{
                try {{
                    Reflect.construct(String, [], {expression});
                    return false;
                }} catch (error) {{
                    return error instanceof TypeError;
                }}
            }})()''')
        if mode in ('trace', 'all'): page.stop_access_trace()
        if mode in ('get', 'all'): page.watch_reflect_get(False)
        if mode in ('has', 'all'): page.watch_reflect_has(False)
        if mode in ('ownkeys', 'all'): page.watch_reflect_ownkeys(False)
        if mode in ('keys', 'all'): page.watch_object_keys(False)
        for expression, name, length in EXPRESSIONS:
            assert shape(page, expression) == expected(name, length), expression


@pytest.mark.parametrize('expression,name,length', [
    ('performance.now','now',0), ('crypto.getRandomValues','getRandomValues',1),
])
def test_utility_callback_shapes(expression, name, length):
    with iv8.Page() as page:
        assert shape(page, expression) == expected(name, length)


@pytest.mark.parametrize('name', ['Event','CustomEvent','MouseEvent','KeyboardEvent','FocusEvent','MessageEvent'])
def test_event_constructor_shapes(name):
    reference = json.loads((Path(__file__).parent/'fixtures/epub_bindings/events.json').read_text())
    with iv8.Page() as page:
        assert shape(page, name) == reference[name]
        assert page.eval(f'(new {name}("probe")).type') == 'probe'


def set_monitors(page, enabled, order=('trace', 'get', 'has', 'ownkeys', 'keys')):
    for channel in order:
        if channel == 'trace':
            if enabled:
                page.start_access_trace()
            else:
                page.stop_access_trace()
        else:
            method = {
                'get': page.watch_reflect_get,
                'has': page.watch_reflect_has,
                'ownkeys': page.watch_reflect_ownkeys,
                'keys': page.watch_object_keys,
            }[channel]
            method(enabled)


def take_watch_hits(page):
    return [
        page.read_reflect_get_hits(), page.read_reflect_has_hits(),
        page.read_reflect_ownkeys_hits(), page.read_object_keys_hits(),
    ]


@pytest.mark.parametrize('order', [
    ('trace', 'get', 'has', 'ownkeys', 'keys'),
    ('get', 'has', 'ownkeys', 'keys', 'trace'),
    ('has', 'trace', 'keys', 'get', 'ownkeys'),
])
def test_monitoring_install_order_preserves_shapes_and_single_hits(order):
    with iv8.Page() as page:
        page.load('<html><body></body></html>', 'https://epub.test/')
        set_monitors(page, True, order)
        set_monitors(page, True, tuple(reversed(order)))
        for expression, name, length in EXPRESSIONS:
            assert shape(page, expression) == expected(name, length)
        take_watch_hits(page)
        result = json.loads(page.eval('''JSON.stringify((() => {
            const target = {value:7};
            return [Reflect.get(target, 'value'), Reflect.has(target, 'value'),
                    Reflect.ownKeys(target), Object.keys(target)];
        })())'''))
        assert result == [7, True, ['value'], ['value']]
        assert [len(hits) for hits in take_watch_hits(page)] == [1, 1, 1, 1]


def test_wrappers_preserve_proxy_delegation_receiver_and_exceptions():
    probe = '''JSON.stringify((() => {
        const calls = [];
        const symbol = Symbol('binding');
        const target = {visible:7};
        Object.defineProperty(target, 'hidden', {value:9});
        Object.defineProperty(target, 'accessor', {
            configurable:true, get() { calls.push('getter'); return this.receiverValue; }
        });
        target[symbol] = 11;
        const proxy = new Proxy(target, {
            get(object, key, receiver) {
                calls.push('get:' + String(key)); return Reflect.get(object, key, receiver);
            },
            has(object, key) { calls.push('has:' + String(key)); return Reflect.has(object, key); },
            ownKeys(object) { calls.push('ownKeys'); return Reflect.ownKeys(object); },
            getOwnPropertyDescriptor(object, key) {
                calls.push('descriptor:' + String(key)); return Reflect.getOwnPropertyDescriptor(object, key);
            }
        });
        const result = {
            receiver:Reflect.get(proxy, 'accessor', {receiverValue:41}),
            symbolValue:Reflect.get(proxy, symbol),
            hasSymbol:Reflect.has(proxy, symbol),
            keys:Object.keys(proxy), ownKeys:Reflect.ownKeys(proxy).map(String),
            objectDescriptor:Object.getOwnPropertyDescriptor(proxy, 'hidden').value,
            reflectDescriptor:Reflect.getOwnPropertyDescriptor(proxy, 'hidden').value,
            ownSymbol:Object.hasOwn(proxy, symbol),
            inheritedMethod:Object.prototype.hasOwnProperty.call(proxy, 'hidden')
        };
        const sentinel = new Error('delegate sentinel');
        const throwing = new Proxy({}, { get() { throw sentinel; } });
        try { Reflect.get(throwing, 'failure'); }
        catch (error) { result.sameException = error === sentinel; }
        result.typeErrors = [];
        for (const invoke of [() => Reflect.get(null, 'value'),
                              () => Reflect.has(null, 'value'),
                              () => Reflect.ownKeys(null), () => Object.keys(null),
                              () => Object.getOwnPropertyDescriptor(null, 'value'),
                              () => Reflect.getOwnPropertyDescriptor(null, 'value'),
                              () => Object.hasOwn(null, 'value')]) {
            try { invoke(); result.typeErrors.push(false); }
            catch (error) { result.typeErrors.push(error instanceof TypeError); }
        }
        result.calls = calls;
        return result;
    })())'''
    with iv8.Page() as baseline:
        original = json.loads(baseline.eval(probe))
    with iv8.Page() as page:
        set_monitors(page, True)
        assert json.loads(page.eval(probe)) == original
        assert original['receiver'] == 41 and original['sameException'] is True
        assert all(original['typeErrors'])


def test_cached_native_wrappers_delegate_while_disabled_and_reenable_once():
    with iv8.Page() as page:
        set_monitors(page, True)
        page.eval('''globalThis.cachedGet = Reflect.get; globalThis.cachedHas = Reflect.has;
            globalThis.cachedOwnKeys = Reflect.ownKeys; globalThis.cachedKeys = Object.keys;''')
        for expression, name, length in (
            ('cachedGet', 'get', 2), ('cachedHas', 'has', 2),
            ('cachedOwnKeys', 'ownKeys', 1), ('cachedKeys', 'keys', 1),
        ):
            assert shape(page, expression) == expected(name, length)
        take_watch_hits(page)
        set_monitors(page, False)
        operation = '''JSON.stringify([cachedGet({value:7}, 'value'), cachedHas({value:7}, 'value'),
            cachedOwnKeys({value:7}), cachedKeys({value:7})])'''
        assert json.loads(page.eval(operation)) == [7, True, ['value'], ['value']]
        assert take_watch_hits(page) == [[], [], [], []]
        set_monitors(page, True, ('keys', 'ownkeys', 'has', 'get', 'trace'))
        assert page.eval('''cachedGet === Reflect.get && cachedHas === Reflect.has &&
            cachedOwnKeys === Reflect.ownKeys && cachedKeys === Object.keys''')
        assert json.loads(page.eval(operation)) == [7, True, ['value'], ['value']]
        assert [len(hits) for hits in take_watch_hits(page)] == [1, 1, 1, 1]


def test_monitoring_shapes_and_independent_channel_state_survive_reload():
    html = '''<html><body><script>
        Reflect.get({value:1}, 'value'); Reflect.has({value:1}, 'value');
        Reflect.ownKeys({value:1}); Object.keys({value:1});
    </script></body></html>'''
    with iv8.Page() as page:
        set_monitors(page, True)
        for generation, expected_counts in enumerate(([1, 1, 1, 1], [0, 1, 1, 0], [1, 1, 1, 1])):
            if generation == 1:
                page.watch_reflect_get(False)
                page.watch_object_keys(False)
            elif generation == 2:
                page.watch_reflect_get(True)
                page.watch_object_keys(True)
            page.load(html, 'https://epub.test/generation-' + str(generation))
            assert [len(hits) for hits in take_watch_hits(page)] == expected_counts
            for expression, name, length in EXPRESSIONS:
                assert shape(page, expression) == expected(name, length)


@pytest.mark.parametrize('before_load', [False, True])
def test_monitoring_bindings_coexist_with_history_named_forms_and_events(before_load):
    with iv8.Page() as page:
        if before_load:
            set_monitors(page, True)
        page.load('''<html><body><form id="bindingForm"><input name="action"></form></body></html>''',
                  'https://epub.test/')
        if not before_load:
            set_monitors(page, True)
        result = json.loads(page.eval('''JSON.stringify((() => {
            const form = Reflect.get(window, 'bindingForm');
            const control = document.querySelector('input');
            let received;
            form.addEventListener('binding-probe', event => {
                received = event.detail.value; event.preventDefault();
            });
            const event = new CustomEvent('binding-probe', {detail:{value:7}, cancelable:true});
            history.pushState({formId:form.id}, '', '/combined');
            const objectDescriptor = Object.getOwnPropertyDescriptor(form, 'action');
            const reflectDescriptor = Reflect.getOwnPropertyDescriptor(form, 'action');
            return {
                windowNamed:form === document.querySelector('form'),
                controlNamed:Reflect.get(form, 'action') === control,
                has:Reflect.has(form, 'action'), own:Object.hasOwn(form, 'action'),
                prototypeOwn:Object.prototype.hasOwnProperty.call(form, 'action'),
                descriptor:objectDescriptor.value === control && reflectDescriptor.value === control &&
                    !objectDescriptor.writable && !objectDescriptor.enumerable && objectDescriptor.configurable,
                namedEnumeration:Reflect.ownKeys(form).includes('action') && !Object.keys(form).includes('action'),
                canceled:form.dispatchEvent(event) === false && event.defaultPrevented,
                received, state:history.state.formId, length:history.length, path:location.pathname
            };
        })())'''))
        assert result == {
            'windowNamed': True, 'controlNamed': True, 'has': True, 'own': True,
            'prototypeOwn': True, 'descriptor': True, 'namedEnumeration': True,
            'canceled': True, 'received': 7, 'state': 'bindingForm', 'length': 2, 'path': '/combined',
        }
        for expression, name, length in EXPRESSIONS:
            assert shape(page, expression) == expected(name, length)
        events = page.read_access_trace()['events']
        assert any(event['object_type'] == 'window' and event['member'] == 'bindingForm'
                   and event['operation'] == 'get' for event in events)
        assert any(event['object_type'] == 'history' and event['member'] == 'pushState'
                   and event['operation'] == 'call' for event in events)


@pytest.mark.parametrize('mode', ['none', 'trace', 'all'])
def test_all_binding_shapes_match_current_chrome_capture(mode):
    reference = json.loads((Path(__file__).parent / 'fixtures/epub_bindings/chrome_current.json').read_text())
    with iv8.Page() as page:
        page.load('<html><body></body></html>', 'https://epub-bindings.test/')
        if mode == 'trace':
            page.start_access_trace()
        elif mode == 'all':
            set_monitors(page, True)
        for expression, captured in reference['functions'].items():
            actual = json.loads(page.eval('''JSON.stringify((fn => ({
                name:fn.name, length:fn.length, source:Function.prototype.toString.call(fn),
                prototype:Object.hasOwn(fn, 'prototype'),
                constructable:(() => {
                    try { Reflect.construct(String, [], fn); return true; }
                    catch (error) { if (!(error instanceof TypeError)) throw error; return false; }
                })()
            }))(''' + expression + '))'))
            assert actual == captured, expression
