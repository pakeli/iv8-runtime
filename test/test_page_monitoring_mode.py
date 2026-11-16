"""Page debug mode streams real native API use; prod and manual tracing stay distinct."""

import threading

import pytest

import iv8


pytestmark = pytest.mark.skipif(
    not getattr(iv8, "_v8_linked", False), reason="requires a V8-linked build")
BASE = "https://monitoring-mode.test/page/"
HTML = "<html><head></head><body><div id='original'></div></body></html>"
PUMPS = [
    ("run_timers", "setTimeout(() => navigator.userAgent, 0)", (), None),
    ("run_jobs", "queueMicrotask(() => navigator.userAgent)", (), None),
    ("run_animation_frame", "requestAnimationFrame(() => navigator.userAgent)", (), None),
    ("run_idle_callbacks", "requestIdleCallback(() => navigator.userAgent)", (), None),
    ("tick", "setTimeout(() => navigator.userAgent, 0)", (), True),
    ("advance", "setTimeout(() => navigator.userAgent, 5)", (5,), True),
    ("run_until_idle", "setTimeout(() => navigator.userAgent, 5)", (), True),
]


def test_fresh_page_and_default_load_do_not_enable_or_print_monitoring(capsys):
    with iv8.Page() as page:
        assert isinstance(page.eval("navigator.userAgent"), str)
        page.load("<script>navigator.userAgent</script>", BASE)
        assert capsys.readouterr().out == ""
        report = page.read_access_trace()
        assert not report["enabled"] and report["events"] == []


def test_empty_debug_load_has_no_environment_bootstrap_noise(capsys):
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        output = capsys.readouterr().out
        assert output == ""
        report = page.read_access_trace()
        assert report["enabled"] and report["filter_object_types"] is None
        assert report["events"] == []


def test_debug_load_prints_inline_resource_and_host_script_use_in_order(capsys):
    html = """<html><head>
        <script>navigator.userAgent;</script>
        <script src="external.js"></script>
        </head><body></body></html>"""
    with iv8.Page() as page:
        page.load(html, BASE, mode="debug",
                  resources={BASE + "external.js": "screen.width;"},
                  scripts=[{"name": "host-script.js", "code": "history.length;"}])
        output = capsys.readouterr().out
        assert output.index("GET navigator.userAgent") < output.index("GET screen.width")
        assert output.index("GET screen.width") < output.index("GET history.length")
        assert page.ready_state == "complete"
        assert page.read_access_trace()["events"] == []


def test_debug_observes_user_api_access_inside_load_lifecycle_listeners(capsys):
    html = """<html><head><script>
        document.addEventListener('DOMContentLoaded', event => {
            globalThis.contentReady = event.type;
            navigator.hardwareConcurrency;
        });
        addEventListener('load', event => {
            globalThis.windowReady = event.type;
            screen.width;
        });
        </script></head><body></body></html>"""
    with iv8.Page() as page:
        page.load(html, BASE, mode="debug")
        output = capsys.readouterr().out
        assert output.count("GET navigator.hardwareConcurrency") == 1
        assert output.count("GET screen.width") == 1
        assert page.eval("contentReady === 'DOMContentLoaded' && windowReady === 'load'")
        assert page.read_access_trace()["events"] == []


def test_debug_eval_streams_actual_values_and_does_not_repeat_prior_events(capsys):
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        capsys.readouterr()
        assert page.eval("navigator.hardwareConcurrency", name="debug-eval.js") == 8
        output = capsys.readouterr().out
        assert "GET navigator.hardwareConcurrency -> 8" in output
        assert page.read_access_trace()["events"] == []
        assert page.eval("2 + 3") == 5
        assert capsys.readouterr().out == ""
        value = page.eval("navigator")
        assert isinstance(value, iv8.JSValue) and value.context_alive
        assert "GET window.navigator" in capsys.readouterr().out


@pytest.mark.parametrize("method,setup,arguments,expected", PUMPS,
                         ids=[entry[0] for entry in PUMPS])
def test_debug_prints_callback_api_use_after_every_event_pump(
        capsys, method, setup, arguments, expected):
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        page.eval(setup)
        capsys.readouterr()
        assert getattr(page, method)(*arguments) is expected
        output = capsys.readouterr().out
        assert output.count("GET navigator.userAgent") == 1
        assert page.read_access_trace()["events"] == []
        getattr(page, method)(*arguments)
        assert capsys.readouterr().out == ""


@pytest.mark.parametrize("method,arguments", [
    ("eval", ("navigator.userAgent",)),
    *[(entry[0], entry[2]) for entry in PUMPS],
])
def test_debug_flushes_before_propagating_the_same_native_exception(
        capsys, monkeypatch, method, arguments):
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        capsys.readouterr()
        native = page._native
        failure = iv8.JSError("Error", "native execution failed", "original stack",
                              "failed-pump.js", 7, 11)

        def fail_after_api_use(*unused_arguments, **unused_keywords):
            native.eval("navigator.userAgent", False, "failed-pump.js")
            raise failure

        class FailingNative:
            def __getattr__(self, name):
                return fail_after_api_use if name == method else getattr(native, name)

        monkeypatch.setattr(page, "_native", FailingNative())
        with pytest.raises(iv8.JSError) as caught:
            getattr(page, method)(*arguments)
        assert caught.value is failure
        assert "GET navigator.userAgent" in capsys.readouterr().out
        assert page.read_access_trace()["events"] == []


def test_rejected_busy_eval_does_not_drain_an_active_call_trace(capsys, monkeypatch):
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        capsys.readouterr()
        native = page._native
        # Leave real entries buffered as if they belong to the active thread.
        native.eval("navigator.userAgent", False, "active-thread.js")
        before = native.read_access_trace(False)
        assert before["events"]
        failure = iv8.JSContextBusyError("an operation is already active")
        snapshots = []

        def reject_eval(*unused_arguments):
            raise failure

        def record_snapshot(*arguments):
            snapshots.append(arguments)
            return native.read_access_trace(*arguments)

        class BusyNative:
            def __getattr__(self, name):
                if name == "eval":
                    return reject_eval
                if name == "read_access_trace":
                    return record_snapshot
                return getattr(native, name)

        monkeypatch.setattr(page, "_native", BusyNative())
        with pytest.raises(iv8.JSContextBusyError) as caught:
            page.eval("screen.width")
        assert caught.value is failure
        assert snapshots == [] and capsys.readouterr().out == ""
        assert native.read_access_trace(False) == before


@pytest.mark.parametrize("throws", [False, True], ids=["return", "exception"])
def test_stdout_failure_preserves_original_eval_result_or_error(capsys, monkeypatch, throws):
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        capsys.readouterr()
        native = page._native
        original_errors, output_attempts = [], []

        def record_eval(*arguments):
            try:
                return native.eval(*arguments)
            except iv8.JSError as error:
                original_errors.append(error)
                raise

        def fail_print(*arguments, **unused_keywords):
            output_attempts.append(arguments)
            raise OSError("stdout is unavailable")

        class RecordingNative:
            def __getattr__(self, name):
                return record_eval if name == "eval" else getattr(native, name)

        monkeypatch.setattr(page, "_native", RecordingNative())
        with monkeypatch.context() as output_patch:
            output_patch.setattr("builtins.print", fail_print)
            if throws:
                with pytest.raises(iv8.JSError) as caught:
                    page.eval("navigator.userAgent; throw new TypeError('output sentinel')",
                              name="mode-output-error.js")
                assert caught.value is original_errors[0]
                assert caught.value.name == "TypeError"
                assert caught.value.message == "output sentinel"
                assert caught.value.resource_name == "mode-output-error.js"
            else:
                assert page.eval("navigator.hardwareConcurrency") == 8
        assert output_attempts
        assert any("GET navigator." in str(arguments[0]) for arguments in output_attempts)
        assert page.read_access_trace()["events"] == []


def test_debug_eval_logs_throwing_native_entry_before_raising_js_error(capsys):
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        capsys.readouterr()
        with pytest.raises(iv8.JSError) as caught:
            page.eval("document.body.appendChild(1)", name="mode-native-error.js")
        assert caught.value.name == "TypeError"
        assert caught.value.resource_name == "mode-native-error.js"
        output = capsys.readouterr().out
        assert "appendChild(1)" in output and "!! TypeError:" in output
        assert page.read_access_trace()["events"] == []
        assert page.eval("6 * 7") == 42


@pytest.mark.parametrize("script_kind", ["inline", "resource", "host"])
def test_debug_failed_load_still_prints_earlier_api_entries(capsys, script_kind):
    code = "navigator.userAgent; throw new Error('mode-load-failure')"
    html, options = HTML, {}
    if script_kind == "inline":
        html = "<script>" + code + "</script>"
        resource_name = BASE + "#inline-script-1"
    elif script_kind == "resource":
        html = '<script src="failure.js"></script>'
        resource_name = BASE + "failure.js"
        options["resources"] = {resource_name: code}
    else:
        resource_name = "mode-host-failure.js"
        options["scripts"] = [{"name": resource_name, "code": code}]
    with iv8.Page() as page:
        with pytest.raises(iv8.JSError) as caught:
            page.load(html, BASE, mode="debug", **options)
        assert caught.value.name == "Error"
        assert caught.value.message == "mode-load-failure"
        assert caught.value.resource_name == resource_name
        assert page.ready_state == "loading"
        assert "GET navigator.userAgent" in capsys.readouterr().out
        assert page.read_access_trace()["events"] == []


def test_debug_prints_before_python_result_conversion_failure(capsys):
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        capsys.readouterr()
        with pytest.raises(iv8.JSConversionError):
            page.eval("navigator.userAgent; (() => 1)", to_py=True)
        assert "GET navigator.userAgent" in capsys.readouterr().out
        assert page.read_access_trace()["events"] == []


@pytest.mark.parametrize("reload_options", [{}, {"mode": None}], ids=["omitted", "none"])
def test_omitted_or_none_mode_preserves_debug_across_reload(capsys, reload_options):
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        capsys.readouterr()
        page.load("<script>screen.width</script>", BASE, **reload_options)
        assert "GET screen.width" in capsys.readouterr().out
        page.eval("navigator.userAgent")
        assert "GET navigator.userAgent" in capsys.readouterr().out
        assert page.read_access_trace()["events"] == []


def test_prod_stops_debug_output_and_disables_trace_across_omitted_reload(capsys):
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        capsys.readouterr()
        page.load("<script>navigator.userAgent</script>", BASE, mode="prod")
        page.eval("screen.width")
        report = page.read_access_trace()
        assert not report["enabled"] and report["events"] == []
        page.load("<script>history.length</script>", BASE)
        assert capsys.readouterr().out == ""
        page.load("<script>navigator.userAgent</script>", BASE, mode="debug")
        assert "GET navigator.userAgent" in capsys.readouterr().out


def test_prod_clears_preexisting_manual_events_without_printing_them(capsys):
    with iv8.Page() as page:
        page.load(HTML, BASE)
        page.start_access_trace()
        page.eval("navigator.userAgent")
        assert page.read_access_trace(clear=False)["events"]
        page.load(HTML, BASE, mode="prod")
        report = page.read_access_trace()
        assert not report["enabled"] and report["events"] == []
        assert capsys.readouterr().out == ""


def test_prod_does_not_disable_separate_call_watch_channel(capsys):
    with iv8.Page() as page:
        page.watch_apis(["document.querySelector"])
        page.load(HTML, BASE, mode="prod")
        page.eval("document.querySelector('#original')")
        assert [hit["path"] for hit in page.read_watch_api_hits()] == ["document.querySelector"]
        assert not page.read_access_trace()["enabled"]
        assert capsys.readouterr().out == ""


def test_omitted_mode_keeps_existing_manual_trace_controls_and_buffer(capsys):
    with iv8.Page() as page:
        page.load(HTML, BASE)
        page.start_access_trace(max_events=13, object_types=["navigator"])
        page.eval("navigator.userAgent")
        report = page.read_access_trace(clear=False)
        assert report["enabled"] and report["max_events"] == 13
        assert report["filter_object_types"] == ["navigator"]
        assert [(event["object_type"], event["member"]) for event in report["events"]] == [
            ("navigator", "userAgent")]
        assert capsys.readouterr().out == ""
        page.load(HTML, BASE, mode=None)
        page.read_access_trace(clear=True)
        page.eval("navigator.language")
        report = page.read_access_trace()
        assert report["enabled"] and report["max_events"] == 13
        assert report["filter_object_types"] == ["navigator"]
        assert [(event["object_type"], event["member"]) for event in report["events"]] == [
            ("navigator", "language")]
        assert capsys.readouterr().out == ""


def test_debug_emits_more_than_default_trace_capacity_without_dropping(capsys):
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        page.eval("globalThis.cachedNavigator=navigator")
        capsys.readouterr()
        assert page.eval("for (let index=0;index<10025;index++) cachedNavigator.userAgent; 42") == 42
        output = capsys.readouterr().out
        assert output.count("GET navigator.userAgent") == 10025
        report = page.read_access_trace()
        assert report["events"] == [] and report["dropped_events"] == 0
        assert "dropped" not in output.lower()


def test_debug_and_manual_buffers_are_independent_between_pages(capsys):
    with iv8.Page() as debug_page, iv8.Page() as manual_page:
        debug_page.load(HTML, BASE, mode="debug")
        manual_page.load(HTML, BASE)
        manual_page.start_access_trace(object_types=["navigator"])
        capsys.readouterr()
        manual_page.eval("navigator.language")
        assert capsys.readouterr().out == ""
        debug_page.eval("screen.width")
        output = capsys.readouterr().out
        assert "GET screen.width" in output and "navigator.language" not in output
        assert debug_page.read_access_trace()["events"] == []
        events = manual_page.read_access_trace()["events"]
        assert [(event["object_type"], event["member"]) for event in events] == [
            ("navigator", "language")]


@pytest.mark.parametrize("mode,error_type", [
    (False, TypeError), (0, TypeError), ([], TypeError), ({}, TypeError),
    (b"debug", TypeError), (object(), TypeError),
    ("", ValueError), ("Debug", ValueError), ("production", ValueError), ("off", ValueError),
])
def test_invalid_mode_neither_reloads_nor_mutates_manual_trace(capsys, mode, error_type):
    with iv8.Page() as page:
        page.load(HTML, BASE)
        page.eval("globalThis.preserved=19;globalThis.cachedNavigator=navigator")
        page.start_access_trace(max_events=13, object_types=["navigator"])
        page.eval("cachedNavigator.userAgent")
        before = page.read_access_trace(clear=False)
        with pytest.raises(error_type):
            page.load("<script>throw Error('must not execute')</script>",
                      "https://must-not-load.test/", mode=mode)
        assert page.read_access_trace(clear=False) == before
        assert page.eval("preserved") == 19 and page.ready_state == "complete"
        assert capsys.readouterr().out == ""


def test_invalid_mode_does_not_turn_off_active_debug_mode(capsys):
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        capsys.readouterr()
        with pytest.raises(ValueError):
            page.load(HTML, BASE, mode="invalid")
        page.eval("navigator.userAgent")
        assert "GET navigator.userAgent" in capsys.readouterr().out


def test_mode_requires_keyword_without_reloading_page(capsys):
    with iv8.Page() as page:
        page.load(HTML, BASE)
        page.eval("globalThis.preserved=23")
        with pytest.raises(TypeError):
            page.load(HTML, BASE, None, None, "debug")
        assert page.eval("preserved") == 23
        assert not page.read_access_trace()["enabled"]
        assert capsys.readouterr().out == ""


@pytest.mark.parametrize("competing_operation", [
    "eval", "load_prod", "read", "peek", "start", "stop", "dispose",
])
def test_debug_execution_owns_trace_until_output_finishes(
        capsys, monkeypatch, competing_operation):
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        capsys.readouterr()
        drain_reached = threading.Event()
        allow_drain = threading.Event()
        results, errors = [], []
        emit_trace = page._emit_debug_trace

        def gated_emit():
            if threading.current_thread() is owner:
                # Native execution has returned, but its completed trace still
                # belongs to this public operation until output is finished.
                drain_reached.set()
                if not allow_drain.wait(5):
                    raise AssertionError("debug owner was not released to drain")
            emit_trace()

        def execute_owner():
            try:
                results.append(page.eval("navigator.hardwareConcurrency",
                                         name="debug-owner.js"))
            except BaseException as error:
                errors.append(error)

        monkeypatch.setattr(page, "_emit_debug_trace", gated_emit)
        owner = threading.Thread(target=execute_owner, name="debug-owner")
        owner.start()
        try:
            assert drain_reached.wait(5), "debug owner never reached output"
            before = page._native.read_access_trace(False)
            assert any(event["member"] == "hardwareConcurrency" and
                       event["outcome"] == "returned"
                       for event in before["events"])
            with pytest.raises(iv8.JSContextBusyError):
                if competing_operation == "eval":
                    page.eval("screen.width")
                elif competing_operation == "load_prod":
                    page.load("<script>screen.width</script>", BASE, mode="prod")
                elif competing_operation == "read":
                    page.read_access_trace()
                elif competing_operation == "peek":
                    page.read_access_trace(clear=False)
                elif competing_operation == "start":
                    page.start_access_trace(max_events=1)
                elif competing_operation == "stop":
                    page.stop_access_trace()
                else:
                    page.dispose()
            assert page._debug_trace and not page._native.disposed
            assert page._native.read_access_trace(False) == before
            assert capsys.readouterr().out == ""
        finally:
            allow_drain.set()
            owner.join(5)
        assert not owner.is_alive(), "debug owner did not finish output"
        assert errors == [] and results == [8]
        output = capsys.readouterr().out
        assert output.count("GET navigator.hardwareConcurrency -> 8") == 1
        assert "GET screen.width" not in output
        assert page.read_access_trace()["events"] == []
        assert page.eval("navigator.hardwareConcurrency") == 8
        assert "GET navigator.hardwareConcurrency -> 8" in capsys.readouterr().out


@pytest.mark.parametrize("interrupt_type", [KeyboardInterrupt, SystemExit])
def test_output_interrupt_propagates_and_releases_execution_guard(
        capsys, monkeypatch, interrupt_type):
    with iv8.Page() as page:
        page.load(HTML, BASE, mode="debug")
        capsys.readouterr()
        interruption = interrupt_type("debug output was interrupted")

        def interrupt_output(*unused_arguments, **unused_keywords):
            raise interruption

        with monkeypatch.context() as output_patch:
            output_patch.setattr("builtins.print", interrupt_output)
            with pytest.raises(interrupt_type) as caught:
                page.eval("navigator.hardwareConcurrency")
            assert caught.value is interruption
        assert page.eval("21 * 2") == 42
        assert page.read_access_trace()["events"] == []
