"""M11-24 acceptance tests: navigator metadata."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-24.test/"


@on_only
def test_defaults():
    with iv8.Page() as page:
        assert page.eval("navigator.languages.join(',')") == "en-US,en"
        assert page.eval("navigator.onLine") is True
        assert page.eval("navigator.cookieEnabled") is True
        assert page.eval("navigator.hardwareConcurrency") == 8


@on_only
def test_configured_values():
    environment = {
        "languages": ["ja-JP", "ja"],
        "onLine": False,
        "cookieEnabled": False,
        "hardwareConcurrency": 4,
    }
    with iv8.Page(environment=environment) as page:
        page.load("<html><body></body></html>", BASE)
        assert page.eval("navigator.languages.join(',')") == "ja-JP,ja"
        assert page.eval("navigator.onLine") is False
        assert page.eval("navigator.cookieEnabled") is False
        assert page.eval("navigator.hardwareConcurrency") == 4


@on_only
def test_languages_is_a_fresh_read_only_array():
    with iv8.Page() as page:
        assert page.eval("Array.isArray(navigator.languages)") is True
        assert page.eval(
            "(navigator.languages.push('x'), navigator.languages.length)") == 3
        assert page.eval("navigator.languages.length") == 2
        assert page.eval(
            "(navigator.languages = [], navigator.languages.join(','))"
        ) == "en-US,en"


@on_only
def test_surface_is_read_only():
    with iv8.Page() as page:
        assert page.eval("(navigator.onLine = false, navigator.onLine)") is True
        assert page.eval(
            "(navigator.cookieEnabled = false, navigator.cookieEnabled)") is True
        assert page.eval(
            "(navigator.hardwareConcurrency = 1, "
            "navigator.hardwareConcurrency)") == 8


def test_no_new_python_surface():
    for attr in ("languages", "onLine", "cookieEnabled",
                 "hardwareConcurrency"):
        assert not hasattr(iv8.Page, attr)
