---
title: Lock screen
nav_order: 4
---

# Lock screen

Touch the reader on the lock screen (display on or asleep) and macOS unlocks.

## Set up (once)

1. **Store your login password** - App → Lock Screen → type it → **Check and store**
   (or `ridgekeyctl password set`). It is checked against your account and kept in the System
   keychain, readable only by root.
2. **Give RidgeKey Agent Accessibility permission:** System Settings → Privacy & Security →
   **Accessibility** → **+** → press **Cmd-Shift-G**, paste `/usr/local/ridgekey/RidgeKeyAgent.app`
   → Open → make sure **RidgeKey Agent** is on. (App → Lock Screen → **Open Accessibility
   settings** takes you there.)
3. Lock the screen (**Ctrl-Cmd-Q**) and touch the reader.

The permission is needed only once: RidgeKey signs the agent with a local certificate, so
upgrades keep it.

## How it works

When the screen locks, RidgeKey Agent asks the daemon to wait for your finger. After a match the
daemon hands your stored password to the agent - only to the genuine, root-owned, hardened agent,
and only while your screen is locked - and the agent types it into Apple's own lock screen.

- After 5 wrong fingers, or if the typed password does not unlock (e.g. you changed it), the agent
  stops for that lock - type your password as usual.
- **Changed your password?** Store the new one (step 1). The app shows "outdated" until you do.
- The login window after a restart (FileVault) still needs your password once.

## Problems

See [Troubleshooting → Lock screen](troubleshooting.md#lock-screen).
