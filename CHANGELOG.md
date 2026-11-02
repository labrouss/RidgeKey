# Changelog

All releases tested on the HP EliteBook 840 G8 (Synaptics 06cb:00f0, macOS Tahoe).

## Unreleased
- Readers: Synaptics **06cb:00bd** reported working by a user.

## 1.0.0 - First production release
- Everything from 0.1 – 0.10, tested together on the 840 G8: lock screen, admin dialogs, sudo,
  finger actions and the RidgeKey app.
- Documentation site with screenshots; README as a product page.
- Releases are no longer marked pre-release; notes come from `.github/release-notes/`.

## 0.10.0 - Finger actions
- Single, double or triple tap, or hold an enrolled finger for N seconds, to **lock the screen**
  or **run a Shortcut** (picked from your Shortcuts in the app).
- The reader waits in hardware while idle - nothing polls. sudo, the lock screen and admin
  dialogs always take priority.
- Changing actions needs an enrolled finger or your password; one action per finger + gesture.
- In-app **Help** opens the documentation site (GitHub Pages), now app-first with screenshots.

## 0.9.0 - Hardening
- Enrolling or deleting a fingerprint needs an enrolled finger or your password; the app starts
  listening for a finger right away.
- Only the installed RidgeKey app and `ridgekeyctl` (code signatures recorded at install) may
  manage prints and the stored password.
- Password checks lock out for 30 s after 3 failures.

## 0.8.0 - More readers, one package
- All libfprint match-on-chip drivers: Synaptics, Goodix, Elan, FPC, Egis, FocalTech, Realtek -
  each audited and patched so it can never erase the reader's whole storage.
- Single `.pkg` installer built by CI, with a check that warns when no supported reader is found.
- CI publishes the package and the compiled bundle as separate artifacts.

## 0.7.0 - Admin dialogs
- System Settings locks, installers and other admin prompts accept an administrator's
  fingerprint (authorization plugin + `ridgekey-admin`); **Use Password** falls back.
- One touch per action: follow-up requests from the same dialog reuse it.
- `sudo` shows "Touch the fingerprint sensor (Ctrl-C to type your password)".
- Key app icon, full logo in About; RidgeKey Agent ships as an app, so Accessibility shows its icon.

## 0.6.0 - The RidgeKey app
- SwiftUI app: status, enroll/delete with progress, lock-screen password, sudo switch.

## 0.4.0 – 0.5.0 - Lock screen
- RidgeKey Agent types your stored password into Apple's own lock screen after a match -
  display on or asleep - and never into an unlocked desktop.
- Stable local code signature (`ridgekey-sign`), so the Accessibility permission survives upgrades.

## 0.3.0 - Stored password
- Login password checked, then kept in the System keychain (root only); lock-state tracking.

## 0.2.0 - sudo
- `pam_ridgekey.so` + `ridgekey-sudo`: fingerprint for `sudo`, password as fallback.

## 0.1.0 - The daemon
- `ridgekeyd` launchd daemon owns the reader; `ridgekeyctl` enrolls, verifies, identifies and
  deletes over XPC.

## Before 0.1.0
- Reference USB capture of the 00f0 on Linux and protocol notes.
- libfprint built as a macOS library (Xcode command-line tools only, no Homebrew).
