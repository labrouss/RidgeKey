---
title: Finger actions
nav_order: 7
---

# Finger actions

Give your enrolled fingers jobs: **single, double or triple tap**, or **hold N seconds** →
**lock the screen** or **run a Shortcut** (your own macros from the Shortcuts app).

## Set up

**App:** Finger Actions → **Add action** → finger · gesture · action → **Save**. "Listening" turns
on a moment later.

**Terminal:**
```sh
ridgekeyctl action add left-index tap lock
ridgekeyctl action add left-index double shortcut "Focus"
ridgekeyctl action add right-thumb hold 2 shortcut "Work mode"
ridgekeyctl actions                      # list
ridgekeyctl action remove left-index double
ridgekeyctl action clear
```
Up to 20 actions. Shortcut names must match the Shortcuts app exactly (the app offers a list).

## Gestures

| Gesture | How | When it runs |
|---|---|---|
| single tap | short touch | at once - unless that finger also has a double/triple action |
| double / triple tap | quick taps, less than 0.7 s apart | 0.7 s after the last tap, or at the finger's highest tap count |
| hold N s | keep the finger on for N seconds (1-10) | when you lift |

Only the first tap is matched against your fingerprints; the extra taps only need a touch, so
they can be quick.

## When RidgeKey listens

While you are logged in, the screen is unlocked, and the reader is idle. The reader waits for a
finger in its own firmware - **no polling, no CPU use**. sudo, the lock screen, admin dialogs and
the RidgeKey app always get the reader first; listening resumes afterwards.

Only **your own** enrolled fingers trigger actions - Windows Hello/BIOS fingerprints and other
people's fingers do nothing. RidgeKey Agent runs the action as you; nothing runs while the screen
is locked.

## Problems

"Listening: off" or an action does not run → [Troubleshooting → Finger actions](troubleshooting.md#finger-actions).
