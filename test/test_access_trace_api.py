"""First-slice host access trace API tests (channel/state, not runtime events)."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://access-trace.test/"


def test_surface_shape_both_build_modes():
    for name in ("start_access_trace", "stop_access_trace",
                 "read_access_trace"):
        assert callable(getattr(iv8.Page, name, None)), name
    # No new top-level export; access tracing is a Page-level second-layer API.
    for name in ("start_access_trace", "stop_access_trace", "read_access_trace",
                 "AccessTrace", "AccessEvent"):
        assert not hasattr(iv8, name)
    # The older channels remain present.
    for name in ("watch_apis", "read_watch_api_hits", "watch_reflect_get",
                 "watch_object_keys"):
        assert callable(getattr(iv8.Page, name, None)), name


@on_only
def test_disabled_report_shape():
    with iv8.Page() as page:
        report = page.read_access_trace()
        assert set(report) == {
            "events", "enabled", "generation", "max_events",
            "dropped_events", "filter_object_types", "coverage",
        }
        assert report["events"] == []
        assert report["enabled"] is False
        assert report["generation"] == 1
        assert report["max_events"] == 0
        assert report["dropped_events"] == 0
        assert report["filter_object_types"] is None
        coverage = report["coverage"]
        assert coverage["phase"] == "enter"
        assert coverage["operations"] == ["get", "set", "call"]
        assert "navigator" in coverage["object_types"]
        assert coverage["not_covered"]


@on_only
def test_start_stop_read_and_new_session():
    with iv8.Page() as page:
        assert page.start_access_trace() is None
        report = page.read_access_trace(clear=False)
        assert report["enabled"] is True
        assert report["max_events"] == 10000
        assert report["filter_object_types"] is None

        assert page.stop_access_trace() is None
        assert page.read_access_trace(clear=False)["enabled"] is False

        page.start_access_trace(max_events=3, object_types=["navigator",
                                                            "document",
                                                            "navigator"])
        report = page.read_access_trace(clear=False)
        assert report["enabled"] is True
        assert report["max_events"] == 3
        assert report["filter_object_types"] == ["navigator", "document"]

        # clear-read preserves the session configuration.
        assert page.read_access_trace(clear=True)["filter_object_types"] == [
            "navigator", "document"]
        assert page.read_access_trace(clear=False)["filter_object_types"] == [
            "navigator", "document"]


@on_only
def test_empty_filter_is_valid():
    with iv8.Page() as page:
        page.start_access_trace(object_types=[])
        report = page.read_access_trace()
        assert report["enabled"] is True
        assert report["filter_object_types"] == []


@on_only
def test_start_validation():
    with iv8.Page() as page:
        for bad in (True, False, "10", 1.0, None):
            with pytest.raises(TypeError):
                page.start_access_trace(max_events=bad)
        for bad in (0, -1):
            with pytest.raises(ValueError):
                page.start_access_trace(max_events=bad)
        for bad in ("navigator", ("navigator",), {"navigator": True}):
            with pytest.raises(TypeError):
                page.start_access_trace(object_types=bad)
        for bad in ([1], ["navigator", None]):
            with pytest.raises(TypeError):
                page.start_access_trace(object_types=bad)
        with pytest.raises(ValueError):
            page.start_access_trace(object_types=["not-a-type"])
        with pytest.raises(TypeError):
            page.read_access_trace(clear="yes")


@on_only
def test_load_keeps_configuration_and_advances_generation():
    with iv8.Page() as page:
        page.start_access_trace(max_events=7, object_types=["navigator"])
        before = page.read_access_trace(clear=False)
        assert before["generation"] == 1
        page.load("<html><body></body></html>", BASE)
        after = page.read_access_trace(clear=False)
        assert after["enabled"] is True
        assert after["max_events"] == 7
        assert after["filter_object_types"] == ["navigator"]
        assert after["generation"] == 2


@on_only
def test_pages_are_independent():
    with iv8.Page() as first, iv8.Page() as second:
        first.start_access_trace(max_events=2)
        assert first.read_access_trace(clear=False)["enabled"] is True
        assert second.read_access_trace(clear=False)["enabled"] is False


@on_only
def test_dispose_rejects_all_trace_methods():
    page = iv8.Page()
    page.dispose()
    for call in (
        lambda: page.start_access_trace(),
        lambda: page.stop_access_trace(),
        lambda: page.read_access_trace(),
    ):
        with pytest.raises(iv8.JSContextDisposedError):
            call()


@on_only
def test_no_js_visible_trace_surface():
    with iv8.Page() as page:
        page.start_access_trace()
        page.load("<html><body></body></html>", BASE)
        for name in ("start_access_trace", "stop_access_trace",
                     "read_access_trace", "access_trace", "AccessTrace",
                     "__access_trace__"):
            assert page.eval(f"typeof globalThis.{name}") == "undefined"
