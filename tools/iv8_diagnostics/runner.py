"""Pinned offline Page replay with per-scenario process isolation."""

import argparse
from hashlib import sha256
import json
import math
import os
from pathlib import Path
import re
import stat
import subprocess
import sys
import tempfile
from urllib.parse import urlsplit

try:
    import iv8
    from .environment import diagnose_environment
except ImportError:
    iv8 = None
    diagnose_environment = None


MAX_ASSET_BYTES = 16 * 1024 * 1024
MAX_TOTAL_BYTES = 64 * 1024 * 1024
MAX_STEPS = 100
MAX_CHECKS = 100
IDENTIFIER = re.compile(r"[A-Za-z0-9][A-Za-z0-9_.-]{0,79}\Z")
DIGEST = re.compile(r"[0-9a-f]{64}\Z")
ERROR_NAMES = {"Error", "EvalError", "RangeError", "ReferenceError", "SyntaxError",
               "TypeError", "URIError", "AggregateError", "DataCloneError",
               "SecurityError", "NotSupportedError", "InvalidStateError"}
CLI = Path(__file__).resolve().parents[1] / "run_offline_scenarios.py"


class ManifestError(ValueError):
    """The scenario cannot be safely interpreted as this schema version."""


def _hash_bytes(value):
    return sha256(value).hexdigest()


def _text_evidence(value):
    text = str(value)
    return {"sha256": _hash_bytes(text.encode("utf-8", errors="replace")),
            "characters": len(text)}


def _object_pairs(pairs):
    result = {}
    for key, value in pairs:
        if key in result:
            raise ManifestError("duplicate JSON object key")
        result[key] = value
    return result


def _reject_constant(value):
    raise ManifestError("non-finite JSON number")


def _read_file(path, limit):
    if not path.is_file():
        raise ManifestError("scenario inputs must be regular files")
    with path.open("rb") as source:
        if not stat.S_ISREG(os.fstat(source.fileno()).st_mode):
            raise ManifestError("scenario inputs must be regular files")
        raw = source.read(limit + 1)
    if len(raw) > limit:
        raise ManifestError("scenario file exceeds size limit")
    return raw


def _keys(value, required, optional=()):
    if not isinstance(value, dict):
        raise ManifestError("expected an object")
    if not set(required) <= value.keys() or value.keys() - set(required) - set(optional):
        raise ManifestError("missing or unknown manifest field")


def _integer(value, minimum, maximum):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ManifestError("integer outside scenario limits")


def _identifier(value):
    if not isinstance(value, str) or not IDENTIFIER.fullmatch(value):
        raise ManifestError("invalid scenario/check identifier")


def _url(value):
    if not isinstance(value, str):
        raise ManifestError("URL must be a string")
    try:
        parsed = urlsplit(value)
        if parsed.scheme not in {"http", "https"} or not parsed.hostname or parsed.username or parsed.password:
            raise ValueError
        parsed.port
    except ValueError as error:
        raise ManifestError("expected an absolute HTTP(S) URL without credentials") from error


def _json_value(value):
    if value is None or type(value) in {str, bool, int}:
        return
    if type(value) is float and math.isfinite(value):
        return
    if isinstance(value, list):
        for item in value:
            _json_value(item)
        return
    if isinstance(value, dict) and all(isinstance(key, str) for key in value):
        for item in value.values():
            _json_value(item)
        return
    raise ManifestError("expected a finite JSON value")


def load_scenario(path):
    """Validate all inputs before any Page exists; execute these same bytes."""
    manifest_path = Path(path).resolve()
    manifest_bytes = _read_file(manifest_path, 1024 * 1024)
    manifest = json.loads(manifest_bytes, object_pairs_hook=_object_pairs,
                          parse_constant=_reject_constant)
    _json_value(manifest)
    _keys(manifest, {"schema_version", "id", "base_url", "html"},
          {"resources", "environment", "reference", "transport", "steps", "checks", "expected_error"})
    if type(manifest["schema_version"]) is not int or manifest["schema_version"] != 1:
        raise ManifestError("unsupported schema_version")
    _identifier(manifest["id"])
    _url(manifest["base_url"])
    for field in ("environment", "reference", "transport"):
        if field in manifest and not isinstance(manifest[field], dict):
            raise ManifestError("environment/reference/transport must be objects")
    resources = manifest.get("resources", [])
    if not isinstance(resources, list) or len(resources) > 100:
        raise ManifestError("resources must be a list of at most 100 entries")
    steps = manifest.get("steps", [])
    if not isinstance(steps, list) or len(steps) > MAX_STEPS:
        raise ManifestError("steps must be a bounded list")
    actions = {"jobs", "timers", "animation_frame", "idle", "tick", "advance", "run_until_idle"}
    for step in steps:
        _keys(step, {"action"}, {"ms", "max_steps"})
        action = step["action"]
        if not isinstance(action, str) or action not in actions:
            raise ManifestError("unknown pump action")
        if action == "advance":
            _keys(step, {"action", "ms", "max_steps"})
            _integer(step["ms"], 0, 86_400_000)
        elif "ms" in step:
            raise ManifestError("ms is only valid for advance")
        if action in {"advance", "run_until_idle"}:
            if "max_steps" not in step:
                raise ManifestError("bounded pump requires max_steps")
            _integer(step["max_steps"], 1, 10000)
        elif "max_steps" in step:
            raise ManifestError("max_steps is not valid for this pump")
    checks = manifest.get("checks", [])
    if not isinstance(checks, list) or len(checks) > MAX_CHECKS:
        raise ManifestError("checks must be a bounded list")
    check_ids = set()
    for check in checks:
        _keys(check, {"id", "expression", "expected"})
        _identifier(check["id"])
        if check["id"] in check_ids:
            raise ManifestError("duplicate check identifier")
        check_ids.add(check["id"])
        if not isinstance(check["expression"], str) or not check["expression"].strip():
            raise ManifestError("check expression must be non-empty JavaScript")
    if "expected_error" in manifest:
        expected = manifest["expected_error"]
        _keys(expected, {"phase", "name"}, {"channel", "resource_name", "line", "column"})
        if checks:
            raise ManifestError("expected-error scenarios cannot also require checks")
        phases = {"load"} | {f"step:{index}" for index in range(len(steps))}
        if not isinstance(expected["phase"], str) or expected["phase"] not in phases or not isinstance(expected["name"], str):
            raise ManifestError("invalid expected error phase/name")
        if "channel" in expected and (not isinstance(expected["channel"], str) or expected["channel"] not in {"synchronous", "asynchronous"}):
            raise ManifestError("invalid expected error channel")
        for field in ("line", "column"):
            if field in expected:
                _integer(expected[field], 0, 2**31 - 1)
        if "resource_name" in expected and not isinstance(expected["resource_name"], str):
            raise ManifestError("expected resource_name must be a string")
    assets = []
    total_bytes = 0

    def read_asset(asset, role):
        nonlocal total_bytes
        _keys(asset, {"path", "sha256"} | ({"url"} if role == "resource" else set()))
        if not isinstance(asset["path"], str) or not asset["path"]:
            raise ManifestError("asset path must be non-empty")
        if not isinstance(asset["sha256"], str) or not DIGEST.fullmatch(asset["sha256"]):
            raise ManifestError("asset SHA-256 must be lowercase hex")
        asset_path = manifest_path.parent / asset["path"]
        raw = _read_file(asset_path, MAX_ASSET_BYTES)
        total_bytes += len(raw)
        if len(raw) > MAX_ASSET_BYTES or total_bytes > MAX_TOTAL_BYTES:
            raise ManifestError("scenario assets exceed size limit")
        if _hash_bytes(raw) != asset["sha256"]:
            raise ManifestError("asset SHA-256 mismatch")
        record = {"role": role, "sha256": asset["sha256"], "bytes": len(raw)}
        if role == "resource":
            record["url_sha256"] = _hash_bytes(asset["url"].encode())
        assets.append(record)
        return raw.decode("utf-8")

    html = read_asset(manifest["html"], "html")
    sources = {}
    for resource in resources:
        if not isinstance(resource, dict) or "url" not in resource:
            raise ManifestError("resource requires URL")
        _url(resource["url"])
        if resource["url"] in sources:
            raise ManifestError("duplicate resource URL")
        sources[resource["url"]] = read_asset(resource, "resource")
    return manifest, html, sources, assets, _hash_bytes(manifest_bytes)


def _empty_result():
    return {"schema_version": 1, "id": None, "worker_phase": "validation",
            "regression": {"status": "failed"},
            "execution": {"status": "not_run", "first_error": None, "checkpoints": []},
            "checks": {"status": "not_run", "results": []},
            "environment": {"status": "unknown"},
            "server": {"status": "not_tested", "reason": "offline_only"},
            "assets": [], "runtime": {}, "trace": {}, "navigation": {}}


def _error_record(error, phase, channel, known_resources):
    get = error.get if isinstance(error, dict) else lambda key, default=None: getattr(error, key, default)
    name = get("name") or type(error).__name__
    resource = get("resource_name") or ""
    # Resource queries, arbitrary throw messages and stacks may contain session data.
    label = known_resources.get(resource, "<unmapped>")
    for url, candidate in known_resources.items():
        if candidate == "<html>" and re.fullmatch(re.escape(url) + r"#inline-script-\d+", resource):
            label = "<html:inline-script>"
    record = {"phase": phase, "channel": channel,
              "name": name if isinstance(name, str) and name in ERROR_NAMES else "Error",
              "original_name": _text_evidence(name),
              "resource_name": label,
              "resource": _text_evidence(resource),
              "line": get("line"), "column": get("column"),
              "message": _text_evidence(get("message") or ""),
              "stack": _text_evidence(get("stack") or "")}
    return record


def _same_json(actual, expected):
    if expected is None:
        return actual is None
    if type(expected) is bool:
        return type(actual) is bool and actual == expected
    if type(expected) in {int, float}:
        return type(actual) in {int, float} and actual == expected
    if type(expected) is str:
        return type(actual) is str and actual == expected
    if isinstance(expected, list):
        return isinstance(actual, list) and len(actual) == len(expected) and all(
            _same_json(left, right) for left, right in zip(actual, expected))
    return isinstance(actual, dict) and actual.keys() == expected.keys() and all(
        _same_json(actual[key], value) for key, value in expected.items())


def _run_worker(manifest_path, save_progress=None):
    result = _empty_result()
    try:
        manifest, html, sources, assets, manifest_hash = load_scenario(manifest_path)
    except (ValueError, OSError, TypeError, KeyError, RecursionError) as error:
        result["execution"]["status"] = "invalid"
        result["execution"]["validation_error"] = {"type": type(error).__name__, "detail": _text_evidence(error)}
        result["execution"]["validation_error"]["message"] = (
            str(error) if isinstance(error, ManifestError) else "Invalid or unreadable scenario input")
        return result
    result.update(id=manifest["id"], assets=assets, manifest_sha256=manifest_hash)
    if iv8 is None or diagnose_environment is None:
        result["execution"]["status"] = "unavailable"
        return result
    result["environment"] = diagnose_environment(manifest.get("environment"),
        reference=manifest.get("reference"), transport=manifest.get("transport"))
    result["runtime"] = {"package_version": iv8.__version__,
        "v8_linked": getattr(iv8, "_v8_linked", False),
        "v8_version": getattr(iv8, "_v8_runtime_version", None),
        "python_version": sys.version.split()[0],
        "native_sha256": _hash_bytes(Path(iv8._core.__file__).read_bytes()),
        "facade_sha256": _hash_bytes(Path(iv8.page.__file__).read_bytes()),
        "environment_diagnostics_sha256": _hash_bytes(Path(__file__).with_name("environment.py").read_bytes()),
        "runner_sha256": _hash_bytes(Path(__file__).read_bytes())}
    result["configuration_sha256"] = _hash_bytes(json.dumps({
        "environment": iv8.page._normalize_environment(manifest.get("environment"))
            if result["environment"]["status"] != "fail" else manifest.get("environment"),
        "reference": manifest.get("reference"), "transport": manifest.get("transport")},
        sort_keys=True, allow_nan=False).encode())
    if not result["runtime"]["v8_linked"]:
        result["execution"]["status"] = "unavailable"
        return result
    if result["environment"]["status"] == "fail":
        result["execution"]["status"] = "invalid"
        return result
    result["worker_phase"] = "execution"
    if save_progress is not None:
        save_progress(result)
    execution = result["execution"]
    execution["status"] = "passed"
    known_resources = {url: f"<resource:{index}>" for index, url in enumerate(sources)}
    known_resources[manifest["base_url"]] = "<html>"
    try:
        with iv8.Page(environment=manifest.get("environment")) as page:
            page.capture_async_errors(True)
            page.start_access_trace(max_events=10000)

            def checkpoint(phase, action, bounded=False):
                synchronous = None
                outcome = None
                try:
                    outcome = action()
                except Exception as error:
                    synchronous = _error_record(error, phase, "synchronous", known_resources)
                asynchronous = page.read_async_errors(clear=False)
                errors = [_error_record(error, phase, "asynchronous", known_resources)
                          for error in asynchronous["errors"][:1]]
                record = {"phase": phase, "complete": not (bounded and outcome is False),
                          "budget_exhausted": bounded and outcome is False,
                          "synchronous_error": synchronous,
                          "async_error_count": len(asynchronous["errors"]),
                          "async_dropped_errors": asynchronous["dropped_errors"],
                          "first_observed_async_error": errors[0] if errors else None}
                execution["checkpoints"].append(record)
                execution["first_error"] = synchronous or (errors[0] if errors else None)
                if execution["first_error"]:
                    execution["status"] = "failed"
                elif not record["complete"]:
                    execution["status"] = "incomplete"
                if save_progress is not None:
                    save_progress(result)
                return execution["status"] == "passed"

            proceed = checkpoint("load", lambda: page.load(html, manifest["base_url"], resources=sources))
            for index, step in enumerate(manifest.get("steps", [])):
                if not proceed:
                    break
                action = step["action"]
                if action == "advance":
                    callback = lambda step=step: page.advance(step["ms"], max_steps=step["max_steps"])
                elif action == "run_until_idle":
                    callback = lambda step=step: page.run_until_idle(max_steps=step["max_steps"])
                else:
                    callback = getattr(page, {"jobs": "run_jobs", "timers": "run_timers",
                        "animation_frame": "run_animation_frame", "idle": "run_idle_callbacks", "tick": "tick"}[action])
                proceed = checkpoint(f"step:{index}", callback, action in {"advance", "run_until_idle"})
            # Freeze execution evidence before assertions can execute more JS.
            report = page.access_trace_report()
            result["trace"] = {"event_count": report["events"]["count"],
                "dropped_events": report["trace"]["dropped_events"],
                "by_operation": report["events"]["by_operation"],
                "coverage": report["coverage"],
                "missing_candidates_total": len(report["missing_candidates"]),
                "missing_candidates_truncated": len(report["missing_candidates"]) > 100,
                "missing_candidates": [{"object_type": item["object_type"],
                    "member_sha256": _hash_bytes(item["member"].encode()), "count": item["count"]}
                    for item in report["missing_candidates"][:100]]}
            navigation = page.read_navigation_requests(clear=False)
            result["navigation"] = {"count": len(navigation["requests"]),
                                     "dropped_requests": navigation["dropped_requests"]}
            if execution["status"] == "passed":
                result["worker_phase"] = "checks"
                if save_progress is not None:
                    save_progress(result)
                result["checks"]["status"] = "passed"
                for check in manifest.get("checks", []):
                    record = {"id": check["id"], "status": "failed"}
                    try:
                        actual = page.eval(check["expression"], to_py=True, name="<offline-check>")
                        if _same_json(actual, check["expected"]) and not page.read_async_errors(clear=False)["errors"]:
                            record["status"] = "passed"
                    except Exception as error:
                        record["error"] = _error_record(error, "check:" + check["id"], "synchronous", known_resources)
                    result["checks"]["results"].append(record)
                    if record["status"] == "failed":
                        result["checks"]["status"] = "failed"
    except Exception as error:
        execution["status"] = "failed"
        execution["first_error"] = _error_record(error, "setup", "synchronous", known_resources)
    expected = manifest.get("expected_error")
    if expected is not None:
        error = execution["first_error"]
        matches = execution["status"] == "failed" and error is not None and all(
            (error["resource"]["sha256"] == _hash_bytes(value.encode()) if key == "resource_name"
             else error["original_name"]["sha256"] == _hash_bytes(value.encode()) if key == "name"
             else error.get(key) == value) for key, value in expected.items())
    else:
        matches = execution["status"] == "passed" and result["checks"]["status"] == "passed"
    result["regression"]["status"] = "passed" if matches else "failed"
    result["worker_phase"] = "complete"
    return result


def run_isolated_scenario(manifest_path, timeout=10):
    """Run a fresh worker; stop non-terminating JS at the wall deadline."""
    if type(timeout) not in {int, float} or not math.isfinite(timeout) or not 0 < timeout <= 300:
        raise ValueError("timeout must be finite and between 0 and 300 seconds")
    result = _empty_result()
    with tempfile.TemporaryDirectory(prefix="iv8-offline-") as directory:
        output = Path(directory) / "result.json"
        try:
            completed = subprocess.run([sys.executable, str(CLI), "--_worker",
                str(Path(manifest_path).resolve()), "--_result", str(output)],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=timeout, check=False)
        except subprocess.TimeoutExpired:
            if output.is_file():
                result = json.loads(output.read_text(encoding="utf-8"))
            if result["worker_phase"] == "checks":
                result["checks"]["status"] = "failed"
                result["checks"]["reason"] = "timeout"
            else:
                result["execution"]["status"] = "timeout"
            result["worker_timeout"] = True
            result["regression"]["status"] = "failed"
            return result
        if completed.returncode or not output.is_file():
            result["execution"]["status"] = "failed"
            result["execution"]["worker_exit_code"] = completed.returncode
            return result
        return json.loads(output.read_text(encoding="utf-8"))


def main(argv=None):
    parser = argparse.ArgumentParser(description="Run SHA-256-pinned local Page scenarios without HTTP transport.")
    parser.add_argument("manifests", nargs="*", type=Path)
    parser.add_argument("--output", type=Path, help="Directory for per-scenario reports and summary.json")
    parser.add_argument("--timeout", type=float, default=10, help="Wall seconds per scenario (maximum 300)")
    parser.add_argument("--_worker", type=Path, help=argparse.SUPPRESS)
    parser.add_argument("--_result", type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args(argv)
    if args._worker is not None:
        if args._result is None:
            parser.error("worker output is required")
        def save(report):
            temporary = args._result.with_suffix(".tmp")
            temporary.write_text(json.dumps(report, ensure_ascii=True, indent=2, allow_nan=False), encoding="utf-8")
            temporary.replace(args._result)

        save(_run_worker(args._worker, save_progress=save))
        return 0
    if not args.manifests or args.output is None:
        parser.error("provide one or more manifests and --output")
    if not math.isfinite(args.timeout) or not 0 < args.timeout <= 300:
        parser.error("timeout must be between 0 and 300 seconds")
    args.output.mkdir(parents=True, exist_ok=True)
    summary = {"schema_version": 1, "scenarios": [], "server": {"status": "not_tested"}}
    for index, manifest in enumerate(args.manifests):
        report = run_isolated_scenario(manifest, args.timeout)
        filename = f"{index:03d}-{report['id'] or 'invalid'}.json"
        (args.output / filename).write_text(json.dumps(report, ensure_ascii=True, indent=2, allow_nan=False), encoding="utf-8")
        summary["scenarios"].append({"id": report["id"], "report": filename,
            "regression": report["regression"]["status"], "execution": report["execution"]["status"],
            "checks": report["checks"]["status"], "environment": report["environment"]["status"]})
    summary["status"] = "passed" if all(item["regression"] == "passed" for item in summary["scenarios"]) else "failed"
    (args.output / "summary.json").write_text(json.dumps(summary, indent=2), encoding="utf-8")
    print(json.dumps(summary))
    return 0 if summary["status"] == "passed" else 1
