"""Window.history is an observable entry before any History member is read."""

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")


def test_exact_history_entry_read_survives_stop_and_formats_as_text():
    with iv8.Page() as page:
        page.start_access_trace()
        value = page.eval('window.history')
        assert value.context_alive
        page.stop_access_trace()
        report = page.read_access_trace(format='text')
        assert report.splitlines() == [
            'GET window.window -> <Object>',
            'GET window.history -> <HostObject>',
        ]


def test_history_parent_and_child_reads_have_distinct_completed_events():
    with iv8.Page() as page:
        page.start_access_trace()
        assert page.eval('window.history.length', name='history-entry.js') == 1
        events = page.read_access_trace()['events']
        assert [(event['object_type'], event['label'], event['member'], event['operation']) for event in events] == [
            ('window', 'window', 'window', 'get'),
            ('window', 'window', 'history', 'get'),
            ('history', 'history', 'length', 'get'),
        ]
        assert events[0]['value_summary'] == 'object'
        assert events[1]['value_summary'] == 'host_object'
        assert all(event['outcome'] == 'returned' for event in events)
        assert all(event['resource_name'] == 'history-entry.js' for event in events)
        assert events[0]['object_id'] == events[1]['object_id']
        assert events[1]['object_id'] != events[2]['object_id']


def test_history_entry_filter_and_call_only_watch_contract():
    with iv8.Page() as page:
        page.watch_apis(['window.history'])
        page.start_access_trace(object_types=['window'])
        assert page.eval('window.history.length') == 1
        assert [(event['member'], event['operation']) for event in page.read_access_trace()['events']] == [
            ('window', 'get'), ('history', 'get'),
        ]
        assert page.read_watch_api_hits() == []
        page.start_access_trace(object_types=['history'])
        assert page.eval('window.history.length') == 1
        assert [(event['member'], event['operation']) for event in page.read_access_trace()['events']] == [('length', 'get')]


def test_history_entry_shape_identity_and_receiver_exception_are_preserved():
    with iv8.Page() as page:
        page.start_access_trace()
        assert page.eval('''(() => {
            const descriptor = Object.getOwnPropertyDescriptor(window, 'history');
            return history === window.history && descriptor.enumerable && descriptor.configurable &&
                descriptor.set === undefined && descriptor.get.name === 'get history' &&
                descriptor.get.length === 0 && !Object.hasOwn(descriptor.get, 'prototype') &&
                Function.prototype.toString.call(descriptor.get) === 'function get history() { [native code] }';
        })()''')
        page.read_access_trace()
        with pytest.raises(iv8.JSError) as caught:
            page.eval("Object.getOwnPropertyDescriptor(window, 'history').get.call({})")
        assert caught.value.name == 'TypeError'
        events = page.read_access_trace()['events']
        assert [(event['member'], event['operation'], event['outcome']) for event in events] == [
            ('window', 'get', 'returned'), ('history', 'get', 'threw'),
        ]
        assert events[1]['exception_name'] == 'TypeError'


def test_history_entry_trace_uses_new_generation_and_stops_recording():
    with iv8.Page() as page:
        page.start_access_trace()
        page.eval('window.history', name='before.js')
        first = page.read_access_trace()['events'][0]
        page.load('<html><body></body></html>', 'https://history-entry.test/')
        page.read_access_trace()
        page.eval('window.history', name='after.js')
        second = page.read_access_trace()['events'][0]
        assert second['generation'] == first['generation'] + 1
        assert second['object_id'] != first['object_id']
        page.stop_access_trace()
        page.eval('window.history')
        assert page.read_access_trace()['events'] == []
