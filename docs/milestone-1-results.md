# Milestone 1 results: libfprint on macOS (2026-10-30)

**Setup:** HP EliteBook 840 G8, macOS Tahoe 26.7.1 (OpenCore 1.0.8, Intel), Synaptics Prometheus
06cb:00f0 with firmware 10.1 build 3478575. CI bundle with libfprint 1.94.10 and patches 0001–0003,
running libfprint's own example programs.

| Test | Result | Time from touch to result |
|---|---|---|
| Probe (USB reset, GET_VERSION) + open | ✅ | ~120 ms from start |
| Enroll, right index | ✅ 8/8 with one normal "try again" | ~45 s total |
| Verify, enrolled finger ×2 | ✅ MATCH, scores 61.7 / 61.9 | ~0.2 s |
| Verify, different finger | ✅ NO MATCH | ~0.3 s |
| Identify ×2 (gallery of 1) | ✅ matched | ~0.3 s |
| Close (POWER_DOWN_NOTIFY) | ✅ every run | — |

Print id written to the sensor: `FP1-20261030-7-F90895DF-labrouss`
(`FP1-<date>-<FpFinger enum: 7 = right index>-<random>-<user>`).

## Findings
- **Fixed during this milestone:** device creation aborted on macOS
  (`g_string_set_size` with `len - 1`, Linux sysfs wakeup code). Patch 0003 fixes it.
- **The host must keep its own list of prints.** libfprint reports "Device has not storage" because
  the synaptics driver can't list the prints stored on the sensor. ridgekeyd therefore needs a
  root-only record that maps each macOS user to its sensor print ids. It deletes only ids from that
  record (DEL_USER_FP), never the whole database.
- **Identify works**, so the lock screen can match any enrolled finger without asking which one.
- `Received message with 0 sequence number 0x91, ignoring!` is the finger on/off event and is
  expected; it shows up on Linux too.
- **No macOS-specific USB problems:** no kext needed, no root needed, and the USB reset during
  probe works through libusb.
