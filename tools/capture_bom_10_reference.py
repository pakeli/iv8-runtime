"""Capture BOM-10 Chrome fixtures via a private local DevTools pipe (POSIX).

Manual evidence collection only; pytest reads the checked-in JSON offline.
No user browser profile or third-party Python packages are used.
"""

import argparse
from datetime import datetime, timezone
import fcntl
import hashlib
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import json
import os
from pathlib import Path
import platform
import select
import signal
import subprocess
import tempfile
import threading
import time


REPO = Path(__file__).resolve().parents[1]
FIXTURES = REPO / "test/fixtures/bom_10"


class FixtureHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        self.server.requests.append({"method": "GET", "path": self.path})
        payload = b"<!doctype html><html><head><title>BOM-10 reference</title></head><body></body></html>"
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, *_args):
        pass


class ChromePipe:
    def __init__(self, executable, profile, log_path):
        self.buffer = b""
        self.sequence = 0
        self.events = []
        child_read, self.writer = os.pipe()
        self.reader, child_write = os.pipe()
        read_copy = fcntl.fcntl(child_read, fcntl.F_DUPFD_CLOEXEC, 10)
        write_copy = fcntl.fcntl(child_write, fcntl.F_DUPFD_CLOEXEC, 10)
        self.arguments = [
            executable, "--headless=new", "--disable-gpu", "--no-first-run",
            "--no-default-browser-check", "--disable-background-networking",
            "--disable-component-update", "--disable-domain-reliability",
            "--disable-sync", "--disable-extensions", "--no-pings",
            "--metrics-recording-only", "--disable-crash-reporter",
            "--safebrowsing-disable-auto-update",
            "--disable-features=MediaRouter,OptimizationHints",
            "--host-resolver-rules=MAP bom-fixture.invalid 127.0.0.1, MAP * ~NOTFOUND, EXCLUDE 127.0.0.1",
            "--no-proxy-server", "--lang=en-US",
            "--user-data-dir=" + str(profile), "--remote-debugging-pipe",
        ]
        with log_path.open("wb") as log:
            self.pid = os.posix_spawn(executable, self.arguments, os.environ, file_actions=[
                (os.POSIX_SPAWN_DUP2, read_copy, 3),
                (os.POSIX_SPAWN_DUP2, write_copy, 4),
                (os.POSIX_SPAWN_DUP2, log.fileno(), 1),
                (os.POSIX_SPAWN_DUP2, log.fileno(), 2),
            ])
        for descriptor in (child_read, child_write, read_copy, write_copy):
            os.close(descriptor)

    def receive(self, deadline):
        while b"\0" not in self.buffer:
            remaining = deadline - time.monotonic()
            if remaining <= 0 or not select.select([self.reader], [], [], remaining)[0]:
                raise TimeoutError("Chrome DevTools response timed out")
            chunk = os.read(self.reader, 1024 * 1024)
            if not chunk:
                raise RuntimeError("Chrome closed its DevTools pipe")
            self.buffer += chunk
        message, self.buffer = self.buffer.split(b"\0", 1)
        return json.loads(message)

    def call(self, method, params=None, session=None):
        self.sequence += 1
        message = {"id": self.sequence, "method": method, "params": params or {}}
        if session:
            message["sessionId"] = session
        payload = json.dumps(message).encode() + b"\0"
        while payload:
            payload = payload[os.write(self.writer, payload):]
        deadline = time.monotonic() + 45
        while True:
            response = self.receive(deadline)
            if response.get("id") == self.sequence:
                if "error" in response:
                    raise RuntimeError(response["error"])
                return response.get("result", {})
            self.events.append(response)

    def wait_event(self, method, session):
        deadline = time.monotonic() + 45
        while True:
            for index, event in enumerate(self.events):
                if event.get("method") == method and event.get("sessionId") == session:
                    return self.events.pop(index)
            self.events.append(self.receive(deadline))

    def close(self):
        try:
            self.call("Browser.close")
        except (OSError, RuntimeError, TimeoutError):
            pass
        finally:
            os.close(self.writer)
            os.close(self.reader)
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            if os.waitpid(self.pid, os.WNOHANG)[0]:
                return
            time.sleep(0.05)
        os.kill(self.pid, signal.SIGTERM)
        os.waitpid(self.pid, 0)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--chrome", default="/Applications/Google Chrome.app/Contents/MacOS/Google Chrome")
    parser.add_argument("--output-dir", type=Path, default=FIXTURES)
    parser.add_argument("--artifacts-dir", type=Path, default=REPO / "data/bom-10-reference-2026-11-27")
    parser.add_argument("--groups", nargs="+", choices=("crypto", "storage", "digest_edges", "storage_tree"),
                        default=("crypto", "storage"))
    args = parser.parse_args()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    args.artifacts_dir.mkdir(parents=True, exist_ok=True)
    version = subprocess.check_output([args.chrome, "--version"], text=True).strip()
    server = ThreadingHTTPServer(("127.0.0.1", 0), FixtureHandler)
    server.requests = []
    thread = threading.Thread(target=server.serve_forever, daemon=True)
    thread.start()
    port = server.server_port
    cases = {
        "secure": f"http://127.0.0.1:{port}/reference",
        "insecure": f"http://bom-fixture.invalid:{port}/reference",
        "opaque": "about:blank",
    }
    try:
        for kind, url in cases.items():
            server.requests.clear()
            with tempfile.TemporaryDirectory(prefix=kind + "-", dir=args.artifacts_dir) as profile:
                browser = ChromePipe(args.chrome, profile, args.artifacts_dir / (kind + ".log"))
                try:
                    target = browser.call("Target.createTarget", {"url": "about:blank"})["targetId"]
                    session = browser.call("Target.attachToTarget", {"targetId": target, "flatten": True})["sessionId"]
                    browser.call("Page.enable", session=session)
                    browser.call("Runtime.enable", session=session)
                    if url != "about:blank":
                        browser.call("Page.navigate", {"url": url}, session=session)
                        browser.wait_event("Page.loadEventFired", session)
                    for group in args.groups:
                        probe = (FIXTURES / (group + "_probe.js")).read_bytes()
                        response = browser.call("Runtime.evaluate", {
                            "expression": probe.decode(), "awaitPromise": True, "returnByValue": True,
                        }, session=session)
                        if "exceptionDetails" in response or "value" not in response.get("result", {}):
                            raise RuntimeError(json.dumps(response))
                        fixture = {
                            "browser": version,
                            "captured_at_utc": datetime.now(timezone.utc).isoformat(),
                            "probe_sha256": hashlib.sha256(probe).hexdigest(),
                            "execution": {
                                "os": platform.system(), "machine": platform.machine(),
                                "profile": "fresh temporary headless profile for each origin class",
                                "origin": url.replace(str(port), "<port>"),
                                "network": "local fixture server only; external DNS disabled; no user profile, permission grant or user gesture",
                                "transport": "private local DevTools pipe; Runtime.evaluate awaitPromise, 45s timeout",
                                "arguments": [arg.replace(profile, "<temporary-profile>") for arg in browser.arguments],
                            },
                            "requests": list(server.requests), "result": response["result"]["value"],
                        }
                        output = args.output_dir / (group + "-" + kind + ".json")
                        output.write_text(json.dumps(fixture, ensure_ascii=False, indent=2) + "\n")
                        print(str(output), flush=True)
                finally:
                    browser.close()
    finally:
        server.shutdown()
        server.server_close()
        thread.join(timeout=5)


if __name__ == "__main__":
    main()
