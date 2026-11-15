"""Bounded Window.event scopes and explicit reportError dispatch."""

import json
from pathlib import Path

import pytest

import iv8


pytestmark = pytest.mark.skipif(not getattr(iv8, "_v8_linked", False), reason="requires V8")
BASE = "https://window-errors.test/"


def test_window_event_restores_outer_callback_and_idle_value_after_listener_errors():
    with iv8.Page() as page:
        page.capture_async_errors()
        page.load("<html><body></body></html>", BASE)
        assert page.eval("""(() => {
            const target = document.createElement('button'); document.body.appendChild(target);
            const outer = new Event('click'); const inner = new Event('inner');
            const seen = [];
            window.addEventListener('inner', () => seen.push(window.event === inner));
            target.addEventListener('click', () => {
                seen.push(window.event === outer);
                window.dispatchEvent(inner);
                seen.push(window.event === outer);
                throw Error('synthetic listener restoration');
            });
            target.onclick = () => { seen.push(window.event === outer); };
            target.dispatchEvent(outer);
            return seen.length === 4 && seen.every(Boolean) && window.event === undefined;
        })()""")
        errors = page.read_async_errors()["errors"]
        assert len(errors) == 1 and errors[0]["kind"] == "listener"


def test_window_event_replaceable_shadow_survives_dispatch_and_resets_on_load():
    reference = json.loads((Path(__file__).parent / "fixtures/presence_03_window_errors/chrome.json").read_text())
    assert reference["result"]["assigned"]["descriptor"]["data"] is True
    with iv8.Page() as page:
        assert page.eval("""(() => {
            const original = Object.getOwnPropertyDescriptor(window, 'event');
            window.event = 42;
            const descriptor = Object.getOwnPropertyDescriptor(window, 'event');
            let observed;
            window.addEventListener('synthetic-shadow', () => { observed = window.event; });
            window.dispatchEvent(new Event('synthetic-shadow'));
            return observed === 42 && window.event === 42 && descriptor.value === 42 &&
                descriptor.writable && descriptor.enumerable && descriptor.configurable &&
                !('get' in descriptor) && typeof original.get === 'function';
        })()""")
        page.load("<html><body></body></html>", BASE)
        assert page.eval("""window.event === undefined &&
            typeof Object.getOwnPropertyDescriptor(window, 'event').get === 'function'""")


def test_report_error_exposes_original_value_native_event_and_bounded_handler_model():
    with iv8.Page() as page:
        assert page.eval("""(() => {
            const error = new Error('synthetic-report');
            const seen = [];
            window.addEventListener('error', event => {
                seen.push(event instanceof Event && event.type === 'error' &&
                    event.error === error && event.message.includes('synthetic-report') &&
                    window.event === event && event.target === window &&
                    event.currentTarget === window && event.cancelable && !event.bubbles &&
                    !event.composed && !event.isTrusted && typeof event.filename === 'string' &&
                    typeof event.lineno === 'number' && typeof event.colno === 'number');
                event.preventDefault();
            });
            // This hosted slice retains the existing one-Event handler contract.
            window.onerror = event => { seen.push(event.error === error && window.event === event); };
            const result = reportError(error);
            return result === undefined && seen.length === 2 && seen.every(Boolean) &&
                window.event === undefined;
        })()""")


def test_report_error_primitive_missing_argument_and_independent_context_state():
    with iv8.Page() as first, iv8.Page() as second:
        first.eval("window.event = 'first-only'")
        assert first.eval("""(() => {
            let seen = false;
            window.addEventListener('error', event => {
                seen = event.error === 'synthetic-primitive' &&
                    event.message.includes('synthetic-primitive') && window.event === 'first-only';
                event.preventDefault();
            });
            reportError('synthetic-primitive');
            return seen && window.event === 'first-only';
        })()""")
        assert second.eval("window.event === undefined")
        with pytest.raises(iv8.JSError) as caught:
            second.eval("reportError()")
        assert caught.value.name == "TypeError"
        assert second.eval("window.event === undefined")
