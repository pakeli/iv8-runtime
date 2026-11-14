"""Shared request declarations remain aligned without importing an HTTP client."""

from copy import deepcopy
import importlib.util
import json
from pathlib import Path

import pytest


MODULE_PATH = Path(__file__).resolve().parents[1] / "tools/iv8_diagnostics/request_config.py"
SPEC = importlib.util.spec_from_file_location("standalone_request_config", MODULE_PATH)
MODULE = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(MODULE)
build_request_config = MODULE.build_request_config
audit_request_config = MODULE.audit_request_config
merge_request_headers = MODULE.merge_request_headers
MAC156 = ("Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) "
          "AppleWebKit/537.36 (KHTML, like Gecko) Chrome/156.0.0.0 Safari/537.36")


def _environment():
    return {"navigator": {"userAgent": MAC156, "platform": "MacIntel",
                          "language": "zh-CN", "languages": ["zh-CN", "zh", "en"],
                          "hardwareConcurrency": 8, "maxTouchPoints": 5},
            "screen": {"width": 1920}, "private": {"nested": [1]}}


def _transport(**overrides):
    return {"browser_major": 156, "platform": "macOS", "client_version": "0.16.3",
            "profile": "chrome156", **overrides}


def _config(**kwargs):
    return build_request_config(_environment(), impersonate="chrome156", transport=_transport(), **kwargs)


def test_mac156_is_single_source_and_independent_deepcopy():
    environment = _environment()
    headers = {"user-agent": "obsolete", "Accept-Language": "en-US", "sec-ch-ua-platform": '"Windows"',
               "sec-ch-ua-mobile": "?1", "Upgrade-Insecure-Requests": None,
               "Cookie": "private-cookie", "Authorization": "private-authorization"}
    transport = _transport()
    original = deepcopy((environment, headers, transport))
    config = build_request_config(environment, headers, impersonate="chrome156", transport=transport)
    assert config["headers"]["User-Agent"] == MAC156
    assert config["headers"]["Accept-Language"] == "zh-CN,zh;q=0.9,en;q=0.8"
    assert config["headers"]["sec-ch-ua-platform"] == '"macOS"'
    assert config["headers"]["sec-ch-ua-mobile"] == "?0"
    assert config["headers"]["Upgrade-Insecure-Requests"] is None
    assert config["default_headers"] is False
    assert "sec-ch-ua" not in config["headers"]
    assert audit_request_config(environment, config["headers"], transport)["status"] == "pass"
    assert (environment, headers, transport) == original
    config["environment"]["private"]["nested"].append(2)
    config["transport"]["profile"] = "changed"
    assert (environment, headers, transport) == original


def test_flat_environment_and_unrelated_fields_are_preserved():
    environment = dict(_environment()["navigator"], screenWidth=1920)
    config = build_request_config(environment)
    assert config["environment"] == environment
    assert config["environment"] is not environment
    assert config["headers"]["User-Agent"] == MAC156


@pytest.mark.parametrize("field,value", [("User-Agent", "private-UA"),
                                         ("Accept-Language", "zh-CN,zh;q=0.9"),
                                         ("sec-ch-ua-platform", '"Windows"'),
                                         ("sec-ch-ua-mobile", "?1")])
def test_audit_rejects_each_known_header_mismatch_without_echoing(field, value):
    config = _config()
    config["headers"][field] = value
    report = audit_request_config(config["environment"], config["headers"], config["transport"])
    assert report["status"] == "fail"
    assert MAC156 not in json.dumps(report)
    assert "private-UA" not in json.dumps(report)


def test_windows_navigator_with_mac_user_agent_is_rejected():
    environment = _environment()
    environment["navigator"]["platform"] = "Win32"
    with pytest.raises(ValueError, match="configuration conflicts"):
        build_request_config(environment)


@pytest.mark.parametrize("header", ["User-Agent", "Accept-Language", "sec-ch-ua", "sec-ch-ua-platform", "sec-ch-ua-mobile"])
def test_identity_none_is_rejected_in_build_and_merge(header):
    with pytest.raises(ValueError):
        build_request_config(_environment(), {header: None})
    with pytest.raises(ValueError):
        merge_request_headers(_config(), {header: None})


def test_duplicate_case_identity_headers_cannot_hide_conflicts():
    with pytest.raises(ValueError):
        build_request_config(_environment(), {"User-Agent": MAC156, "user-agent": "private-UA"})
    config = build_request_config(_environment(), {"User-Agent": MAC156, "user-agent": MAC156})
    assert sum(name.lower() == "user-agent" for name in config["headers"]) == 1


def test_request_headers_preserve_removal_semantics_and_prevent_identity_override():
    config = _config(headers={"Accept": "text/html", "Upgrade-Insecure-Requests": "1"})
    original = deepcopy(config)
    merged = merge_request_headers(config, {"accept": "*/*", "Upgrade-Insecure-Requests": None,
                                            "user-agent": MAC156})
    assert merged["accept"] == "*/*"
    assert merged["Upgrade-Insecure-Requests"] is None
    assert sum(name.lower() == "user-agent" for name in merged) == 1
    assert config == original
    for headers in ({"User-Agent": "private-UA"}, {"Accept-Language": "en-US"},
                    {"sec-ch-ua-platform": '"Windows"'}, {"sec-ch-ua-full-version": '"156.0"'}):
        with pytest.raises(ValueError) as caught:
            merge_request_headers(config, headers)
        assert "private-UA" not in str(caught.value)


def test_existing_brands_are_checked_without_repairing_grease():
    brands = '"Not,A;Brand";v="99", "Chromium";v="156", "Google Chrome";v="156"'
    config = _config(headers={"sec-ch-ua": brands})
    assert config["headers"]["sec-ch-ua"] == brands
    assert audit_request_config(config["environment"], config["headers"], config["transport"])["status"] == "pass"
    with pytest.raises(ValueError):
        _config(headers={"sec-ch-ua": brands.replace('v="156"', 'v="150"')})


@pytest.mark.parametrize("value", ['"Chromium";v="156",', 'Chromium;v="156"', '"Chromium";v="private"'])
def test_malformed_or_unverifiable_chromium_versions_fail(value):
    with pytest.raises(ValueError):
        _config(headers={"sec-ch-ua": value})


@pytest.mark.parametrize("impersonate", ["chrome", "chrome150", "chrome156_android", 156, True, "private-profile"])
def test_impersonation_must_be_pinned_and_match_ua_and_transport(impersonate):
    with pytest.raises(ValueError) as caught:
        build_request_config(_environment(), impersonate=impersonate, transport=_transport())
    assert "private-profile" not in str(caught.value)


def test_explicit_platform_profile_and_transport_profile_must_agree():
    transport = _transport(profile="chrome156_macos")
    assert build_request_config(_environment(), impersonate="chrome156_macos", transport=transport)["impersonate"] == "chrome156_macos"
    with pytest.raises(ValueError):
        build_request_config(_environment(), impersonate="chrome156", transport=transport)


@pytest.mark.parametrize("metadata", [None, {}, {"profile": "chrome156"}, _transport(platform="private-OS")])
def test_missing_or_unknown_transport_evidence_stays_unknown(metadata):
    config = build_request_config(_environment(), transport=metadata)
    report = audit_request_config(config["environment"], config["headers"], metadata)
    assert report["status"] == "unknown"
    assert "private-OS" not in json.dumps(report)


@pytest.mark.parametrize("field,value", [("browser_major", True), ("browser_major", 156.0),
                                         ("browser_major", 0), ("browser_major", -1),
                                         ("platform", 1), ("client_version", 1), ("profile", 156)])
def test_invalid_transport_numbers_and_types_are_failures(field, value):
    config = _config()
    report = audit_request_config(config["environment"], config["headers"], _transport(**{field: value}))
    assert report["status"] == "fail"


@pytest.mark.parametrize("field,value", [("userAgent", 156), ("platform", True), ("language", 1),
                                         ("languages", "en-US"), ("languages", []), ("languages", [1]),
                                         ("languages", ["zh-CN", "zh-cn"]), ("language", "en-US")])
def test_invalid_or_inconsistent_navigator_input_is_rejected(field, value):
    environment = _environment()
    environment["navigator"][field] = value
    with pytest.raises(ValueError):
        build_request_config(environment)


def test_android_mobile_comes_from_ua_and_not_touch_capacity():
    environment = _environment()
    environment["navigator"].update(userAgent="Mozilla/5.0 (Linux; Android 15) Chrome/156.0.0.0 Mobile Safari/537.36",
                                     platform="Linux armv8l", maxTouchPoints=0)
    assert build_request_config(environment)["headers"]["sec-ch-ua-mobile"] == "?1"
    environment["navigator"]["userAgent"] = environment["navigator"]["userAgent"].replace(" Mobile", "")
    assert build_request_config(environment)["headers"]["sec-ch-ua-mobile"] == "?0"


def test_sensitive_unrelated_headers_never_enter_reports():
    config = _config(headers={"Cookie": "private-cookie-78231", "Authorization": "private-auth-78231",
                              "Upgrade-Insecure-Requests": None, "sec-ch-ua-arch": '"private-arch-78231"'})
    report = audit_request_config(config["environment"], config["headers"], config["transport"])
    serialized = json.dumps(report, allow_nan=False)
    assert "private-" not in serialized
    assert MAC156 not in serialized
    assert any(item["code"] == "ua_ch_high_entropy" for item in report["unchecked"])
    assert any(item["code"] == "runtime_and_tls_unmeasured" for item in report["unchecked"])


def test_invalid_control_character_in_brand_is_a_structured_failure():
    config = _config()
    config["headers"]["sec-ch-ua"] = '"Google\tChrome";v="156"'
    report = audit_request_config(config["environment"], config["headers"], config["transport"])
    assert report["status"] == "fail"
    assert any(item["code"] == "ua_ch_brands_schema" for item in report["findings"])
