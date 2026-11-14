"""Inspect installed curl_cffi native profiles without making a request.

Only installed enums, alias resolution and native registry metadata are read.
An optional native handle check has no URL and never calls perform(). Registry
versions describe a profile; they do not establish TLS or browser equivalence.
"""

import re


_MOVING_ALIASES = {
    "chrome", "chrome_android", "edge", "firefox", "safari", "safari_ios",
    "safari_beta", "safari_ios_beta", "tor",
}


def _native_metadata(curl_module):
    requests_module = getattr(curl_module, "requests", None)
    impersonate_module = getattr(requests_module, "impersonate", None)
    fingerprints_module = getattr(curl_module, "fingerprints", None)
    enum = getattr(impersonate_module, "BrowserType", None)
    if enum is None:
        enum = getattr(curl_module, "BrowserType", None)
    names = set()
    if enum is not None:
        try:
            names = {
                value for item in enum
                if isinstance(value := getattr(item, "value", item), str)
            }
        except TypeError:
            pass
    registry = getattr(fingerprints_module, "NATIVE_IMPERSONATE_TARGETS", None)
    rows = {
        row["target_name"]: row
        for row in registry or ()
        if isinstance(row, dict) and isinstance(row.get("target_name"), str)
    }
    resolver = getattr(impersonate_module, "resolve_latest_browser_type", None)
    return names, rows, resolver


def inspect_curl_profile(curl_module, target):
    """Return JSON-safe installed profile metadata with no Session or requests.

    ``client_version`` is the installed curl_cffi package version. ``platform``
    is the registry's OS label, not a navigator.platform value. No OS is inferred
    from the profile name or host computer. Moving aliases are invalid: callers
    must pin an explicit installed native profile.
    """
    client_version = getattr(curl_module, "__version__", None)
    report = {
        "schema_version": 1,
        "kind": "curl_profile",
        "status": "unknown",
        "supported": None,
        "profile": None,
        "browser_major": None,
        "platform": None,
        "client_version": client_version if isinstance(client_version, str) else None,
        "browser": None,
        "browser_version": None,
        "platform_version": None,
        "browser_major_source": None,
        "alias": False,
        "alias_resolution": None,
        "native_setup_return_code": None,
        "verified_by": [],
        "findings": [],
        "unchecked": [
            "No request was made; actual HTTP headers and TLS behavior were not measured.",
            "Profile version metadata does not establish target-browser equivalence.",
        ],
    }

    def finding(code, message):
        report["findings"].append({"code": code, "message": message})

    if not isinstance(target, str) or not re.fullmatch(r"[a-z][a-z0-9_.]*", target):
        report.update(status="invalid", supported=False)
        finding("invalid_profile", "An explicit native profile name is required.")
        return report
    report["profile"] = target
    names, rows, resolver = _native_metadata(curl_module)
    resolved = target
    if callable(resolver):
        try:
            resolution = resolver(target)
            if isinstance(resolution, str):
                resolved = resolution
                report["verified_by"].append("installed_alias_resolver")
        except Exception:
            report["unchecked"].append("Installed alias resolution could not be inspected.")
    if target in _MOVING_ALIASES or resolved != target:
        report.update(status="invalid", supported=False, alias=True,
                      alias_resolution=resolved if resolved != target else None)
        finding("moving_alias", "Pin an explicit profile; browser aliases can change across package versions.")
        return report

    if not names and not rows:
        finding("native_registry_unavailable", "Installed native profile support could not be determined.")
        return report
    if target not in names and target not in rows:
        report.update(status="unsupported", supported=False)
        finding("unsupported_profile", "Profile is absent from installed native profile declarations.")
        return report
    if target in names:
        report["verified_by"].append("installed_enum")
    row = rows.get(target)
    if row is not None:
        report["verified_by"].append("native_registry")
        for source, destination in (("browser", "browser"), ("version", "browser_version"),
                                    ("os", "platform"), ("os_version", "platform_version")):
            value = row.get(source)
            if isinstance(value, str) and value:
                report[destination] = value
    if report["platform"] is None:
        report["unchecked"].append("Native registry OS metadata is unavailable; platform remains unknown.")
    version = report["browser_version"]
    if version is not None and re.fullmatch(r"\d+(?:\.\d+)*", version):
        major = version.split(".")[0]
        if len(major) < 10:
            report["browser_major"] = int(major)
            report["browser_major_source"] = "native_registry"
    elif version is None:
        # A canonical installed Chrome target explicitly encodes its major. This
        # is a profile label, never an inference about the actual browser/TLS.
        match = re.fullmatch(r"chrome(\d+)(?:a|_android|_macos)?", target)
        if match and len(match.group(1)) < 10:
            report["browser_major"] = int(match.group(1))
            report["browser_major_source"] = "installed_profile_name"
    if report["browser_major"] is None:
        report["unchecked"].append("Browser major could not be parsed from installed profile metadata.")

    constructor = getattr(curl_module, "Curl", None)
    if not callable(constructor):
        report.update(status="supported", supported=True)
        report["unchecked"].append("Native support is declared by installed metadata; no handle check was available.")
        return report
    handle = None
    try:
        handle = constructor()
        return_code = handle.impersonate(target, default_headers=False)
        if not isinstance(return_code, int) or isinstance(return_code, bool):
            finding("native_setup_unknown", "Native profile setup did not return a recognized status code.")
        else:
            report["native_setup_return_code"] = return_code
            report["verified_by"].append("offline_native_setup")
            report.update(status="supported" if return_code == 0 else "unsupported",
                          supported=return_code == 0)
            if return_code != 0:
                finding("native_setup_rejected", "The installed native library rejected profile setup.")
    except Exception:
        finding("native_setup_failed", "Native profile setup could not be inspected offline.")
    finally:
        if handle is not None:
            try:
                handle.close()
            except Exception:
                report.update(status="unknown", supported=None)
                finding("native_close_failed", "The local native handle did not report successful cleanup.")
    return report
