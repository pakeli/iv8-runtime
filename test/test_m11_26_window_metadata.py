"""M11-26 acceptance tests: outer window geometry metadata."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-26.test/"


@on_only
def test_defaults():
    with iv8.Page() as page:
        assert page.eval("outerWidth") == 1280
        assert page.eval("outerHeight") == 720
        assert page.eval("screenX") == 0
        assert page.eval("screenY") == 0
        assert page.eval("window.outerWidth === outerWidth") is True


@on_only
def test_configured_values_are_independent():
    environment = {
        "innerWidth": 640,
        "innerHeight": 480,
        "outerWidth": 1111,
        "outerHeight": 777,
        "screenX": 12,
        "screenY": 34,
    }
    with iv8.Page(environment=environment) as page:
        page.load("<html><body></body></html>", BASE)
        assert page.eval("outerWidth") == 1111
        assert page.eval("outerHeight") == 777
        assert page.eval("screenX") == 12
        assert page.eval("screenY") == 34
        assert page.eval("innerWidth") == 640


@on_only
def test_surface_is_read_only():
    with iv8.Page(environment={"outerWidth": 1111, "outerHeight": 777,
                               "screenX": 12, "screenY": 34}) as page:
        assert page.eval("(outerWidth = 1, outerWidth)") == 1111
        assert page.eval("(outerHeight = 1, outerHeight)") == 777
        assert page.eval("(screenX = 1, screenX)") == 12
        assert page.eval("(screenY = 1, screenY)") == 34


@on_only
def test_window_name_is_not_added():
    with iv8.Page() as page:
        assert page.eval("typeof window.name") == "undefined"


def test_no_new_python_surface():
    for attr in ("outerWidth", "outerHeight", "screenX", "screenY"):
        assert not hasattr(iv8.Page, attr)
