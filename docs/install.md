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

### From the app (1.1 and later) {#updates}

**RidgeKey** → **About** → **Updates**, or **RidgeKey** menu → **Check for Updates…**

1. **Check Now** asks GitHub for the latest release. When one is newer, **About** in the sidebar
   gets a badge and **Overview** shows "RidgeKey x.y is available".
2. **Download** saves `RidgeKey-<version>.pkg` to your Downloads folder and checks it against the
   release's SHA-256 checksum. A file that doesn't match is deleted.
3. **Install…** opens it in Installer, which asks for an administrator's password as usual.

{% include screenshot.html name="updates" alt="About - Updates" %}

**Check for updates automatically** (on by default) checks at most every 6 hours, when the app
opens. Nothing is downloaded or installed without you clicking.

### From Terminal

```sh
ridgekey-update            # check (exit 0 = up to date, 3 = update available)
ridgekey-update download   # save the verified package to ~/Downloads
ridgekey-update install    # download, verify and install (asks for sudo)
```

Run it as yourself, not with `sudo`. Problems: [Troubleshooting → Updates](troubleshooting.md#updates).

### Manually

Download the package from the [releases page](https://github.com/labrouss/RidgeKey/releases) and
install it the same way as the first time. Your enrolled fingers, stored password, finger actions and
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
