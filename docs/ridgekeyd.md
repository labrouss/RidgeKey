# ridgekeyd, ridgekeyctl and sudo

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

## Lock-screen unlock

1. `ridgekeyctl password set`: store your login password (see below).
2. **System Settings → Privacy & Security → Accessibility** → enable **ridgekey-agent**
   (`/usr/local/ridgekey/bin/ridgekey-agent`). After an upgrade, remove it with "−" and add it
   again with "+"; the build is signed without a developer certificate, so macOS treats each
   build as a new app.
3. Lock the screen (Ctrl-Cmd-Q) and touch the sensor.

**How it works:**
- `ridgekey-agent` runs in your login session (LaunchAgent). When the screen locks it asks
  `ridgekeyd` to wait for a finger.
- After a match, the agent wakes the display, types your password into Apple's own lock
  screen, presses Return, and wipes its copy. The native lock screen stays in charge, so your
  login keychain unlocks as usual.
- If the typed password doesn't unlock the screen (for example the display was still waking),
  touch the sensor again.
- After 5 wrong fingers it stops for that lock; type your password as usual.

**When the daemon releases the password:**

| Check | Done by |
|---|---|
| The caller is `/usr/local/ridgekey/bin/ridgekey-agent`, identified from the connection's audit token | `ridgekeyd` |
| Its code signature is valid and uses the hardened runtime (no DYLD injection or debugger attach) | `ridgekeyd` |
| The agent and every folder above it are owned by root and not writable by others | `ridgekeyd` |
| The caller is the console user, and macOS reports that user's screen locked (IORegistry) | `ridgekeyd` |
| A finger matched that user's RidgeKey prints in this same request | `ridgekeyd` |
| The stored password still works (PAM `checkpw`) | `ridgekeyd` |

Anything else, including `ridgekeyctl` and root itself, gets `permission`. Agent log:
`~/Library/Logs/RidgeKey-agent.log` (it never contains the password).

**Caveat:** with SIP disabled (common on hackintoshes), root and kernel extensions can get
around these process checks. That doesn't matter much here, since root can read the System
keychain anyway.

## Lock-screen password

```sh
ridgekeyctl password set       # asks for your login password (no echo)
ridgekeyctl password check     # stored? still your current password?
ridgekeyctl password clear
```

- **Checked before storing:** the password must be your current login password (PAM `checkpw`)
  or it isn't saved. It is then read back to prove the daemon can use it without a prompt.
- **Where it lives:** one item per user in `/Library/Keychains/System.keychain` (service
  `io.github.labrouss.ridgekey.password`), encrypted at rest and readable only by root. The
  item's access list allows root processes without a prompt, so an upgraded (re-signed)
  `ridgekeyd` can still read it.
- **Never sent back:** the password is never written to the log and never returned over XPC.
- **After you change your login password,** `password check` reports it as OUTDATED and the
  daemon won't type it. Run `password set` again.
- `uninstall.sh --purge` removes all stored passwords; a plain uninstall keeps them.

The daemon also follows the console: `ridgekeyctl status` and the log show who is logged in at
the screen and whether it is locked (read from the IORegistry `IOConsoleUsers`, which user
processes cannot fake).

## Fingerprint for sudo

```sh
sudo ridgekey-sudo enable      # then, in a NEW terminal:  sudo -k; sudo -v
sudo ridgekey-sudo disable
ridgekey-sudo status
```

`enable` adds one line at the top of `/etc/pam.d/sudo_local`. macOS includes that file from
`/etc/pam.d/sudo` and keeps it across updates; `/etc/pam.d/sudo` itself is never edited.

```
auth       sufficient     /usr/local/ridgekey/lib/pam/pam_ridgekey.so
```

| You do | sudo does |
|---|---|
| touch with an enrolled finger | runs, no password |
| wrong finger ×3, no touch within 10 s, Ctrl-C | asks for your password as usual |
| no prints enrolled, daemon stopped, sensor busy | asks for your password straight away |
| SSH session | password only. Otherwise whoever is at the laptop could approve a remote `sudo` with a touch. Add `allow_remote` to the line to override. |

Module options (append to the line): `timeout=N` (seconds per try, default 10), `tries=N`
(default 3), `allow_remote`, `debug` (logs to the system log).

**Safety:**
- Before editing, `ridgekey-sudo` checks that the module and every folder above it are owned by
  root and not writable by others.
- It then proves OpenPAM can load the module with a throwaway PAM service, using
  `ridgekey-pamcheck`, before sudo depends on it.
- A backup is kept as `sudo_local.ridgekey-backup`.
- `uninstall.sh` removes the line **before** deleting any file, and upgrades swap the install
  folder in one step. OpenPAM fails the whole sudo stack when a listed module is missing, so the
  module path must never disappear while the line is there.

**If sudo ever breaks**, remove the line through an administrator prompt that doesn't use sudo:
```sh
osascript -e 'do shell script "sed -i \"\" /pam_ridgekey/d /etc/pam.d/sudo_local" with administrator privileges'
```

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

## Known limitations (0.4.0)

- **Callers are identified by their effective uid** (`xpc_connection_get_euid`). A later version
  should check code signatures, for example to allow only the RidgeKey app to enroll.
- **Enroll needs no password:** any process running as you can start an enrollment, but it still
  needs a finger on the sensor. The GUI will ask for your password first.
- **Accessibility permission has to be granted again after every upgrade** (ad-hoc signing).
- **The login window after a reboot (FileVault / first login) isn't covered;** that needs the
  password typed by hand once.
