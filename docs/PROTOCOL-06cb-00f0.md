---
title: Synaptics 00f0 protocol notes
parent: Developers
nav_order: 2
---

# Synaptics Prometheus (06cb:00f0) USB / BMKT protocol - observed

Source: Linux capture 2026-10-30 (HP EliteBook 840 G8, Ubuntu 26.04 live, libfprint 1.95.1+tod1,
fprintd 1.94.5), sensor firmware **Prometheus 10.01.3478575**, IOTA config **0005**.
Reference implementation: libfprint `drivers/synaptics/` (LGPL-2.1).

## USB
| | |
|---|---|
| Device | USB 2.0, full speed (12M), class ff/10/ff, no manufacturer/product strings, serial string present, bus-powered + remote wakeup (`bmAttributes a0h`), MaxPower 50 (100 mA) |
| Interface 0 | vendor class 0xFF/00/00, one configuration, **3 endpoints** |
| EP 0x01 OUT (bulk, 64) | requests |
| EP 0x81 IN (bulk, 64) | replies |
| EP 0x83 IN (interrupt, 8, bInterval 4, 7-byte reports) | "async message pending" (byte 0 bit 2 = `USB_ASYNC_MESSAGE_PENDING`) |
| ~~EP 0x82~~ | **not present** on 00f0 (libfprint defines `USB_EP_FINGERPRINT 0x82`, but the descriptor has no such endpoint) |

macOS (Tahoe 26.7.1): enumerates as `IOUSBHostDevice`, `IOUSBHostInterface@0` has no driver attached;
libusb 1.0.30 opens it without root (`testlibusb -v` reads the serial).

## Framing
| Direction | Bytes |
|---|---|
| Sensor FW command | `cmd` (1 byte): `0x01` GET_VERSION, `0xA7` ACE_COMMAND (carries a BMKT message), `0xA8` ASYNCMSG_READ |
| BMKT message (after `0xA7`) | `FE` `seq` `msg_id` `payload_len` `payload…` |
| Reply | 2-byte status (`00 00` = OK) + BMKT message `FE seq msg_id len payload` |
| Async | interrupt report → host sends `0xA8` → reply carries the pending BMKT message (`seq` = request's seq, or 0 for events) |

`seq` increments per command (1..255, wraps, 0 reserved for unsolicited events).

## Observed sequences
**Open**: `01` GET_VERSION (38-byte reply) → `FPS_INIT 0x11` → `FPS_INIT_OK 0x13`.
Repeated `FPS_INIT` later answers `FPS_INIT_FAIL 0x12` payload `00 67` = already initialised (ignore).

**Enroll** (`ENROLL_USER 0x51`, payload: `00?` + flags + user-id string, e.g. `FP1-20261030-7-7DCEC9EF-ubuntu`):
`ENROLL_READY 0x54` → per touch: `EVT_FINGER_REPORT 0x91` (01 on / 00 off), `CAPTURE_COMPLETE 0x60`,
`ENROLL_REPORT 0x55` progress % (0c,19,25,32,3e,4b,57,64 = 12…100 %, 8 touches) → `ENROLL_OK 0x59`
payload `01 46 <user id>`. **Likely** `01` = finger id and `46` = ASCII `F`, the first byte of the user id
`FP1-…` (not a length) - confirm in the trace.

**Verify** (`VERIFY_USER 0x65`, payload: user id + finger): `VERIFY_READY 0x66` → touch →
`CAPTURE_COMPLETE 0x60` → `VERIFY_OK 0x68` payload `<score 0x60/0x50> 01 <user id "FP1-…">` or
`VERIFY_FAIL 0x67` payload `00 d5` (bad/early capture, retry).

**Delete one** (`DEL_USER_FP 0x81`, user id + finger) → `DEL_USER_FP_OK 0x83`.
**Delete all** (`DEL_FULL_DB 0x84`) → `DELETE_PROGRESS 0x87` … `DEL_FULL_DB_OK 0x86`.
**Never send `DEL_FULL_DB` from the macOS backend** - it erases Windows Hello / BIOS prints too.

**Close**: `POWER_DOWN_NOTIFY 0xA1` → `POWER_DOWN_READY`.

## Not yet captured (from libfprint source)
`ID_USER 0x61` (identify any finger), `GET_TEMPLATE_RECORDS 0x71` / `GET_NEXT_QUERY_RESPONSE 0x72`
(list stored prints), `CANCEL_OP 0x41`.
