---
title: Admin dialogs
nav_order: 6
---

# Admin dialogs

System Settings locks, installers and other "… wants to make changes" requests show a RidgeKey
notice first: **touch an administrator's finger** - or click **Use Password** for the normal
dialog.

{% include screenshot.html name="admin-prompt" alt="RidgeKey notice over an admin request" %}

## Turn on

**RidgeKey** → **Admin Dialogs** → switch on **Use fingerprint for admin dialogs** (macOS asks
for an administrator's password once to make the change).

{% include screenshot.html name="admin-dialogs" alt="Admin Dialogs pane" %}

Approving needs an **administrator** with enrolled fingers **and** a stored password - the
**Stored password** row shows it; store it under [Lock Screen](lock-screen.md) if needed.

**Try it:** System Settings → **Users & Groups** → click a lock or **Unlock**, and touch the reader.

| You do | macOS does |
|---|---|
| touch an enrolled administrator's finger | approves - one touch covers the follow-up requests of that action |
| wrong finger 3×, no touch within 20 s, or **Use Password** | shows the normal password dialog |
| no prints, no stored password, RidgeKey not running | shows the normal password dialog straight away |

## Safety

- Your password always works: RidgeKey can only approve or step aside, never block.
- Turning it on first checks that macOS can load RidgeKey's plugin; every upgrade re-checks.
- Turning it off restores macOS's original rules exactly. So does uninstalling.

## From Terminal

```sh
sudo ridgekey-admin enable           # the common admin dialogs
ridgekey-admin status
sudo ridgekey-admin disable          # original rules back
ridgekey-admin watch                 # shows which right a dialog uses
sudo ridgekey-admin enable <right>   # add one that is not covered
sudo ridgekey-admin candidates       # every admin-password right on this Mac
```

**If admin dialogs ever misbehave:** `sudo ridgekey-admin disable` (sudo is not affected).
