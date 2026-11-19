"""Native Window data hooks retain ordinary property semantics in a real Page."""

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")


@pytest.mark.parametrize("tracing", [False, True])
def test_window_data_redefinition_readonly_and_delete_keep_data_semantics(tracing):
    with iv8.Page() as page:
        if tracing:
            page.start_access_trace()
        assert page.eval("""(() => {
            const original = window.open;
            const descriptor = Object.getOwnPropertyDescriptor(window, 'open');
            if (!('value' in descriptor) || descriptor.value !== original ||
                !descriptor.writable || !descriptor.enumerable || !descriptor.configurable) return false;
            const replacement = () => 'replacement';
            Object.defineProperty(window, 'open', {value:replacement});
            if (window.open !== replacement || window.open() !== 'replacement') return false;
            Object.defineProperty(window, 'open', {writable:false});
            if (Reflect.set(window, 'open', original)) return false;
            let strictError;
            try { (function() {'use strict';window.open = original;})(); }
            catch (error) {strictError = error.name;}
            const locked = Object.getOwnPropertyDescriptor(window, 'open');
            return strictError === 'TypeError' && locked.value === replacement &&
                locked.writable === false && locked.configurable &&
                delete window.open && !Object.hasOwn(window, 'open');
        })()""")


@pytest.mark.parametrize("tracing", [False, True])
def test_window_data_inherited_receiver_writes_do_not_modify_global(tracing):
    with iv8.Page() as page:
        if tracing:
            page.start_access_trace()
        assert page.eval("""(() => {
            const original = window.open, child = Object.create(window);
            const replacement = () => 'child';
            if (!Reflect.set(window, 'open', replacement, child) ||
                !Object.hasOwn(child, 'open') || child.open !== replacement || window.open !== original) return false;
            const frozen = Object.freeze(Object.create(window));
            if (Reflect.set(window, 'open', replacement, frozen)) return false;
            let strictError;
            try {(function() {'use strict';frozen.open = replacement;})();}
            catch (error) {strictError = error.name;}
            return strictError === 'TypeError' && window.open === original &&
                !Object.hasOwn(frozen, 'open');
        })()""")


def test_shell_watch_hits_keep_outer_before_nested_call_order():
    with iv8.Page() as page:
        page.eval("cookieStore.addEventListener('change', () => CSS.escape('a b'))")
        page.watch_apis(['cookieStore.dispatchEvent', 'CSS.escape'])
        page.start_access_trace()
        assert page.eval("cookieStore.dispatchEvent(new Event('change'))")
        assert [hit['path'] for hit in page.read_watch_api_hits()] == [
            'cookieStore.dispatchEvent', 'CSS.escape',
        ]
        calls = [event for event in page.read_access_trace()['events'] if event['operation'] == 'call']
        assert [(event['label'], event['member']) for event in calls] == [
            ('window', 'Event'),
            ('cookieStore', 'dispatchEvent'), ('CSS', 'escape'),
        ]
