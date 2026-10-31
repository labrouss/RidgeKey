<p align="center"><img src="assets/ridgekey-logo.png" alt="RidgeKey" width="560"></p>

# RidgeKey

Fingerprint authentication for Hackintosh laptops with **match-in-sensor** readers, built on
[libfprint](https://fprint.freedesktop.org/). The first target is the **Synaptics Prometheus
06cb:00f0** in the HP EliteBook 840 G8 (macOS Tahoe, Intel).

> **Status:** working on the HP EliteBook 840 G8 (macOS Tahoe, 2026-10-31): enroll, `sudo`, the
> lock screen (display on or asleep) and admin dialogs (System Settings locks, installers) all
> accept a fingerprint, with the password as fallback. Early, but usable.

> **Safety:** this sensor shares one print database with Windows Hello and the HP BIOS.
> RidgeKey **never** sends `BMKT_CMD_DEL_FULL_DB` (0x84), and every build removes that code path
> (see `patches/libfprint/`). It only deletes prints it created itself.

## Roadmap

| Phase | What | Status |
|---|---|---|
| 1 | Reference capture on Linux (`linux/`), protocol notes (`docs/PROTOCOL-06cb-00f0.md`) | ✅ |
| 2 | libfprint as a macOS library, with the example enroll/verify working against the 00f0 | ✅ |
| 3 | `ridgekeyd` launchd daemon + `ridgekeyctl` client over XPC ([docs](docs/ridgekeyd.md)) | ✅ 0.1.0 tested on the 840 G8 (enroll, verify, identify, cancel, reboot) |
| 4a | `sudo` with fingerprint: `pam_ridgekey.so` + `ridgekey-sudo` ([docs](docs/ridgekeyd.md#fingerprint-for-sudo)) | ✅ 0.2.0 tested: match, wrong finger, Ctrl-C |
| 4c | Stored password: checked, then kept in the System keychain (root only); lock-state tracking | ✅ 0.3.0 tested |
| 4b | Lock screen: `ridgekey-agent` types the stored password into Apple's own lock screen after a match ([docs](docs/ridgekeyd.md#lock-screen-unlock)) | ✅ 0.4.3 tested: display on and asleep |
| 4d | **RidgeKey.app** (SwiftUI): status, enroll/delete with progress, lock-screen password, sudo toggle | ✅ 0.6.0 tested on the 840 G8 |
| 4e | Stable local code signature, so Accessibility survives upgrades (`ridgekey-sign`) | ✅ 0.6.0: confirmed on the 840 G8 (reinstalls keep the certificate and the Accessibility grant) |
| 4f | Admin password dialogs by fingerprint: `RidgeKey.bundle` authorization plugin + `ridgekey-admin` ([docs](docs/ridgekeyd.md#fingerprint-for-admin-dialogs)) | ✅ 0.7.0 tested on the 840 G8: Users & Groups unlocks with one touch (follow-ups reuse it), Use Password falls back |
| 5 | **More readers**: all libfprint match-on-chip drivers, each audited for erase-all paths; installer reader check ([supported sensors](#supported-sensors)) | ✅ 0.8.0: package and reader check confirmed on the 840 G8 |
| 6 | **Hardening**: enroll/delete need your password or an enrolled finger; only the installed ridgekeyctl / RidgeKey app (signatures recorded at install) may manage prints and the stored password; password lockout ([docs](docs/ridgekeyd.md#permissions)) | 🔧 0.9.0 |

Why not an Authorization plugin for the lock screen? Since macOS 14 the lock screen uses its own
modern UI. Getting a plugin to run there means reverting `system.login.screensaver` to the legacy
UI, and developers report the keychain not unlocking, double lock screens and focus problems on
Tahoe.

## Try it

1. Download **`RidgeKey-<version>.pkg`** from the latest
   [release](https://github.com/labrouss/RidgeKey/releases), or from Actions → **macos-build** →
   artifact **`RidgeKey-<version>-pkg`** (the compiled bundle for Terminal installs is the separate
   artifact `ridgekey-<version>-macos-x86_64-bundle`).
2. Open it. It is not signed with an Apple Developer ID, so the first time: right-click → **Open**
   (or System Settings → Privacy & Security → **Open Anyway**). Upgrades keep your fingers and
   settings.

   Terminal alternative, same result:
   ```sh
   tar xzf ridgekey-*-macos-x86_64.tar.gz && cd ridgekey-*-macos-x86_64
   xattr -dr com.apple.quarantine .     # only if downloaded with a browser
   sudo ./install.sh
   ```
3. Open **RidgeKey** from Applications to enroll fingers, store the lock-screen password and turn
   on fingerprint for sudo. Or use the command line (no sudo):
   ```sh
   ridgekeyctl enroll right-index
   ridgekeyctl verify        # exit code 0 = match
   ridgekeyctl list
   ```
4. Optional, fingerprint for `sudo` (your password keeps working):
   ```sh
   sudo ridgekey-sudo enable    # then in a NEW terminal: sudo -k; sudo -v
   ```
5. Optional, fingerprint for admin dialogs (System Settings locks, installers). The password
   dialog still appears when the fingerprint fails:
   ```sh
   sudo ridgekey-admin enable
   ```

Details, permissions and the XPC API: [docs/ridgekeyd.md](docs/ridgekeyd.md).

## Build

| | |
|---|---|
| CI | `.github/workflows/macos-build.yml`: store unit tests on Linux, a full build on GitHub's `macos-15-intel` runner, then an install test on a fresh runner (daemon, XPC permissions, the PAM module inside OpenPAM, sudo_local enable/disable, uninstall) |
| Build | `ci/build-libfprint-macos.sh` (same script in CI and locally; Xcode CLT only, no Homebrew) |
| Stack | libusb 1.0.30 → GLib 2.84 → gusb 0.3.10 → libfprint v1.94.10 (`-Ddrivers=synaptics`, `patches/libfprint/`) → `src/` |
| Output | `ridgekey-<ver>-macos-x86_64.tar.gz`: relocatable and self-contained, with an installer |

## Supported sensors

RidgeKey builds libfprint's **match-on-chip** drivers: the prints live on the reader itself, as on
the 840 G8. Every release lists the exact USB IDs in `supported-devices.txt`, and the installer
warns when none of them is connected.

| Driver | Vendor (USB vendor id) | Status |
|---|---|---|
| `synaptics` | Synaptics (06cb) | ✅ tested: 06cb:00f0, HP EliteBook 840 G8, FW 10.01.3478575 |
| `goodixmoc` | Goodix (27c6) | untested - reports welcome |
| `elanmoc` | Elan (04f3) | untested |
| `fpcmoc` | FPC (10a5) | untested |
| `egismoc` | Egis (1c7a) | untested |
| `focaltech_moc` | FocalTech | untested |
| `realtek` | Realtek (0bda) | untested |

**Safety, for every driver:** the reader's whole print store is never erased - it may hold Windows
Hello / BIOS prints. No driver registers libfprint's `clear_storage`, each driver's erase-all command
aborts if it is ever queued, and goodixmoc no longer wipes its store when it cannot read its
template list (upstream does). See `patches/libfprint/` - the build checks all of it.

Find your reader's ID: Linux `lsusb`; Windows Device Manager → Biometric devices → Details →
Hardware Ids; macOS `ioreg -p IOUSB -l -w0 | grep -E '"idVendor"|"idProduct"'` (decimal).

## License

RidgeKey's own code is **MIT** (see `LICENSE`). libfprint, GLib, gusb and libusb are
LGPL-2.1-or-later third-party libraries, linked dynamically. Our libfprint patches are LGPL as well.

Not affiliated with or endorsed by Apple, Synaptics, HP or the libfprint project. Inspired by
[hack-touchid](https://github.com/hackintosh-user/hack-touchid); it shares no code with it.
