---
title: Finger actions
nav_order: 7
---

# Finger actions

Give your enrolled fingers jobs: a **single, double or triple tap**, or a **hold of N seconds**,
can **lock the screen** or **run a Shortcut** - your own macros from the Shortcuts app.

Open **RidgeKey** → **Finger Actions**.

{% include screenshot.html name="finger-actions" alt="Finger Actions pane" %}

## Add actions

1. Click **Add action**. A row appears with a finger and gesture that are still free.
2. Choose the **finger**, the **gesture** (single tap, double tap, triple tap, hold - with the
   seconds for a hold) and the **action**: *Lock the screen* or *Run a Shortcut* - then pick the
   Shortcut from the list (⟳ reloads it after you create a new one in the Shortcuts app).
3. Click **Save…** and confirm it is you: touch an enrolled finger, or type your password.

A moment later **Listening** shows **waiting for your fingers**. Remove a row with its trash icon
and **Save…** again. Each finger + gesture can have one action (clashing rows are marked red).

## Gestures

| Gesture | How | When the action runs |
|---|---|---|
| single tap | a short touch | at once - unless that finger also has a double/triple tap action |
| double / triple tap | quick taps, less than 0.7 s apart | 0.7 s after the last tap (at once at the finger's highest tap count) |
| hold | keep the finger on for the seconds you chose (1-10) | when you lift |

## When RidgeKey listens

While you are logged in, the screen is unlocked and the reader is idle. The reader waits for a
finger by itself - **no polling, no CPU use**. sudo, the lock screen, admin dialogs and the
RidgeKey app always get the reader first; listening resumes afterwards.

Only **your own** enrolled fingers trigger actions; Windows Hello / BIOS fingerprints and other
people's fingers do nothing. Nothing runs while the screen is locked.

## From Terminal

```sh
ridgekeyctl actions                                              # list
ridgekeyctl action add left-index tap lock
ridgekeyctl action add left-index double shortcut "Focus"
ridgekeyctl action add right-thumb hold 2 shortcut "Work mode"
ridgekeyctl action remove left-index double
ridgekeyctl action clear
```
Changes are confirmed like enrolling (`--password` to use the password).

Problems → [Troubleshooting → Finger actions](troubleshooting.md#finger-actions).
