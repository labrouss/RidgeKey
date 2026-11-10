---
title: Developers
nav_order: 9
has_children: true
---

# Developers

How RidgeKey is built, tested and put together. For using RidgeKey, start at [Home](index.md).

## Components

```
 RidgeKey.app ─┐                       ┌─ lock screen  (RidgeKey Agent)
 ridgekeyctl  ─┼─ XPC ─▶ ridgekeyd ────┼─ admin dialogs (authorization plugin)
               │        (libfprint)    ├─ sudo          (pam_ridgekey)
               │            │          └─ finger actions (lock / Shortcuts)
               │        USB reader
```

| Part | Source | Role |
|---|---|---|
| `ridgekeyd` | `src/daemon/` | root launchd daemon; the only process that opens the reader. XPC service `io.github.labrouss.ridgekey` |
| `ridgekeyctl` | `src/ctl/` | command-line client |
| RidgeKey.app | `app/` | SwiftUI settings app |
| RidgeKey Agent | `src/agent/` | per-user agent: types the stored password into the lock screen, runs finger actions |
| `pam_ridgekey.so` | `src/pam/` | PAM module for `sudo` (`/etc/pam.d/sudo_local`) |
| `RidgeKey.bundle` | `src/authplugin/` | authorization plugin for admin dialogs |
| scripts | `scripts/` | `install.sh`, `uninstall.sh`, `ridgekey-sudo`, `ridgekey-admin`, `ridgekey-update`, `ridgekey-sign`, `build-app.sh`, `build-pkg.sh` |
| libfprint patches | `patches/libfprint/` | macOS build fixes and the never-erase-everything guards |

Protocol, permissions and every XPC request: [Technical reference](ridgekeyd.md).

## Build

| | |
|---|---|
| Script | `ci/build-libfprint-macos.sh` - the same in CI and locally; Xcode command-line tools only, no Homebrew |
| Stack | libusb 1.0.30 → GLib 2.84 → gusb 0.3.10 → libfprint 1.94.10 (match-on-chip drivers, patched) → `src/` |
| Output | `RidgeKey-<ver>.pkg` and `ridgekey-<ver>-macos-x86_64.tar.gz` (relocatable bundle with `install.sh`) |
| App | `scripts/build-app.sh` (Swift Package in `app/`) |

```sh
ci/build-libfprint-macos.sh          # everything; results in ~/src/fprint-build/dist (WORK=, DIST= to change)
bash scripts/build-app.sh            # RidgeKey.app
```

## CI

`.github/workflows/macos-build.yml` runs on every push that touches the code, the app or assets:

1. **Unit tests** on Linux - print store and finger-action rules (`tests/store/`).
2. **Build** on GitHub's `macos-15-intel` runner; verifies that every driver's erase-all path is
   patched out.
3. **Install test** on a fresh runner: daemon, XPC permissions and trusted clients, the PAM
   module inside OpenPAM, sudo and admin-dialog enable/disable, finger-action rules, the `.pkg`,
   uninstall.

Artifacts: `RidgeKey-<ver>-pkg` and `ridgekey-<ver>-macos-x86_64-bundle`. Tags `v*` publish a
release.

## Releases and the updater

The app (`app/Sources/RidgeKey/Updater.swift`) and `ridgekey-update` read
`api.github.com/repos/labrouss/RidgeKey/releases/latest`. For a release to be offered:

- the tag is `v<major>.<minor>.<patch>` (compared numerically; `-suffix` tags are pre-releases and
  never "latest"),
- it has `RidgeKey-<version>.pkg` **and** `RidgeKey-<version>.pkg.sha256` as assets - CI's release
  job uploads both,
- its version is newer than `RK_VERSION` of the installed app.

To try the "update available" path with an up-to-date install:
`defaults write io.github.labrouss.ridgekey.app updates.pretendVersion 0.9.0` (app; `defaults delete`
to undo) or `RK_PRETEND_VERSION=0.9.0 ridgekey-update`.

## Design notes

**Why does the lock screen use an agent, not an authorization plugin?** Since macOS 14 the lock
screen has its own modern UI. A plugin only runs there after reverting `system.login.screensaver`
to the legacy UI, and developers report the keychain not unlocking, double lock screens and focus
problems on Tahoe. RidgeKey Agent types into Apple's own lock screen instead.

**Never erase the reader.** Match-on-chip readers share one print store with Windows Hello and
the BIOS. No driver registers libfprint's `clear_storage`, each erase-all command aborts if
queued, and the build fails if a guard is missing.

## History

Version by version: [CHANGELOG](https://github.com/labrouss/RidgeKey/blob/main/CHANGELOG.md).
The reverse-engineering notes from the start of the project:
[Synaptics 00f0 protocol notes](PROTOCOL-06cb-00f0.md).
