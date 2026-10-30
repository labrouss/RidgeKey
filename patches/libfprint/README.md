# Patches to libfprint

Applied by `ci/build-libfprint-macos.sh` (in order) on top of upstream libfprint
(<https://gitlab.freedesktop.org/libfprint/libfprint>). Tested against v1.94.9 and 1.95.2+tod1;
the CI default is v1.94.10.

| Patch | Why |
|---|---|
| `0001-synaptics-never-send-DEL_FULL_DB.patch` | The Synaptics sensor shares one print database with Windows Hello and the HP BIOS. `BMKT_CMD_DEL_FULL_DB` (0x84) erases all of it. The patch unregisters `clear_storage`, so `fp_device_clear_storage()` fails with "not supported", and it aborts with `g_error()` if the command is ever queued. |
| `0002-build-no-version-script-on-macos.patch` | Apple's linker (ld64) has no `-Wl,--version-script`, so the patch skips the symbol map on Darwin. |

**License:** these patches modify libfprint and are licensed **LGPL-2.1-or-later**, like libfprint.
They are not covered by RidgeKey's MIT license. See the third-party section of
`LICENSE`.

The CI build fails if patch 0001 is not in effect: `clear_storage` still registered, the guard
missing, or a new `BMKT_CMD_DEL_FULL_DB` sender added upstream.
