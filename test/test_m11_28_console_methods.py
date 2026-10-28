"""M11-28 acceptance tests: extended console methods."""

import logging

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-28.test/"
METHODS = ("debug", "trace", "table", "dir", "group", "groupEnd",
           "time", "timeEnd", "assert", "count")


@on_only
def test_methods_exist_and_return_undefined():
    with iv8.Page() as page:
        for method in METHODS:
            assert page.eval(f"typeof console.{method}") == "function", method
        assert page.eval("console.debug('x')") is iv8.JSUndefined
        assert page.eval("console.groupEnd()") is iv8.JSUndefined


@on_only
def test_levels_and_messages(caplog):
    with caplog.at_level(logging.DEBUG, logger="iv8.console"):
        with iv8.Page() as page:
            page.eval("console.debug('debug message')")
            page.eval("console.trace('trace message')")
            page.eval("console.table({a: 1})")
            page.eval("console.dir('dir message')")
            page.eval("console.group('group message')")
            page.eval("console.groupEnd()")
            page.eval("console.time('t')")
            page.eval("console.timeEnd('t')")
    records = [(r.levelno, r.getMessage()) for r in caplog.records
               if r.name == "iv8.console"]
    assert (logging.DEBUG, "debug message") in records
    assert (logging.DEBUG, "trace message") in records
    assert (logging.INFO, "[object Object]") in records
    assert (logging.INFO, "dir message") in records
    assert (logging.INFO, "group message") in records
    assert (logging.INFO, "Timer started: t") in records
    assert (logging.INFO, "Timer ended: t") in records
    assert all(message != "" for _, message in records)


@on_only
def test_assert_and_count(caplog):
    with caplog.at_level(logging.DEBUG, logger="iv8.console"):
        with iv8.Page() as page:
            page.eval("console.assert(true, 'ignored')")
            page.eval("console.assert(false, 'bad')")
            page.eval("console.assert(false)")
            page.eval("console.count('a')")
            page.eval("console.count('a')")
            page.load("<html><body></body></html>", BASE)
            page.eval("console.count('a')")
    records = [(r.levelno, r.getMessage()) for r in caplog.records
               if r.name == "iv8.console"]
    assert (logging.ERROR, "Assertion failed: bad") in records
    assert (logging.ERROR, "Assertion failed") in records
    assert (logging.INFO, "a: 1") in records
    assert (logging.INFO, "a: 2") in records
    # The console host is recreated by load(), so the counter restarts.
    assert sum(1 for level, message in records
               if level == logging.INFO and message == "a: 1") == 2


def test_no_new_python_surface():
    for attr in METHODS:
        assert not hasattr(iv8.Page, attr)
