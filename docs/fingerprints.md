---
title: Fingerprints
nav_order: 3
---

# Fingerprints

Open **RidgeKey** → **Fingerprints**.

{% include screenshot.html name="fingerprints" alt="Fingerprints pane" %}

## Enroll a finger

1. Under **Add a fingerprint**, pick the **Finger** and click **Enroll…**
2. **Confirm it is you.**
   - If you already have an enrolled finger, the window is listening right away: **touch one of
     your enrolled fingers** - or type your login password and click **Use password**.
   - For your first finger, type your login password and click **Continue**.
3. **Touch the reader, lift, repeat** - about 8 times, until the progress bar is full. Vary the
   angle slightly between touches.

{% include screenshot.html name="confirm" alt="Confirm it is you" %}
{% include screenshot.html name="enroll" alt="Enrolling" %}

The new finger appears under **Enrolled**. Enrolling a finger again replaces its old print.
**Overview → Test my fingerprint** checks it.

## Delete a finger

Click the trash icon next to the finger under **Enrolled**, then confirm the same way (an enrolled
finger or your password).

RidgeKey only deletes fingerprints it enrolled itself. Windows Hello and BIOS fingerprints on the
same reader are never touched.

## Why the confirmation?

Without it, anyone at your unlocked Mac for a minute could add their own finger - and then use it
for sudo or admin dialogs. Three wrong passwords block password checks for 30 seconds. Only the
installed RidgeKey app (and `ridgekeyctl`) can change fingerprints.

## Tips for good enrollments

- Cover the reader fully; vary the angle a little between touches.
- If a finger sometimes fails, enroll it a second time under another finger name.
- Dry or cold fingers match worse.

## From Terminal

```sh
ridgekeyctl list
ridgekeyctl enroll right-index             # confirms with an enrolled finger, or asks your password
ridgekeyctl enroll right-index --password  # always the password
ridgekeyctl verify                         # exit code 0 = match
ridgekeyctl delete right-index
```
Finger names: `left-thumb left-index left-middle left-ring left-little right-thumb right-index
right-middle right-ring right-little`.
