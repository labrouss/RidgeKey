---
title: Lock screen
nav_order: 4
---

# Lock screen

Touch the reader on the lock screen - display on or asleep - and your Mac unlocks.

Open **RidgeKey** → **Lock Screen**.

{% include screenshot.html name="lock-screen" alt="Lock Screen pane" %}

## Set up (once)

1. **Store your login password.** Under **Stored login password**, type it and click
   **Check and store**. The status becomes **stored and valid**. (It is checked against your
   account and kept in the System keychain, readable only by root.)
2. **Allow RidgeKey Agent.** Click **Open Accessibility settings**. In the list, switch on
   **RidgeKey Agent**. If it is not listed: click **+**, press **Cmd-Shift-G**, paste
   `/usr/local/ridgekey/RidgeKeyAgent.app`, click **Open**, and switch it on.
3. **Try it:** lock the screen (**Ctrl-Cmd-Q**) and touch the reader.

You need step 2 only once: RidgeKey keeps the agent's signature across upgrades.

## Good to know

- **Changed your password?** Store the new one (step 1). Until then the status shows
  **outdated** and the lock screen asks for the password.
- After 5 wrong fingers, the lock screen waits for your password for that lock.
- The login window after a restart (FileVault) still needs your password once.
- **Remove** deletes the stored password; the lock screen then needs your password again.

## How it works

When the screen locks, RidgeKey Agent asks RidgeKey to wait for your finger. After a match,
RidgeKey hands the stored password to the agent - only to the genuine, root-owned agent, and only
while your screen is locked - and the agent types it into Apple's own lock screen.

## From Terminal

```sh
ridgekeyctl password set        # store / update the password
ridgekeyctl password check      # still valid?
ridgekeyctl password clear
```

Problems → [Troubleshooting → Lock screen](troubleshooting.md#lock-screen).
