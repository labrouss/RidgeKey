# Patches to libfprint

Applied by `ci/build-libfprint-macos.sh` (in order) on top of upstream libfprint
(<https://gitlab.freedesktop.org/libfprint/libfprint>). Tested against v1.94.9 and 1.95.2+tod1;
the CI default is v1.94.10.

| Patch | Why |
|---|---|
| `0001-synaptics-never-send-DEL_FULL_DB.patch` | The Synaptics sensor shares one print database with Windows Hello and the HP BIOS. `BMKT_CMD_DEL_FULL_DB` (0x84) erases all of it. The patch unregisters `clear_storage`, so `fp_device_clear_storage()` fails with "not supported", and it aborts with `g_error()` if the command is ever queued. |
| `0002-build-no-version-script-on-macos.patch` | Apple's linker (ld64) has no `-Wl,--version-script`, so the patch skips the symbol map on Darwin. |
| `0003-device-skip-sysfs-wakeup-off-linux.patch` | `fpi_device_configure_wakeup()` writes Linux `/sys/bus/usb/.../power/*`. On macOS gusb reports no parent hub, so the port path was empty and `g_string_set_size (ports, len - 1)` aborted with *"adding 18446744073709551615 to string would overflow"* (seen on the 840 G8, 2026-10-30). Now a no-op off Linux, with an empty-path guard on Linux. |
| `0004-moc-drivers-never-erase-the-whole-store.patch` | The same rule for the other match-on-chip drivers. goodixmoc (`DELETE_ALL`), fpcmoc (`FPC_CMD_DELETE_DB`), egismoc (delete every enrolled id) and realtek (delete record `0xff`) each have an erase-everything command: `clear_storage` is unregistered and each command aborts with `g_error()` if queued. goodixmoc also wiped its whole store on open when the template list was unreadable; it now fails the open instead. Checked against 1.94.9 and 1.95.1 (Ubuntu's orig tarballs); the goodixmoc hunk avoids the state names upstream renamed in between (`FP_INIT_*` → `GOODIX_INIT_*`). |

**License:** these patches modify libfprint and are licensed **LGPL-2.1-or-later**, like libfprint.
They are not covered by RidgeKey's MIT license. See the third-party section of
`LICENSE`.

The CI build fails if patches 0001 / 0004 are not in effect: any shipped driver still registering
`clear_storage`, a guard missing, goodixmoc's wipe-on-open still present, a new
`BMKT_CMD_DEL_FULL_DB` sender, or elanmoc starting to use its (currently unused) remove-all command.
