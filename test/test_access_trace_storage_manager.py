"""Step 5e: access trace for navigator.storage (StorageManager)."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://access-storage-manager.test/"
HTML = "<html><body></body></html>"


def _load(page):
    page.load(HTML, BASE)


def _events(page):
    return page.read_access_trace(clear=False)["events"]


@on_only
def test_storage_manager_reads_and_calls_are_traced():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval(
            """
            globalThis.estimate = null;
            globalThis.persisted = null;
            globalThis.persist = null;
            navigator.storage.estimate().then(v => { estimate = v; });
            navigator.storage.persisted().then(v => { persisted = v; });
            navigator.storage.persist().then(v => { persist = v; });
            """,
            name="probe.js"
        )
        page.run_jobs()
        assert page.eval("estimate.usage") == 0
        assert page.eval("estimate.quota") == 10737418240
        assert page.eval("persisted") is False
        assert page.eval("persist") is False

        events = _events(page)
        assert [(event["object_type"], event["label"], event["member"],
                 event["operation"]) for event in events] == [
            ("navigator", "navigator", "storage", "get"),
            ("storage_manager", "navigator.storage", "estimate", "call"),
            ("navigator", "navigator", "storage", "get"),
            ("storage_manager", "navigator.storage", "persisted", "call"),
            ("navigator", "navigator", "storage", "get"),
            ("storage_manager", "navigator.storage", "persist", "call"),
        ]
        assert all(event["source_kind"] == "eval" for event in events)
        assert all(event["resource_name"] == "probe.js" for event in events)
        assert len({event["object_id"] for event in events
                    if event["object_type"] == "storage_manager"}) == 1


@on_only
def test_storage_manager_keeps_plain_object_shape_and_identity():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("navigator.storage === navigator.storage") is True
        assert page.eval(
            "navigator.storage.estimate === navigator.storage.estimate"
        ) is True
        assert page.eval(
            "Object.getPrototypeOf(navigator.storage) === Object.prototype"
        ) is True
        assert page.eval("Object.keys(navigator.storage).join(',')") == (
            "estimate,persisted,persist")
        assert page.eval(
            """
            JSON.stringify(
                Object.getOwnPropertyDescriptor(navigator.storage, 'estimate'))
            """
        ) == '{"writable":false,"enumerable":true,"configurable":false}'

        page.start_access_trace()
        assert page.eval(
            """
            navigator.storage = null;
            navigator.storage.estimate = 1;
            [typeof navigator.storage,
             typeof navigator.storage.estimate].join('|');
            """
        ) == "object|function"
        # Each parent read is traced; the rejected read-only writes and the
        # plain-object method write are not.
        assert [(event["object_type"], event["member"], event["operation"])
                for event in _events(page)] == [
            ("navigator", "storage", "get"),
            ("navigator", "storage", "get"),
            ("navigator", "storage", "get")]


@on_only
def test_storage_manager_descriptor_read_evaluates_the_parent_getter():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        descriptor = page.eval(
            """
            (() => {
              const d = Object.getOwnPropertyDescriptor(navigator, 'storage');
              return [typeof d.value, d.writable, d.enumerable,
                      d.configurable,
                      d.value === navigator.storage].join('|');
            })();
            """,
            name="probe.js"
        )
        assert descriptor == "object|false|true|false|true"
        assert [(event["object_type"], event["member"], event["operation"])
                for event in _events(page)] == [
            ("navigator", "storage", "get"),
            ("navigator", "storage", "get")]


@on_only
def test_storage_manager_filter_and_generation():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace(object_types=["storage_manager"])
        page.eval("navigator.storage.estimate();"
                  "navigator.storage.persisted();",
                  name="probe.js")
        events = _events(page)
        assert [(event["object_type"], event["member"]) for event in events] == [
            ("storage_manager", "estimate"),
            ("storage_manager", "persisted"),
        ]
        first_id = events[0]["object_id"]
        assert events[0]["generation"] == 2

        page.load(HTML, BASE)
        page.eval("navigator.storage.persist();", name="probe.js")
        events = [
            event for event in _events(page)
            if event["object_type"] == "storage_manager"
            and event["source_kind"] == "eval"
        ]
        assert [event["generation"] for event in events] == [2, 2, 3]
        assert events[0]["object_id"] == events[1]["object_id"] == first_id
        assert events[2]["object_id"] != first_id
