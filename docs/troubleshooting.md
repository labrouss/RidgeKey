---
title: Troubleshooting
nav_order: 8
---

# Troubleshooting
{: .no_toc }

1. TOC
{:toc}

## First checks

**In the app: Overview.**

| Row | Should say | If not |
|---|---|---|
| ridgekeyd | the version | RidgeKey is not running - reinstall, or see Terminal below |
| Sensor | your reader's name | no supported reader found - check it is connected; after sleep wait a few seconds and click ⟳ |
| Fingerprints / Lock-screen password / Fingerprint for sudo | ✓ | open that pane and set it up |

**Test my fingerprint** (Overview) checks the reader and your prints in one go.

**In Terminal:**

```sh
ridgekeyctl status                                   # daemon version, console user, reader
tail -30 /Library/Logs/RidgeKey/ridgekeyd.log        # daemon log
tail -20 ~/Library/Logs/RidgeKey-agent.log           # lock screen / finger actions agent
```

| `ridgekeyctl status` says | Meaning / fix |
|---|---|
| `cannot reach ridgekeyd` | daemon not running: `sudo launchctl kickstart -k system/io.github.labrouss.ridgekey`, or reinstall |
| `sensor: none found` | no supported reader on USB - check `ioreg -p IOUSB -l -w0 \| grep -E '"idVendor"\|"idProduct"'` against `supported-devices.txt`; after sleep, wait a few seconds |
| `(busy)` | another fingerprint request is running (e.g. sudo waiting) |

## Installing

| Problem | Fix |
|---|---|
| "RidgeKey….pkg cannot be opened" | not Developer-ID signed: right-click → **Open**, or System Settings → Privacy & Security → **Open Anyway** |
| "No supported fingerprint reader found" in the installer | a warning only - continue if your reader is on the [list](index.md#supported-readers) but disconnected |

## Fingerprints

| Message | Fix |
|---|---|
| `bad-password` | not your login password |
| `locked-out` | 3 wrong passwords - wait 30 s |
| `no-match` | the confirming finger is not one of yours - try another or use the password |
| `untrusted-client` | use the installed `/Applications/RidgeKey.app` or `ridgekeyctl`; if they are refused too, reinstall (it re-records their signatures) |
| `data-full` | the reader's storage is full - remove old prints (Windows Hello settings / BIOS) or old RidgeKey prints |

## Lock screen

| Symptom | Fix |
|---|---|
| Touch does nothing on the lock screen | App → **Lock Screen**: status **stored and valid**? **Open Accessibility settings**: **RidgeKey Agent** on? Agent log says `ridgekeyd accepted this agent`? |
| Agent log: `Accessibility permission needed` | System Settings → Privacy & Security → Accessibility → add/enable RidgeKey Agent ([steps](lock-screen.md#set-up-once)) |
| Agent log: `REFUSED this agent` | reinstall (the agent must be root-owned, signed, hardened) |
| Stopped working after a password change | store the new password |
| Two "RidgeKey Agent"/"ridgekey-agent" entries | remove the old `ridgekey-agent` one (from an older version) |
| Restart the agent | `launchctl kickstart -k gui/$(id -u)/io.github.labrouss.ridgekey.agent` |

## sudo

| Symptom | Fix |
|---|---|
| No "Touch the fingerprint sensor" line | check `/etc/pam.d/sudo_local` has the `pam_ridgekey.so` line without `nocue`; open a new Terminal |
| Asks for the password straight away | no fingers enrolled, daemon down, or an SSH session (by design) |
| sudo broken | see [sudo → If sudo ever breaks](sudo.md) |

## Admin dialogs

| Symptom | Fix |
|---|---|
| No RidgeKey notice | `ridgekey-admin status` - is that right enabled? (`ridgekey-admin watch` shows which right a dialog uses) |
| Notice appears but touch is refused | the finger must belong to an **administrator** who has a **stored password** |
| Anything odd | `sudo ridgekey-admin disable` restores the original dialogs |

## Finger actions

| Symptom | Fix |
|---|---|
| **Finger Actions** shows "Listening: off" | needs: a saved action, enrolled fingers, unlocked screen, RidgeKey Agent running. `ridgekeyctl status` shows `(listening for finger actions)` when on. Restart the agent (above) after an upgrade. |
| Action not run | `grep "action:" /Library/Logs/RidgeKey/ridgekeyd.log \| tail` - `no rule` means the gesture did not match a rule (e.g. a single tap on a double-tap-only finger) |
| Shortcut does not run | the name must match exactly; test with `shortcuts run "Name"` |
| Double tap seen as single | tap a bit faster (taps must be < 0.7 s apart) |

## Report a problem

**RidgeKey** → **About** shows the app and `ridgekeyd` versions (they should match) and has a
**Report an issue** link.

{% include screenshot.html name="about" alt="About pane" %}

Open an [issue](https://github.com/labrouss/RidgeKey/issues/new) with `ridgekeyctl status`, the
RidgeKey version, your reader's USB ID and the relevant log lines (logs never contain passwords).
