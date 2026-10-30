"""M11-35 acceptance tests: BOM metadata completion."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-35.test/"


def _load(page, base=BASE):
    page.load("<html><body></body></html>", base)


def test_no_new_python_surface():
    for name in ("isSecureContext", "maxTouchPoints", "pdfViewerEnabled",
                 "doNotTrack", "connection", "orientation"):
        assert not hasattr(iv8.Page, name)


# --- window ---------------------------------------------------------------------

@on_only
def test_window_metadata_defaults():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("typeof window.name") == "string"
        assert page.eval("window.name") == ""
        assert page.eval("window.length") == 0
        assert page.eval("window.screenLeft") == 0
        assert page.eval("window.screenTop") == 0
        assert page.eval("window.isSecureContext") is True


@on_only
def test_window_name_is_mutable_and_persists_across_load():
    with iv8.Page() as page:
        _load(page)
        page.eval("window.name = 'first'")
        assert page.eval("window.name") == "first"
        page.eval("window.name = 123")
        assert page.eval("window.name") == "123"
        _load(page)
        assert page.eval("window.name") == "123"
    with iv8.Page() as other:
        assert other.eval("window.name") == ""


@on_only
def test_window_metadata_is_read_only_where_required():
    env = {"screenX": 12, "screenY": 34}
    with iv8.Page(environment=env) as page:
        _load(page)
        assert page.eval("window.screenLeft") == 12
        assert page.eval("window.screenTop") == 34
        assert page.eval(
            "(window.length = 5, window.length)") == 0
        assert page.eval(
            "(window.screenLeft = 99, window.screenLeft)") == 12
        assert page.eval(
            "(window.screenTop = 99, window.screenTop)") == 34
        assert page.eval(
            "(window.isSecureContext = false, window.isSecureContext)") is True


@on_only
def test_secure_context_is_derived_from_the_base_url():
    with iv8.Page() as page:
        page.load("<html><body></body></html>", "https://example.test/")
        assert page.eval("isSecureContext") is True
        page.load("<html><body></body></html>", "http://example.test/")
        assert page.eval("isSecureContext") is False
        page.load("<html><body></body></html>", "http://localhost/")
        assert page.eval("isSecureContext") is True
        page.load("<html><body></body></html>", "file:///tmp/page.html")
        assert page.eval("isSecureContext") is True


# --- screen ---------------------------------------------------------------------

@on_only
def test_screen_metadata_defaults_and_orientation():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("screen.availLeft") == 0
        assert page.eval("screen.availTop") == 0
        assert page.eval("screen.orientation.type") == "landscape-primary"
        assert page.eval("screen.orientation.angle") == 0


@on_only
def test_screen_metadata_overrides_and_portrait_orientation():
    env = {
        "screenWidth": 1080,
        "screenHeight": 1920,
        "screenAvailLeft": 10,
        "screenAvailTop": 20,
    }
    with iv8.Page(environment=env) as page:
        _load(page)
        assert page.eval("screen.availLeft") == 10
        assert page.eval("screen.availTop") == 20
        assert page.eval("screen.orientation.type") == "portrait-primary"
        assert page.eval("screen.orientation.angle") == 90


@on_only
def test_screen_metadata_is_read_only():
    with iv8.Page(environment={"screenAvailLeft": 10,
                               "screenAvailTop": 20}) as page:
        _load(page)
        assert page.eval(
            "(screen.availLeft = 1, screen.availLeft)") == 10
        assert page.eval(
            "(screen.availTop = 1, screen.availTop)") == 20
        assert page.eval(
            "(screen.orientation.type = 'x', screen.orientation.type)"
        ) == "landscape-primary"


# --- navigator ------------------------------------------------------------------

@on_only
def test_navigator_metadata_defaults():
    with iv8.Page() as page:
        _load(page)
        assert page.eval("navigator.maxTouchPoints") == 0
        assert page.eval("navigator.pdfViewerEnabled") is False
        assert page.eval("navigator.doNotTrack") == "0"
        assert page.eval("navigator.connection.effectiveType") == "4g"
        assert page.eval("navigator.connection.rtt") == 50
        assert page.eval("navigator.connection.downlink") == 10.0
        assert page.eval("navigator.connection.saveData") is False


@on_only
def test_navigator_metadata_flat_overrides():
    env = {
        "maxTouchPoints": 5,
        "pdfViewerEnabled": True,
        "doNotTrack": "1",
        "connectionEffectiveType": "3g",
        "connectionRtt": 120,
        "connectionDownlink": 1.5,
        "connectionSaveData": True,
    }
    with iv8.Page(environment=env) as page:
        _load(page)
        assert page.eval("navigator.maxTouchPoints") == 5
        assert page.eval("navigator.pdfViewerEnabled") is True
        assert page.eval("navigator.doNotTrack") == "1"
        assert page.eval("navigator.connection.effectiveType") == "3g"
        assert page.eval("navigator.connection.rtt") == 120
        assert page.eval("navigator.connection.downlink") == 1.5
        assert page.eval("navigator.connection.saveData") is True


@on_only
def test_navigator_metadata_nested_overrides():
    env = {
        "navigator": {
            "maxTouchPoints": 2,
            "pdfViewerEnabled": True,
            "doNotTrack": "1",
            "connectionEffectiveType": "2g",
            "connectionRtt": 250,
            "connectionDownlink": 0.5,
            "connectionSaveData": False,
        },
        "screen": {"availLeft": 4, "availTop": 6},
    }
    with iv8.Page(environment=env) as page:
        _load(page)
        assert page.eval("navigator.maxTouchPoints") == 2
        assert page.eval("navigator.connection.effectiveType") == "2g"
        assert page.eval("screen.availLeft") == 4
        assert page.eval("screen.availTop") == 6


@on_only
def test_navigator_metadata_is_read_only():
    with iv8.Page() as page:
        _load(page)
        assert page.eval(
            """
            (() => {
              navigator.maxTouchPoints = 9;
              navigator.pdfViewerEnabled = true;
              navigator.doNotTrack = '1';
              navigator.connection.rtt = 1;
              navigator.connection.downlink = 1;
              navigator.connection.saveData = true;
              return [navigator.maxTouchPoints,
                      navigator.pdfViewerEnabled,
                      navigator.doNotTrack,
                      navigator.connection.rtt,
                      navigator.connection.downlink,
                      navigator.connection.saveData].join(',');
            })();
            """
        ) == "0,false,0,50,10,false"
