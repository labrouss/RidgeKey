---
title: Technical reference
parent: Developers
nav_order: 1
---

# ridgekeyd, ridgekeyctl and sudo

`ridgekeyd` is a root launchd daemon that owns the fingerprint sensor. Clients talk to it over
XPC on the Mach service **`io.github.labrouss.ridgekey`** (system domain). `ridgekeyctl` is the
command-line client; the RidgeKey app, the PAM module and the authorization plugin use the same API.

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
2. **System Settings → Privacy & Security → Accessibility** → enable **RidgeKey Agent**
   (`/usr/local/ridgekey/RidgeKeyAgent.app`, a background-only app with the RidgeKey icon; in the
   "+" file dialog press Cmd-Shift-G and paste the path). This is needed only once: `install.sh`
   signs the agent with a local certificate, so later builds keep the permission (see below).
   Upgrading from 0.7.0 or older: remove the old **ridgekey-agent** entry with "−".
3. Optional check: `/usr/local/ridgekey/bin/ridgekey-agent --type-test`, run from Terminal,
   types a sample (capitals, digits, symbols, an accented letter) into whatever app has focus,
   using the same code as the lock screen but without Return. Click into an empty TextEdit
   document and compare the text with the sample it prints.
4. Lock the screen (Ctrl-Cmd-Q) and touch the sensor.

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
| The caller is `/usr/local/ridgekey/RidgeKeyAgent.app`, identified from the connection's audit token | `ridgekeyd` |
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

### Local code signature

macOS ties Accessibility to the agent's code signature. CI builds are ad-hoc signed, which would
make every upgrade look like a new app. On each install, `ridgekey-sign` signs the agent with a
**certificate created on your Mac** the first time:
- **What it is:** self-signed, code-signing only, valid 20 years. Its key lives in
  `/Library/Application Support/RidgeKey/signing.keychain-db`, root-only, and never leaves this
  Mac.
- **The signature:** hardened runtime, identifier `io.github.labrouss.ridgekey.agent`, and a
  designated requirement naming that certificate. Every build therefore looks like the same app
  to macOS.
- **If signing fails,** the agent keeps the ad-hoc signature and still works; only the
  Accessibility re-grant comes back. Check with `sudo ridgekey-sign status`.
- `uninstall.sh --purge` deletes the certificate; a plain uninstall keeps it, so a reinstall
  signs with the same identity.

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
(default 3), `allow_remote`, `debug` (logs to the system log), `nocue` (no "Touch the
fingerprint sensor" line - by default it is written to the terminal even though sudo asks PAM
modules to be silent).

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

## Fingerprint for admin dialogs

```sh
sudo ridgekey-admin enable          # the common admin dialogs (or name rights: enable system.preferences)
sudo ridgekey-admin disable         # restore every original rule exactly
ridgekey-admin status
sudo ridgekey-admin candidates      # every admin-password right on this Mac
ridgekey-admin watch                # which rights apps ask for (to find more to enable)
ridgekey-admin selftest             # can macOS load RidgeKey.bundle? (sudo)
```

Needs an **administrator** with enrolled prints **and** a stored password (`ridgekeyctl password
set`, the same one the lock screen uses).

macOS decides who may do what with rules in its authorization database. `enable` rewrites each
chosen right so that authd tries RidgeKey first and the untouched original second:

```
system.preferences  =  any 1 of:  is-root (if the original allowed root)
                                  io.github.labrouss.ridgekey.fingerprint
                                     RidgeKey:fingerprint,privileged   (our plugin)
                                     builtin:authenticate,privileged   (macOS checks the password)
                                  io.github.labrouss.ridgekey.orig.system.preferences  (the original)
```

| You do | macOS does |
|---|---|
| touch with an enrolled admin's finger | approves; RidgeKey handed that admin's stored password to macOS, which verified it |
| wrong finger ×3, no touch within 20 s, **Use Password** | the normal password dialog |
| no admin prints, password not stored or outdated, daemon stopped, screen locked | the normal password dialog straight away |

**Safety:**
- The plugin only ever answers *allow* or *deny*. authd aborts the whole request on an internal
  error, which would skip the password fallback, so the plugin never reports one.
- `enable` first runs `selftest`: a throwaway right whose only mechanism is
  `RidgeKey:selftest`. Unless macOS loads and runs the plugin, nothing is changed.
- Each original definition is saved in `/Library/Application Support/RidgeKey/authdb/` and
  written back by `disable`. A right that a macOS update replaced since is left as it is.
- `install.sh` re-runs the selftest after an upgrade and switches the feature off if it fails.
  `uninstall.sh` restores every rule **before** removing the plugin.
- Only rights that ask for an administrator's password are accepted.
- Matching only considers administrators who have a stored password, and only while someone is
  at the console with the screen unlocked.

**If admin dialogs ever misbehave:** `sudo ridgekey-admin disable` (sudo is not affected by
this feature).

## Finger actions

```sh
ridgekeyctl action add left-index tap lock                          # single tap = lock the screen
ridgekeyctl action add left-index double shortcut "Focus"           # double tap
ridgekeyctl action add left-index triple shortcut "Meeting mode"    # triple tap
ridgekeyctl action add right-thumb hold 2 shortcut "Work mode"      # hold 2 s = run a Shortcut
ridgekeyctl actions                                                 # list
ridgekeyctl action remove right-thumb [tap|hold]     ridgekeyctl action clear
```
Or in the app: **Finger Actions**.

- **When:** while you are logged in at the console, the screen is unlocked and the sensor is
  idle. The sensor waits for a finger in its own firmware: no polling, ~0% CPU. sudo, the lock
  screen, admin dialogs and the app always take the sensor first; listening resumes after.
- **Tap / hold:** the time from "finger on" to "finger lifted". On the 840 G8's Synaptics reader
  the match arrives after the lift, so a hold is measured exactly and the action runs on release.
  The longest hold rule reached wins; otherwise the tap rule.
- **Double / triple tap:** after the first (matched) tap, quick extra touches are counted from the
  sensor's "finger on" signal - no second match needed - until no touch comes within 0.7 s.
  RidgeKey only waits like that for fingers that have a double/triple rule, so single taps on
  other fingers stay instant. Very fast taps may be missed if the sensor is still re-arming.
- **Who acts:** ridgekeyd only matches your own enrolled fingers (Windows Hello / BIOS prints and
  other people's fingers do nothing) and tells RidgeKey Agent; the agent locks the screen
  (`SACLockScreenImmediate`, Ctrl-Cmd-Q as fallback) or runs `shortcuts run "<name>"` as you.
- **Rules:** up to 20 per user in `/Library/Application Support/RidgeKey/actions/<uid>.conf`
  (root, 0600), changed only through the installed `ridgekeyctl` / RidgeKey app.

## Permissions

| Operation | Who |
|---|---|
| status | anyone |
| list, verify | the user themselves (caller's effective uid); root for anyone |
| **enroll, delete** | the user, through a **trusted client**, after **confirming**: an already enrolled finger of theirs, or their login password. Root: anyone, no confirmation |
| **password-set, password-clear** | the user, through a **trusted client** (set also checks the password); root for anyone |
| password-check | the user; root for anyone |
| actions-get | the user; root for anyone |
| actions-set | the user, through a **trusted client**, after **confirming** (like enroll); root for anyone |
| identify, purge | root only |
| authorize | root only (the authorization plugin); console user present, screen unlocked |
| unlock | the RidgeKey Agent only (path, signature, hardened runtime, root-owned) |
| cancel | the client that started the operation; root |

**Trusted clients** are the installed `/usr/local/ridgekey/bin/ridgekeyctl` and
`/Applications/RidgeKey.app`. `install.sh` records each one's code signature (its designated
requirement; for these ad-hoc signed builds that is the exact binary's hash) in the root-owned
`/usr/local/ridgekey/etc/trusted-clients`. ridgekeyd checks the *running* caller against it, so a
copy elsewhere, a modified binary or another program is refused (`untrusted-client`).

**Confirmation** (enroll / delete): `ridgekeyctl` uses an enrolled finger when you have one,
otherwise asks for your password (`--password` / `--finger` to choose, `--stdin` for scripts). The
app shows a "Confirm it is you" sheet. Three wrong passwords lock that user's password checks for
30 seconds (`locked-out`); the same limit applies to `password set`.

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

## Known limitations (0.9.0)

- **Verify is open to any process running as you** (it only answers "match / no match" for your
  own prints, and sudo's PAM module needs it).
- **The login window after a reboot (FileVault / first login) isn't covered;** that needs the
  password typed by hand once.
