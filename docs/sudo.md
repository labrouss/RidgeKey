---
title: sudo
nav_order: 5
---

# sudo

```sh
sudo ridgekey-sudo enable      # or: App → sudo → toggle
```
Then, in a **new** Terminal window: `sudo -k; sudo -v` → `Touch the fingerprint sensor (Ctrl-C to
type your password)`.

| You do | sudo does |
|---|---|
| touch an enrolled finger | runs, no password |
| wrong finger 3×, no touch within 10 s, Ctrl-C | asks for your password |
| no fingers enrolled, daemon stopped | asks for your password straight away |
| SSH session | password only (otherwise someone at the laptop could approve your remote sudo) |

`enable` adds one line to `/etc/pam.d/sudo_local` (which macOS keeps across updates); the system
file `/etc/pam.d/sudo` is never edited. `sudo ridgekey-sudo disable` removes it.

**Options** - append to the line in `/etc/pam.d/sudo_local`: `timeout=N` (seconds per try, default
10), `tries=N` (default 3), `allow_remote`, `nocue` (no "Touch the fingerprint sensor" line),
`debug`.

**If sudo ever breaks**, remove the line through an administrator prompt that does not use sudo:
```sh
osascript -e 'do shell script "sed -i \"\" /pam_ridgekey/d /etc/pam.d/sudo_local" with administrator privileges'
```
