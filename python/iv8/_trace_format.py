"""Human-readable access tracing, using captured metadata only."""

import json
import re


# Frozen native bound in src/core/access_summary.cpp, not a new capture limit.
_STRING_PREVIEW_BYTES = 64
_IDENTIFIER = re.compile(r"[A-Za-z_$][A-Za-z0-9_$]*\Z")
_TYPES = {
    "object": "<Object>", "array": "<Array>", "function": "<Function>",
    "host_object": "<HostObject>", "bigint": "<BigInt>", "symbol": "<Symbol>",
    "unknown": "<unknown>",
}


def _quoted(text):
    rendered = json.dumps(text, ensure_ascii=False)
    for character, escaped in (("\x7f", "\\u007f"), ("\x85", "\\u0085"),
                               ("\u2028", "\\u2028"), ("\u2029", "\\u2029")):
        rendered = rendered.replace(character, escaped)
    return rendered


def _inline(text):
    return _quoted(text)[1:-1]


def _unescape_preview(text):
    """Decode only native summary escapes; never evaluate log contents."""
    output = []
    index = 0
    simple = {"\\": "\\", "n": "\n", "r": "\r", "t": "\t"}
    while index < len(text):
        if text[index] == "\\" and index + 1 < len(text):
            following = text[index + 1]
            if following in simple:
                output.append(simple[following])
                index += 2
                continue
            if following == "x" and index + 3 < len(text):
                digits = text[index + 2:index + 4]
                if all(character in "0123456789abcdefABCDEF" for character in digits):
                    output.append(chr(int(digits, 16)))
                    index += 4
                    continue
        output.append(text[index])
        index += 1
    return "".join(output)


def _summary(value, missing):
    if value is None:
        return missing
    if value in ("undefined", "null"):
        return value
    if value in _TYPES:
        return _TYPES[value]
    if value.startswith("boolean:") and value[8:] in ("true", "false"):
        return value[8:]
    if value.startswith("number:"):
        number = value[7:]
        return {"nan": "NaN", "inf": "Infinity", "-inf": "-Infinity"}.get(number, number)
    if value.startswith("string:"):
        parts = value.split(":", 2)
        try:
            length = int(parts[1])
            preview = parts[2]
        except (ValueError, IndexError):
            return "<summary: " + _quoted(value) + ">"
        if length > _STRING_PREVIEW_BYTES and preview.endswith("..."):
            if preview == "...":
                return f"<String: {length} bytes; preview not recorded>"
            text = _unescape_preview(preview[:-3])
            return f"{_quoted(text)} … <truncated, {length} bytes>"
        text = _unescape_preview(preview)
        if length >= 0 and len(text.encode("utf-8")) == length:
            return _quoted(text)
    return "<summary: " + _quoted(value) + ">"


def _path(event):
    label = event.get("label") or event.get("object_type") or "<unknown>"
    path = _inline(label)
    member = event.get("member", "")
    if event.get("operation") == "own_keys":
        return path
    if _IDENTIFIER.fullmatch(member):
        return path + "." + member
    return path + "[" + _quoted(member) + "]"


def _arguments(event):
    summaries = event.get("argument_summaries")
    if summaries is None:
        return "<arguments not recorded>"
    rendered = [_summary(value, "<value not recorded>") for value in summaries]
    count = event.get("argument_count", len(summaries))
    if count > len(summaries):
        rendered.append(f"… <{count - len(summaries)} more arguments>")
    return ", ".join(rendered)


def _event_line(event):
    operation = event.get("operation", "unknown")
    path = _path(event)
    if operation == "call":
        line = f"CALL {path}({_arguments(event)})"
    elif operation == "set":
        value = _summary(event.get("value_summary"), "<value not recorded>")
        line = f"SET {path} = {value}"
    else:
        verb = {"get": "GET", "get_probe": "GET", "get_method": "METHOD",
                "has": "HAS", "delete": "DELETE", "own_keys": "KEYS",
                "get_own_property_descriptor": "DESCRIBE",
                "define_property": "DEFINE"}.get(operation, operation.upper())
        line = f"{verb} {path}"

    if event.get("outcome") == "threw":
        name = event.get("exception_name") or "<exception>"
        message = event.get("exception_message", "")
        if message.startswith("Uncaught "):
            message = message[len("Uncaught "):]
        if message.startswith(name + ": "):
            message = message[len(name) + 2:]
        return line + " !! " + _inline(name) + (": " + _inline(message) if message else "")
    if operation == "get_probe":
        return line + (" -> <missing>" if event.get("resolution") == "missing"
                       else " -> <value not recorded>")
    if operation == "get_method":
        return line
    if operation in ("set", "define_property"):
        return line + ("" if event.get("outcome") == "returned"
                       else " [completion not recorded]")
    if operation == "get":
        result = _summary(event.get("value_summary"), "<value not recorded>")
    else:
        result = _summary(event.get("return_summary"), "<return value not recorded>")
    return line + " -> " + result


def format_access_trace(report):
    """Render one snapshot without inspecting live values or mutating events."""
    lines = [_event_line(event) for event in report["events"]]
    dropped = report.get("dropped_events", 0)
    if dropped:
        lines.append(f"WARNING: {dropped} trace event(s) dropped "
                     f"(capacity {report['max_events']}).")
    return "\n".join(lines)
