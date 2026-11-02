"""Step 6b: method-reference reads and call folding."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://access-methods.test/"
HTML = "<html><body></body></html>"


def _load(page):
    page.load(HTML, BASE)


def _events(page):
    return page.read_access_trace(clear=False)["events"]


@on_only
def test_method_reference_reads_are_get_method_and_calls_are_folded():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        assert page.eval(
            "const fn = hostProbe.add; typeof fn;", name="probe.js"
        ) == "function"
        events = _events(page)
        assert [(event["member"], event["operation"]) for event in events] == [
            ("add", "get_method")]
        assert "outcome" not in events[0]
        assert "argument_count" not in events[0]

        # A fresh session: an ordinary call is one call event, not a
        # get_method followed by a call.
        page.start_access_trace()
        assert page.eval("hostProbe.add(2, 3);", name="probe.js") == 5
        events = _events(page)
        assert [(event["member"], event["operation"]) for event in events] == [
            ("add", "call")]
        assert events[0]["outcome"] == "returned"
        assert events[0]["argument_summaries"] == ["number:2", "number:3"]
        assert events[0]["return_summary"] == "number:5"


@on_only
def test_intervening_access_keeps_reference_and_call_separate():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        assert page.eval(
            """
            const fn = hostProbe.echo;
            hostProbe.answer;
            fn.call(hostProbe, 'x');
            """,
            name="probe.js"
        ) == "x"
        events = _events(page)
        assert [(event["member"], event["operation"]) for event in events] == [
            ("echo", "get_method"),
            ("answer", "get"),
            ("echo", "call"),
        ]
        assert events[1]["value_summary"] == "number:42"
        assert events[2]["argument_summaries"] == ["string:1:x"]
        assert events[2]["return_summary"] == "string:1:x"
        assert events[2]["outcome"] == "returned"


@on_only
def test_method_descriptor_identity_override_and_non_function_read():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("hostProbe.echo === hostProbe.echo") is True
        page.start_access_trace()
        assert page.eval(
            """
            (() => {
              const d = Object.getOwnPropertyDescriptor(hostProbe, 'echo');
              const same = hostProbe.echo === hostProbe.echo;
              hostProbe.echo = 5;
              return [typeof d.value, d.writable, d.enumerable, d.configurable,
                      same, typeof hostProbe.echo,
                      hostProbe.echo].join('|');
            })();
            """,
            name="shape.js"
        ) == "function|true|true|true|true|number|5"
        events = _events(page)
        assert [(event["member"], event["operation"]) for event in events] == [
            ("echo", "get_method"),
            ("echo", "get_method"),
            ("echo", "get_method"),
            ("echo", "set"),
        ]
        assert all("outcome" not in event for event in events[:3])
        assert events[3]["value_summary"] == "number:5"
        assert events[3]["outcome"] == "returned"
