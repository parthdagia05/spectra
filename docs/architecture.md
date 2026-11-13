# spectra Architecture

## Overview

spectra is a host-side privacy transparency tool. It runs on a computer (Linux/macOS) and queries an Android phone over ADB to answer: which apps used the mic/camera, where they sent data, and whether the timing is suspicious.

## Design principles

1. **Host-side only** — nothing installed on the phone
2. **Platform-abstracted** — five C functions per OS
3. **UID-based correlation** — Android assigns each app a unique UID, which appears in both `/proc/net/tcp` and `dumpsys appops`
4. **Metadata only** — we never read encrypted traffic content

## Module diagram

```
┌─────────────────────────────────────────────────┐
│                   main.c (CLI)                   │
│         arg parsing, command dispatch            │
└──────┬──────────────┬───────────────┬────────────┘
       │              │               │
       v              v               v
┌─────────────┐ ┌───────────┐ ┌─────────────────┐
│  collector   │ │ collector │ │    store.c      │
│  _sensor.c   │ │  _net.c   │ │  (SQLite)       │
└──────┬───────┘ └─────┬─────┘ └─────────────────┘
       │               │
       └───────┬───────┘
               v
       ┌───────────────┐
       │ correlator.c  │
       │ (join by UID, │
       │  risk score)  │
       └───────┬───────┘
               v
       ┌───────────────┐
       │  report.c     │
       │ (terminal,    │
       │  JSON, HTML)  │
       └───────────────┘

Platform backends (selected at build time + runtime):
┌──────────────┬──────────────┬──────────────┬──────────────┐
│  android.c   │   linux.c    │   darwin.c   │  windows.c   │
│  (ADB/dumpsys│  (/proc,     │  (lsof,      │  (stub)      │
│   /proc)     │   netlink)   │   libproc)   │              │
└──────────────┴──────────────┴──────────────┴──────────────┘
```

## Data flow (Android scan)

```
1. User runs: spectra scan --android

2. Connection check
   └─ adb get-state → "device"

3. Sensor collection
   ├─ adb shell dumpsys appops
   │  └─ Parse CAMERA/RECORD_AUDIO per Uid block
   │     Extract: package, timestamp, duration, fg/bg state
   └─ adb shell cmd appops query-op CAMERA/RECORD_AUDIO
      └─ Currently active sensor users

4. Network collection
   ├─ adb shell cat /proc/net/tcp[6]
   │  └─ Parse hex fields: remote IP, port, state, UID
   │     Filter: state == 01 (ESTABLISHED)
   ├─ adb shell pm list packages -U
   │  └─ Build UID → package name table
   ├─ adb shell dumpsys netstats detail
   │  └─ Per-UID byte counts (rxBytes, txBytes)
   └─ getnameinfo() on host
      └─ Reverse DNS for each remote IP

5. Correlation
   └─ For each app in both sensor + network lists:
      ├─ Match by UID
      ├─ Check timing overlap
      ├─ Compute risk score (0-100)
      └─ Generate explanation

6. App details
   └─ adb shell dumpsys package <pkg>
      └─ Label, install source, permissions, first install date

7. Report generation
   └─ Terminal / JSON / HTML output

8. Store
   └─ Save scan JSON to ~/.spectra/scans.db
```

## Platform abstraction

Every platform backend implements:

```c
int  platform_scan_sensors(spectra_sensor_event_t *out, int max);
int  platform_scan_network(spectra_net_event_t *out, int max);
int  platform_get_process_info(const char *package, spectra_process_info_t *out);
int  platform_is_known_package(const char *pkg_or_path);
const char *platform_name(void);
int  platform_check_connection(void);
```

The Android backend is always compiled alongside the host backend. The `--android` flag selects it at runtime; without it, spectra uses the host platform functions.

| Backend | Sensor detection | Network detection | Process info |
|---------|-----------------|-------------------|--------------|
| Android | dumpsys appops + cmd appops query-op | /proc/net/tcp + pm list + netstats | dumpsys package |
| Linux | /proc/*/fd → /dev/video*, PipeWire/PulseAudio | /proc/net/tcp, netlink SOCK_DIAG | /proc/pid/comm, dpkg -S |
| macOS | lsof (VDCAssistant, CoreAudio) | lsof -i -n -P | proc_name, proc_pidpath |
| Windows | stub | stub | stub |

## Risk scoring

```
Base:
  sensor_active:        +40
  bytes_sent > 10KB:    +min(bytes_KB * 0.05, 20)
  unknown destination:  +30
  timing overlap:       +10
  dual sensor (mic+cam):+10

Bonuses:
  background access:    +20

Cap: 100
```

## Key insight: UID-based matching

On Android, each app gets a unique UID at install time. This UID appears in:
- `/proc/net/tcp` (the `uid` column)
- `dumpsys appops` (the `Uid XXXXX` header)
- `pm list packages -U` (the `uid:XXXXX` field)

This means we can join sensor access to network activity without root, without PID-to-socket mapping, and without packet capture. The UID is the join key.

## Build

CMake selects the host platform backend at configure time. `android.c` is always compiled since it runs on the host.

```cmake
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    list(APPEND SPECTRA_SOURCES src/platform/linux.c)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    list(APPEND SPECTRA_SOURCES src/platform/darwin.c)
endif()
# android.c always in SPECTRA_SOURCES
```
