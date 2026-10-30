/* ridgekeyd - RidgeKey fingerprint daemon (launchd, XPC, libfprint)
 * SPDX-License-Identifier: MIT
 *
 * Threads:
 *   main thread   GLib main loop. ALL libfprint calls and all daemon state live here.
 *   xpc queue     receives XPC messages and queues them onto the main loop (run_on_main).
 *                 Replies and events are sent from the main thread (XPC is thread-safe).
 *
 * One sensor operation at a time. The sensor is opened for each operation and closed
 * afterwards (POWER_DOWN), which also makes sleep/wake a non-issue.
 *
 * Safety: never calls fp_device_clear_storage(). Deletes only prints recorded in the store.
 * The bundled libfprint additionally refuses BMKT_CMD_DEL_FULL_DB (patch 0001).
 */
#define G_LOG_DOMAIN "ridgekeyd"

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include <glib.h>
#include <glib-unix.h>
#include <gio/gio.h>
#include <fprint.h>
#include <xpc/xpc.h>
#include <dispatch/dispatch.h>

#include "ridgekey-protocol.h"
#include "console.h"
#include "peer.h"
#include "secret.h"
#include "store.h"

#define RK_MAX_RETRIES          3
#define RK_TIMEOUT_MATCH_DEF    30
#define RK_TIMEOUT_MATCH_MAX    RK_TIMEOUT_MAX_SECONDS
#define RK_TIMEOUT_ENROLL       180
#define RK_TIMEOUT_DELETE       60

/* ------------------------------------------------------------------------------------------ */
/* Requests                                                                                    */

typedef struct {
  xpc_connection_t conn;   /* retained */
  xpc_object_t     msg;    /* retained */
  uid_t            caller; /* effective uid of the peer */
} Request;

static Request *
request_new (xpc_connection_t conn, xpc_object_t msg)
{
  Request *r = g_new0 (Request, 1);

  r->conn = (xpc_connection_t) xpc_retain (conn);
  r->msg = xpc_retain (msg);
  r->caller = xpc_connection_get_euid (conn);
  return r;
}

static void
request_free (Request *r)
{
  if (!r)
    return;
  xpc_release (r->msg);
  xpc_release (r->conn);
  g_free (r);
}

/* A reply dictionary for this request (falls back to a plain message if no reply expected). */
static xpc_object_t
reply_new (Request *r)
{
  xpc_object_t reply = xpc_dictionary_create_reply (r->msg);

  if (!reply)
    reply = xpc_dictionary_create (NULL, NULL, 0);
  return reply;
}

static void
reply_send (Request *r, xpc_object_t reply)
{
  xpc_connection_send_message (r->conn, reply);
  xpc_release (reply);
}

static void G_GNUC_PRINTF (3, 4)
reply_error (Request *r, const char *code, const char *fmt, ...)
{
  xpc_object_t reply = reply_new (r);
  va_list ap;
  g_autofree char *msg = NULL;

  va_start (ap, fmt);
  msg = g_strdup_vprintf (fmt, ap);
  va_end (ap);
  xpc_dictionary_set_bool (reply, RK_KEY_OK, false);
  xpc_dictionary_set_string (reply, RK_KEY_ERROR, code);
  xpc_dictionary_set_string (reply, RK_KEY_MESSAGE, msg);
  g_message ("uid %u: %s failed: %s (%s)", (unsigned) r->caller,
             xpc_dictionary_get_string (r->msg, RK_KEY_OP) ?: "?", msg, code);
  reply_send (r, reply);
}

/* ------------------------------------------------------------------------------------------ */
/* Daemon state                                                                                */

typedef enum { OP_ENROLL, OP_VERIFY, OP_IDENTIFY, OP_DELETE, OP_PURGE, OP_UNLOCK } OpKind;

static const char *const op_names[] = { "enroll", "verify", "identify", "delete", "purge", "unlock" };

typedef struct {
  OpKind        kind;
  Request      *req;
  xpc_object_t  reply;          /* filled in as the operation progresses */
  FpDevice     *dev;            /* borrowed from the context */
  GCancellable *cancel;
  guint         timeout_id;
  gboolean      timed_out;
  gboolean      opened;
  gulong        finger_handler;
  gint          retries;

  uid_t         uid;            /* target user */
  char         *username;
  FpFinger      finger;
  FpPrint      *old_print;      /* enroll: replaced print; delete: print to delete */
  GPtrArray    *gallery;        /* verify / identify / purge */
  guint         purge_index;
  gint64        purge_deleted;

  GError       *error;          /* final error, if any */
  const char   *err_code;       /* overrides the code derived from error */
} Op;

static FpContext *ctx;
static GMainLoop *loop;
static Op        *current;
static gboolean   quitting;

static void op_finish (Op *op, GError *error);
static void op_run_identify (Op *op);
static void purge_next (Op *op);

static void
send_event (Op *op, xpc_object_t ev)
{
  xpc_connection_send_message (op->req->conn, ev);
  xpc_release (ev);
}

static const char *
error_code (Op *op, const GError *e)
{
  if (op->err_code)
    return op->err_code;
  if (op->timed_out)
    return RK_ERR_TIMEOUT;
  if (g_error_matches (e, G_IO_ERROR, G_IO_ERROR_CANCELLED))
    return RK_ERR_CANCELLED;
  if (e->domain == FP_DEVICE_ERROR)
    switch (e->code)
      {
      case FP_DEVICE_ERROR_DATA_FULL:      return RK_ERR_DATA_FULL;
      case FP_DEVICE_ERROR_DATA_NOT_FOUND: return RK_ERR_NOT_FOUND;
      case FP_DEVICE_ERROR_BUSY:           return RK_ERR_BUSY;
      default:                             return RK_ERR_FAILED;
      }
  if (e->domain == G_FILE_ERROR)
    return RK_ERR_STORAGE;
  return RK_ERR_FAILED;
}

static void
op_free (Op *op)
{
  if (op->timeout_id)
    g_source_remove (op->timeout_id);
  if (op->finger_handler)
    g_signal_handler_disconnect (op->dev, op->finger_handler);
  g_clear_object (&op->cancel);
  g_clear_object (&op->old_print);
  g_clear_pointer (&op->gallery, g_ptr_array_unref);
  g_clear_error (&op->error);
  if (op->reply)
    xpc_release (op->reply);
  g_free (op->username);
  request_free (op->req);
  g_free (op);
}

static void
op_complete (Op *op)
{
  if (op->error)
    {
      const char *code = error_code (op, op->error);
      const char *msg = op->timed_out ? "timed out waiting for the sensor" : op->error->message;

      if (g_error_matches (op->error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_DATA_FULL))
        msg = "the sensor's storage is full - remove prints in Windows Hello / BIOS or old RidgeKey prints";
      xpc_dictionary_set_bool (op->reply, RK_KEY_OK, false);
      xpc_dictionary_set_string (op->reply, RK_KEY_ERROR, code);
      xpc_dictionary_set_string (op->reply, RK_KEY_MESSAGE, msg);
      g_message ("%s for %s: %s (%s)", op_names[op->kind], op->username ?: "-", msg, code);
    }
  else
    {
      xpc_dictionary_set_bool (op->reply, RK_KEY_OK, true);
      g_message ("%s for %s: ok", op_names[op->kind], op->username ?: "-");
    }

  xpc_connection_send_message (op->req->conn, op->reply);
  current = NULL;
  op_free (op);

  if (quitting)
    g_main_loop_quit (loop);
}

static void
on_closed (GObject *src, GAsyncResult *res, gpointer data)
{
  Op *op = data;
  g_autoptr(GError) err = NULL;

  if (!fp_device_close_finish (FP_DEVICE (src), res, &err))
    g_warning ("closing the sensor failed: %s", err->message);
  op_complete (op);
}

/* Takes ownership of error (may be NULL). */
static void
op_finish (Op *op, GError *error)
{
  if (error && !op->error)
    op->error = error;
  else if (error)
    g_error_free (error);

  if (op->timeout_id)
    {
      g_source_remove (op->timeout_id);
      op->timeout_id = 0;
    }
  if (op->opened)
    {
      op->opened = FALSE;
      fp_device_close (op->dev, NULL, on_closed, op);
    }
  else
    {
      op_complete (op);
    }
}

static gboolean
on_timeout (gpointer data)
{
  Op *op = data;

  op->timeout_id = 0;
  op->timed_out = TRUE;
  g_cancellable_cancel (op->cancel);
  return G_SOURCE_REMOVE;
}

static void
on_finger_status (GObject *obj, GParamSpec *pspec, gpointer data)
{
  Op *op = data;
  FpFingerStatusFlags st = fp_device_get_finger_status (FP_DEVICE (obj));
  xpc_object_t ev = xpc_dictionary_create (NULL, NULL, 0);

  xpc_dictionary_set_string (ev, RK_KEY_EVENT, RK_EV_FINGER);
  xpc_dictionary_set_bool (ev, RK_KEY_PRESENT, (st & FP_FINGER_STATUS_PRESENT) != 0);
  xpc_dictionary_set_bool (ev, RK_KEY_NEEDED, (st & FP_FINGER_STATUS_NEEDED) != 0);
  send_event (op, ev);
}

static void
send_retry (Op *op, const GError *e)
{
  xpc_object_t ev = xpc_dictionary_create (NULL, NULL, 0);

  xpc_dictionary_set_string (ev, RK_KEY_EVENT, RK_EV_RETRY);
  xpc_dictionary_set_string (ev, RK_KEY_MESSAGE, e->message);
  send_event (op, ev);
}

/* ------------------------------------------------------------------------------------------ */
/* enroll                                                                                      */

static void
on_enroll_progress (FpDevice *dev, gint completed, FpPrint *print, gpointer data, GError *error)
{
  Op *op = data;
  xpc_object_t ev;

  if (error)
    {
      send_retry (op, error);
      return;
    }
  ev = xpc_dictionary_create (NULL, NULL, 0);
  xpc_dictionary_set_string (ev, RK_KEY_EVENT, RK_EV_ENROLL_PROGRESS);
  xpc_dictionary_set_int64 (ev, RK_KEY_STAGE, completed);
  xpc_dictionary_set_int64 (ev, RK_KEY_STAGES, fp_device_get_nr_enroll_stages (dev));
  send_event (op, ev);
}

static void
on_replaced_deleted (GObject *src, GAsyncResult *res, gpointer data)
{
  Op *op = data;
  g_autoptr(GError) err = NULL;

  /* The new print is saved already; a failure here only leaves an orphan on the sensor. */
  if (!fp_device_delete_print_finish (FP_DEVICE (src), res, &err))
    g_warning ("could not delete the replaced print from the sensor: %s", err->message);
  op_finish (op, NULL);
}

static void
on_unsaved_deleted (GObject *src, GAsyncResult *res, gpointer data)
{
  Op *op = data;
  g_autoptr(GError) err = NULL;

  if (!fp_device_delete_print_finish (FP_DEVICE (src), res, &err))
    g_warning ("could not remove the unsaved print from the sensor: %s", err->message);
  op_finish (op, NULL);   /* op->error already holds the storage error */
}

static void
on_enrolled (GObject *src, GAsyncResult *res, gpointer data)
{
  Op *op = data;
  GError *err = NULL;
  g_autoptr(FpPrint) print = fp_device_enroll_finish (FP_DEVICE (src), res, &err);

  if (!print)
    {
      op_finish (op, err);
      return;
    }

  xpc_dictionary_set_string (op->reply, RK_KEY_FINGER, rk_finger_name (op->finger));

  if (!store_save (op->uid, print, &err))
    {
      /* Don't leave a print on the sensor that RidgeKey has no record of. */
      op->error = err;
      fp_device_delete_print (op->dev, print, NULL, on_unsaved_deleted, op);
      return;
    }

  if (op->old_print)
    fp_device_delete_print (op->dev, op->old_print, NULL, on_replaced_deleted, op);
  else
    op_finish (op, NULL);
}

static void
op_run_enroll (Op *op)
{
  FpPrint *tmpl = fp_print_new (op->dev);   /* floating; fp_device_enroll sinks it */

  fp_print_set_finger (tmpl, op->finger);
  fp_print_set_username (tmpl, op->username);
  fp_print_set_description (tmpl, "RidgeKey");
  fp_device_enroll (op->dev, tmpl, op->cancel, on_enroll_progress, op, NULL, on_enrolled, op);
}

/* ------------------------------------------------------------------------------------------ */
/* verify / identify                                                                           */

/* Unlock: after a match for the locked console user, hand the stored password to the agent. */
static void
release_password (Op *op)
{
  ConsoleState cs = console_get_state ();
  GError *err = NULL;
  char *pw;

  if (cs.uid != op->uid || !cs.locked)
    {
      op->err_code = RK_ERR_NOT_LOCKED;
      op_finish (op, g_error_new_literal (G_IO_ERROR, G_IO_ERROR_FAILED,
                                          "the screen is no longer locked for this user"));
      return;
    }
  pw = secret_load (op->username, &err);
  if (!pw)
    {
      op->err_code = g_error_matches (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND) ? RK_ERR_NO_PASSWORD : RK_ERR_STORAGE;
      op_finish (op, err);
      return;
    }
  if (!secret_validate (op->username, pw, &err))
    {
      secret_free (pw);
      op->err_code = RK_ERR_PASSWORD_OUTDATED;
      op_finish (op, err);
      return;
    }
  xpc_dictionary_set_string (op->reply, RK_KEY_PASSWORD, pw);
  secret_free (pw);
  g_message ("unlock: fingerprint matched for %s - password released to the agent", op->username);
  op_finish (op, NULL);
}

static void
on_identified (GObject *src, GAsyncResult *res, gpointer data)
{
  Op *op = data;
  GError *err = NULL;
  g_autoptr(FpPrint) match = NULL;
  g_autoptr(FpPrint) scanned = NULL;

  if (!fp_device_identify_finish (FP_DEVICE (src), res, &match, &scanned, &err))
    {
      if (err->domain == FP_DEVICE_RETRY && op->retries < RK_MAX_RETRIES &&
          !g_cancellable_is_cancelled (op->cancel))
        {
          op->retries++;
          send_retry (op, err);
          g_error_free (err);
          op_run_identify (op);
          return;
        }
      op_finish (op, err);
      return;
    }

  /* A print that is not in the gallery (Windows Hello, BIOS, another tool) gives match == NULL. */
  xpc_dictionary_set_bool (op->reply, RK_KEY_MATCHED, match != NULL);
  if (match)
    {
      uid_t uid = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (match), RK_PRINT_UID_KEY));
      struct passwd *pw = getpwuid (uid);

      xpc_dictionary_set_string (op->reply, RK_KEY_USER, pw ? pw->pw_name : "");
      xpc_dictionary_set_string (op->reply, RK_KEY_FINGER, rk_finger_name (fp_print_get_finger (match)));
      g_message ("%s: matched %s / %s", op_names[op->kind], pw ? pw->pw_name : "?",
                 rk_finger_name (fp_print_get_finger (match)));
      if (op->kind == OP_UNLOCK)
        {
          release_password (op);
          return;
        }
    }
  op_finish (op, NULL);
}

static void
op_run_identify (Op *op)
{
  fp_device_identify (op->dev, op->gallery, op->cancel, NULL, NULL, NULL, on_identified, op);
}

/* ------------------------------------------------------------------------------------------ */
/* delete / purge                                                                              */

static void
on_deleted (GObject *src, GAsyncResult *res, gpointer data)
{
  Op *op = data;
  GError *err = NULL;

  if (!fp_device_delete_print_finish (FP_DEVICE (src), res, &err))
    {
      op_finish (op, err);
      return;
    }
  if (!store_remove (op->uid, op->finger, &err))
    {
      op_finish (op, err);
      return;
    }
  xpc_dictionary_set_string (op->reply, RK_KEY_FINGER, rk_finger_name (op->finger));
  op_finish (op, NULL);
}

static void
on_purge_deleted (GObject *src, GAsyncResult *res, gpointer data)
{
  Op *op = data;
  GError *err = NULL;
  FpPrint *p = g_ptr_array_index (op->gallery, op->purge_index);
  uid_t uid = GPOINTER_TO_UINT (g_object_get_data (G_OBJECT (p), RK_PRINT_UID_KEY));

  if (!fp_device_delete_print_finish (FP_DEVICE (src), res, &err))
    {
      xpc_dictionary_set_int64 (op->reply, RK_KEY_DELETED, op->purge_deleted);
      op_finish (op, err);
      return;
    }
  if (!store_remove (uid, fp_print_get_finger (p), &err))
    {
      xpc_dictionary_set_int64 (op->reply, RK_KEY_DELETED, op->purge_deleted);
      op_finish (op, err);
      return;
    }
  op->purge_deleted++;
  op->purge_index++;
  purge_next (op);
}

static void
purge_next (Op *op)
{
  if (op->purge_index >= op->gallery->len)
    {
      xpc_dictionary_set_int64 (op->reply, RK_KEY_DELETED, op->purge_deleted);
      op_finish (op, NULL);
      return;
    }
  /* One DEL_USER_FP per recorded print - never the whole database. */
  fp_device_delete_print (op->dev, g_ptr_array_index (op->gallery, op->purge_index),
                          op->cancel, on_purge_deleted, op);
}

/* ------------------------------------------------------------------------------------------ */
/* operation start                                                                             */

static void
on_opened (GObject *src, GAsyncResult *res, gpointer data)
{
  Op *op = data;
  GError *err = NULL;

  if (!fp_device_open_finish (FP_DEVICE (src), res, &err))
    {
      op_finish (op, err);
      return;
    }
  op->opened = TRUE;

  switch (op->kind)
    {
    case OP_ENROLL:   op_run_enroll (op); break;
    case OP_VERIFY:
    case OP_IDENTIFY:
    case OP_UNLOCK:   op_run_identify (op); break;
    case OP_DELETE:   fp_device_delete_print (op->dev, op->old_print, op->cancel, on_deleted, op); break;
    case OP_PURGE:    purge_next (op); break;
    }
}

static FpDevice *
pick_device (void)
{
  GPtrArray *devs = fp_context_get_devices (ctx);   /* transfer none */

  return (devs && devs->len > 0) ? g_ptr_array_index (devs, 0) : NULL;
}

/* Resolve the target user of a request; replies and returns FALSE on error. */
static gboolean
resolve_user (Request *r, uid_t *uid, char **name)
{
  const char *u = xpc_dictionary_get_string (r->msg, RK_KEY_USER);
  struct passwd *pw = u ? getpwnam (u) : getpwuid (r->caller);

  if (!pw)
    {
      reply_error (r, RK_ERR_BAD_REQUEST, "unknown user '%s'", u ? u : "(caller)");
      return FALSE;
    }
  if (pw->pw_uid != r->caller && r->caller != 0)
    {
      reply_error (r, RK_ERR_PERMISSION, "only root can act on another user's prints");
      return FALSE;
    }
  *uid = pw->pw_uid;
  *name = g_strdup (pw->pw_name);
  return TRUE;
}

static void
start_op (Request *r, OpKind kind)
{
  Op *op;
  FpDevice *dev;
  guint timeout = RK_TIMEOUT_DELETE;
  int finger = 0;

  if (current)
    {
      reply_error (r, RK_ERR_BUSY, "the sensor is busy with another request");
      request_free (r);
      return;
    }

  op = g_new0 (Op, 1);
  op->kind = kind;
  op->req = r;

  if (kind == OP_IDENTIFY || kind == OP_PURGE)
    {
      if (r->caller != 0)
        {
          reply_error (r, RK_ERR_PERMISSION, "%s is only available to root", op_names[kind]);
          op_free (op);
          return;
        }
      op->username = g_strdup ("(all users)");
    }
  else if (!resolve_user (r, &op->uid, &op->username))
    {
      op_free (op);
      return;
    }

  if (kind == OP_ENROLL || kind == OP_DELETE)
    {
      finger = rk_finger_from_name (xpc_dictionary_get_string (r->msg, RK_KEY_FINGER));
      if (!finger)
        {
          reply_error (r, RK_ERR_BAD_REQUEST, "missing or unknown finger name");
          op_free (op);
          return;
        }
      op->finger = (FpFinger) finger;
    }

  switch (kind)
    {
    case OP_ENROLL:
      op->old_print = store_load (op->uid, op->finger, NULL);   /* NULL if none */
      timeout = RK_TIMEOUT_ENROLL;
      break;

    case OP_DELETE:
      {
        g_autoptr(GError) err = NULL;

        op->old_print = store_load (op->uid, op->finger, &err);
        if (!op->old_print)
          {
            reply_error (r, RK_ERR_NOT_FOUND, "no RidgeKey print for %s / %s",
                         op->username, rk_finger_name (finger));
            op_free (op);
            return;
          }
        break;
      }

    case OP_VERIFY:
    case OP_IDENTIFY:
    case OP_UNLOCK:
      {
        gint64 t = xpc_dictionary_get_int64 (r->msg, RK_KEY_TIMEOUT);

        op->gallery = kind == OP_IDENTIFY ? store_load_all () : store_load_user (op->uid);
        if (op->gallery->len == 0)
          {
            reply_error (r, RK_ERR_NO_PRINTS, "no enrolled RidgeKey prints for %s", op->username);
            op_free (op);
            return;
          }
        timeout = (t > 0) ? (guint) MIN (t, RK_TIMEOUT_MATCH_MAX) : RK_TIMEOUT_MATCH_DEF;
        break;
      }

    case OP_PURGE:
      op->gallery = store_load_all ();
      if (op->gallery->len == 0)
        {
          xpc_object_t reply = reply_new (r);

          xpc_dictionary_set_bool (reply, RK_KEY_OK, true);
          xpc_dictionary_set_int64 (reply, RK_KEY_DELETED, 0);
          reply_send (r, reply);
          op_free (op);
          return;
        }
      break;
    }

  dev = pick_device ();
  if (!dev)
    {
      reply_error (r, RK_ERR_NO_DEVICE, "no supported fingerprint sensor found");
      op_free (op);
      return;
    }

  g_message ("uid %u: %s for %s", (unsigned) r->caller, op_names[kind], op->username);
  op->dev = dev;
  op->reply = reply_new (r);
  op->cancel = g_cancellable_new ();
  op->timeout_id = g_timeout_add_seconds (timeout, on_timeout, op);
  op->finger_handler = g_signal_connect (dev, "notify::finger-status",
                                         G_CALLBACK (on_finger_status), op);
  current = op;
  fp_device_open (dev, op->cancel, on_opened, op);
}

/* ------------------------------------------------------------------------------------------ */
/* immediate requests                                                                          */

static void
do_status (Request *r)
{
  xpc_object_t reply = reply_new (r);
  FpDevice *dev = pick_device ();

  xpc_dictionary_set_bool (reply, RK_KEY_OK, true);
  xpc_dictionary_set_string (reply, RK_KEY_VERSION, RK_VERSION);
  xpc_dictionary_set_bool (reply, RK_KEY_BUSY, current != NULL);
  xpc_dictionary_set_string (reply, RK_KEY_DEVICE, dev ? fp_device_get_name (dev) : "");
  if (dev)
    {
      xpc_dictionary_set_string (reply, RK_KEY_DRIVER, fp_device_get_driver (dev));
      xpc_dictionary_set_string (reply, RK_KEY_DEVICE_ID, fp_device_get_device_id (dev));
      xpc_dictionary_set_int64 (reply, RK_KEY_STAGES, fp_device_get_nr_enroll_stages (dev));
    }
  {
    ConsoleState cs = console_get_state ();
    struct passwd *pw = cs.uid != RK_NO_CONSOLE_USER ? getpwuid (cs.uid) : NULL;

    xpc_dictionary_set_string (reply, RK_KEY_CONSOLE_USER, pw ? pw->pw_name : "");
    xpc_dictionary_set_bool (reply, RK_KEY_SCREEN_LOCKED, cs.locked);
  }
  reply_send (r, reply);
}

static void
do_list (Request *r)
{
  uid_t uid;
  g_autofree char *name = NULL;
  g_autoptr(GArray) fingers = NULL;
  xpc_object_t reply, arr;
  guint i;

  if (!resolve_user (r, &uid, &name))
    return;
  fingers = store_list_fingers (uid);
  reply = reply_new (r);
  arr = xpc_array_create (NULL, 0);
  for (i = 0; i < fingers->len; i++)
    xpc_array_set_string (arr, XPC_ARRAY_APPEND, rk_finger_name ((int) g_array_index (fingers, guint, i)));
  xpc_dictionary_set_bool (reply, RK_KEY_OK, true);
  xpc_dictionary_set_string (reply, RK_KEY_USER, name);
  xpc_dictionary_set_value (reply, RK_KEY_FINGERS, arr);
  xpc_dictionary_set_bool (reply, RK_KEY_PASSWORD_STORED, secret_exists (name));
  xpc_release (arr);
  reply_send (r, reply);
}

static void
do_password_set (Request *r)
{
  uid_t uid;
  g_autofree char *name = NULL;
  g_autoptr(GError) err = NULL;
  const char *password = xpc_dictionary_get_string (r->msg, RK_KEY_PASSWORD);
  xpc_object_t reply;

  if (!resolve_user (r, &uid, &name))
    return;
  if (!password || !*password)
    {
      reply_error (r, RK_ERR_BAD_REQUEST, "missing password");
      return;
    }
  if (!secret_validate (name, password, &err))
    {
      reply_error (r, g_error_matches (err, G_IO_ERROR, G_IO_ERROR_PERMISSION_DENIED)
                      ? RK_ERR_BAD_PASSWORD : RK_ERR_FAILED, "%s", err->message);
      return;
    }
  if (!secret_store (name, password, &err))
    {
      reply_error (r, RK_ERR_STORAGE, "%s", err->message);
      return;
    }
  /* Read it back: proves the keychain ACL lets ridgekeyd read the item without a prompt. */
  {
    char *back = secret_load (name, &err);
    gboolean same = back && strcmp (back, password) == 0;

    secret_free (back);
    if (!same)
      {
        secret_clear (name, NULL);
        reply_error (r, RK_ERR_STORAGE, "stored password could not be read back: %s",
                     err ? err->message : "content differs");
        return;
      }
  }
  g_message ("uid %u: stored the login password of %s", (unsigned) r->caller, name);
  reply = reply_new (r);
  xpc_dictionary_set_bool (reply, RK_KEY_OK, true);
  xpc_dictionary_set_string (reply, RK_KEY_USER, name);
  reply_send (r, reply);
}

static void
do_agent_hello (Request *r)
{
  g_autoptr(GError) err = NULL;
  uid_t euid = 0;
  pid_t pid = 0;
  xpc_object_t reply;

  if (!peer_is_agent (r->conn, &euid, &pid, &err))
    {
      g_warning ("agent check FAILED for pid %d (uid %u): %s", (int) pid, (unsigned) euid, err->message);
      reply_error (r, RK_ERR_PERMISSION, "%s", err->message);
      return;
    }
  g_message ("agent verified: pid %d, uid %u", (int) pid, (unsigned) euid);
  reply = reply_new (r);
  xpc_dictionary_set_bool (reply, RK_KEY_OK, true);
  reply_send (r, reply);
}

/* Only the verified agent, for the console user, while the screen is locked. */
static void
do_unlock (Request *r)
{
  g_autoptr(GError) err = NULL;
  uid_t euid = 0;
  pid_t pid = 0;
  ConsoleState cs;
  struct passwd *pw;

  if (!peer_is_agent (r->conn, &euid, &pid, &err))
    {
      g_warning ("unlock refused for pid %d: %s", (int) pid, err->message);
      reply_error (r, RK_ERR_PERMISSION, "%s", err->message);
      request_free (r);
      return;
    }
  cs = console_get_state ();
  if (cs.uid != euid || !cs.locked)
    {
      reply_error (r, RK_ERR_NOT_LOCKED, "the screen is not locked for uid %u", (unsigned) euid);
      request_free (r);
      return;
    }
  pw = getpwuid (euid);
  if (!pw || !secret_exists (pw->pw_name))
    {
      reply_error (r, RK_ERR_NO_PASSWORD, "no stored password - run: ridgekeyctl password set");
      request_free (r);
      return;
    }
  start_op (r, OP_UNLOCK);
}

static void
do_password_check (Request *r)
{
  uid_t uid;
  g_autofree char *name = NULL;
  g_autoptr(GError) err = NULL;
  char *pw;
  gboolean valid = FALSE;
  xpc_object_t reply;

  if (!resolve_user (r, &uid, &name))
    return;
  pw = secret_load (name, &err);
  if (!pw && !g_error_matches (err, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    {
      reply_error (r, RK_ERR_STORAGE, "%s", err->message);
      return;
    }
  if (pw)
    {
      g_autoptr(GError) verr = NULL;

      valid = secret_validate (name, pw, &verr);
      if (!valid)
        g_message ("stored password of %s no longer valid: %s", name, verr->message);
      secret_free (pw);
    }
  reply = reply_new (r);
  xpc_dictionary_set_bool (reply, RK_KEY_OK, true);
  xpc_dictionary_set_string (reply, RK_KEY_USER, name);
  xpc_dictionary_set_bool (reply, RK_KEY_PASSWORD_STORED, pw != NULL || valid);
  xpc_dictionary_set_bool (reply, RK_KEY_PASSWORD_VALID, valid);
  reply_send (r, reply);
}

static void
do_password_clear (Request *r)
{
  uid_t uid;
  g_autofree char *name = NULL;
  g_autoptr(GError) err = NULL;
  xpc_object_t reply;

  if (!resolve_user (r, &uid, &name))
    return;
  if (!secret_clear (name, &err))
    {
      reply_error (r, RK_ERR_STORAGE, "%s", err->message);
      return;
    }
  g_message ("uid %u: removed the stored password of %s", (unsigned) r->caller, name);
  reply = reply_new (r);
  xpc_dictionary_set_bool (reply, RK_KEY_OK, true);
  xpc_dictionary_set_string (reply, RK_KEY_USER, name);
  reply_send (r, reply);
}

static void
do_cancel (Request *r)
{
  xpc_object_t reply;

  if (!current || (current->req->conn != r->conn && r->caller != 0))
    {
      reply_error (r, RK_ERR_NOT_FOUND, "no operation of yours is running");
      return;
    }
  g_cancellable_cancel (current->cancel);
  reply = reply_new (r);
  xpc_dictionary_set_bool (reply, RK_KEY_OK, true);
  reply_send (r, reply);
}

/* Main thread: dispatch one request. Takes ownership of r. */
static gboolean
handle_request (gpointer data)
{
  Request *r = data;
  const char *op = xpc_dictionary_get_string (r->msg, RK_KEY_OP);

  if (!op)
    reply_error (r, RK_ERR_BAD_REQUEST, "missing \"op\"");
  else if (quitting)
    reply_error (r, RK_ERR_BUSY, "ridgekeyd is shutting down");
  else if (strcmp (op, RK_OP_STATUS) == 0)
    do_status (r);
  else if (strcmp (op, RK_OP_LIST) == 0)
    do_list (r);
  else if (strcmp (op, RK_OP_CANCEL) == 0)
    do_cancel (r);
  else if (strcmp (op, RK_OP_PASSWORD_SET) == 0)
    do_password_set (r);
  else if (strcmp (op, RK_OP_PASSWORD_CLEAR) == 0)
    do_password_clear (r);
  else if (strcmp (op, RK_OP_PASSWORD_CHECK) == 0)
    do_password_check (r);
  else if (strcmp (op, RK_OP_AGENT_HELLO) == 0)
    do_agent_hello (r);
  else if (strcmp (op, RK_OP_ENROLL) == 0)
    return (start_op (r, OP_ENROLL), G_SOURCE_REMOVE);
  else if (strcmp (op, RK_OP_VERIFY) == 0)
    return (start_op (r, OP_VERIFY), G_SOURCE_REMOVE);
  else if (strcmp (op, RK_OP_IDENTIFY) == 0)
    return (start_op (r, OP_IDENTIFY), G_SOURCE_REMOVE);
  else if (strcmp (op, RK_OP_DELETE) == 0)
    return (start_op (r, OP_DELETE), G_SOURCE_REMOVE);
  else if (strcmp (op, RK_OP_PURGE) == 0)
    return (start_op (r, OP_PURGE), G_SOURCE_REMOVE);
  else if (strcmp (op, RK_OP_UNLOCK) == 0)
    return (do_unlock (r), G_SOURCE_REMOVE);
  else
    reply_error (r, RK_ERR_BAD_REQUEST, "unknown op \"%s\"", op);

  request_free (r);
  return G_SOURCE_REMOVE;
}

/* Main thread: a client went away - cancel its running operation. Releases conn. */
static gboolean
handle_peer_gone (gpointer data)
{
  xpc_connection_t conn = data;

  if (current && current->req->conn == conn)
    {
      g_message ("client disconnected - cancelling %s", op_names[current->kind]);
      g_cancellable_cancel (current->cancel);
    }
  xpc_release (conn);
  return G_SOURCE_REMOVE;
}

/* ------------------------------------------------------------------------------------------ */
/* XPC listener (runs on the xpc queue)                                                        */

/* Always queue onto the main loop. (g_main_context_invoke() could run the function on the
 * calling thread when the main context is not owned yet, e.g. before g_main_loop_run.) */
static void
run_on_main (GSourceFunc fn, gpointer data)
{
  GSource *src = g_idle_source_new ();

  g_source_set_priority (src, G_PRIORITY_DEFAULT);
  g_source_set_callback (src, fn, data, NULL);
  g_source_attach (src, NULL);
  g_source_unref (src);
}

static void
setup_peer (xpc_connection_t peer, dispatch_queue_t queue)
{
  xpc_retain (peer);   /* released in handle_peer_gone */
  xpc_connection_set_target_queue (peer, queue);
  xpc_connection_set_event_handler (peer, ^(xpc_object_t ev) {
    xpc_type_t t = xpc_get_type (ev);

    if (t == XPC_TYPE_DICTIONARY)
      run_on_main (handle_request, request_new (peer, ev));
    else if (ev == XPC_ERROR_CONNECTION_INVALID)
      run_on_main (handle_peer_gone, peer);
    /* XPC_ERROR_CONNECTION_INTERRUPTED does not occur for peer connections */
  });
  xpc_connection_resume (peer);
}

/* ------------------------------------------------------------------------------------------ */

static void
on_console_changed (const ConsoleState *cs, gpointer data)
{
  struct passwd *pw = cs->uid != RK_NO_CONSOLE_USER ? getpwuid (cs->uid) : NULL;

  if (cs->uid == RK_NO_CONSOLE_USER)
    g_message ("console: login window");
  else
    g_message ("console: %s (uid %u), screen %s", pw ? pw->pw_name : "?", (unsigned) cs->uid,
               cs->locked ? "LOCKED" : "unlocked");
}

static gboolean
on_signal (gpointer data)
{
  g_message ("signal received, shutting down");
  quitting = TRUE;
  if (current)
    g_cancellable_cancel (current->cancel);   /* quits after the sensor is closed */
  else
    g_main_loop_quit (loop);
  return G_SOURCE_CONTINUE;
}

/* "2026-10-30 22:29:25.983 [level] message" - one line per entry, for /Library/Logs. */
static void
log_handler (const gchar *domain, GLogLevelFlags level, const gchar *message, gpointer data)
{
  g_autoptr(GDateTime) now = g_date_time_new_now_local ();
  g_autofree gchar *ts = g_date_time_format (now, "%Y-%m-%d %H:%M:%S");
  const char *lvl = "info";

  if (level & G_LOG_LEVEL_DEBUG)
    {
      if (!g_getenv ("RIDGEKEYD_DEBUG"))
        return;
      lvl = "debug";
    }
  else if (level & (G_LOG_LEVEL_ERROR | G_LOG_LEVEL_CRITICAL))
    lvl = "error";
  else if (level & G_LOG_LEVEL_WARNING)
    lvl = "warning";
  fprintf (stderr, "%s.%03d [%s] %s%s%s\n", ts, g_date_time_get_microsecond (now) / 1000, lvl,
           domain && strcmp (domain, G_LOG_DOMAIN) ? domain : "",
           domain && strcmp (domain, G_LOG_DOMAIN) ? ": " : "", message);
  fflush (stderr);
}

static int
run_probe (void)
{
  GPtrArray *devs = fp_context_get_devices (ctx);
  guint i;

  if (!devs || devs->len == 0)
    {
      printf ("no supported fingerprint sensor found\n");
      return 2;
    }
  for (i = 0; i < devs->len; i++)
    {
      FpDevice *d = g_ptr_array_index (devs, i);

      printf ("%s  driver=%s  id=%s  enroll-stages=%d\n", fp_device_get_name (d),
              fp_device_get_driver (d), fp_device_get_device_id (d),
              fp_device_get_nr_enroll_stages (d));
    }
  return 0;
}

int
main (int argc, char **argv)
{
  gboolean probe = FALSE, version = FALSE;
  g_autofree char *store_dir = NULL;
  g_autoptr(GError) err = NULL;
  GOptionEntry entries[] = {
    { "probe", 0, 0, G_OPTION_ARG_NONE, &probe, "List supported sensors and exit", NULL },
    { "store", 0, 0, G_OPTION_ARG_FILENAME, &store_dir, "Print record directory (default " RK_STORE_ROOT_DEFAULT ")", "DIR" },
    { "version", 0, 0, G_OPTION_ARG_NONE, &version, "Print version and exit", NULL },
    { NULL }
  };
  g_autoptr(GOptionContext) oc = g_option_context_new ("- RidgeKey fingerprint daemon");
  dispatch_queue_t queue;
  xpc_connection_t listener;

  g_option_context_add_main_entries (oc, entries, NULL);
  if (!g_option_context_parse (oc, &argc, &argv, &err))
    {
      g_printerr ("%s\n", err->message);
      return 2;
    }
  if (version)
    {
      printf ("ridgekeyd %s\n", RK_VERSION);
      return 0;
    }

  if (!probe)
    {
      g_log_set_handler (G_LOG_DOMAIN, G_LOG_LEVEL_MASK | G_LOG_FLAG_FATAL | G_LOG_FLAG_RECURSION, log_handler, NULL);
      g_log_set_default_handler (log_handler, NULL);
    }

  ctx = fp_context_new ();
  if (probe)
    {
      int rc = run_probe ();

      g_object_unref (ctx);
      return rc;
    }

  if (geteuid () != 0)
    g_warning ("not running as root - the print record must be root-only; run via launchd");

  if (!store_init (store_dir ? store_dir : RK_STORE_ROOT_DEFAULT, &err))
    {
      g_critical ("print store: %s", err->message);
      return 1;
    }

  loop = g_main_loop_new (NULL, FALSE);
  g_unix_signal_add (SIGTERM, on_signal, NULL);
  g_unix_signal_add (SIGINT, on_signal, NULL);

  queue = dispatch_queue_create ("io.github.labrouss.ridgekey.xpc", DISPATCH_QUEUE_SERIAL);
  listener = xpc_connection_create_mach_service (RK_SERVICE_NAME, queue,
                                                 XPC_CONNECTION_MACH_SERVICE_LISTENER);
  xpc_connection_set_event_handler (listener, ^(xpc_object_t peer) {
    if (xpc_get_type (peer) == XPC_TYPE_CONNECTION)
      setup_peer ((xpc_connection_t) peer, queue);
    else if (xpc_get_type (peer) == XPC_TYPE_ERROR && peer != XPC_ERROR_CONNECTION_INVALID)
      g_warning ("listener error: %s", xpc_dictionary_get_string (peer, XPC_ERROR_KEY_DESCRIPTION));
    /* CONNECTION_INVALID is the expected result of cancelling the listener at shutdown */
  });
  xpc_connection_resume (listener);
  console_watch (on_console_changed, NULL);

  g_message ("ridgekeyd %s listening on %s (%s)", RK_VERSION, RK_SERVICE_NAME,
             pick_device () ? fp_device_get_name (pick_device ()) : "no sensor yet");
  g_main_loop_run (loop);

  xpc_connection_cancel (listener);
  g_main_loop_unref (loop);
  g_object_unref (ctx);
  g_message ("ridgekeyd stopped");
  return 0;
}
