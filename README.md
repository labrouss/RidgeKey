<p align="center"><img src="assets/ridgekey-logo.png" alt="RidgeKey" width="560"></p>

# RidgeKey

Fingerprint authentication for Hackintosh laptops with **match-in-sensor** readers, built on
[libfprint](https://fprint.freedesktop.org/). The first target is the **Synaptics Prometheus
06cb:00f0** in the HP EliteBook 840 G8 (macOS Tahoe, Intel).

> **Status:** early development. ✅ Milestone 1 reached on 2026-10-30: libfprint enroll, verify and identify
> work on macOS Tahoe against the 06cb:00f0 ([results](docs/milestone-1-results.md)).

> **Safety:** this sensor shares one print database with Windows Hello and the HP BIOS.
> RidgeKey **never** sends `BMKT_CMD_DEL_FULL_DB` (0x84), and every build removes that code path
> (see `patches/libfprint/`). It only deletes prints it created itself.

## Roadmap

| Phase | What | Status |
|---|---|---|
| 1 | Reference capture on Linux (`linux/`), protocol notes (`docs/PROTOCOL-06cb-00f0.md`) | ✅ |
| 2 | libfprint as a macOS library, with the example enroll/verify working against the 00f0 | ✅ |
| 3 | `ridgekeyd` launchd daemon + `ridgekeyctl` client over XPC ([docs](docs/ridgekeyd.md)) | 🔧 0.1.0, waiting for its first test on the Mac |
| 4 | Front-ends: SwiftUI settings and enrollment, lock-screen password typing, `sudo` PAM module, Authorization plugin | planned |
| 5 | Stored passwords: encrypted, root-only APFS volume with its key in the System keychain | planned |

## Try it

1. Get a build: Actions → **macos-build** → latest run → artifact `ridgekey-macos-x86_64`
   (or a GitHub Release from a `v*` tag).
2. Install:
   ```sh
   tar xzf ridgekey-*-macos-x86_64.tar.gz && cd ridgekey-*-macos-x86_64
   xattr -dr com.apple.quarantine .     # only if downloaded with a browser
   sudo ./install.sh
   ```
3. Use (no sudo):
   ```sh
   ridgekeyctl enroll right-index
   ridgekeyctl verify        # exit code 0 = match
   ridgekeyctl list
   ```

Details, permissions and the XPC API: [docs/ridgekeyd.md](docs/ridgekeyd.md).

## Build

| | |
|---|---|
| CI | `.github/workflows/macos-build.yml`: store unit tests on Linux, then a full build on GitHub's `macos-15-intel` runner |
| Build | `ci/build-libfprint-macos.sh` (same script in CI and locally; Xcode CLT only, no Homebrew) |
| Stack | libusb 1.0.30 → GLib 2.84 → gusb 0.3.10 → libfprint v1.94.10 (`-Ddrivers=synaptics`, `patches/libfprint/`) → `src/` |
| Output | `ridgekey-<ver>-macos-x86_64.tar.gz`: relocatable and self-contained, with an installer |

## Supported sensors

| Sensor | USB id | Laptop | Status |
|---|---|---|---|
| Synaptics Prometheus (FW 10.01.3478575) | 06cb:00f0 | HP EliteBook 840 G8 | ✅ enroll / verify / identify (libfprint examples) |

Other Synaptics Prometheus readers supported by libfprint's `synaptics` driver should work the same
way. Reports are welcome.

## License

RidgeKey's own code is **MIT** (see `LICENSE`). libfprint, GLib, gusb and libusb are
LGPL-2.1-or-later third-party libraries, linked dynamically. Our libfprint patches are LGPL as well.

Not affiliated with or endorsed by Apple, Synaptics, HP or the libfprint project. Inspired by
[hack-touchid](https://github.com/hackintosh-user/hack-touchid); it shares no code with it.
