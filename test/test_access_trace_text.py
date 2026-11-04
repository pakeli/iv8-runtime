"""Readable access logs: pure metadata formatting plus real-V8 integration."""

import copy
import json

import pytest

import iv8
from iv8._trace_format import format_access_trace


on_only = pytest.mark.skipif(not iv8._v8_linked, reason="requires a V8-linked build")


def _event(**changes):
    event = {
        "seq": 1, "generation": 1, "object_id": 1,
        "object_type": "document", "label": "document", "member": "cookie",
        "operation": "get", "phase": "enter", "resource_name": "<eval>",
        "source_kind": "eval", "outcome": "returned", "value_summary": "string:0:",
    }
    event.update(changes)
    return event


def _report(*events, dropped=0):
    return {
        "events": list(events), "enabled": True, "generation": 1,
        "max_events": 10, "dropped_events": dropped,
        "filter_object_types": None, "coverage": {"not_covered": ["in"]},
    }


@pytest.mark.parametrize("summary,expected", [
    ("string:0:", '""'),
    ("string:3:a:b", '"a:b"'),
    ("string:3:值", '"值"'),
    (r"string:1:\n", r'"\n"'),
    (r"string:2:\\n", r'"\\n"'),
    (r"string:3:\x00hi", r'"\u0000hi"'),
    (r"string:1:\x7f", r'"\u007f"'),
    ('string:1:"', r'"\""'),
    ("number:0", "0"), ("number:-0", "-0"), ("number:2.5", "2.5"),
    ("number:nan", "NaN"), ("number:inf", "Infinity"),
    ("number:-inf", "-Infinity"),
    ("boolean:true", "true"), ("boolean:false", "false"),
    ("null", "null"), ("undefined", "undefined"),
    ("array", "<Array>"), ("object", "<Object>"),
    ("function", "<Function>"), ("host_object", "<HostObject>"),
    ("bigint", "<BigInt>"), ("symbol", "<Symbol>"),
])
def test_values_are_readable_and_distinct(summary, expected):
    assert format_access_trace(_report(_event(value_summary=summary))) == \
        "GET document.cookie -> " + expected


def test_original_user_event_and_report_are_not_modified():
    report = _report(_event())
    before = copy.deepcopy(report)
    assert format_access_trace(report) == 'GET document.cookie -> ""'
    assert report == before


def test_64_byte_real_ellipsis_and_67_byte_truncation_are_distinct():
    real = "a" * 61 + "..."
    truncated = "a" * 64 + "..."
    assert format_access_trace(_report(_event(value_summary="string:64:" + real))) == \
        'GET document.cookie -> "' + real + '"'
    assert format_access_trace(_report(_event(value_summary="string:67:" + truncated))) == \
        'GET document.cookie -> "' + "a" * 64 + '" … <truncated, 67 bytes>'


def test_multibyte_truncation_and_omitted_preview_are_explicit():
    assert format_access_trace(_report(_event(value_summary="string:67:" + "值" * 21 + "..."))) == \
        'GET document.cookie -> "' + "值" * 21 + '" … <truncated, 67 bytes>'
    assert format_access_trace(_report(_event(value_summary="string:5000:..."))) == \
        "GET document.cookie -> <String: 5000 bytes; preview not recorded>"


@pytest.mark.parametrize("summary", ["string:bad:x", "string:3", "string:-1:x", "string:8:...", "future:type"])
def test_unrecognized_or_incomplete_summaries_are_not_invented(summary):
    assert "<summary: " in format_access_trace(_report(_event(value_summary=summary)))


def test_paths_controls_and_values_stay_on_one_line():
    text = format_access_trace(_report(_event(
        label="document\nforged", member='a.b"\n',
        value_summary="string:3:\u2028")))
    assert text == r'GET document\nforged["a.b\"\n"] -> "\u2028"'
    assert len(text.splitlines()) == 1
    for character in ("\x85", "\u2028", "\u2029"):
        value = character.encode("utf-8")
        output = format_access_trace(_report(_event(value_summary=f"string:{len(value)}:" + character)))
        assert len(output.splitlines()) == 1


def test_call_arguments_values_and_argument_limit():
    event = _event(operation="call", label="hostProbe", member="add",
                   argument_count=2, argument_summaries=["number:2", "number:3"],
                   return_summary="number:5")
    assert format_access_trace(_report(event)) == "CALL hostProbe.add(2, 3) -> 5"
    event.update(argument_count=10, argument_summaries=["null"] * 8)
    assert "… <2 more arguments>" in format_access_trace(_report(event))
    event.update(argument_count=0, argument_summaries=[])
    assert "hostProbe.add()" in format_access_trace(_report(event))


def test_absent_values_and_returns_are_not_undefined():
    event = _event()
    del event["value_summary"]
    assert format_access_trace(_report(event)) == "GET document.cookie -> <value not recorded>"
    event.update(operation="call")
    assert format_access_trace(_report(event)) == \
        "CALL document.cookie(<arguments not recorded>) -> <return value not recorded>"


def test_setter_is_an_assignment_and_incomplete_attempt_is_marked():
    event = _event(operation="set", value_summary="string:3:a=1")
    assert format_access_trace(_report(event)) == 'SET document.cookie = "a=1"'
    del event["outcome"]
    assert format_access_trace(_report(event)).endswith(" [completion not recorded]")


def test_throwing_operations_show_exception_without_duplicate_prefix_or_newline():
    event = _event(operation="set", value_summary="object", outcome="threw",
                   exception_name="TypeError", exception_message="Uncaught TypeError: bad\nvalue")
    assert format_access_trace(_report(event)) == \
        r"SET document.cookie = <Object> !! TypeError: bad\nvalue"


@pytest.mark.parametrize("operation,expected", [
    ("has", "HAS document.cookie -> true"),
    ("get_own_property_descriptor", "DESCRIBE document.cookie -> true"),
    ("delete", "DELETE document.cookie -> true"),
    ("own_keys", "KEYS document -> true"),
    ("define_property", "DEFINE document.cookie"),
    ("get_method", "METHOD document.cookie"),
])
def test_other_operations_remain_identifiable(operation, expected):
    assert format_access_trace(_report(_event(operation=operation, return_summary="boolean:true"))) == expected


def test_missing_probe_is_distinct_from_captured_undefined():
    event = _event(operation="get_probe", resolution="missing")
    assert format_access_trace(_report(event)) == "GET document.cookie -> <missing>"
    assert format_access_trace(_report(_event(value_summary="undefined"))) == \
        "GET document.cookie -> undefined"


def test_empty_trace_and_capacity_warning():
    assert format_access_trace(_report()) == ""
    assert format_access_trace(_report(dropped=3)) == \
        "WARNING: 3 trace event(s) dropped (capacity 10)."


class _NativeTrace:
    def __init__(self):
        self.report = _report(_event())
        self.calls = []

    def read_access_trace(self, clear):
        self.calls.append(clear)
        snapshot = copy.deepcopy(self.report)
        if clear:
            self.report["events"].clear()
        return snapshot


def _stub_page():
    page = object.__new__(iv8.Page)
    page._native = _NativeTrace()
    return page


def test_page_default_raw_aggregate_and_text_keep_read_clear_contract():
    page = _stub_page()
    assert page.read_access_trace(clear=False) == page._native.report
    assert page.access_trace_report()["events"]["count"] == 1
    assert page.read_access_trace(clear=False, format="text") == 'GET document.cookie -> ""'
    assert page.read_access_trace(format="text") == 'GET document.cookie -> ""'
    assert page.read_access_trace()["events"] == []
    assert page._native.calls == [False, False, False, True, True]


@pytest.mark.parametrize("kwargs,error", [
    ({"format": None}, TypeError), ({"format": True}, TypeError),
    ({"format": 1}, TypeError), ({"format": "json"}, ValueError),
    ({"clear": "yes", "format": "text"}, TypeError),
])
def test_validation_happens_before_native_read_or_clear(kwargs, error):
    page = _stub_page()
    with pytest.raises(error):
        page.read_access_trace(**kwargs)
    assert page._native.calls == []
    assert len(page._native.report["events"]) == 1


@on_only
def test_native_cookie_user_workflow_and_raw_compatibility():
    with iv8.Page() as page:
        page.start_access_trace()
        page.eval("document.cookie; document.cookie = 'a=1'; document.cookie")
        expected = 'GET document.cookie -> ""\nSET document.cookie = "a=1"\nGET document.cookie -> "a=1"'
        assert page.read_access_trace(clear=False, format="text") == expected
        assert page.access_trace_report()["events"]["count"] == 3
        raw = page.read_access_trace(clear=False)
        assert raw["events"][0]["value_summary"] == "string:0:"
        assert page.read_access_trace(format="text") == expected
        assert page.read_access_trace()["events"] == []


@on_only
def test_native_calls_missing_and_method_reference():
    with iv8.Page() as page:
        page.start_access_trace()
        page.eval("hostProbe.add(2, 3); hostProbe.echo(null); document.notThere; hostProbe.echo;")
        assert page.read_access_trace(format="text").splitlines() == [
            "CALL hostProbe.add(2, 3) -> 5", "CALL hostProbe.echo(null) -> null",
            "GET document.notThere -> <missing>", "METHOD hostProbe.echo",
        ]


@on_only
def test_native_truncation_and_real_ellipsis():
    with iv8.Page() as page:
        page.start_access_trace()
        page.eval('hostProbe.echo("a".repeat(67)); hostProbe.echo("a".repeat(61) + "...");'
                  'hostProbe.echo("值".repeat(21) + "😀"); hostProbe.echo("x".repeat(5000));')
        lines = page.read_access_trace(format="text").splitlines()
        assert lines[0].count("<truncated, 67 bytes>") == 2
        assert "<truncated" not in lines[1]
        assert lines[2].count('"' + "值" * 21 + '" … <truncated, 67 bytes>') == 2
        assert lines[3].count("<String: 5000 bytes; preview not recorded>") == 2


@on_only
def test_native_throw_is_readable_and_formatting_does_not_execute_getters():
    with iv8.Page() as page:
        page.eval("globalThis.reads = 0; globalThis.payload = {get danger() { reads++; return 1; }}")
        page.start_access_trace()
        page.eval("hostProbe.echo(payload)")
        assert page.read_access_trace(clear=False, format="text") == \
            "CALL hostProbe.echo(<Object>) -> <Object>"
        assert page.eval("reads") == 0
        page.start_access_trace()
        with pytest.raises(iv8.JSError):
            page.eval("hostProbe.add({valueOf() {throw new RangeError('log\\nboom')}}, 2)")
        output = page.read_access_trace(format="text")
        assert "CALL hostProbe.add(<Object>, 2) !! RangeError:" in output
        assert "log\\nboom" in output
        assert len(output.splitlines()) == 1


@on_only
def test_native_capacity_and_dispose():
    page = iv8.Page()
    try:
        page.start_access_trace(max_events=1)
        page.eval("document.cookie; document.cookie;")
        assert page.read_access_trace(format="text").splitlines() == [
            'GET document.cookie -> ""', 'WARNING: 1 trace event(s) dropped (capacity 1).',
        ]
    finally:
        page.dispose()
    with pytest.raises(iv8.JSContextDisposedError):
        page.read_access_trace(format="text")
