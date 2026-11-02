---
title: Home
nav_order: 1
permalink: /
---

![RidgeKey](assets/ridgekey-logo.png)

# RidgeKey
{: .fs-9 }

Touch ID-style fingerprint login for macOS on laptops whose reader macOS doesn't support.
{: .fs-6 .fw-300 }

[Download](https://github.com/labrouss/RidgeKey/releases/latest){: .btn .btn-primary .mr-2 }
[Install guide](install.md){: .btn .mr-2 }
[GitHub](https://github.com/labrouss/RidgeKey){: .btn }

Hackintosh laptops often have a perfectly good fingerprint reader that macOS ignores. RidgeKey
puts it to work: enroll your fingers in a small settings app, then touch the sensor wherever
macOS would ask for your password. For **match-on-chip** readers (the fingerprint is stored and
matched inside the reader), built on [libfprint](https://fprint.freedesktop.org/).

| With RidgeKey you can | In the app | Page |
|---|---|---|
| Enroll and manage fingers | Fingerprints | [Fingerprints](fingerprints.md) |
| Unlock the lock screen with a touch | Lock Screen | [Lock screen](lock-screen.md) |
| Approve `sudo` in Terminal | sudo | [sudo](sudo.md) |
| Approve admin dialogs (System Settings locks, installers) | Admin Dialogs | [Admin dialogs](admin-dialogs.md) |
| Tap, double/triple tap or hold a finger to lock the screen or run a Shortcut | Finger Actions | [Finger actions](finger-actions.md) |

Your password **always keeps working**: every fingerprint feature falls back to it.

## Quick start

1. [Install](install.md) `RidgeKey-<version>.pkg` from the latest
   [release](https://github.com/labrouss/RidgeKey/releases).
2. Open **RidgeKey** from Applications. **Overview** shows whether your reader was found.
3. **Fingerprints** → pick a finger → **Enroll…**
4. Turn on what you want: **Lock Screen**, **sudo**, **Admin Dialogs**, **Finger Actions**.

{% include screenshot.html name="overview" alt="RidgeKey Overview" %}

In the app, **Help** (⌘?) and the **?** button open the page for what you are looking at.
Something not working? [Troubleshooting](troubleshooting.md).

## Supported readers

Working: **Synaptics 06cb:00f0** (HP EliteBook 840 G8, macOS Tahoe) and **06cb:00bd** (reported by a
user). RidgeKey also includes
libfprint's other match-on-chip drivers - Goodix, Elan, FPC, Egis, FocalTech, Realtek - untested
so far; **Overview** marks such a reader "untested". Every release lists the exact USB IDs in
`supported-devices.txt`, and the installer warns when none is connected. Reports for other
readers are welcome on [GitHub](https://github.com/labrouss/RidgeKey/issues).

## Safety

Laptop readers often store Windows Hello and BIOS fingerprints too. RidgeKey **never erases the
reader's whole storage**: it only deletes fingerprints it enrolled itself, and every build removes
the drivers' erase-everything commands (see [Developers](development.md)).

## More

[What's new](https://github.com/labrouss/RidgeKey/blob/main/CHANGELOG.md) ·
[Developers](development.md) ·
[Report an issue](https://github.com/labrouss/RidgeKey/issues/new)
