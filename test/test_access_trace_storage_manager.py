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
            ("window", "window", "estimate", "get_probe"),
            ("window", "window", "persisted", "get_probe"),
            ("window", "window", "persist", "get_probe"),
            ("window", "window", "navigator", "get"),
            ("navigator", "navigator", "storage", "get"),
            ("storage_manager", "navigator.storage", "estimate", "get"),
            ("storage_manager", "navigator.storage", "estimate", "call"),
            ("window", "window", "navigator", "get"),
            ("navigator", "navigator", "storage", "get"),
            ("storage_manager", "navigator.storage", "persisted", "get"),
            ("storage_manager", "navigator.storage", "persisted", "call"),
            ("window", "window", "navigator", "get"),
            ("navigator", "navigator", "storage", "get"),
            ("storage_manager", "navigator.storage", "persist", "get"),
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
        # Parent reads and the final method reference are observable. Rejected
        # read-only assignments do not create native set events.
        assert [(event["object_type"], event["member"], event["operation"])
                for event in _events(page)] == [
            ("window", "navigator", "get"),
            ("window", "navigator", "get"),
            ("navigator", "storage", "get"),
            ("window", "navigator", "get"),
            ("navigator", "storage", "get"),
            ("window", "navigator", "get"),
            ("navigator", "storage", "get"),
            ("storage_manager", "estimate", "get")]


@on_only
def test_storage_manager_descriptor_does_not_evaluate_the_parent_getter():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        descriptor = page.eval(
            """
            (() => {
              const own = Object.getOwnPropertyDescriptor(navigator, 'storage');
              const d = Object.getOwnPropertyDescriptor(Navigator.prototype, 'storage');
              return [own === undefined, typeof d.get, d.enumerable,
                      d.configurable,
                      d.get.call(navigator) === navigator.storage].join('|');
            })();
            """,
            name="probe.js"
        )
        assert descriptor == "true|function|true|true|true"
        assert [(event["object_type"], event["member"], event["operation"])
                for event in _events(page)] == [
            ("window", "navigator", "get"),
            ("navigator", "storage", "get_own_property_descriptor"),
            ("window", "Navigator", "get"),
            ("window", "navigator", "get"),
            ("navigator", "storage", "get"),
            ("window", "navigator", "get"),
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
        assert [(event["object_type"], event["member"], event["operation"]) for event in events] == [
            ("storage_manager", "estimate", "get"), ("storage_manager", "estimate", "call"),
            ("storage_manager", "persisted", "get"), ("storage_manager", "persisted", "call"),
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
        assert [(event["generation"], event["operation"]) for event in events] == [
            (2, "get"), (2, "call"), (2, "get"), (2, "call"), (3, "get"), (3, "call")]
        assert {event["object_id"] for event in events[:4]} == {first_id}
        assert events[4]["object_id"] == events[5]["object_id"] != first_id
