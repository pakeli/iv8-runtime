"""Step 7: aggregated access-trace usage reports and workflow boundaries."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://access-report.test/"
HTML = "<html><body></body></html>"


def _load(page):
    page.load(HTML, BASE)


def test_report_surface_shape_both_build_modes():
    assert callable(getattr(iv8.Page, "access_trace_report", None))
    # Still a Page-level API only; no new top-level export.
    assert not hasattr(iv8, "access_trace_report")


@on_only
def test_report_aggregates_accesses_missing_candidates_and_is_non_destructive():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval(
            "navigator.userAgent; navigator.userAgent;"
            "navigator.notThere; hostProbe.add(1, 2);",
            name="report.js"
        )
        report = page.access_trace_report()
        assert report["trace"]["enabled"] is True
        assert report["trace"]["dropped_events"] == 0
        assert report["trace"]["filter_object_types"] is None
        assert report["events"]["count"] == 4
        assert report["events"]["by_operation"] == {
            "get": 2, "get_probe": 1, "call": 1}
        assert report["events"]["by_object_type"] == {
            "navigator": 3, "host_probe": 1}
        accesses = {
            (entry["object_type"], entry["member"], entry["operation"]): entry
            for entry in report["accesses"]
        }
        user_agent = accesses[("navigator", "userAgent", "get")]
        assert user_agent["count"] == 2
        assert user_agent["first_seq"] < user_agent["last_seq"]
        assert user_agent["first_resource_name"] == "report.js"
        assert user_agent["first_source_kind"] == "eval"
        assert user_agent["outcomes"] == {"returned": 2}
        missing_probe = accesses[("navigator", "notThere", "get_probe")]
        assert missing_probe["resolution"] == "missing"
        call = accesses[("host_probe", "add", "call")]
        assert call["count"] == 1
        assert call["outcomes"] == {"returned": 1}
        assert report["missing_candidates"] == [{
            "object_type": "navigator",
            "label": "navigator",
            "object_id": missing_probe["object_id"],
            "member": "notThere",
            "count": 1,
            "first_seq": missing_probe["first_seq"],
            "last_seq": missing_probe["last_seq"],
            "first_resource_name": "report.js",
            "first_source_kind": "eval",
            "resolution": "missing",
        }]
        assert report["warnings"] == []
        assert report["progress"] is None
        assert "not_covered" in report["coverage"]

        # Default is a non-destructive read.
        assert len(page.read_access_trace(clear=False)["events"]) == 4
        cleared = page.access_trace_report(clear=True)
        assert cleared["events"]["count"] == 4
        assert page.read_access_trace(clear=False)["events"] == []


@on_only
def test_report_groups_same_member_on_different_targets_and_echoes_progress():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        page.eval(
            "navigator.notThere; navigator.notThere; screen.notThere;",
            name="missing.js"
        )
        progress = {
            "event_loop": "run_until_idle",
            "complete": True,
            "max_steps": 10,
        }
        report = page.access_trace_report(progress=progress)
        candidates = report["missing_candidates"]
        assert [(entry["object_type"], entry["label"], entry["count"])
                for entry in candidates] == [
            ("navigator", "navigator", 2),
            ("screen", "screen", 1),
        ]
        assert candidates[0]["object_id"] != candidates[1]["object_id"]
        assert report["progress"] == progress
        assert report["progress"] is not progress


@on_only
def test_report_warnings_for_recording_filter_capacity_and_empty_logs():
    with iv8.Page() as page:
        _load(page)
        never_started = page.access_trace_report()
        assert never_started["trace"]["enabled"] is False
        assert {warning["code"] for warning in never_started["warnings"]} == {
            "trace_not_recording", "no_events"}

        page.start_access_trace(object_types=["navigator"])
        page.eval("screen.width;", name="filtered.js")
        filtered = page.access_trace_report()
        assert filtered["events"]["count"] == 0
        assert {warning["code"] for warning in filtered["warnings"]} == {
            "object_type_filter_active", "no_events"}

        page.start_access_trace(max_events=1)
        page.eval("navigator.userAgent; screen.width;", name="capacity.js")
        capped = page.access_trace_report()
        assert capped["trace"]["dropped_events"] == 1
        assert capped["events"]["count"] == 1
        assert {warning["code"] for warning in capped["warnings"]} == {
            "capacity_exceeded"}

        page.stop_access_trace()
        stopped = page.access_trace_report()
        assert stopped["trace"]["enabled"] is False
        assert stopped["events"]["count"] == 1
        assert {warning["code"] for warning in stopped["warnings"]} == {
            "capacity_exceeded", "trace_not_recording"}


@on_only
def test_report_remains_available_after_a_js_error():
    with iv8.Page() as page:
        _load(page)
        page.start_access_trace()
        with pytest.raises(iv8.JSError):
            page.eval(
                "hostProbe.add({valueOf() { throw new Error('report boom'); }}, 1);",
                name="throw.js"
            )
        report = page.access_trace_report()
        add = [entry for entry in report["accesses"]
               if entry["object_type"] == "host_probe"
               and entry["member"] == "add"][0]
        assert add["outcomes"] == {"threw": 1}
        raw = page.read_access_trace(clear=False)["events"]
        assert raw[0]["exception_name"] == "Error"
        assert "report boom" in raw[0]["exception_message"]
        assert page.read_access_trace(clear=False)["events"]


@on_only
def test_report_validation_and_disposed_behavior():
    page = iv8.Page()
    page.start_access_trace()
    with pytest.raises(TypeError):
        page.access_trace_report(clear="yes")
    with pytest.raises(TypeError):
        page.access_trace_report(progress=["not", "a", "dict"])
    page.dispose()
    with pytest.raises(iv8.JSContextDisposedError):
        page.access_trace_report()
