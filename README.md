# spectra

Privacy transparency tool that monitors Android phones over ADB. Plug in your phone, run one command, and see exactly which apps accessed your microphone, camera, location and contacts, where they sent data, when it happened, and whether you should be worried.

**Zero install on the phone. Zero battery impact. Full transparency.**

## What spectra answers

1. **WHO** used my mic, camera, location and contacts?
2. **WHERE** did each app send data?
3. **WHEN** did it happen and how often?
4. **HOW MUCH** data left my phone?
5. **SHOULD I BE WORRIED?** Every score is a sum of readable reasons, judged against what that kind of app normally does, with a concrete fix for each finding.

## Quick start

```bash
# macOS
brew install cmake cjson android-platform-tools
# Ubuntu
sudo apt install build-essential cmake libsqlite3-dev libcjson-dev android-tools-adb

# Build
mkdir build && cd build
cmake .. && make

# Scan your Android phone (USB debugging must be enabled)
./spectra scan --android

# Scan your local machine
./spectra scan

# JSON output
./spectra scan --android --json

# HTML report
./spectra scan --android --html report.html
```

## Requirements

**Host machine:**
- C11 compiler, CMake 3.16+
- SQLite3, cJSON
- ADB (android-platform-tools)

**Android phone:**
- USB debugging enabled (Settings > Developer Options > USB Debugging)
- Android 12+ recommended (for full appops history)
- No root needed for core functionality

## Architecture

```
Host Computer                          Android Phone
┌──────────────────────┐               ┌─────────────────┐
│  spectra CLI           │               │                 │
│  ┌────────────────┐  │    ADB        │  /proc/net/tcp  │
│  │ Sensor         │──┼───────────────┤  dumpsys appops │
│  │ Collector      │  │    shell      │  dumpsys netstats│
│  ├────────────────┤  │    commands   │  pm list        │
│  │ Network        │──┼───────────────┤  /proc/uid_stat │
│  │ Collector      │  │               │                 │
│  ├────────────────┤  │               └─────────────────┘
│  │ Correlator     │  │
│  │ (join by UID)  │  │
│  ├────────────────┤  │
│  │ Report         │  │
│  │ (terminal/     │  │
│  │  JSON/HTML)    │  │
│  ├────────────────┤  │
│  │ Store (SQLite) │  │
│  └────────────────┘  │
└──────────────────────┘
```

Four layers, each a clean C module:

- **Collectors** — gather sensor access (dumpsys appops) and network connections (/proc/net/tcp) over ADB
- **Correlator** — joins sensor + network events by UID, computes risk scores
- **Report** — formatted terminal output, JSON, or HTML
- **Store** — SQLite scan history at `~/.spectra/scans.db`

Platform abstraction: five C functions per platform. Android runs on the host via ADB. Linux uses /proc + netlink. macOS uses lsof + libproc. Adding a new OS = implementing five functions.

## Commands

```
spectra scan                     # scan local machine
spectra scan --android           # scan connected Android phone
spectra scan --android --last 48 # look back 48 hours
spectra scan --json              # JSON output
spectra scan --html report.html  # HTML report
spectra scan --live              # re-scan every 2 seconds
spectra scan --android --no-store          # guest scan, not saved
spectra scan --android --device <serial>   # choose a phone when several are plugged in
spectra history                  # list past scans
spectra history --device "S24"   # filter by device
spectra diff <id1> <id2>         # compare two scans
spectra devices                  # list connected Android devices
```

## Try it on your phone (guest scan)

Anyone with an Android phone can be scanned from a laptop that has spectra installed. Nothing is installed on the phone and nothing leaves the laptop.

1. **Turn on Developer options.** Settings > About phone > tap the build number 7 times. On Xiaomi/Redmi tap "MIUI version" or "OS version"; on Samsung it is under About phone > Software information; on OnePlus/Pixel it is "Build number".
2. **Turn on USB debugging.** Settings > System (or Additional settings) > Developer options > USB debugging.
3. **Plug in the cable** and, if the phone is not detected, swipe down the notification and switch the USB mode from "Charging only" to "File transfer".
4. **Tap "Allow"** on the "Allow USB debugging?" prompt on the phone. Unlock the phone first; the prompt only appears on an unlocked screen.
5. **Scan without saving:**
   ```bash
   spectra scan --android --no-store            # terminal report, not saved to history
   spectra scan --android --no-store --html report.html
   ```
   In the dashboard, turn on **Guest scan** before pressing Scan Now; the dashboard also lets you pick the phone when several are plugged in.
6. **Afterwards, on the phone:** Developer options > "Revoke USB debugging authorizations", then turn USB debugging off. This removes the laptop's access.

What the other person should know: the report shows their app list, which apps read their contacts, how often their location was read, and where apps send data. Ask before scanning. iPhones cannot be scanned.

## How it works

**Sensor detection (Android):**
1. `dumpsys appops --op <OP> --include-discrete N` for CAMERA, RECORD_AUDIO, FINE_LOCATION, COARSE_LOCATION and READ_CONTACTS — the per-access history (same source as Android's own Privacy Dashboard), so counts are every access in the last 24 h, not just the last one per app state
2. The summary part of the same dump — precise timestamp and duration of the latest access per app
3. `cmd appops query-op` — identifies currently active camera / mic users
4. Extracts: package name, access count, timestamps, durations, foreground/background state
5. `dumpsys package` for every app seen, fetched in one batched adb call — installer, install date, granted permissions

**Network detection (Android):**
1. `/proc/net/tcp` + `/proc/net/tcp6` — all TCP connections with UIDs
2. `pm list packages -U` — maps UID to package name
3. `dumpsys netstats detail` — per-app byte counts
4. Reverse DNS on host for destination hostnames

**Risk model (explainable):**
- Every app gets a **category** from its package id: system, messaging, caller-ID, social, commerce, travel, finance, utility, health, media, browser, assistant, or unknown.
- Every sensor use is judged against what that kind of app is expected to do. Example: a messaging app syncing contacts in the background is normal; a utility app reading contacts in the background is not.
- Every point added to the score comes with a one-line reason, so the score is always the sum of things you can read:
  - Sensor a category does not need: +15
  - Read while you were not using the app: +25 (foreground-only sensor) or +30 (unneeded sensor)
  - Microphone or camera active right now: +10 expected, +30 foreground-only, +40 unneeded
  - Location read 50+ / 200+ times in 24 h (non-system): +10 / +15
  - Touched mic, camera, location and contacts: +10; unrecognised app using mic and camera: +20
  - Unrecognised app: +10
  - Sent data to a server with no hostname: +10; uploaded 1 MB+ while using mic/camera: +10
  - Installed outside an app store: +20; installer unknown: +5
  - Holds READ_SMS / READ_CALL_LOG without being a messaging or caller-ID app: +10 each
  - Caller-ID app reading contacts: +15 (they upload your address book)
- Verdicts: **expected** (<15), **low** (15-24), **review** (25-49), **high** (50+)
- Each finding carries a **recommended action** ("Set X's Location permission to Allow only while using the app"), chosen by priority: active sensor > background location > background mic/camera > unneeded contacts > sideloaded > frequent location > unknown destination.

**Privacy report:** `spectra scan --android --html report.html` writes a shareable report that leads with the findings, their reasons and what to do, followed by the raw sensor and network tables.

## What spectra does NOT do

- **Claim apps are spying.** We show metadata; you decide what's normal.
- **Read encrypted traffic.** We see destination IP/domain and byte count, not payload.
- **Install anything on your phone.** Everything runs on the host over ADB.
- **Work on iOS.** Apple doesn't expose equivalent APIs.
- **Use packet capture.** /proc/net/tcp is simpler and sufficient.
- **See what Android itself doesn't record.** spectra reads Android's own access accounting over a normal (non-root) ADB shell. Spyware running with system or kernel privileges that bypasses that accounting, traffic inside a VPN tunnel, and app-private storage are invisible to it, as they are to every non-root tool.
- **List UDP/QUIC connections.** The network view is a snapshot of TCP sockets at scan time; per-app byte totals do include UDP traffic.

## Benchmarks

| Metric | Target | Measured |
|--------|--------|----------|
| Android scan latency | < 2s | Pending device test |
| Local scan latency | < 1s | 0.24s (p50) |
| Memory (host RSS) | < 10 MB | 10.4 MB |
| Detection latency | < 3s | Pending device test |

Run benchmarks: `bash bench/overhead.sh ./build/spectra`

## Testing

```bash
cd build && ctest
```

Unit tests cover the correlation engine: empty input, sensor-only events, background access bonus, full sensor+network correlation, allowlisted destinations, dual-sensor detection, and risk score capping.

## License

MIT
