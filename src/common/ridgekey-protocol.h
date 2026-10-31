/* RidgeKey - XPC protocol shared by ridgekeyd and its clients.
 * SPDX-License-Identifier: MIT
 *
 * Transport: XPC dictionaries over the Mach service RK_SERVICE_NAME.
 *
 * Request  (client -> daemon, xpc_connection_send_message_with_reply):
 *   "op"       string  one of RK_OP_*
 *   "user"     string  optional macOS short user name (default: the caller). Only root may
 *                      act on another user.
 *   "finger"   string  finger name (rk_finger_names), for enroll / delete
 *   "timeout"  int64   optional seconds for verify / identify (default 30, max 120)
 *   "password" string  for password-set: the user's current login password (checked, then
 *                      stored in the System keychain; never logged, never returned)
 *
 * Event    (daemon -> client, plain messages while an operation runs):
 *   "event"    string  RK_EV_*
 *     finger:          "present" bool, "needed" bool
 *     enroll-progress: "stage" int64, "stages" int64
 *     retry:           "message" string   (bad scan, try again)
 *
 * Reply    (daemon -> client, the reply to the request):
 *   "ok"       bool
 *   "error"    string  RK_ERR_* when ok == false
 *   "message"  string  human readable detail
 *   op specific:
 *     status:   "device" string (name, "" if none), "driver", "device-id", "stages" int64,
 *               "busy" bool, "version" string (RidgeKey version),
 *               "console-user" string ("" at the login window), "screen-locked" bool
 *     list:     "fingers" array of finger-name strings, "user" string,
 *               "password-stored" bool
 *     enroll:   "finger" string
 *     verify / identify: "matched" bool, and when matched: "user" string, "finger" string
 *     purge:    "deleted" int64
 *     password-check: "password-stored" bool, "password-valid" bool
 *     unlock:   "matched" bool; when matched also "password" string (agent only)
 */
#ifndef RIDGEKEY_PROTOCOL_H
#define RIDGEKEY_PROTOCOL_H

#include <string.h>

#define RK_VERSION        "0.9.0"
#define RK_SERVICE_NAME   "io.github.labrouss.ridgekey"

/* The only program allowed to receive the stored password (op "unlock"). */
/* The agent is a background-only app bundle, so System Settings (Accessibility) shows it as
 * "RidgeKey Agent" with the RidgeKey icon. */
#define RK_AGENT_BUNDLE   "/usr/local/ridgekey/RidgeKeyAgent.app"
#define RK_AGENT_PATH     RK_AGENT_BUNDLE "/Contents/MacOS/ridgekey-agent"
/* Written by install.sh: the installed clients allowed to manage prints and stored passwords. */
#define RK_TRUSTED_CLIENTS "/usr/local/ridgekey/etc/trusted-clients"

/* verify / identify wait at most this long for a finger */
#define RK_TIMEOUT_MAX_SECONDS 120

/* keys */
#define RK_KEY_OP        "op"
#define RK_KEY_USER      "user"
#define RK_KEY_FINGER    "finger"
#define RK_KEY_FINGERS   "fingers"
#define RK_KEY_TIMEOUT   "timeout"
#define RK_KEY_EVENT     "event"
#define RK_KEY_OK        "ok"
#define RK_KEY_ERROR     "error"
#define RK_KEY_MESSAGE   "message"
#define RK_KEY_MATCHED   "matched"
#define RK_KEY_STAGE     "stage"
#define RK_KEY_STAGES    "stages"
#define RK_KEY_PRESENT   "present"
#define RK_KEY_NEEDED    "needed"
#define RK_KEY_DEVICE    "device"
#define RK_KEY_DRIVER    "driver"
#define RK_KEY_DEVICE_ID "device-id"
#define RK_KEY_BUSY      "busy"
#define RK_KEY_VERSION   "version"
#define RK_KEY_DELETED   "deleted"
#define RK_KEY_PASSWORD  "password"
#define RK_KEY_PASSWORD_STORED "password-stored"
#define RK_KEY_PASSWORD_VALID  "password-valid"
#define RK_KEY_CONSOLE_USER    "console-user"
#define RK_KEY_SCREEN_LOCKED   "screen-locked"
#define RK_KEY_RIGHT           "right"    /* authorize: the authorization right being requested */
#define RK_KEY_CLIENT          "client"   /* authorize: path of the app asking for it */
#define RK_KEY_CLIENT_PID      "client-pid" /* authorize: pid of that app = authd's creator-pid (0 = unknown) */
#define RK_KEY_CALLER_PID      "caller-pid" /* authorize: process that submitted the request (log only) */
#define RK_KEY_AUTH            "auth"     /* enroll/delete: "password" (+ "password") or "finger" */
#define RK_KEY_REUSED          "reused"   /* authorize reply: approved from a recent touch */

/* operations */
#define RK_OP_STATUS    "status"    /* anyone */
#define RK_OP_LIST      "list"      /* own user, root: any */
#define RK_OP_ENROLL    "enroll"    /* own user, root: any */
#define RK_OP_VERIFY    "verify"    /* own user, root: any - matches only that user's prints */
#define RK_OP_IDENTIFY  "identify"  /* root only - matches every RidgeKey user's prints */
#define RK_OP_DELETE    "delete"    /* own user, root: any - only prints RidgeKey enrolled */
#define RK_OP_PURGE     "purge"     /* root only - delete every RidgeKey print, one by one */
#define RK_OP_CANCEL    "cancel"    /* the client that started the operation, or root */
#define RK_OP_PASSWORD_SET   "password-set"    /* own user, root: any - validated first */
#define RK_OP_PASSWORD_CLEAR "password-clear"  /* own user, root: any */
#define RK_OP_PASSWORD_CHECK "password-check"  /* own user, root: any - reads it back and re-checks it */
#define RK_OP_AGENT_HELLO "agent-hello" /* the agent at startup: runs the agent checks, logs the result */
#define RK_OP_UNLOCK    "unlock"    /* the RidgeKey agent only (see peer.c), for the console user while
                                     * the screen is locked: wait for a matching finger, then reply
                                     * with "password" so the agent can type it into the lock screen */
#define RK_OP_AUTHORIZE "authorize" /* root only (the RidgeKey authorization plugin): show an on-screen
                                     * prompt, match any enrolled admin's finger, then reply with
                                     * "user" + "password" for macOS to verify (builtin:authenticate).
                                     * Needs someone at the console with the screen unlocked. */

/* events */
#define RK_EV_FINGER          "finger"
#define RK_EV_ENROLL_PROGRESS "enroll-progress"
#define RK_EV_RETRY           "retry"
#define RK_EV_CONFIRM         "confirm"     /* enroll/delete: touch an enrolled finger to confirm */
#define RK_EV_CONFIRMED       "confirmed"   /* confirmation accepted, the operation continues */

/* error codes */
#define RK_ERR_BAD_REQUEST "bad-request"
#define RK_ERR_PERMISSION  "permission"
#define RK_ERR_BUSY        "busy"
#define RK_ERR_NO_DEVICE   "no-device"
#define RK_ERR_NOT_FOUND   "not-found"
#define RK_ERR_NO_PRINTS   "no-prints"
#define RK_ERR_TIMEOUT     "timeout"
#define RK_ERR_CANCELLED   "cancelled"
#define RK_ERR_DATA_FULL   "data-full"
#define RK_ERR_STORAGE     "storage"
#define RK_ERR_FAILED      "failed"
#define RK_ERR_BAD_PASSWORD "bad-password"
#define RK_ERR_NO_PASSWORD  "no-password"         /* unlock: nothing stored */
#define RK_ERR_PASSWORD_OUTDATED "password-outdated" /* unlock: stored one no longer works */
#define RK_ERR_NOT_LOCKED   "not-locked"          /* unlock: not the locked console user */
#define RK_ERR_NOT_ADMIN    "not-admin"           /* authorize: matched user is not an administrator */
#define RK_ERR_NO_CONSOLE   "no-console"          /* authorize: nobody at the console, or screen locked */
#define RK_ERR_AUTH_REQUIRED "auth-required"      /* enroll/delete: confirm with a password or an enrolled finger */
#define RK_ERR_NO_MATCH     "no-match"            /* enroll/delete: the confirming finger is not enrolled */
#define RK_ERR_LOCKED_OUT   "locked-out"          /* too many wrong passwords - wait */
#define RK_ERR_UNTRUSTED    "untrusted-client"    /* not the installed ridgekeyctl / RidgeKey app */

/* Finger names, indexed by libfprint's FpFinger (1..10). Index 0 = unknown. */
static const char *const rk_finger_names[] = {
  "unknown",
  "left-thumb", "left-index", "left-middle", "left-ring", "left-little",
  "right-thumb", "right-index", "right-middle", "right-ring", "right-little",
};
#define RK_FINGER_FIRST 1
#define RK_FINGER_LAST  10

/* Returns 1..10, or 0 when the name is unknown. */
static inline int
rk_finger_from_name (const char *name)
{
  int i;

  if (!name)
    return 0;
  for (i = RK_FINGER_FIRST; i <= RK_FINGER_LAST; i++)
    if (strcmp (name, rk_finger_names[i]) == 0)
      return i;
  return 0;
}

static inline const char *
rk_finger_name (int finger)
{
  if (finger < RK_FINGER_FIRST || finger > RK_FINGER_LAST)
    return rk_finger_names[0];
  return rk_finger_names[finger];
}

#endif /* RIDGEKEY_PROTOCOL_H */
