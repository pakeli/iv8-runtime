"""EPUB named-access regression within the existing four-control DOM slice."""
import json
from pathlib import Path

import pytest
import iv8

pytestmark = pytest.mark.skipif(not getattr(iv8, '_v8_linked', False), reason='requires V8')
BASE = 'http://epub-named.test/'
HTML = '<html><head></head><body></body></html>'


def evaluate(page, script):
    return json.loads(page.eval('JSON.stringify((() => {' + script + '})())'))


@pytest.mark.parametrize('trace', [False, True])
@pytest.mark.parametrize('tag', ['input', 'button', 'select', 'textarea'])
def test_form_control_names_override_inherited_properties(trace, tag):
    with iv8.Page() as page:
        if trace:
            page.start_access_trace()
        page.load(HTML, BASE)
        result = evaluate(page, '''
            const form = document.createElement('form');
            form.id = 'originalForm';
            form.action = '/original';
            document.body.appendChild(form);
            const results = {};
            for (const name of ['action','id','textContent','innerText','appendChild','constructor']) {
                const control = document.createElement(''' + json.dumps(tag) + ''');
                control.name = name;
                Node.prototype.appendChild.call(form, control);
                results[name] = form[name] === control;
                const descriptor = Object.getOwnPropertyDescriptor(form, name);
                results[name + 'Descriptor'] = descriptor.value === control && !descriptor.writable &&
                    !descriptor.enumerable && descriptor.configurable && Object.hasOwn(form, name);
            }
            results.window = window.originalForm === form;
            form.action = '/updated';
            const getter = Object.getOwnPropertyDescriptor(HTMLFormElement.prototype, 'action').get;
            results.setter = getter.call(form).endsWith('/updated');
            return results;
        ''')
        assert all(result.values()), result


def test_named_form_definition_and_assignment_follow_browser():
    with iv8.Page() as page:
        page.load(HTML, BASE)
        result = evaluate(page, '''
            const form = document.createElement('form');
            form.custom = 37;
            const control = document.createElement('input');
            control.name = 'custom';
            form.appendChild(control);
            form.custom = 48;
            const same = form.custom === control;
            let error;
            try { Object.defineProperty(form, 'custom', {value: 13}); }
            catch (caught) { error = caught.name; }
            const deleted = delete form.custom;
            const afterDelete = form.custom === control;
            control.remove();
            return {same, error, deleted, afterDelete, restored: form.custom === undefined};
        ''')
        assert result == {'same': True, 'error': 'TypeError', 'deleted': True,
                          'afterDelete': True, 'restored': True}


def test_form_past_names_follow_ownership():
    with iv8.Page() as page:
        page.load(HTML, BASE)
        assert page.eval('''(() => {
            const form = document.createElement('form');
            const control = document.createElement('input');
            control.name = 'previousName'; form.appendChild(control);
            const initial = form.previousName === control;
            control.name = 'nextName';
            const renamed = form.previousName === control && form.nextName === control;
            control.remove();
            return initial && renamed && form.previousName === undefined && form.nextName === undefined;
        })()''')


def test_window_named_lookup_tracks_live_tree_and_global_overrides():
    with iv8.Page() as page:
        page.load(HTML, BASE)
        assert page.eval('''(() => {
            const node = document.createElement('div'); node.id = 'epubItem';
            if (window.epubItem !== undefined) return false;
            document.body.appendChild(node);
            if (window.epubItem !== node) return false;
            node.id = 'epubRenamed';
            if (window.epubItem !== undefined || window.epubRenamed !== node) return false;
            window.epubRenamed = 19;
            if (window.epubRenamed !== 19) return false;
            delete window.epubRenamed;
            if (window.epubRenamed !== node) return false;
            node.remove();
            return window.epubRenamed === undefined;
        })()''')


def test_window_named_collisions_and_duplicates():
    with iv8.Page() as page:
        page.load(HTML, BASE)
        assert page.eval('''(() => {
            const builtin = window.document;
            const collision = document.createElement('div'); collision.id = 'document';
            document.body.appendChild(collision);
            if (window.document !== builtin) return false;
            const first = document.createElement('div'); first.id = 'epubDuplicate';
            const second = document.createElement('form');
            // The supported form name is represented as an HTML attribute.
            second.setAttribute('name', 'epubDuplicate');
            document.body.appendChild(first); document.body.appendChild(second);
            const collection = window.epubDuplicate;
            if (collection.length !== 2 || collection[0] !== first || collection[1] !== second) return false;
            second.remove();
            return collection.length === 1 && window.epubDuplicate === first;
        })()''')


def test_window_properties_prototype_matches_chrome_fixture():
    reference = json.loads((Path(__file__).parent / 'fixtures/epub_named_properties/chrome.json').read_text())
    with iv8.Page() as page:
        result = evaluate(page, '''
            const named = Object.getPrototypeOf(Window.prototype);
            return {tag:Object.prototype.toString.call(named), ownKeys:Reflect.ownKeys(named).map(String),
                    parentIsEventTarget:Object.getPrototypeOf(named) === EventTarget.prototype};
        ''')
        assert result == reference['result']['windowPrototype']


def test_window_named_lookup_is_traced():
    with iv8.Page() as page:
        page.load('<html><body><div id="epubNamed"></div></body></html>', BASE)
        page.start_access_trace()
        assert page.eval('window.epubNamed === document.getElementById("epubNamed")')
        events = page.read_access_trace()['events']
        assert any(event['object_type'] == 'window' and event['member'] == 'epubNamed'
                   and event['operation'] == 'get' and event['outcome'] == 'returned'
                   for event in events)


@pytest.mark.parametrize('move', [False, True])
def test_form_past_names_drop_at_ownership_change(move):
    with iv8.Page() as page:
        page.load(HTML, BASE)
        assert page.eval("""(() => {
            const form = document.createElement('form');
            const other = document.createElement('form');
            const input = document.createElement('input');
            input.name = 'formerName'; form.appendChild(input);
            if (form.formerName !== input) return false;
            input.name = 'currentName';
            """ + ('other.appendChild(input);' if move else 'input.remove();') + """
            form.appendChild(input);
            return form.formerName === undefined && form.currentName === input;
        })()""")


def test_form_past_name_enumeration_preserves_access_order():
    with iv8.Page() as page:
        page.load(HTML, BASE)
        result = evaluate(page, """
            const form = document.createElement('form');
            const controls = [];
            for (const name of ['oldA','oldB','oldC']) {
                const input = document.createElement('input'); input.name = name;
                form.appendChild(input); controls.push(input);
            }
            void form.oldB; void form.oldA; void form.oldC;
            for (let index = 0; index < controls.length; index++) controls[index].name = 'new' + index;
            return Object.getOwnPropertyNames(form).filter(name => !/^[0-9]+$/.test(name));
        """)
        assert result == ['new0', 'new1', 'new2', 'oldB', 'oldA', 'oldC']



def test_window_named_elements_preserve_inherited_builtins():
    with iv8.Page() as page:
        page.load(HTML, BASE)
        assert page.eval("""(() => {
            for (const name of ['addEventListener','hasOwnProperty','constructor']) {
                const original = window[name];
                const node = document.createElement('div'); node.id = name;
                document.body.appendChild(node);
                if (window[name] !== original) return false;
            }
            return true;
        })()""")


def test_duplicate_named_controls_follow_nearest_form_owner():
    with iv8.Page() as page:
        page.load(HTML, BASE)
        assert page.eval('''(() => {
            const outer = document.createElement('form');
            const inner = document.createElement('form');
            const first = document.createElement('input');
            const second = document.createElement('button');
            const nested = document.createElement('select');
            for (const control of [first, second, nested]) control.name = 'sharedName';
            outer.appendChild(first); outer.appendChild(inner); outer.appendChild(second);
            inner.appendChild(nested); document.body.appendChild(outer);
            const collection = outer.sharedName;
            if (collection.length !== 2 || collection[0] !== first || collection[1] !== second ||
                inner.sharedName !== nested) return false;
            inner.appendChild(second);
            if (collection.length !== 1 || collection[0] !== first || outer.sharedName !== first ||
                inner.sharedName.length !== 2) return false;
            outer.appendChild(second);
            return collection.length === 2 && collection[0] === first && collection[1] === second &&
                   inner.sharedName === nested;
        })()''')


@pytest.mark.parametrize('trace', [False, True])
def test_history_and_named_properties_coexist_in_one_page(trace):
    with iv8.Page() as page:
        if trace:
            page.start_access_trace()
        page.load(f'''<html><body>
            <form id="epubHistoryForm" action="{BASE}submit"><input name="action" id="epubAction"></form>
            <div id="history"></div>
        </body></html>''', BASE)
        result = evaluate(page, '''
            const originalHistory = window.history;
            const form = window.epubHistoryForm;
            const control = document.getElementById('epubAction');
            history.pushState({source:'named'}, '', '/named-next?step=1#state');
            const actionGetter = Object.getOwnPropertyDescriptor(HTMLFormElement.prototype, 'action').get;
            return {
                namedForm: form === document.querySelector('form'),
                namedControl: form.action === control,
                builtinHistory: window.history === originalHistory && history instanceof History,
                state: history.state.source,
                length: history.length,
                url: document.URL,
                action: actionGetter.call(form)
            };
        ''')
        assert result == {
            'namedForm': True, 'namedControl': True, 'builtinHistory': True,
            'state': 'named', 'length': 2,
            'url': BASE + 'named-next?step=1#state', 'action': BASE + 'submit',
        }
        assert page.read_navigation_requests()['requests'] == []


@pytest.mark.parametrize('trace', [False, True])
def test_reload_resets_history_and_form_past_names_together(trace):
    with iv8.Page() as page:
        if trace:
            page.start_access_trace()
        page.load('''<html><body>
            <form id="epubReloadForm"><input name="formerName"></form>
            <div id="oldWindowName"></div>
        </body></html>''', BASE)
        assert page.eval('''(() => {
            const form = window.epubReloadForm;
            const control = form.formerName;
            control.name = 'renamedControl';
            history.pushState({generation:'old'}, '', '/old-generation');
            return form.formerName === control && form.renamedControl === control &&
                   window.oldWindowName === document.getElementById('oldWindowName');
        })()''')
        page.load('''<html><body>
            <form id="epubReloadForm"><input name="freshName"></form>
            <div id="freshWindowName"></div>
        </body></html>''', BASE + 'reloaded')
        assert page.eval('''(() => {
            const form = window.epubReloadForm;
            return form === document.querySelector('form') &&
                   form.freshName === form.querySelector('input') &&
                   form.formerName === undefined && form.renamedControl === undefined &&
                   window.oldWindowName === undefined &&
                   window.freshWindowName === document.getElementById('freshWindowName') &&
                   history.length === 1 && history.state === null &&
                   location.href.endsWith('/reloaded');
        })()''')


@pytest.mark.parametrize('trace', [False, True])
def test_independent_pages_isolate_history_and_named_ownership(trace):
    with iv8.Page() as first, iv8.Page() as second:
        if trace:
            first.start_access_trace()
            second.start_access_trace()
        first.load('''<html><body>
            <form id="sharedForm"><input name="firstControl"></form><div id="firstOnly"></div>
        </body></html>''', BASE)
        second.load('''<html><body>
            <form id="sharedForm"><input name="secondControl"></form><div id="secondOnly"></div>
        </body></html>''', BASE)
        first.eval("history.pushState({owner:'first'}, '', '/first')")
        assert first.eval('''(() => {
            const form = window.sharedForm;
            return form.firstControl === form.querySelector('input') &&
                   form.secondControl === undefined && window.secondOnly === undefined &&
                   window.firstOnly === document.getElementById('firstOnly') &&
                   history.state.owner === 'first' && history.length === 2;
        })()''')
        assert second.eval('''(() => {
            const form = window.sharedForm;
            return form.secondControl === form.querySelector('input') &&
                   form.firstControl === undefined && window.firstOnly === undefined &&
                   window.secondOnly === document.getElementById('secondOnly') &&
                   history.state === null && history.length === 1;
        })()''')
        second.eval("history.replaceState({owner:'second'}, '', '/second')")
        assert first.eval("history.state.owner === 'first' && location.pathname === '/first'")
        assert second.eval("history.state.owner === 'second' && location.pathname === '/second'")


def test_history_and_named_lookup_survive_access_trace_toggle():
    with iv8.Page() as page:
        page.load('''<html><body>
            <form id="traceForm"><input name="action"></form>
        </body></html>''', BASE)
        page.start_access_trace()
        assert page.eval('''window.traceForm === document.querySelector('form') &&
            window.traceForm.action === document.querySelector('input') && history.length === 1''')
        before = page.read_access_trace(clear=False)['events']
        assert before
        page.stop_access_trace()
        page.eval('''document.querySelector('form').id = 'renamedTraceForm';
            history.pushState({tracing:false}, '', '/untraced');''')
        assert page.eval('''window.traceForm === undefined &&
            window.renamedTraceForm.action === document.querySelector('input') &&
            history.state.tracing === false''')
        assert page.read_access_trace(clear=False)['events'] == before
        page.start_access_trace()
        assert page.eval('''window.renamedTraceForm === document.querySelector('form') &&
            history.state.tracing === false''')
        events = page.read_access_trace()['events']
        assert any(event['object_type'] == 'window' and event['member'] == 'renamedTraceForm'
                   and event['operation'] == 'get' for event in events)
        assert any(event['object_type'] == 'history' and event['member'] == 'state'
                   and event['operation'] == 'get' for event in events)
