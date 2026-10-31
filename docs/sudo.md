---
title: sudo
nav_order: 5
---

# sudo

Approve `sudo` in Terminal with a touch.

## Turn on

**RidgeKey** → **sudo** → switch on **Use fingerprint for sudo** (macOS asks for an
administrator's password - or fingerprint, if [admin dialogs](admin-dialogs.md) are on).

{% include screenshot.html name="sudo-pane" alt="sudo pane" %}

Then open a **new** Terminal window and run `sudo -s`:

{% include screenshot.html name="sudo" alt="sudo asks for a fingerprint" %}

| You do | sudo does |
|---|---|
| touch an enrolled finger | runs, no password |
| wrong finger 3×, no touch within 10 s, or Ctrl-C | asks for your password |
| no fingers enrolled, RidgeKey not running | asks for your password straight away |
| SSH session | password only (otherwise someone at the laptop could approve your remote sudo) |

## How it works

Turning it on adds one line to `/etc/pam.d/sudo_local`, which macOS keeps across updates; the
system file `/etc/pam.d/sudo` is never edited. Turning it off removes the line.

## From Terminal

```sh
sudo ridgekey-sudo enable
sudo ridgekey-sudo disable
ridgekey-sudo status
```
Options - append to the `pam_ridgekey.so` line in `/etc/pam.d/sudo_local`: `timeout=N` (seconds
per try, default 10), `tries=N` (default 3), `allow_remote`, `nocue` (no "Touch the fingerprint
sensor" line), `debug`.

## If sudo ever breaks

Remove the line through an administrator prompt that does not use sudo:
```sh
osascript -e 'do shell script "sed -i \"\" /pam_ridgekey/d /etc/pam.d/sudo_local" with administrator privileges'
```
