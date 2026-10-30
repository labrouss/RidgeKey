# ridgekeyd and ridgekeyctl

`ridgekeyd` is a root launchd daemon that owns the fingerprint sensor. Clients talk to it over
XPC on the Mach service **`io.github.labrouss.ridgekey`** (system domain). `ridgekeyctl` is the
command-line client; the future SwiftUI app, PAM module and Authorization plugin use the same API.

## Install

From an extracted bundle (`ridgekey-<ver>-macos-x86_64.tar.gz` from CI):

```sh
xattr -dr com.apple.quarantine .   # only if downloaded with a browser
sudo ./install.sh                  # upgrade = run again; prints are kept
```

| Path | What |
|---|---|
| `/usr/local/ridgekey/{bin,lib}` | daemon, client, bundled libraries |
| `/usr/local/bin/ridgekeyctl` | symlink |
| `/Library/LaunchDaemons/io.github.labrouss.ridgekey.plist` | launchd job (starts at boot, restarts after a crash) |
| `/Library/Application Support/RidgeKey/prints/<uid>/<finger>.print` | print record (0700 / 0600, root) |
| `/Library/Logs/RidgeKey/ridgekeyd.log` | log |

Uninstall: `sudo ./uninstall.sh` keeps the prints; `sudo ./uninstall.sh --purge` first deletes
every RidgeKey print from the sensor, one at a time.

## ridgekeyctl

```
ridgekeyctl status                 sensor + daemon status
ridgekeyctl list                   your enrolled fingers
ridgekeyctl enroll right-index     ~8 touches; replaces your earlier RidgeKey print of that finger
ridgekeyctl verify [-t 30]         exit 0 = match, 1 = no match / error
ridgekeyctl delete right-index
sudo ridgekeyctl identify          match against every user's prints
sudo ridgekeyctl purge             delete every RidgeKey print (asks for confirmation)
sudo ridgekeyctl -u alice list     act on another user
```
Add `-v` to see finger on/off events. Ctrl-C cancels. Exit code 3 = daemon not reachable.

## Permissions

| Operation | Who |
|---|---|
| status | anyone |
| list, enroll, verify, delete | the user themselves (caller's effective uid); root for anyone |
| identify, purge | root only |
| cancel | the client that started the operation; root |

## Safety rules

- The daemon never calls `fp_device_clear_storage()`, and the bundled libfprint also refuses
  `BMKT_CMD_DEL_FULL_DB` (patch 0001).
- It deletes only prints in its own record, one at a time (`DEL_USER_FP`).
- Verify and identify only accept matches against RidgeKey's own prints. A Windows Hello or BIOS
  print on the same sensor gives "no match", even though the sensor recognises it.
- The sensor is opened for each operation and closed afterwards, which powers it down and
  survives sleep/wake. Operations run one at a time; a second request gets `busy`.

## XPC protocol

Defined in [`src/common/ridgekey-protocol.h`](../src/common/ridgekey-protocol.h): request keys,
the `finger`, `enroll-progress` and `retry` events, reply fields, and error codes (`busy`,
`no-device`, `permission`, `not-found`, `no-prints`, `timeout`, `cancelled`, `data-full`, `storage`,
`failed`, `bad-request`).

## Known limitations (0.1.0)

- **Callers are identified by their effective uid** (`xpc_connection_get_euid`). A later version
  should check code signatures, for example to allow only the RidgeKey app to enroll.
- **Enroll needs no password:** any process running as you can start an enrollment, but it still
  needs a finger on the sensor. The GUI will ask for your password first.
- **Unlocking isn't wired up yet.** No lock screen, `sudo` (PAM) or Authorization plugin
  integration yet; those come next and build on `verify` / `identify`.
