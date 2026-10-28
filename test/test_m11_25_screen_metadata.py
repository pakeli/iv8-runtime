"""M11-25 acceptance tests: screen color/pixel depth."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-25.test/"


@on_only
def test_defaults():
    with iv8.Page() as page:
        assert page.eval("screen.colorDepth") == 24
        assert page.eval("screen.pixelDepth") == 24


@on_only
def test_configured_values_are_independent():
    environment = {
        "screenWidth": 800,
        "screenHeight": 600,
        "screenColorDepth": 30,
        "screenPixelDepth": 32,
    }
    with iv8.Page(environment=environment) as page:
        page.load("<html><body></body></html>", BASE)
        assert page.eval("screen.width") == 800
        assert page.eval("screen.colorDepth") == 30
        assert page.eval("screen.pixelDepth") == 32


@on_only
def test_surface_is_read_only():
    with iv8.Page(environment={"screenColorDepth": 30,
                               "screenPixelDepth": 30}) as page:
        assert page.eval(
            "(screen.colorDepth = 1, screen.colorDepth)") == 30
        assert page.eval(
            "(screen.pixelDepth = 1, screen.pixelDepth)") == 30


def test_no_new_python_surface():
    for attr in ("colorDepth", "pixelDepth"):
        assert not hasattr(iv8.Page, attr)
