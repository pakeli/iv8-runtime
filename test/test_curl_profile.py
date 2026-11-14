"""Installed curl profile inspection uses fake modules and cannot send requests."""

from enum import Enum
import importlib
import importlib.util
import json
from pathlib import Path
import sys
from types import SimpleNamespace

import pytest


PACKAGE_DIRECTORY = Path(__file__).resolve().parents[1] / "tools" / "iv8_diagnostics"
if "iv8_diagnostics" not in sys.modules:
    package_spec = importlib.util.spec_from_file_location(
        "iv8_diagnostics", PACKAGE_DIRECTORY / "__init__.py",
        submodule_search_locations=[str(PACKAGE_DIRECTORY)],
    )
    package = importlib.util.module_from_spec(package_spec)
    sys.modules[package_spec.name] = package
    package_spec.loader.exec_module(package)
inspect_curl_profile = importlib.import_module("iv8_diagnostics.curl_profile").inspect_curl_profile


def _fake_module(version="0.17.0", result=0, fail_setup=False, fail_close=False):
    calls = []

    class FakeCurl:
        def __init__(self):
            calls.append(("create",))

        def impersonate(self, target, default_headers=True):
            calls.append(("impersonate", target, default_headers))
            if fail_setup:
                raise RuntimeError("private setup exception")
            return result

        def close(self):
            calls.append(("close",))
            if fail_close:
                raise RuntimeError("private cleanup exception")

        def setopt(self, *args):
            pytest.fail("Inspector must not set a URL or other request options")

        def perform(self, *args, **kwargs):
            pytest.fail("Inspector must not perform any request")

    def forbidden_session(*args, **kwargs):
        pytest.fail("Inspector must not create a Session")

    latest = "chrome150" if version == "0.16.3" else "chrome152"
    enum = Enum("BrowserType", {name: name for name in ("chrome150", "chrome152", "chrome152_macos")})
    module = SimpleNamespace(
        __version__=version,
        BrowserType=enum,
        Curl=FakeCurl,
        Session=forbidden_session,
        requests=SimpleNamespace(impersonate=SimpleNamespace(
            BrowserType=enum,
            resolve_latest_browser_type=lambda target: latest if target == "chrome" else target,
        )),
        fingerprints=SimpleNamespace(NATIVE_IMPERSONATE_TARGETS=[
            {"target_name": "chrome150", "browser": "Chrome", "version": "150",
             "os": "macOS", "os_version": "Tahoe"},
            {"target_name": "chrome152", "browser": "Chrome", "version": "152",
             "os": "Windows", "os_version": "10"},
            {"target_name": "chrome152_macos", "browser": "Chrome", "version": "152",
             "os": "macOS", "os_version": "26.0"},
        ]),
    )
    return module, calls


@pytest.mark.parametrize("version", ["0.16.3", "0.17.0"])
def test_explicit_profile_metadata_and_no_url_native_setup(version):
    module, calls = _fake_module(version)
    report = inspect_curl_profile(module, "chrome150")
    assert report["status"] == "supported"
    assert report["supported"] is True
    assert report["profile"] == "chrome150"
    assert report["browser_major"] == 150
    assert report["platform"] == "macOS"
    assert report["platform_version"] == "Tahoe"
    assert report["client_version"] == version
    assert report["browser_major_source"] == "native_registry"
    assert report["native_setup_return_code"] == 0
    assert calls == [("create",), ("impersonate", "chrome150", False), ("close",)]
    assert "offline_native_setup" in report["verified_by"]
    assert any("not measured" in text for text in report["unchecked"])
    json.dumps(report, allow_nan=False)


@pytest.mark.parametrize("version,resolution", [("0.16.3", "chrome150"), ("0.17.0", "chrome152")])
def test_moving_chrome_alias_is_rejected_before_handle_creation(version, resolution):
    module, calls = _fake_module(version)
    report = inspect_curl_profile(module, "chrome")
    assert report["status"] == "invalid"
    assert report["supported"] is False
    assert report["alias"] is True
    assert report["alias_resolution"] == resolution
    assert report["findings"][0]["code"] == "moving_alias"
    assert not calls


def test_default_and_macos_profiles_use_registry_os_not_host_or_major():
    module, _ = _fake_module()
    assert inspect_curl_profile(module, "chrome152")["platform"] == "Windows"
    assert inspect_curl_profile(module, "chrome152_macos")["platform"] == "macOS"


def test_missing_os_registry_remains_unknown_instead_of_guessing_from_profile_suffix():
    module, _ = _fake_module()
    del module.fingerprints
    report = inspect_curl_profile(module, "chrome152_macos")
    assert report["supported"] is True
    assert report["platform"] is None
    assert report["platform_version"] is None
    assert report["browser_major"] == 152
    assert report["browser_major_source"] == "installed_profile_name"
    assert any("platform remains unknown" in text for text in report["unchecked"])


def test_unsupported_native_profile_does_not_load_custom_fingerprints_or_open_handle():
    module, calls = _fake_module()
    module.fingerprints.FingerprintManager = SimpleNamespace(
        load_fingerprints=lambda: pytest.fail("Must not load external fingerprint configuration"),
    )
    report = inspect_curl_profile(module, "chrome156")
    assert report["status"] == "unsupported"
    assert report["supported"] is False
    assert not calls


def test_native_library_rejection_overrides_installed_enum():
    module, calls = _fake_module(result=43)
    report = inspect_curl_profile(module, "chrome150")
    assert report["supported"] is False
    assert report["status"] == "unsupported"
    assert report["native_setup_return_code"] == 43
    assert report["findings"][0]["code"] == "native_setup_rejected"
    assert calls[-1] == ("close",)


def test_setup_exception_closes_handle_without_echoing_exception_text():
    module, calls = _fake_module(fail_setup=True)
    report = inspect_curl_profile(module, "chrome150")
    assert report["status"] == "unknown"
    assert report["supported"] is None
    assert calls[-1] == ("close",)
    assert "private" not in json.dumps(report)


def test_cleanup_exception_is_reported_as_unknown_without_echo():
    module, calls = _fake_module(fail_close=True)
    report = inspect_curl_profile(module, "chrome150")
    assert report["status"] == "unknown"
    assert report["supported"] is None
    assert report["findings"][0]["code"] == "native_close_failed"
    assert "private" not in json.dumps(report)
    assert calls[-1] == ("close",)


def test_absent_native_handle_keeps_declaration_evidence_explicit():
    module, calls = _fake_module()
    del module.Curl
    report = inspect_curl_profile(module, "chrome150")
    assert report["supported"] is True
    assert report["native_setup_return_code"] is None
    assert "offline_native_setup" not in report["verified_by"]
    assert any("declared by installed metadata" in text for text in report["unchecked"])
    assert not calls


def test_absent_native_metadata_cannot_establish_support():
    module, calls = _fake_module()
    del module.BrowserType
    del module.requests.impersonate.BrowserType
    del module.fingerprints
    report = inspect_curl_profile(module, "chrome150")
    assert report["status"] == "unknown"
    assert report["supported"] is None
    assert not calls


def test_unknown_registry_version_does_not_fall_back_to_guessing_profile_major():
    module, _ = _fake_module()
    module.fingerprints.NATIVE_IMPERSONATE_TARGETS[0]["version"] = "custom-build"
    report = inspect_curl_profile(module, "chrome150")
    assert report["browser_major"] is None
    assert report["browser_major_source"] is None


@pytest.mark.parametrize("target", [None, True, 150, "", "https://private.invalid/secret", "chrome\n150"])
def test_invalid_profile_input_never_creates_handle_or_echoes_arbitrary_value(target):
    module, calls = _fake_module()
    report = inspect_curl_profile(module, target)
    assert report["status"] == "invalid"
    assert report["profile"] is None
    assert not calls
    assert "secret" not in json.dumps(report)
