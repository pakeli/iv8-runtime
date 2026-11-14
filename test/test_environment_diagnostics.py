"""Offline diagnostics report relationships without exposing configured values."""

import copy
import importlib.util
import json
from pathlib import Path
import sys

import pytest

from iv8.page import _DEFAULT_ENVIRONMENT, _INT_KEYS


PACKAGE_DIRECTORY = Path(__file__).resolve().parents[1] / "tools" / "iv8_diagnostics"
if "iv8_diagnostics" not in sys.modules:
    package_spec = importlib.util.spec_from_file_location(
        "iv8_diagnostics", PACKAGE_DIRECTORY / "__init__.py",
        submodule_search_locations=[str(PACKAGE_DIRECTORY)],
    )
    package = importlib.util.module_from_spec(package_spec)
    sys.modules[package_spec.name] = package
    package_spec.loader.exec_module(package)
if "iv8_diagnostics.environment" not in sys.modules:
    environment_spec = importlib.util.spec_from_file_location(
        "iv8_diagnostics.environment", PACKAGE_DIRECTORY / "environment.py",
    )
    environment_module = importlib.util.module_from_spec(environment_spec)
    sys.modules[environment_spec.name] = environment_module
    environment_spec.loader.exec_module(environment_module)
diagnose_environment = sys.modules["iv8_diagnostics.environment"].diagnose_environment


MAC_UA = "Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) Chrome/156.0.0.0 Safari/537.36"
WINDOWS_UA = "Mozilla/5.0 (Windows NT 10.0; Win64; x64) Chrome/156.0.0.0 Safari/537.36"


def _profile():
    return dict(_DEFAULT_ENVIRONMENT, userAgent=MAC_UA, platform="MacIntel")


def _findings(report, code):
    return [finding for finding in report["findings"] if finding["code"] == code]


def test_complete_valid_profile_passes_without_echoing_input_or_mutating_it():
    environment = _profile()
    reference = {"browser_version": "156.0.0.0", "conditions": {"mode": "headless"},
                 "environment": copy.deepcopy(environment)}
    transport = {"browser_major": 156, "http_user_agent": MAC_UA}
    original = copy.deepcopy((environment, reference, transport))
    report = diagnose_environment(environment, reference, transport)
    assert report["schema_version"] == 1
    assert report["kind"] == "environment"
    assert report["status"] == "pass"
    assert report["checks"]["performed"] == report["checks"]["passed"]
    assert report["checks"]["performed"] > len(environment)
    assert report["findings"] == []
    assert (environment, reference, transport) == original
    assert MAC_UA not in json.dumps(report, allow_nan=False)
    assert {item["code"] for item in report["unchecked"]} == {"runtime_unobserved", "transport_unmeasured"}


@pytest.mark.parametrize("environment", [[], "private-invalid-value", {"unknown-private-key": 1},
                                           {"hardwareConcurrency": True}, {"languages": []},
                                           {"navigator": {}, "userAgent": "private-UA"}])
def test_invalid_environment_schema_returns_redacted_failure(environment):
    report = diagnose_environment(environment)
    assert report["status"] == "fail"
    assert report["checks"]["performed"] == 1
    assert _findings(report, "environment_schema")
    serialized = json.dumps(report, allow_nan=False)
    assert "private-invalid-value" not in serialized
    assert "unknown-private-key" not in serialized
    assert "private-UA" not in serialized


def test_partial_nested_reference_compares_only_explicit_fields():
    report = diagnose_environment(_profile(), reference={
        "browser_version": "156", "conditions": {},
        "environment": {"navigator": {"language": "fr-FR"}, "screen": {"width": 1920}},
    })
    assert report["status"] == "warning"
    differences = _findings(report, "reference_field")
    assert [finding["fields"] for finding in differences] == [["language", "reference.environment.language"]]
    unchecked_fields = next(item["fields"] for item in report["unchecked"]
                            if item["code"] == "reference_fields_missing")
    assert "userAgent" in unchecked_fields
    assert "screenWidth" not in unchecked_fields
    assert "language" not in unchecked_fields


def test_empty_reference_environment_does_not_compare_default_values():
    without_reference = diagnose_environment(_profile())
    with_reference = diagnose_environment(_profile(), reference={"environment": {}})
    assert with_reference["checks"]["performed"] == without_reference["checks"]["performed"]
    assert not _findings(with_reference, "reference_field")
    assert next(item["fields"] for item in with_reference["unchecked"]
                if item["code"] == "reference_fields_missing") == sorted(_DEFAULT_ENVIRONMENT)


def test_reference_metadata_without_environment_is_unchecked_not_matched():
    report = diagnose_environment(_profile(), reference={"browser_version": "156", "conditions": {}})
    assert report["status"] == "pass"
    assert any(item["code"] == "reference_environment_missing" for item in report["unchecked"])
    assert not _findings(report, "reference_field")


def test_unknown_reference_version_is_unchecked_without_guessing_or_echoing():
    report = diagnose_environment(_profile(), reference={
        "browser_version": "private-custom-build-token", "conditions": {}, "environment": {},
    })
    assert report["status"] == "pass"
    assert any(item["code"] == "reference_browser_version_unparsed" for item in report["unchecked"])
    assert "private-custom-build-token" not in json.dumps(report)


@pytest.mark.parametrize("reference", [False, [], {"browser_version": 156}, {"conditions": "private"},
                                       {"environment": {"screen": {"width": "private"}}},
                                       {"private-extra-key": 1}])
def test_invalid_reference_schema_fails(reference):
    report = diagnose_environment(_profile(), reference=reference)
    assert report["status"] == "fail"
    assert any(finding["code"].startswith("reference_") for finding in report["findings"])
    assert "private" not in json.dumps(report)


def test_unknown_ua_is_unknown_without_guessing_a_browser_version_or_os():
    environment = _profile()
    environment["userAgent"] = "PrivateProduct secret-user-token"
    report = diagnose_environment(environment, transport={"browser_major": 156})
    assert report["status"] == "unknown"
    assert _findings(report, "ua_transport_major")[0]["severity"] == "unknown"
    assert _findings(report, "ua_platform_relationship")[0]["severity"] == "unknown"
    assert "secret-user-token" not in json.dumps(report)


def test_declared_transport_and_http_js_majors_are_independently_compared():
    report = diagnose_environment(_profile(), transport={
        "browser_major": 150, "http_user_agent": MAC_UA.replace("156.0", "155.0"),
    })
    assert report["status"] == "warning"
    for code in ("ua_transport_major", "js_http_ua_major", "http_transport_major"):
        assert _findings(report, code)[0]["severity"] == "warning"
    assert any(item["code"] == "transport_unmeasured" for item in report["unchecked"])


@pytest.mark.parametrize("http_user_agent", [
    MAC_UA.replace("156.0.0.0", "156.0.1.2"),
    MAC_UA + " Edg/156.0.0.0",
])
def test_full_http_js_ua_declaration_mismatch_warns_when_major_and_os_match(http_user_agent):
    report = diagnose_environment(_profile(), transport={
        "browser_major": 156, "http_user_agent": http_user_agent,
    })
    assert report["status"] == "warning"
    assert _findings(report, "js_http_ua_declaration")[0]["severity"] == "warning"
    for code in ("ua_transport_major", "js_http_ua_major", "http_transport_major", "js_http_ua_os"):
        assert not _findings(report, code)
    assert http_user_agent not in json.dumps(report)


def test_explicit_os_conflict_and_primary_language_mismatch_warn():
    environment = _profile()
    environment.update(userAgent=WINDOWS_UA, language="fr-FR")
    report = diagnose_environment(environment)
    assert report["status"] == "warning"
    assert _findings(report, "ua_platform_relationship")[0]["severity"] == "warning"
    assert _findings(report, "primary_language")[0]["severity"] == "warning"


def test_android_linux_platform_is_compatible_and_signed_screen_coordinates_are_valid():
    environment = _profile()
    environment.update(userAgent="Mozilla/5.0 (Linux; Android 15) Chrome/156.0.0.0",
                       platform="Linux aarch64", screenX=-1200, screenY=-100,
                       screenAvailLeft=-1920, screenAvailTop=-40)
    report = diagnose_environment(environment)
    assert report["status"] == "pass"
    assert not _findings(report, "ua_platform_relationship")


@pytest.mark.parametrize("value", [float("nan"), float("inf"), float("-inf"), 0, -0.5])
def test_nonfinite_or_nonpositive_dpr_fails_and_output_is_strict_json(value):
    report = diagnose_environment(dict(_profile(), devicePixelRatio=value))
    assert report["status"] == "fail"
    assert _findings(report, "finite_positive_ratio")[0]["fields"] == ["devicePixelRatio"]
    json.dumps(report, allow_nan=False)


@pytest.mark.parametrize("field", ["innerWidth", "screenHeight", "hardwareConcurrency",
                                   "maxTouchPoints", "connectionRtt", "connectionDownlink"])
def test_negative_dimensions_or_capacity_fail(field):
    report = diagnose_environment(dict(_profile(), **{field: -1}))
    assert report["status"] == "fail"
    assert any(finding["severity"] == "fail" and finding["fields"] == [field]
               for finding in report["findings"])


@pytest.mark.parametrize("field", _INT_KEYS)
@pytest.mark.parametrize("value", [-(2 ** 31) - 1, 2 ** 31])
def test_all_native_integer_fields_reject_values_outside_signed_int32(field, value):
    report = diagnose_environment(dict(_profile(), **{field: value}))
    assert report["status"] == "fail"
    assert _findings(report, "native_int_range")[0]["fields"] == [field]
    assert _findings(report, "native_int_range")[0]["severity"] == "fail"


@pytest.mark.parametrize("field", ["screenX", "screenY", "screenAvailLeft", "screenAvailTop"])
@pytest.mark.parametrize("value", [-(2 ** 31), 2 ** 31 - 1])
def test_signed_native_coordinate_boundaries_remain_valid(field, value):
    report = diagnose_environment(dict(_profile(), **{field: value}))
    assert report["status"] == "pass"
    assert not _findings(report, "native_int_range")


def test_zero_hardware_concurrency_warns_without_rejecting_native_configuration():
    report = diagnose_environment(dict(_profile(), hardwareConcurrency=0))
    assert report["status"] == "warning"
    assert report["checks"]["failed"] == 0
    assert _findings(report, "hardware_concurrency_nonzero")[0]["severity"] == "warning"


def test_dimension_relationship_is_warning_and_not_a_hard_browser_invariant():
    report = diagnose_environment(dict(_profile(), innerWidth=2000))
    assert report["status"] == "warning"
    assert not report["checks"]["failed"]
    assert _findings(report, "dimension_relationship")[0]["fields"] == ["innerWidth", "outerWidth"]


@pytest.mark.parametrize("transport", [[], {"browser_major": True}, {"browser_major": 0},
                                       {"http_user_agent": 156}, {"private-key": "secret"}])
def test_invalid_transport_metadata_fails_without_echo(transport):
    report = diagnose_environment(_profile(), transport=transport)
    assert report["status"] == "fail"
    assert _findings(report, "transport_schema")
    assert "secret" not in json.dumps(report)


def test_missing_inputs_are_explicitly_unchecked_and_counts_add_up():
    report = diagnose_environment(None)
    assert report["status"] == "unknown"
    assert {item["code"] for item in report["unchecked"]} >= {"reference_missing", "transport_missing"}
    counts = report["checks"]
    assert counts["performed"] == sum(counts[key] for key in ("passed", "warning", "failed", "unknown"))
