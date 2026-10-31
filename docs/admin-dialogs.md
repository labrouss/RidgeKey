---
title: Admin dialogs
nav_order: 6
---

# Admin dialogs

System Settings locks, installers and other "… wants to make changes" requests show a RidgeKey
notice first: **touch an administrator's finger**, or choose **Use Password** for the normal dialog.

## Turn on

App → **Admin Dialogs** → toggle, or:
```sh
sudo ridgekey-admin enable       # the common admin dialogs
ridgekey-admin status
sudo ridgekey-admin disable      # restores macOS's original rules exactly
```
Approving needs an **administrator** with enrolled fingers **and** a stored password
([Lock screen](lock-screen.md), step 1).

| You do | macOS does |
|---|---|
| touch an enrolled administrator's finger | approves - one touch covers the follow-up requests of the same action (15 s) |
| wrong finger 3×, no touch within 20 s, **Use Password** | the normal password dialog |
| no prints / no stored password / daemon stopped | the normal password dialog straight away |

## Which dialogs

`enable` switches the common ones (System Settings panes, installing software, admin tool
requests). To find others, run `ridgekey-admin watch`, open the dialog, and enable the right it
shows: `sudo ridgekey-admin enable <right>`. `sudo ridgekey-admin candidates` lists every
admin-password right on your Mac.

## Safety

- The plugin only ever answers "allow" or "deny", so macOS can always fall back to your password.
- `enable` refuses unless macOS can load the plugin (self-test); the installer re-checks after
  every upgrade.
- Every changed rule is backed up and restored exactly by `disable` and by the uninstaller.

**If admin dialogs ever misbehave:** `sudo ridgekey-admin disable` (sudo is not affected).
