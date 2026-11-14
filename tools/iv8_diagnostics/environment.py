"""Offline consistency checks for Page configuration and declared metadata.

These checks inspect configuration, not a running browser or network transport.
Passing them does not establish browser equivalence or server acceptance.
"""

import math
import re
from collections.abc import Mapping

from iv8.page import (
    _DEFAULT_ENVIRONMENT,
    _INT_KEYS,
    _NESTED_ENVIRONMENT,
    _normalize_environment,
)


def _explicit_fields(environment):
    if environment is None:
        return set()
    if any(section in environment for section in _NESTED_ENVIRONMENT):
        return {
            _NESTED_ENVIRONMENT[section][key]
            for section, values in environment.items()
            for key in values
        }
    return set(environment)


def _chrome_major(user_agent):
    match = re.search(r"(?:^|[ ()])(?:Chrome|Chromium|CriOS)/(\d+)(?:\.|\s|$)", user_agent)
    return int(match.group(1)) if match and len(match.group(1)) < 10 else None


def _ua_os(user_agent):
    if re.search(r"\b(?:iPhone|iPad|iPod)\b", user_agent):
        return "ios"
    if "Android" in user_agent:
        return "android"
    if "Windows NT" in user_agent:
        return "windows"
    if "Macintosh" in user_agent or "Mac OS X" in user_agent:
        return "macos"
    if "Linux" in user_agent:
        return "linux"
    return None


def _platform_os(platform):
    if re.fullmatch(r"Win(?:32|64|dows)", platform, re.IGNORECASE):
        return {"windows"}
    if platform in ("MacIntel", "MacPPC", "macOS"):
        # iPad desktop mode can report MacIntel; this is not a hard OS proof.
        return {"macos", "ios"}
    if platform in ("iPhone", "iPad", "iPod"):
        return {"ios"}
    if platform.startswith("Linux"):
        return {"linux", "android"}
    if platform == "Android":
        return {"android"}
    return None


def diagnose_environment(environment, reference=None, transport=None):
    """Return a JSON-safe, value-redacted report without constructing a Page.

    ``reference`` may contain ``browser_version`` (string), ``conditions``
    (mapping) and a partial ``environment`` in either Page configuration form.
    Only explicitly supplied reference fields are compared. ``transport`` may
    contain a positive integer ``browser_major`` and string ``http_user_agent``;
    both are caller declarations, never measurements performed by this tool.

    ``pass`` applies to conducted checks only. Missing evidence is enumerated in
    ``unchecked``; an attempted but undecidable comparison yields ``unknown``.
    Format and invalid numeric values fail; plausible but differing relationships
    warn rather than claiming a browser invariant.
    """
    report = {
        "schema_version": 1,
        "kind": "environment",
        "status": "unknown",
        "checks": {"performed": 0, "passed": 0, "warning": 0,
                   "failed": 0, "unknown": 0},
        "findings": [],
        "unchecked": [],
    }

    def record(code, severity, fields, message):
        report["checks"]["performed"] += 1
        bucket = {"pass": "passed", "warning": "warning", "fail": "failed",
                  "unknown": "unknown"}[severity]
        report["checks"][bucket] += 1
        if severity != "pass":
            report["findings"].append({"code": code, "severity": severity,
                                       "fields": list(fields), "message": message})

    def unchecked(code, fields, message):
        report["unchecked"].append({"code": code, "fields": list(fields),
                                    "message": message})

    def finish():
        counts = report["checks"]
        report["status"] = (
            "fail" if counts["failed"] else
            "warning" if counts["warning"] else
            "unknown" if counts["unknown"] or not counts["performed"] else
            "pass"
        )
        return report

    try:
        resolved = _normalize_environment(environment)
    except (TypeError, ValueError, OverflowError):
        record("environment_schema", "fail", ["environment"],
               "Environment does not satisfy the Page configuration schema.")
        return finish()
    record("environment_schema", "pass", ["environment"], "")
    unchecked("runtime_unobserved", [],
              "Configuration checks do not observe runtime API behavior or server acceptance.")

    record("primary_language", "pass" if resolved["language"] == resolved["languages"][0]
           else "warning", ["language", "languages"],
           "Primary language differs from the first configured language.")

    # The supported native bindings cast each integer environment field to int.
    # Coordinates may be negative; representability is separate from dimensions.
    for field in _INT_KEYS:
        record("native_int_range", "pass" if -(2 ** 31) <= resolved[field] < 2 ** 31 else "fail",
               [field], "Configured integer cannot be represented by the native environment binding.")

    nonnegative = (
        "hardwareConcurrency", "maxTouchPoints", "connectionRtt",
        "screenWidth", "screenHeight", "availWidth", "availHeight",
        "screenColorDepth", "screenPixelDepth", "innerWidth", "innerHeight",
        "outerWidth", "outerHeight",
    )
    for field in nonnegative:
        record("nonnegative_value", "pass" if resolved[field] >= 0 else "fail",
               [field], "Configured value must not be negative.")
    record("hardware_concurrency_nonzero", "warning" if resolved["hardwareConcurrency"] == 0 else "pass",
           ["hardwareConcurrency"],
           "Zero configured hardware concurrency merits review; no browser invariant is asserted.")
    for field in ("devicePixelRatio", "connectionDownlink"):
        value = resolved[field]
        valid = math.isfinite(value) and (value > 0 if field == "devicePixelRatio" else value >= 0)
        record("finite_positive_ratio" if field == "devicePixelRatio" else "finite_nonnegative_value",
               "pass" if valid else "fail", [field],
               "Configured numeric value must be finite and within its supported range.")

    for smaller, larger in (("availWidth", "screenWidth"),
                            ("availHeight", "screenHeight"),
                            ("innerWidth", "outerWidth"),
                            ("innerHeight", "outerHeight")):
        record("dimension_relationship", "pass" if resolved[smaller] <= resolved[larger]
               else "warning", [smaller, larger],
               "Configured dimensions merit review; this relationship is not a universal browser rule.")

    claimed_os = _ua_os(resolved["userAgent"])
    platform_os = _platform_os(resolved["platform"])
    if claimed_os is None or platform_os is None:
        record("ua_platform_relationship", "unknown", ["userAgent", "platform"],
               "No unambiguous supported OS relationship can be inferred from these declarations.")
    else:
        record("ua_platform_relationship", "pass" if claimed_os in platform_os else "warning",
               ["userAgent", "platform"], "UA and platform declarations indicate different OS families.")

    if reference is None:
        unchecked("reference_missing", ["reference"], "No browser reference was provided.")
    elif (not isinstance(reference, Mapping)
          or set(reference) - {"browser_version", "conditions", "environment"}
          or ("browser_version" in reference
              and (not isinstance(reference["browser_version"], str) or not reference["browser_version"].strip()))
          or ("conditions" in reference and not isinstance(reference["conditions"], Mapping))):
        record("reference_schema", "fail", ["reference"], "Reference metadata has an invalid schema.")
    else:
        for metadata in ("browser_version", "conditions"):
            if metadata not in reference:
                unchecked("reference_metadata_missing", ["reference." + metadata],
                          "Browser reference provenance is incomplete.")
        if ("browser_version" in reference and not re.fullmatch(
                r"(?:(?:Google )?Chrome[ /])?\d+(?:\.\d+){0,3}",
                reference["browser_version"].strip())):
            unchecked("reference_browser_version_unparsed", ["reference.browser_version"],
                      "Reference browser version format is unrecognized; no version was inferred.")
        if "environment" not in reference:
            unchecked("reference_environment_missing", ["reference.environment"],
                      "Reference metadata supplies no environment fields to compare.")
        reference_environment = reference.get("environment", {})
        try:
            if reference_environment is None:
                raise TypeError
            normalized_reference = _normalize_environment(reference_environment)
            explicit = _explicit_fields(reference_environment)
        except (TypeError, ValueError, OverflowError):
            record("reference_environment_schema", "fail", ["reference.environment"],
                   "Reference environment must be a partial Page configuration mapping.")
        else:
            for field in sorted(explicit):
                record("reference_field", "pass" if resolved[field] == normalized_reference[field]
                       else "warning", [field, "reference.environment." + field],
                       "Configured field differs from the supplied browser reference.")
            absent = sorted(set(_DEFAULT_ENVIRONMENT) - explicit)
            if absent:
                unchecked("reference_fields_missing", absent,
                          "These fields were not supplied by the browser reference and were not compared.")

    if transport is None:
        unchecked("transport_missing", ["transport"], "No declared transport metadata was provided.")
    elif (not isinstance(transport, Mapping)
          or set(transport) - {"browser_major", "http_user_agent"}
          or ("browser_major" in transport
              and (isinstance(transport["browser_major"], bool)
                   or not isinstance(transport["browser_major"], int)
                   or transport["browser_major"] <= 0))
          or ("http_user_agent" in transport and not isinstance(transport["http_user_agent"], str))):
        record("transport_schema", "fail", ["transport"], "Transport metadata has an invalid schema.")
    else:
        unchecked("transport_unmeasured", ["transport"],
                  "Transport metadata is caller-declared; no HTTP or TLS measurement was performed.")
        js_major = _chrome_major(resolved["userAgent"])
        http_major = _chrome_major(transport["http_user_agent"]) if "http_user_agent" in transport else None
        comparisons = []
        if "browser_major" in transport:
            comparisons.append(("ua_transport_major", js_major, transport["browser_major"],
                                ["userAgent", "transport.browser_major"]))
        else:
            unchecked("transport_major_missing", ["transport.browser_major"],
                      "No transport browser major was declared.")
        if "http_user_agent" in transport:
            record("js_http_ua_declaration", "pass" if resolved["userAgent"] == transport["http_user_agent"]
                   else "warning", ["userAgent", "transport.http_user_agent"],
                   "Declared JS and HTTP UA strings differ; this does not establish actual HTTP or TLS behavior.")
            comparisons.append(("js_http_ua_major", js_major, http_major,
                                ["userAgent", "transport.http_user_agent"]))
            if "browser_major" in transport:
                comparisons.append(("http_transport_major", http_major, transport["browser_major"],
                                    ["transport.http_user_agent", "transport.browser_major"]))
            http_os = _ua_os(transport["http_user_agent"])
            record("js_http_ua_os", "unknown" if claimed_os is None or http_os is None else
                   "pass" if claimed_os == http_os else "warning",
                   ["userAgent", "transport.http_user_agent"],
                   "Declared JS and HTTP UA OS families differ or could not be determined.")
        else:
            unchecked("http_ua_missing", ["transport.http_user_agent"], "No HTTP UA was declared.")
        for code, first_major, second_major, fields in comparisons:
            record(code, "unknown" if first_major is None or second_major is None else
                   "pass" if first_major == second_major else "warning", fields,
                   "Declared Chrome majors differ or could not be determined.")
    return finish()
