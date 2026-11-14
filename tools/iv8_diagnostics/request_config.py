"""Share declared HTTP and JavaScript identity without importing an HTTP client.

This file uses only the standard library and may be copied beside a caller.
Configuration checks do not measure TLS or establish browser equivalence.
"""

from collections.abc import Mapping
from copy import deepcopy
import json
import re


IDENTITY_HEADERS = frozenset({
    "user-agent", "accept-language", "sec-ch-ua", "sec-ch-ua-platform",
    "sec-ch-ua-mobile", "sec-ch-ua-full-version", "sec-ch-ua-full-version-list",
    "sec-ch-ua-platform-version", "sec-ch-ua-arch", "sec-ch-ua-bitness",
    "sec-ch-ua-model", "sec-ch-ua-wow64",
})
_HEADER_NAME = re.compile(r"[!#$%&'*+.^_`|~0-9A-Za-z-]+\Z")
_LANGUAGE = re.compile(r"[A-Za-z]{1,8}(?:-[A-Za-z0-9]{1,8})*\Z")
_PROFILE = re.compile(r"chrome([1-9][0-9]{0,8})(?:_(macos|android))?\Z")
_QUOTED = r'"(?:[^"\\\r\n]|\\["\\])*"'
_BRAND_ITEM = re.compile(rf"\s*({_QUOTED})\s*;\s*v\s*=\s*({_QUOTED})\s*(,|$)")
_NAV_FIELDS = ("userAgent", "platform", "language", "languages")
_CANONICAL = {"user-agent": "User-Agent", "accept-language": "Accept-Language"}


class _Invalid(ValueError):
    """Carry only static field/error identifiers, never caller values."""

    def __init__(self, code, field):
        self.code = code
        self.field = field
        super().__init__("Invalid request identity configuration")


def _navigator(environment):
    if not isinstance(environment, Mapping):
        raise _Invalid("environment_schema", "environment")
    if "navigator" in environment:
        if any(field in environment for field in _NAV_FIELDS):
            raise _Invalid("environment_schema", "environment")
        navigator = environment["navigator"]
    else:
        navigator = environment
    if not isinstance(navigator, Mapping):
        raise _Invalid("environment_schema", "environment")
    for field in _NAV_FIELDS[:-1]:
        value = navigator.get(field)
        if not isinstance(value, str) or not value.strip() or any(char in value for char in "\r\n\0"):
            raise _Invalid("navigator_schema", "navigator." + field)
    languages = navigator.get("languages")
    if (not isinstance(languages, list) or not languages
            or any(not isinstance(value, str) or not _LANGUAGE.fullmatch(value) for value in languages)
            or not _LANGUAGE.fullmatch(navigator["language"])
            or len({value.casefold() for value in languages}) != len(languages)):
        raise _Invalid("navigator_languages", "navigator.languages")
    return navigator


def _headers(headers):
    if headers is None:
        return {}
    if not isinstance(headers, Mapping):
        raise _Invalid("headers_schema", "headers")
    normalized = {}
    for name, value in headers.items():
        if not isinstance(name, str) or not _HEADER_NAME.fullmatch(name):
            raise _Invalid("headers_schema", "headers")
        lower = name.lower()
        if lower in IDENTITY_HEADERS:
            if not isinstance(value, str) or not value.strip() or any(char in value for char in "\r\n\0"):
                raise _Invalid("identity_header_schema", "headers." + lower)
            if lower in normalized and normalized[lower][1] != value:
                raise _Invalid("duplicate_identity_header", "headers." + lower)
        normalized[lower] = (_CANONICAL.get(lower, name), deepcopy(value))
    return normalized


def _chrome_major(user_agent):
    match = re.search(r"(?:^|[ ()])(?:Chrome|Chromium|CriOS)/([0-9]{1,9})(?:\.|\s|$)", user_agent)
    return int(match.group(1)) if match else None


def _ua_platform(user_agent):
    if re.search(r"\b(?:iPhone|iPad|iPod)\b", user_agent):
        return "iOS"
    if "Android" in user_agent:
        return "Android"
    if "Windows NT" in user_agent:
        return "Windows"
    if "Macintosh" in user_agent or "Mac OS X" in user_agent:
        return "macOS"
    if "Linux" in user_agent:
        return "Linux"
    return None


def _navigator_platform(platform):
    if platform.lower() in {"win32", "win64", "windows"}:
        return {"Windows"}
    if platform in {"MacIntel", "MacPPC", "macOS"}:
        return {"macOS", "iOS"}
    if platform in {"iPhone", "iPad", "iPod"}:
        return {"iOS"}
    if platform.startswith("Linux"):
        return {"Linux", "Android"}
    if platform == "Android":
        return {"Android"}
    return None


def _mobile(user_agent):
    if _ua_platform(user_agent) is None:
        return None
    return "?1" if re.search(r"\b(?:Mobile|iPhone|iPod)\b", user_agent) else "?0"


def _accept_language(languages):
    return ",".join(language if index == 0 else f"{language};q={max(1, 10 - index) / 10:.1f}"
                    for index, language in enumerate(languages))


def _language_order(value):
    languages = []
    previous_quality = 1.0
    for part in value.split(","):
        pieces = part.strip().split(";")
        language = pieces[0].strip()
        if not _LANGUAGE.fullmatch(language) or len(pieces) > 2:
            return None
        quality = 1.0
        if len(pieces) == 2:
            match = re.fullmatch(r"\s*q\s*=\s*(0(?:\.[0-9]{1,3})?|1(?:\.0{1,3})?)\s*", pieces[1])
            if match is None:
                return None
            quality = float(match.group(1))
        if quality <= 0 or quality > previous_quality:
            return None
        previous_quality = quality
        languages.append(language.casefold())
    return languages


def _brands(value):
    cursor = 0
    items = []
    while cursor < len(value):
        match = _BRAND_ITEM.match(value, cursor)
        if match is None:
            return None
        try:
            items.append((json.loads(match.group(1)), json.loads(match.group(2))))
        except ValueError:
            return None
        cursor = match.end()
        if not match.group(3):
            return items if cursor == len(value) else None
    return None


def _profile(value):
    match = _PROFILE.fullmatch(value) if isinstance(value, str) else None
    if match is None:
        return None
    return int(match.group(1)), {"macos": "macOS", "android": "Android"}.get(match.group(2))


def audit_request_config(environment, headers, transport=None):
    """Audit declarations, returning only static descriptions and field names.

    Missing transport evidence or unknown identity families are explicitly unknown.
    High-entropy UA-CH, TLS, Cookie state and the running SDK remain unchecked.
    """
    report = {"schema_version": 1, "kind": "request_configuration", "status": "unknown",
              "checks": {"performed": 0, "passed": 0, "failed": 0, "unknown": 0},
              "findings": [], "unchecked": []}

    def record(code, status, fields):
        report["checks"]["performed"] += 1
        report["checks"][{"pass": "passed", "fail": "failed", "unknown": "unknown"}[status]] += 1
        if status != "pass":
            report["findings"].append({"code": code, "severity": status, "fields": fields,
                                       "message": "Request identity conflicts or lacks sufficient declared evidence."})

    def unchecked(code, fields):
        report["unchecked"].append({"code": code, "fields": fields,
                                    "message": "This tool does not verify these values or runtime behavior."})

    def finish():
        counts = report["checks"]
        report["status"] = "fail" if counts["failed"] else "unknown" if counts["unknown"] else "pass"
        return report

    try:
        navigator = _navigator(environment)
        normalized = _headers(headers)
    except _Invalid as error:
        record(error.code, "fail", [error.field])
        return finish()
    record("configuration_schema", "pass", ["environment", "headers"])
    ua = navigator["userAgent"]
    major = _chrome_major(ua)
    family = _ua_platform(ua)
    platform_families = _navigator_platform(navigator["platform"])
    values = {name: entry[1] for name, entry in normalized.items()}
    record("http_js_user_agent", "pass" if values.get("user-agent") == ua else "fail",
           ["navigator.userAgent", "headers.user-agent"])
    record("primary_language", "pass" if navigator["language"].casefold() == navigator["languages"][0].casefold() else "fail",
           ["navigator.language", "navigator.languages"])
    actual_languages = _language_order(values["accept-language"]) if "accept-language" in values else None
    record("http_js_languages", "pass" if actual_languages == [value.casefold() for value in navigator["languages"]] else "fail",
           ["navigator.languages", "headers.accept-language"])
    record("ua_navigator_platform", "unknown" if family is None or platform_families is None else
           "pass" if family in platform_families else "fail", ["navigator.userAgent", "navigator.platform"])
    if family is not None and platform_families is not None and family in platform_families:
        record("http_js_platform", "pass" if values.get("sec-ch-ua-platform") == json.dumps(family) else "fail",
               ["navigator.platform", "headers.sec-ch-ua-platform"])
    else:
        unchecked("ua_ch_platform_unresolved", ["headers.sec-ch-ua-platform"])
    mobile = _mobile(ua)
    if mobile is None:
        record("ua_mobile_unresolved", "unknown", ["navigator.userAgent", "headers.sec-ch-ua-mobile"])
    else:
        record("http_js_mobile", "pass" if values.get("sec-ch-ua-mobile") == mobile else "fail",
               ["navigator.userAgent", "headers.sec-ch-ua-mobile"])
    if "sec-ch-ua" in values:
        brands = _brands(values["sec-ch-ua"])
        record("ua_ch_brands_schema", "pass" if brands is not None else "fail", ["headers.sec-ch-ua"])
        if brands is not None:
            recognized = [(brand, version) for brand, version in brands if brand in {"Chromium", "Google Chrome"}]
            if not recognized or major is None:
                record("ua_ch_browser_major", "unknown", ["headers.sec-ch-ua", "navigator.userAgent"])
            else:
                for _, version in recognized:
                    match = re.fullmatch(r"([0-9]{1,9})(?:\.[0-9]+)*", version)
                    record("ua_ch_browser_major", "pass" if match and int(match.group(1)) == major else "fail",
                           ["headers.sec-ch-ua", "navigator.userAgent"])
    else:
        unchecked("ua_ch_brands_missing", ["headers.sec-ch-ua"])
    unchecked("ua_ch_high_entropy", ["navigator.userAgentData", "headers.sec-ch-ua-full-version-list",
                                     "headers.sec-ch-ua-platform-version", "headers.sec-ch-ua-arch",
                                     "headers.sec-ch-ua-bitness", "headers.sec-ch-ua-model"])
    unchecked("runtime_and_tls_unmeasured", ["transport", "runtime", "cookies"])
    if transport is None:
        record("transport_missing", "unknown", ["transport"])
        return finish()
    if (not isinstance(transport, Mapping)
            or set(transport) - {"browser_major", "platform", "client_version", "profile"}
            or ("browser_major" in transport and (type(transport["browser_major"]) is not int or transport["browser_major"] <= 0))
            or any(field in transport and (not isinstance(transport[field], str) or not transport[field].strip()
                   or any(char in transport[field] for char in "\r\n\0")) for field in ("platform", "client_version", "profile"))):
        record("transport_schema", "fail", ["transport"])
        return finish()
    for field in ("browser_major", "platform", "client_version", "profile"):
        if field not in transport:
            record("transport_metadata_missing", "unknown", ["transport." + field])
    if "browser_major" in transport:
        record("transport_browser_major", "unknown" if major is None else
               "pass" if transport["browser_major"] == major else "fail", ["transport.browser_major", "navigator.userAgent"])
    if "platform" in transport:
        transport_family = {"macos": "macOS", "windows": "Windows", "linux": "Linux",
                            "android": "Android", "ios": "iOS"}.get(transport["platform"].lower())
        record("transport_platform", "unknown" if transport_family is None or family is None else
               "pass" if family == transport_family else "fail", ["transport.platform", "navigator.userAgent"])
    if "profile" in transport:
        profile = _profile(transport["profile"])
        record("transport_profile", "unknown" if profile is None or major is None else
               "pass" if profile[0] == major and (profile[1] is None or profile[1] == family) else "fail",
               ["transport.profile", "navigator.userAgent"])
        if profile is not None and "browser_major" in transport:
            record("transport_profile_major", "pass" if profile[0] == transport["browser_major"] else "fail",
                   ["transport.profile", "transport.browser_major"])
    return finish()


def _validate_impersonate(impersonate, navigator, transport):
    if impersonate is None:
        return
    parsed = _profile(impersonate)
    if (parsed is None or parsed[0] != _chrome_major(navigator["userAgent"])
            or (parsed[1] is not None and parsed[1] != _ua_platform(navigator["userAgent"]))
            or (transport is not None and (not isinstance(transport, Mapping) or transport.get("profile") != impersonate))):
        raise ValueError("Pinned impersonation does not match the declared identity")


def build_request_config(environment, headers=None, *, impersonate=None, transport=None):
    """Derive identity headers from navigator, preserving unrelated caller data.

    Existing UA/language/platform/mobile values are replaced. Brand lists are
    never invented or repaired. Conflicts and drifting impersonation aliases fail.
    """
    try:
        navigator = _navigator(environment)
        normalized = _headers(headers)
    except _Invalid:
        raise ValueError("Invalid request identity configuration") from None
    _validate_impersonate(impersonate, navigator, transport)
    derived = {"user-agent": navigator["userAgent"],
               "accept-language": _accept_language(navigator["languages"])}
    family = _ua_platform(navigator["userAgent"])
    platform_families = _navigator_platform(navigator["platform"])
    if family is not None and platform_families is not None and family in platform_families:
        derived["sec-ch-ua-platform"] = json.dumps(family)
    mobile = _mobile(navigator["userAgent"])
    if mobile is not None:
        derived["sec-ch-ua-mobile"] = mobile
    for name in ("user-agent", "accept-language", "sec-ch-ua-platform", "sec-ch-ua-mobile"):
        normalized.pop(name, None)
    for name, value in derived.items():
        normalized[name] = (_CANONICAL.get(name, name), value)
    result_headers = {name: value for name, value in normalized.values()}
    if audit_request_config(environment, result_headers, transport)["status"] == "fail":
        raise ValueError("Request identity configuration conflicts")
    return {"environment": deepcopy(environment), "headers": deepcopy(result_headers),
            "impersonate": impersonate, "default_headers": False,
            "transport": deepcopy(transport)}


def merge_request_headers(config, overrides):
    """Merge request-specific headers without changing the fixed identity."""
    if (not isinstance(config, Mapping) or "environment" not in config or "headers" not in config
            or config.get("default_headers") is not False):
        raise ValueError("Invalid shared request configuration")
    try:
        navigator = _navigator(config["environment"])
        base = _headers(config["headers"])
        additions = _headers(overrides)
    except _Invalid:
        raise ValueError("Invalid request identity configuration") from None
    _validate_impersonate(config.get("impersonate"), navigator, config.get("transport"))
    for lower, entry in additions.items():
        if lower in IDENTITY_HEADERS and (lower not in base or base[lower][1] != entry[1]):
            raise ValueError("Request headers cannot change the fixed identity")
        base[lower] = entry
    result = {name: value for name, value in base.values()}
    if audit_request_config(config["environment"], result, config.get("transport"))["status"] == "fail":
        raise ValueError("Request identity configuration conflicts")
    return deepcopy(result)
