"""Offline scenario verdicts, reproducibility and process isolation."""

from hashlib import sha256
import importlib
import importlib.util
import json
from pathlib import Path
import subprocess
import sys

import pytest

import iv8


ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "tools" / "iv8_diagnostics"
if "iv8_diagnostics" not in sys.modules:
    package_spec = importlib.util.spec_from_file_location(
        "iv8_diagnostics", PACKAGE / "__init__.py",
        submodule_search_locations=[str(PACKAGE)],
    )
    package_module = importlib.util.module_from_spec(package_spec)
    sys.modules[package_spec.name] = package_module
    package_spec.loader.exec_module(package_module)
runner = importlib.import_module("iv8_diagnostics.runner")

on_only = pytest.mark.skipif(
    not getattr(iv8, "_v8_linked", False), reason="requires a V8-linked build",
)
BASE = "https://offline-scenarios.test/start"
SCRIPT_URL = "https://offline-scenarios.test/collector.js"


def _write_manifest(path, manifest):
    path.write_text(json.dumps(manifest), encoding="utf-8")
    return path


def _scenario(tmp_path, source="globalThis.answer = 42;", **overrides):
    html_path = tmp_path / "page.html"
    html_path.write_text(
        '<html><head><script src="/collector.js"></script></head>'
        '<body><div id="result"></div></body></html>', encoding="utf-8",
    )
    script_path = tmp_path / "collector.js"
    script_path.write_text(source, encoding="utf-8")
    manifest = {
        "schema_version": 1,
        "id": "synthetic",
        "base_url": BASE,
        "html": {
            "path": html_path.name,
            "sha256": sha256(html_path.read_bytes()).hexdigest(),
        },
        "resources": [{
            "url": SCRIPT_URL, "path": script_path.name,
            "sha256": sha256(script_path.read_bytes()).hexdigest(),
        }],
        "environment": {},
        "steps": [],
        "checks": [{"id": "answer", "expression": "answer", "expected": 42}],
    }
    manifest.update(overrides)
    path = _write_manifest(tmp_path / "scenario.json", manifest)
    return path, manifest


@on_only
def test_success_records_assets_runtime_and_separate_verdicts(tmp_path):
    path, manifest = _scenario(tmp_path)
    report = runner.run_isolated_scenario(path)
    assert report["schema_version"] == 1
    assert report["id"] == manifest["id"]
    assert report["execution"]["status"] == "passed"
    assert report["execution"]["first_error"] is None
    assert report["checks"]["status"] == "passed"
    assert report["regression"]["status"] == "passed"
    assert report["server"]["status"] == "not_tested"
    hashes = {asset["sha256"] for asset in report["assets"]}
    assert hashes == {
        manifest["html"]["sha256"], manifest["resources"][0]["sha256"],
    }
    assert len(report["runtime"]["native_sha256"]) == 64


def test_resource_hash_mismatch_prevents_script_execution(tmp_path):
    path, manifest = _scenario(tmp_path, "while(true) {}")
    manifest["resources"][0]["sha256"] = "0" * 64
    _write_manifest(path, manifest)
    report = runner.run_isolated_scenario(path, timeout=2)
    assert report["execution"]["status"] == "invalid"
    assert report["regression"]["status"] == "failed"
    assert report["checks"]["status"] == "not_run"
    assert report["server"]["status"] == "not_tested"


@pytest.mark.parametrize("overrides", [
    {"schema_version": 2},
    {"schema_version": True},
    {"id": ""},
    {"steps": [{"action": "advance", "ms": -1, "max_steps": 2}]},
    {"steps": [{"action": "run_until_idle", "max_steps": True}]},
    {"steps": [{"action": "fetch", "url": "https://example.invalid/"}]},
])
def test_invalid_schema_and_pump_options_are_not_run(tmp_path, overrides):
    path, _ = _scenario(tmp_path, "while(true) {}", **overrides)
    report = runner.run_isolated_scenario(path, timeout=2)
    assert report["execution"]["status"] == "invalid"
    assert report["regression"]["status"] == "failed"


@pytest.mark.parametrize("field,value", [
    ("phase", []), ("phase", {}), ("channel", []), ("channel", {}),
])
def test_non_string_expected_error_metadata_is_invalid(tmp_path, field, value):
    expected_error = {"phase": "load", "channel": "synchronous", "name": "TypeError"}
    expected_error[field] = value
    path, _ = _scenario(
        tmp_path, "while(true) {}", checks=[], expected_error=expected_error,
    )
    report = runner.run_isolated_scenario(path, timeout=2)
    assert report["execution"]["status"] == "invalid"
    assert report["regression"]["status"] == "failed"


@pytest.mark.parametrize("field", ["resources", "checks"])
def test_duplicate_resource_urls_and_check_ids_are_invalid(tmp_path, field):
    path, manifest = _scenario(tmp_path, "while(true) {}")
    manifest[field].append(dict(manifest[field][0]))
    _write_manifest(path, manifest)
    report = runner.run_isolated_scenario(path, timeout=2)
    assert report["execution"]["status"] == "invalid"
    assert report["regression"]["status"] == "failed"


@pytest.mark.parametrize("original,duplicate", [
    ('"id": "synthetic"', '"id": "synthetic", "id": "duplicate"'),
    ('"path": "page.html"', '"path": "page.html", "path": "different.html"'),
])
def test_duplicate_json_keys_are_rejected_at_all_levels(tmp_path, original, duplicate):
    path, manifest = _scenario(tmp_path, "while(true) {}")
    path.write_text(json.dumps(manifest).replace(original, duplicate, 1), encoding="utf-8")
    report = runner.run_isolated_scenario(path, timeout=2)
    assert report["execution"]["status"] == "invalid"
    assert report["regression"]["status"] == "failed"


@pytest.mark.skipif(not Path("/dev/zero").exists(), reason="requires a Unix character device")
@pytest.mark.parametrize("target", ["manifest", "asset"])
def test_non_regular_inputs_are_rejected_without_reading_device(tmp_path, target):
    path, manifest = _scenario(tmp_path, "while(true) {}")
    if target == "manifest":
        path = Path("/dev/zero")
    else:
        manifest["html"]["path"] = "/dev/zero"
        _write_manifest(path, manifest)
    report = runner.run_isolated_scenario(path, timeout=2)
    assert report["execution"]["status"] == "invalid"
    assert report["regression"]["status"] == "failed"


def test_missing_local_asset_is_invalid(tmp_path):
    path, manifest = _scenario(tmp_path)
    manifest["html"]["path"] = "does-not-exist.html"
    _write_manifest(path, manifest)
    report = runner.run_isolated_scenario(path)
    assert report["execution"]["status"] == "invalid"
    assert report["regression"]["status"] == "failed"


@on_only
def test_sync_failure_preserves_phase_and_source_location(tmp_path):
    path, _ = _scenario(
        tmp_path, "\nthrow new TypeError('synthetic sync failure');", checks=[],
    )
    report = runner.run_isolated_scenario(path)
    assert report["execution"]["status"] == "failed"
    error = report["execution"]["first_error"]
    assert error["phase"] == "load"
    assert error["channel"] == "synchronous"
    assert error["name"] == "TypeError"
    assert error["resource_name"] == "<resource:0>"
    assert error["resource"]["sha256"] == sha256(SCRIPT_URL.encode()).hexdigest()
    assert error["line"] == 2
    assert isinstance(error["column"], int)
    assert report["regression"]["status"] == "failed"
    assert report["checks"]["status"] == "not_run"


@on_only
def test_timer_failure_is_observed_at_its_pump(tmp_path):
    path, _ = _scenario(
        tmp_path,
        "setTimeout(() => {throw new RangeError('synthetic async failure');}, 5);",
        steps=[{"action": "advance", "ms": 10, "max_steps": 20}], checks=[],
    )
    report = runner.run_isolated_scenario(path)
    error = report["execution"]["first_error"]
    assert report["execution"]["status"] == "failed"
    assert error["phase"] == "step:0"
    assert error["channel"] == "asynchronous"
    assert error["name"] == "RangeError"
    assert error["resource_name"] == "<resource:0>"
    assert error["resource"]["sha256"] == sha256(SCRIPT_URL.encode()).hexdigest()
    assert len(report["execution"]["checkpoints"]) == 2


@on_only
def test_missing_offline_script_is_execution_failure(tmp_path):
    path, _ = _scenario(tmp_path, resources=[], checks=[])
    report = runner.run_isolated_scenario(path)
    assert report["execution"]["status"] == "failed"
    error = report["execution"]["first_error"]
    assert error["resource_name"] == "<unmapped>"
    assert error["resource"]["sha256"] == sha256(SCRIPT_URL.encode()).hexdigest()
    assert report["server"]["status"] == "not_tested"


@on_only
def test_expected_failure_passes_only_the_regression_verdict(tmp_path):
    path, _ = _scenario(
        tmp_path, "throw new TypeError('expected negative control');", checks=[],
        expected_error={
            "phase": "load", "channel": "synchronous", "name": "TypeError",
            "resource_name": SCRIPT_URL,
        },
    )
    report = runner.run_isolated_scenario(path)
    assert report["regression"]["status"] == "passed"
    assert report["execution"]["status"] == "failed"
    assert report["checks"]["status"] == "not_run"
    assert report["server"]["status"] == "not_tested"


@on_only
def test_expected_resource_with_private_query_matches_by_original_hash(tmp_path):
    secret = "private-resource-token-61782"
    resource_url = SCRIPT_URL + "?session=" + secret
    path, manifest = _scenario(
        tmp_path, "throw new TypeError('expected negative control');", checks=[],
        expected_error={
            "phase": "load", "channel": "synchronous", "name": "TypeError",
            "resource_name": resource_url,
        },
    )
    html_path = path.parent / manifest["html"]["path"]
    html_path.write_text(
        '<html><head><script src="' + resource_url + '"></script></head></html>',
        encoding="utf-8",
    )
    manifest["html"]["sha256"] = sha256(html_path.read_bytes()).hexdigest()
    manifest["resources"][0]["url"] = resource_url
    _write_manifest(path, manifest)
    report = runner.run_isolated_scenario(path)
    assert report["execution"]["status"] == "failed"
    assert report["regression"]["status"] == "passed"
    error = report["execution"]["first_error"]
    assert error["resource_name"] == "<resource:0>"
    assert error["resource"]["sha256"] == sha256(resource_url.encode()).hexdigest()
    serialized = json.dumps(report)
    assert secret not in serialized
    assert resource_url not in serialized


@on_only
def test_custom_error_name_message_and_stack_do_not_disclose_values(tmp_path):
    secret = "privateErrorName78231"
    source = (
        "const failure = new Error(" + json.dumps(secret) + ");"
        "failure.name = " + json.dumps(secret) + ";throw failure;"
    )
    path, _ = _scenario(tmp_path, source, checks=[])
    report = runner.run_isolated_scenario(path)
    assert report["execution"]["status"] == "failed"
    error = report["execution"]["first_error"]
    assert error["name"] != secret
    assert error["message"]["sha256"] == sha256(secret.encode()).hexdigest()
    assert secret not in json.dumps(report)


@on_only
def test_custom_expected_error_name_matches_without_disclosing_original(tmp_path):
    private_name = "privateExpectedErrorName31871"
    source = (
        "const failure = new Error('negative control');"
        "failure.name = " + json.dumps(private_name) + ";throw failure;"
    )
    path, _ = _scenario(
        tmp_path, source, checks=[],
        expected_error={"phase": "load", "channel": "synchronous", "name": private_name},
    )
    report = runner.run_isolated_scenario(path)
    assert report["execution"]["status"] == "failed"
    assert report["regression"]["status"] == "passed"
    error = report["execution"]["first_error"]
    assert error["name"] == "Error"
    assert error["original_name"]["sha256"] == sha256(private_name.encode()).hexdigest()
    assert private_name not in json.dumps(report)


@on_only
def test_different_failure_cannot_satisfy_negative_control(tmp_path):
    path, _ = _scenario(
        tmp_path, "throw new RangeError('unexpected failure');", checks=[],
        expected_error={"phase": "load", "name": "TypeError"},
    )
    report = runner.run_isolated_scenario(path)
    assert report["execution"]["status"] == "failed"
    assert report["regression"]["status"] == "failed"


@on_only
def test_expected_failure_must_actually_occur(tmp_path):
    path, _ = _scenario(
        tmp_path, checks=[], expected_error={"phase": "load", "name": "TypeError"},
    )
    report = runner.run_isolated_scenario(path)
    assert report["execution"]["status"] == "passed"
    assert report["regression"]["status"] == "failed"


@on_only
def test_check_failure_does_not_replace_execution_verdict(tmp_path):
    path, _ = _scenario(
        tmp_path, checks=[{"id": "wrong-answer", "expression": "answer", "expected": 0}],
    )
    report = runner.run_isolated_scenario(path)
    assert report["execution"]["status"] == "passed"
    assert report["execution"]["first_error"] is None
    assert report["checks"]["status"] == "failed"
    assert report["checks"]["results"][0]["status"] == "failed"
    assert report["regression"]["status"] == "failed"


@on_only
@pytest.mark.parametrize("expression,expected", [
    ("true", 1),
    ("[true]", [1]),
    ("({value:false})", {"value": 0}),
    ("undefined", None),
])
def test_check_comparison_preserves_json_type_distinctions(tmp_path, expression, expected):
    path, _ = _scenario(
        tmp_path, checks=[{"id": "json-type", "expression": expression, "expected": expected}],
    )
    report = runner.run_isolated_scenario(path)
    assert report["execution"]["status"] == "passed"
    assert report["checks"]["status"] == "failed"
    assert report["regression"]["status"] == "failed"


@on_only
def test_throwing_check_does_not_pollute_collector_trace(tmp_path):
    path, _ = _scenario(
        tmp_path,
        checks=[{
            "id": "throwing-check",
            "expression": "(()=>{navigator.checkOnlyMissing;throw new Error('check');})()",
            "expected": True,
        }],
    )
    report = runner.run_isolated_scenario(path)
    assert report["execution"]["status"] == "passed"
    assert report["execution"]["first_error"] is None
    assert report["checks"]["status"] == "failed"
    assert sha256(b"checkOnlyMissing").hexdigest() not in json.dumps(report["trace"])


@on_only
def test_check_timeout_does_not_replace_completed_execution_verdict(tmp_path):
    path, _ = _scenario(
        tmp_path,
        checks=[{
            "id": "infinite-check", "expression": "(()=>{while(true) {}})()",
            "expected": True,
        }],
    )
    report = runner.run_isolated_scenario(path, timeout=2)
    assert report["execution"]["status"] == "passed"
    assert report["execution"]["first_error"] is None
    assert report["checks"]["status"] == "failed"
    assert report["checks"]["reason"] == "timeout"
    assert report["regression"]["status"] == "failed"
    assert report["server"]["status"] == "not_tested"


@on_only
def test_task_budget_exhaustion_is_incomplete(tmp_path):
    path, _ = _scenario(
        tmp_path, "setInterval(() => {}, 0);",
        steps=[{"action": "run_until_idle", "max_steps": 2}], checks=[],
    )
    report = runner.run_isolated_scenario(path)
    assert report["execution"]["status"] == "incomplete"
    assert report["execution"]["first_error"] is None
    assert report["regression"]["status"] == "failed"


@on_only
def test_idle_tick_is_not_mistaken_for_budget_exhaustion(tmp_path):
    path, _ = _scenario(tmp_path, steps=[{"action": "tick"}])
    report = runner.run_isolated_scenario(path)
    assert report["execution"]["status"] == "passed"
    assert report["regression"]["status"] == "passed"


@on_only
def test_infinite_script_is_stopped_by_worker_wall_timeout(tmp_path):
    path, _ = _scenario(tmp_path, "while(true) {}", checks=[])
    report = runner.run_isolated_scenario(path, timeout=2)
    assert report["execution"]["status"] == "timeout"
    assert report["regression"]["status"] == "failed"
    assert report["server"]["status"] == "not_tested"
    # A subsequent worker must still execute normally after the timed-out one.
    path, _ = _scenario(tmp_path)
    assert runner.run_isolated_scenario(path)["regression"]["status"] == "passed"


@on_only
def test_timeout_preserves_verified_identity_and_completed_checkpoints(tmp_path):
    path, manifest = _scenario(
        tmp_path, "setTimeout(() => {while(true) {}}, 0);", checks=[],
        steps=[{"action": "advance", "ms": 1, "max_steps": 10}],
    )
    report = runner.run_isolated_scenario(path, timeout=2)
    assert report["execution"]["status"] == "timeout"
    assert report["regression"]["status"] == "failed"
    assert report["id"] == manifest["id"]
    assert report["manifest_sha256"] == sha256(path.read_bytes()).hexdigest()
    assert report["runtime"]["v8_linked"] is True
    assert len(report["runtime"]["native_sha256"]) == 64
    assert {asset["sha256"] for asset in report["assets"]} == {
        manifest["html"]["sha256"], manifest["resources"][0]["sha256"],
    }
    assert [item["phase"] for item in report["execution"]["checkpoints"]] == ["load"]
    assert report["server"]["status"] == "not_tested"


@on_only
def test_repeated_scenarios_do_not_share_page_state(tmp_path):
    source = (
        "globalThis.answer = (globalThis.answer || 0) + 1;"
        "document.cookie = 'scenarioCounter=' + answer;"
    )
    path, _ = _scenario(
        tmp_path, source,
        checks=[{"id": "fresh-global", "expression": "answer", "expected": 1}],
    )
    first = runner.run_isolated_scenario(path)
    second = runner.run_isolated_scenario(path)
    assert first["regression"]["status"] == "passed"
    assert second["regression"]["status"] == "passed"
    assert first["assets"] == second["assets"]
    assert first["runtime"]["native_sha256"] == second["runtime"]["native_sha256"]


@on_only
def test_report_omits_cookie_and_check_return_values(tmp_path):
    secret = "private-cookie-and-check-value-78231"
    source = (
        "globalThis.secret = " + json.dumps(secret) + ";"
        "document.cookie = 'session=' + secret; document.cookie;"
        "navigator.syntheticMissing;location.reload();"
    )
    path, _ = _scenario(
        tmp_path, source,
        checks=[{"id": "private-result", "expression": "secret", "expected": "wrong"}],
    )
    report = runner.run_isolated_scenario(path)
    serialized = json.dumps(report)
    assert secret not in serialized
    assert "value_summary" not in serialized
    assert "return_summary" not in serialized
    assert report["navigation"]["count"] == 1
    assert any(
        item["member_sha256"] == sha256(b"syntheticMissing").hexdigest()
        for item in report["trace"]["missing_candidates"]
    )


@on_only
def test_environment_diagnostics_do_not_change_execution_status(tmp_path):
    path, _ = _scenario(
        tmp_path,
        environment={"language": "en-US", "languages": ["fr-FR"]},
    )
    report = runner.run_isolated_scenario(path)
    assert report["execution"]["status"] == "passed"
    assert report["checks"]["status"] == "passed"
    assert report["environment"]["status"] == "warning"


@on_only
@pytest.mark.parametrize("expected,exit_code,status", [(42, 0, "passed"), (0, 1, "failed")])
def test_cli_runs_from_another_directory_and_writes_separate_reports(
        tmp_path, expected, exit_code, status):
    path, _ = _scenario(
        tmp_path, checks=[{"id": "answer", "expression": "answer", "expected": expected}],
    )
    other_directory = tmp_path / "other-cwd"
    other_directory.mkdir()
    output = tmp_path / "reports"
    completed = subprocess.run(
        [sys.executable, str(ROOT / "tools" / "run_offline_scenarios.py"),
         str(path), "--output", str(output)],
        cwd=other_directory, capture_output=True, text=True, timeout=20,
    )
    assert completed.returncode == exit_code, completed.stderr
    summary = json.loads((output / "summary.json").read_text(encoding="utf-8"))
    assert summary["status"] == status
    assert json.loads(completed.stdout) == summary
    entry, = summary["scenarios"]
    report = json.loads((output / entry["report"]).read_text(encoding="utf-8"))
    assert report["execution"]["status"] == "passed"
    assert report["regression"]["status"] == status
    assert report["server"]["status"] == "not_tested"
