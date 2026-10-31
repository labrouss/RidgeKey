---
title: Install, upgrade, uninstall
nav_order: 2
---

# Install, upgrade, uninstall
{: .no_toc }

1. TOC
{:toc}

## Requirements

- macOS 13 Ventura or later, Intel (x86_64). Tested on macOS Tahoe.
- A supported fingerprint reader ([list](index.md#supported-readers)). You can install without one;
  RidgeKey picks the reader up as soon as it is connected.

## Install with the package (recommended)

1. Download **`RidgeKey-<version>.pkg`** from the latest
   [release](https://github.com/labrouss/RidgeKey/releases) (or from a CI run: Actions →
   **macos-build** → artifact `RidgeKey-<version>-pkg`).
2. Open it. The package is not signed with an Apple Developer ID, so the first time macOS blocks
   it: **right-click → Open**, or **System Settings → Privacy & Security → Open Anyway**.
3. Follow the installer. It warns if no supported reader is connected (you can continue).
4. Open **RidgeKey** from Applications.

The package installs:

| What | Where |
|---|---|
| RidgeKey app | `/Applications/RidgeKey.app` |
| Daemon and tools | `/usr/local/ridgekey/` (`ridgekeyctl`, `ridgekey-sudo`, `ridgekey-admin` linked into `/usr/local/bin`) |
| RidgeKey Agent (lock screen, finger actions) | `/usr/local/ridgekey/RidgeKeyAgent.app` |
| Admin-dialog plugin (off until you enable it) | `/Library/Security/SecurityAgentPlugins/RidgeKey.bundle` |
| Your fingerprints record, finger actions | `/Library/Application Support/RidgeKey/` (root only) |
| Log | `/Library/Logs/RidgeKey/ridgekeyd.log` |

Nothing about how you log in changes until you turn a feature on.

## Install from Terminal

The `.tar.gz` (artifact `ridgekey-<version>-macos-x86_64-bundle`) contains the same files:

```sh
tar xzf ridgekey-*-macos-x86_64.tar.gz && cd ridgekey-*-macos-x86_64
xattr -dr com.apple.quarantine .     # only if downloaded with a browser
sudo ./install.sh
```

## Upgrade

Install the new version the same way. Your enrolled fingers, stored password, finger actions and
enabled features are kept:

- `sudo` keeps working during the upgrade (the files are swapped in one step).
- Admin dialogs: the installer checks that macOS can load the new plugin, and switches the
  feature off if it cannot (you get the normal password dialog instead).
- Accessibility (lock screen) is kept, thanks to RidgeKey's local code signature.

**Upgrading from an early 0.7 build or older:** the lock-screen helper is now an app with an icon. In
**System Settings → Privacy & Security → Accessibility**, remove the old **ridgekey-agent** entry
with **−** and add **RidgeKey Agent** (see [Lock screen](lock-screen.md)).

## Uninstall

```sh
sudo "/Library/Application Support/RidgeKey/Installer/<version>/uninstall.sh"            # keeps fingerprints
sudo "/Library/Application Support/RidgeKey/Installer/<version>/uninstall.sh" --purge    # removes them too
```
(Installed from Terminal: run `sudo ./uninstall.sh` in the extracted folder.)

The uninstaller first switches `sudo` and admin dialogs back to password-only, then removes
everything. `--purge` also deletes RidgeKey's fingerprints from the reader (one by one - Windows
Hello and BIOS fingerprints stay), the stored passwords and the local code-signing certificate.
