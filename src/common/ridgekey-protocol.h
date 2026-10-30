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
 *               "busy" bool, "version" string (RidgeKey version)
 *     list:     "fingers" array of finger-name strings, "user" string
 *     enroll:   "finger" string
 *     verify / identify: "matched" bool, and when matched: "user" string, "finger" string
 *     purge:    "deleted" int64
 */
#ifndef RIDGEKEY_PROTOCOL_H
#define RIDGEKEY_PROTOCOL_H

#include <string.h>

#define RK_VERSION        "0.2.1"
#define RK_SERVICE_NAME   "io.github.labrouss.ridgekey"

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

/* operations */
#define RK_OP_STATUS    "status"    /* anyone */
#define RK_OP_LIST      "list"      /* own user, root: any */
#define RK_OP_ENROLL    "enroll"    /* own user, root: any */
#define RK_OP_VERIFY    "verify"    /* own user, root: any - matches only that user's prints */
#define RK_OP_IDENTIFY  "identify"  /* root only - matches every RidgeKey user's prints */
#define RK_OP_DELETE    "delete"    /* own user, root: any - only prints RidgeKey enrolled */
#define RK_OP_PURGE     "purge"     /* root only - delete every RidgeKey print, one by one */
#define RK_OP_CANCEL    "cancel"    /* the client that started the operation, or root */

/* events */
#define RK_EV_FINGER          "finger"
#define RK_EV_ENROLL_PROGRESS "enroll-progress"
#define RK_EV_RETRY           "retry"

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
