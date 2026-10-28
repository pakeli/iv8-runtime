"""M11-27 acceptance tests: navigator identity fields."""

import pytest

import iv8

v8_linked = getattr(iv8, "_v8_linked", False)
on_only = pytest.mark.skipif(not v8_linked, reason="requires a V8-linked build")

BASE = "https://m11-27.test/"
FIELDS = {
    "appName": "Netscape",
    "appCodeName": "Mozilla",
    "product": "Gecko",
    "vendor": "",
    "vendorSub": "",
    "productSub": "20100101",
}


@on_only
def test_defaults():
    with iv8.Page() as page:
        for name, value in FIELDS.items():
            assert page.eval(f"navigator.{name}") == value, name


@on_only
def test_configured_values():
    environment = {
        "appName": "App",
        "appCodeName": "Code",
        "product": "Product",
        "vendor": "Vendor",
        "vendorSub": "Sub",
        "productSub": "ProductSub",
    }
    with iv8.Page(environment=environment) as page:
        page.load("<html><body></body></html>", BASE)
        for name, value in environment.items():
            assert page.eval(f"navigator.{name}") == value, name


@on_only
def test_surface_is_read_only():
    with iv8.Page(environment={"appName": "App", "vendor": "Vendor"}) as page:
        assert page.eval("(navigator.appName = 'x', navigator.appName)") == "App"
        assert page.eval("(navigator.vendor = 'x', navigator.vendor)") == "Vendor"


def test_no_new_python_surface():
    for attr in FIELDS:
        assert not hasattr(iv8.Page, attr)
