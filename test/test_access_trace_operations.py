"""Step 6c: delete, enumeration, and define operation events."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://access-operations.test/"
HTML = "<html><body></body></html>"


def _load(page):
    page.load(HTML, BASE)


def _events(page):
    return page.read_access_trace(clear=False)["events"]


def _ops(page):
    return [(event["object_type"], event["member"], event["operation"])
            for event in _events(page)]


@on_only
def test_delete_define_and_enumeration_on_existing_properties():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        assert page.eval(
            """
            (() => {
              const d = Object.getOwnPropertyDescriptor(
                  navigator, 'userAgent');
              const del = delete navigator.userAgent;
              const keys = Object.keys(navigator);
              Object.defineProperty(
                  navigator, 'traceOwn',
                  {value: 7, configurable: true});
              return [typeof d.value, del,
                      keys.indexOf('userAgent') >= 0,
                      navigator.traceOwn].join('|');
            })();
            """,
            name="ops.js"
        ) == "string|false|true|7"
        ops = _ops(page)
        assert ("navigator", "userAgent", "delete") in ops
        assert ("navigator", "", "own_keys") in ops
        assert ("navigator", "traceOwn", "define_property") in ops


@on_only
def test_missing_property_delete_and_define():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        assert page.eval(
            """
            (() => {
              const d = Object.getOwnPropertyDescriptor(
                  navigator, 'missing');
              const del = delete navigator.missing;
              Object.defineProperty(navigator, 'missing',
                                    {value: 1, configurable: true});
              return [d === undefined, del, navigator.missing].join('|');
            })();
            """,
            name="ops.js"
        ) == "true|true|1"
        ops = _ops(page)
        assert ("navigator", "missing", "delete") in ops
        assert ("navigator", "missing", "define_property") in ops


@on_only
def test_operation_identity_and_object_type_filter():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval(
            "delete navigator.sameMissing;"
            "delete screen.sameMissing;",
            name="ops.js"
        )
        deletes = [
            event for event in _events(page)
            if event["operation"] == "delete"
            and event["member"] == "sameMissing"
        ]
        assert [(event["object_type"], event["label"])
                for event in deletes] == [
            ("navigator", "navigator"), ("screen", "screen")]
        assert deletes[0]["object_id"] != deletes[1]["object_id"]

        page.start_access_trace(object_types=["screen"])
        page.eval(
            "delete navigator.sameMissing;"
            "delete screen.sameMissing;",
            name="ops.js"
        )
        assert [
            (event["object_type"], event["member"])
            for event in _events(page)
            if event["operation"] == "delete"
        ] == [("screen", "sameMissing")]


@on_only
def test_enumeration_result_order_and_identity_are_unchanged():
    with iv8.Page() as page:
        _load(page)
        before = page.eval("Object.keys(navigator).join(',')")
        assert "userAgent" in before.split(",")
        page.start_access_trace()
        after = page.eval(
            "for (const k in navigator) {}"
            "Object.keys(navigator).join(',');",
            name="ops.js"
        )
        assert after == before
        assert "userAgent" in after.split(",")
        own_keys = [event for event in _events(page)
                    if event["operation"] == "own_keys"]
        assert own_keys
        assert all(event["member"] == "" for event in own_keys)
        assert all(event["object_type"] == "navigator"
                   for event in own_keys)


@on_only
def test_method_delete_and_redefine_events():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        assert page.eval(
            """
            (() => {
              const d = Object.getOwnPropertyDescriptor(hostProbe, 'echo');
              const del = delete hostProbe.echo;
              Object.defineProperty(hostProbe, 'echo',
                                    {value: 9, configurable: true});
              return [typeof d.value, del, hostProbe.echo].join('|');
            })();
            """,
            name="ops.js"
        ) == "function|true|9"
        ops = _ops(page)
        assert ("host_probe", "echo", "get_method") in ops
        assert ("host_probe", "echo", "delete") in ops
        assert ("host_probe", "echo", "define_property") in ops
