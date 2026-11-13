#!/usr/bin/env python3
"""Spectra dashboard server.

Serves the dashboard UI and provides API endpoints that run spectra commands
and return results as JSON. Zero external dependencies.

Usage:
    python3 tools/serve.py              # default port 8080
    python3 tools/serve.py --port 3000  # custom port
    python3 tools/serve.py --open       # auto-open browser
"""
import http.server
import json
import os
import re
import signal
import socket
import sqlite3
import subprocess
import sys
import threading
import webbrowser
from pathlib import Path
from urllib.parse import urlparse, parse_qs

PROJECT_ROOT = Path(__file__).resolve().parent.parent
SPECTRA_BIN = PROJECT_ROOT / "build" / "spectra"
DASHBOARD = PROJECT_ROOT / "ui" / "dashboard.html"
SPECTRA_DIR = Path.home() / ".spectra"
ALLOWLIST_FILE = SPECTRA_DIR / "allowlist.txt"
DB_FILE = SPECTRA_DIR / "scans.db"


def find_spectra():
    if SPECTRA_BIN.exists():
        return str(SPECTRA_BIN)
    for candidate in ["spectra", "./spectra"]:
        try:
            subprocess.run([candidate, "--help"], capture_output=True, timeout=5)
            return candidate
        except (FileNotFoundError, subprocess.TimeoutExpired):
            pass
    return None


def adb_env(serial=None):
    """Environment for adb/spectra calls, pinned to one phone when given."""
    env = dict(os.environ)
    if serial:
        env["ANDROID_SERIAL"] = serial
    else:
        env.pop("ANDROID_SERIAL", None)
    return env


def run_cmd(args, timeout=30, serial=None):
    try:
        p = subprocess.run(args, capture_output=True, text=True, timeout=timeout,
                           env=adb_env(serial))
        return p.stdout.strip(), p.stderr.strip(), p.returncode
    except FileNotFoundError:
        return "", "command not found", -1
    except subprocess.TimeoutExpired:
        return "", "timeout", -1


def adb_available():
    out, _, rc = run_cmd(["adb", "version"], timeout=5)
    return rc == 0


def list_devices():
    """All phones adb can see, with their state (device / unauthorized / offline)."""
    out, _, _ = run_cmd(["adb", "devices", "-l"], timeout=10)
    devices = []
    for line in out.splitlines()[1:]:
        parts = line.split()
        if len(parts) >= 2:
            model = ""
            for p in parts[2:]:
                if p.startswith("model:"):
                    model = p[6:].replace("_", " ")
            devices.append({"serial": parts[0], "state": parts[1], "model": model,
                            "details": " ".join(parts[2:])})
    return devices


def pick_serial(requested=None):
    devices = list_devices()
    usable = [d for d in devices if d["state"] == "device"]
    if requested and any(d["serial"] == requested for d in usable):
        return requested, devices
    return (usable[0]["serial"] if usable else None), devices


def device_connected(serial=None):
    out, _, rc = run_cmd(["adb", "get-state"], timeout=5, serial=serial)
    return "device" in out


def device_info(requested=None):
    info = {"device_connected": False, "adb_available": adb_available()}
    if not info["adb_available"]:
        return info
    serial, devices = pick_serial(requested)
    info["devices"] = devices
    info["unauthorized"] = any(d["state"] == "unauthorized" for d in devices)
    if not serial or not device_connected(serial):
        return info
    info["device_connected"] = True
    info["serial"] = serial

    model, _, _ = run_cmd(["adb", "shell", "getprop", "ro.product.model"], timeout=5, serial=serial)
    name, _, _ = run_cmd(["adb", "shell", "getprop", "ro.product.marketname"], timeout=5, serial=serial)
    version, _, _ = run_cmd(["adb", "shell", "getprop", "ro.build.version.release"], timeout=5, serial=serial)
    sdk, _, _ = run_cmd(["adb", "shell", "getprop", "ro.build.version.sdk"], timeout=5, serial=serial)

    info["device_model"] = model.strip()
    info["device_name"] = name.strip() or model.strip()
    info["android_version"] = version.strip()
    info["api_level"] = int(sdk.strip()) if sdk.strip().isdigit() else 0
    info["connection_type"] = "USB"
    return info


def run_scan(requested=None, store=True):
    spectra = find_spectra()
    if not spectra:
        return None, "spectra binary not found (run: cd build && cmake .. && make)"

    serial, devices = pick_serial(requested)
    if not serial:
        if any(d["state"] == "unauthorized" for d in devices):
            return None, "Phone found but not authorized: unlock it and tap Allow on the USB debugging prompt"
        return None, "No phone connected"

    args = [spectra, "scan", "--android", "--json", "--device", serial]
    if not store:
        args.append("--no-store")
    out, err, rc = run_cmd(args, timeout=120, serial=serial)
    if rc != 0:
        return None, (err or "scan failed").strip()

    try:
        data = json.loads(out)
    except json.JSONDecodeError:
        return None, "invalid JSON from spectra"

    # augment with app details if not present
    if "apps" not in data or not data["apps"]:
        data["apps"] = build_app_list(data)

    return data, None


def build_app_list(scan_data):
    """Build app details from sensor + network data."""
    pkgs = set()
    for s in scan_data.get("sensors", []):
        pkgs.add(s.get("package", ""))
    for n in scan_data.get("network", []):
        pkgs.add(n.get("package", ""))
    pkgs.discard("")

    apps = []
    for pkg in pkgs:
        name = pkg
        for s in scan_data.get("sensors", []):
            if s.get("package") == pkg and s.get("app_name"):
                name = s["app_name"]
                break
        for n in scan_data.get("network", []):
            if n.get("package") == pkg and n.get("app_name"):
                name = n["app_name"]
                break
        apps.append({
            "app_name": name,
            "package": pkg,
            "install_source": "",
            "permissions": "",
            "first_install": "",
            "is_system_app": False
        })
    return apps


def get_history():
    if not DB_FILE.exists():
        return []
    try:
        conn = sqlite3.connect(str(DB_FILE))
        conn.row_factory = sqlite3.Row
        rows = conn.execute(
            "SELECT id, device_id, device_name, timestamp, report_json "
            "FROM scans ORDER BY id DESC LIMIT 50"
        ).fetchall()
        conn.close()
        return [dict(r) for r in rows]
    except Exception:
        return []


def read_allowlist():
    if not ALLOWLIST_FILE.exists():
        return []
    return [
        line.strip()
        for line in ALLOWLIST_FILE.read_text().splitlines()
        if line.strip() and not line.strip().startswith("#")
    ]


def write_allowlist(domains):
    SPECTRA_DIR.mkdir(parents=True, exist_ok=True)
    ALLOWLIST_FILE.write_text("\n".join(sorted(set(domains))) + "\n")


class Handler(http.server.BaseHTTPRequestHandler):
    def log_message(self, fmt, *args):
        sys.stderr.write(f"\033[90m{self.address_string()} {fmt % args}\033[0m\n")

    def respond_json(self, data, status=200):
        body = json.dumps(data).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", len(body))
        self.send_header("Access-Control-Allow-Origin", "*")
        self.end_headers()
        self.wfile.write(body)

    def respond_error(self, msg, status=500):
        self.respond_json({"error": msg}, status)

    def do_OPTIONS(self):
        self.send_response(200)
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        self.end_headers()

    def do_GET(self):
        parsed = urlparse(self.path)
        path = parsed.path

        if path == "/" or path == "/index.html":
            self.serve_dashboard()
        elif path == "/api/status":
            q = parse_qs(parsed.query)
            self.respond_json(device_info(q.get("device", [None])[0]))
        elif path == "/api/scan":
            q = parse_qs(parsed.query)
            store = q.get("store", ["1"])[0] not in ("0", "false", "no")
            data, err = run_scan(q.get("device", [None])[0], store=store)
            if err:
                self.respond_error(err)
            else:
                self.respond_json(data)
        elif path == "/api/devices":
            self.respond_json({"devices": list_devices()})
        elif path == "/api/history":
            self.respond_json({"scans": get_history()})
        elif path == "/api/allowlist":
            self.respond_json({"domains": read_allowlist()})
        else:
            self.send_error(404)

    def do_POST(self):
        parsed = urlparse(self.path)
        path = parsed.path

        if path == "/api/allowlist":
            length = int(self.headers.get("Content-Length", 0))
            body = json.loads(self.rfile.read(length)) if length else {}
            action = body.get("action", "")
            domain = body.get("domain", "").strip().lower()

            domains = read_allowlist()
            if action == "add" and domain:
                if domain not in domains:
                    domains.append(domain)
                    write_allowlist(domains)
                self.respond_json({"ok": True, "domains": domains})
            elif action == "remove" and domain:
                domains = [d for d in domains if d != domain]
                write_allowlist(domains)
                self.respond_json({"ok": True, "domains": domains})
            else:
                self.respond_error("invalid action", 400)
        else:
            self.send_error(404)

    def serve_dashboard(self):
        try:
            content = DASHBOARD.read_bytes()
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", len(content))
            self.end_headers()
            self.wfile.write(content)
        except FileNotFoundError:
            self.send_error(404, "dashboard.html not found")


def main():
    port = 8080
    auto_open = False

    args = sys.argv[1:]
    i = 0
    while i < len(args):
        if args[i] in ("--port", "-p") and i + 1 < len(args):
            port = int(args[i + 1])
            i += 2
        elif args[i] == "--open":
            auto_open = True
            i += 1
        else:
            i += 1

    spectra = find_spectra()

    print(f"\n\033[1m  spectra dashboard server\033[0m")
    print(f"  \033[90m{'=' * 40}\033[0m")
    print(f"  Dashboard:  \033[36mhttp://localhost:{port}\033[0m")
    print(f"  Spectra:      {'✓ ' + spectra if spectra else '✗ not found (cd build && cmake .. && make)'}")
    print(f"  ADB:        {'✓ available' if adb_available() else '✗ not found'}")
    if adb_available():
        print(f"  Device:     {'✓ connected' if device_connected() else '✗ not connected'}")
    print(f"  \033[90m{'=' * 40}\033[0m")
    print(f"  Press Ctrl+C to stop\n")

    server = http.server.HTTPServer(("", port), Handler)

    if auto_open:
        threading.Timer(0.5, lambda: webbrowser.open(f"http://localhost:{port}")).start()

    try:
        server.serve_forever()
    except KeyboardInterrupt:
        print("\n  Shutting down.")
        server.shutdown()


if __name__ == "__main__":
    main()
