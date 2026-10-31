---
title: Fingerprints
nav_order: 3
---

# Fingerprints

## Enroll a finger

**App:** Fingerprints → choose a finger → **Enroll…**

- If you already have an enrolled finger, RidgeKey first asks you to **confirm it is you**: touch
  one of your enrolled fingers, **or** type your login password in the same window.
- Your first finger needs your login password.
- Then touch the reader, lift, and repeat (about 8 times) until the progress bar is full.

**Terminal:**
```sh
ridgekeyctl enroll right-index             # confirms with an enrolled finger, or asks your password
ridgekeyctl enroll right-index --password  # always use the password
ridgekeyctl list
ridgekeyctl verify                         # test: exit code 0 = match
```
Finger names: `left-thumb left-index left-middle left-ring left-little right-thumb right-index
right-middle right-ring right-little`. Enrolling a finger again replaces its old print.

## Delete a finger

**App:** the trash icon next to the finger (confirmed the same way). **Terminal:**
`ridgekeyctl delete right-index`.

RidgeKey only deletes fingerprints it enrolled itself. Windows Hello and BIOS fingerprints stored
on the same reader are never touched.

## Why the confirmation?

Without it, anyone at your unlocked Mac for a minute could add their own finger - and then use it
for sudo or admin dialogs. Three wrong passwords lock password checks for 30 seconds.

Only the installed RidgeKey app and `ridgekeyctl` can enroll or delete fingers: a copy of either
elsewhere, or a modified one, is refused (`untrusted-client`).

## Good enrollments

- Cover the reader fully, vary the angle slightly between touches.
- Enroll the same finger on two "slots" (e.g. right-index and right-middle) if one sometimes fails.
- Dry or cold fingers match worse - a second enrollment helps.
