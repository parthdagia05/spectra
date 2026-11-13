#!/usr/bin/env python3
"""Pair and connect ADB to an Android phone over WiFi/hotspot, fast.

Usage: python3 tools/pair_phone.py
"""
import re
import socket
import subprocess
import sys
from concurrent.futures import ThreadPoolExecutor

PORT_LO, PORT_HI = 30000, 61000


def run(*args, timeout=20):
    try:
        p = subprocess.run(args, capture_output=True, text=True, timeout=timeout)
        return (p.stdout + p.stderr).strip()
    except subprocess.TimeoutExpired:
        return "timeout"


def gateway_ip():
    out = run("netstat", "-rn")
    for line in out.splitlines():
        if line.startswith("default") and "en0" in line:
            return line.split()[1]
    return None


def probe(host, port, timeout=0.8):
    s = socket.socket()
    s.settimeout(timeout)
    try:
        return s.connect_ex((host, port))
    finally:
        s.close()


def open_ports(host):
    found = []
    with ThreadPoolExecutor(max_workers=120) as ex:
        results = ex.map(lambda p: (p, probe(host, p)), range(PORT_LO, PORT_HI + 1))
        for port, rc in results:
            if rc == 0:
                found.append(port)
    return found


def pair(host, port, code):
    print(f"  pairing via {host}:{port} ...", end=" ", flush=True)
    out = run("adb", "pair", f"{host}:{port}", code, timeout=25)
    if "Successfully paired" in out:
        print("OK")
        return True
    print("failed" if "protocol fault" in out else out.replace("\n", " "))
    return False


def connect(host, port):
    print(f"  connecting to {host}:{port} ...", end=" ", flush=True)
    out = run("adb", "connect", f"{host}:{port}", timeout=15)
    print(out)
    return out.startswith("connected") or "already connected" in out


def ask(prompt):
    try:
        return input(prompt).strip()
    except (EOFError, KeyboardInterrupt):
        print()
        sys.exit(1)


def main():
    run("adb", "start-server")
    gw = gateway_ip()
    print(f"Mac gateway (the phone, if you're on its hotspot): {gw or 'unknown'}\n")

    print("STEP 1  On the phone: Wireless debugging > 'Pair device with pairing code'.")
    print("        Keep that dialog OPEN until this says OK.")
    ipport = ask("        IP:port shown in the dialog: ")
    m = re.match(r"([\d.]+):(\d+)$", ipport)
    if not m:
        sys.exit("expected something like 100.65.79.160:34527")
    shown_ip, pair_port = m.group(1), int(m.group(2))
    code = ask("        6-digit code: ")

    hosts = [h for h in (gw, shown_ip) if h]
    paired = any(pair(h, pair_port, code) for h in hosts)
    if not paired:
        print("  direct attempts failed - scanning for the pairing port ...")
        for h in hosts:
            for p in open_ports(h):
                if p != pair_port and pair(h, p, code):
                    paired = True
                    break
            if paired:
                break
    if not paired:
        sys.exit("\nPairing failed. Is the dialog still open? Re-open it and run again.")

    print("\nSTEP 2  On the phone: close the dialog. If the main screen shows 'IP address and port'")
    print("        WITH a port, type it. Otherwise toggle Wireless debugging OFF then ON, wait 3s,")
    port = ask("        then type the port (or press Enter to auto-scan): ")

    candidates = []
    if port.isdigit():
        candidates = [(h, int(port)) for h in hosts]
    else:
        print("  scanning for the connect port ...")
        for h in hosts:
            candidates += [(h, p) for p in open_ports(h)]
        if not candidates:
            sys.exit("  no open port found - toggle Wireless debugging OFF/ON and run again.")

    for h, p in candidates:
        if connect(h, p):
            break
    else:
        sys.exit("\nConnect failed. If the phone shows 'Allow wireless debugging?', accept it and rerun.")

    print("\n" + run("adb", "devices", "-l"))
    print("Done. Run:  ./build/spectra scan --android")


if __name__ == "__main__":
    main()
