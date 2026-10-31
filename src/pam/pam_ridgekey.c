/* pam_ridgekey - fingerprint authentication through ridgekeyd (OpenPAM module for macOS)
 * SPDX-License-Identifier: MIT
 *
 * Use as a "sufficient" module in front of the password, e.g. in /etc/pam.d/sudo_local:
 *   auth       sufficient     /usr/local/ridgekey/lib/pam/pam_ridgekey.so
 *
 * Returns PAM_SUCCESS on a fingerprint match. In every other case the stack falls through to the
 * next module (the password):
 *   PAM_AUTH_ERR    the finger was read but did not match (after `tries` attempts)
 *   PAM_IGNORE      no prints enrolled, remote session, daemon unreachable or busy, timeout, Ctrl-C
 *
 * Options:  timeout=N (seconds per attempt, default 10)   tries=N (default 3)
 *           allow_remote (also offer fingerprint in SSH sessions - off by default, because whoever
 *                         touches the sensor would approve someone else's remote sudo)
 *           debug (log to syslog)
 *           noop (load, then return PAM_IGNORE - used by ridgekey-sudo's load check)
 */
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <syslog.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>

#include <security/pam_appl.h>
#include <security/pam_modules.h>
#include <xpc/xpc.h>
#include <dispatch/dispatch.h>

#include "ridgekey-protocol.h"

#define RK_PAM_EXPORT __attribute__ ((visibility ("default")))

typedef struct {
  long timeout;
  long tries;
  int  allow_remote;
  int  debug;
  int  nocue;      /* no "Touch the sensor" lines at all */
  int  noop;
} Options;

static void
parse_options (Options *o, int flags, int argc, const char **argv)
{
  int i;

  o->timeout = 10;
  o->tries = 3;
  o->allow_remote = 0;
  o->debug = 0;
  /* PAM_SILENT is ignored on purpose: sudo sets it by default (sudoers pam_silent), and
   * without the cue the terminal just seems to hang while the sensor waits. */
  o->nocue = 0;
  o->noop = 0;
  for (i = 0; i < argc; i++)
    {
      if (strncmp (argv[i], "timeout=", 8) == 0)
        o->timeout = strtol (argv[i] + 8, NULL, 10);
      else if (strncmp (argv[i], "tries=", 6) == 0)
        o->tries = strtol (argv[i] + 6, NULL, 10);
      else if (strcmp (argv[i], "allow_remote") == 0)
        o->allow_remote = 1;
      else if (strcmp (argv[i], "debug") == 0)
        o->debug = 1;
      else if (strcmp (argv[i], "noop") == 0)
        o->noop = 1;
      else if (strcmp (argv[i], "nocue") == 0)
        o->nocue = 1;
      else
        syslog (LOG_AUTHPRIV | LOG_WARNING, "pam_ridgekey: unknown option '%s'", argv[i]);
    }
  if (o->timeout < 3 || o->timeout > RK_TIMEOUT_MAX_SECONDS)
    o->timeout = 10;
  if (o->tries < 1 || o->tries > 10)
    o->tries = 3;
}

static void __attribute__ ((format (printf, 2, 3)))
dbg (const Options *o, const char *fmt, ...)
{
  va_list ap;

  if (!o->debug)
    return;
  va_start (ap, fmt);
  vsyslog (LOG_AUTHPRIV | LOG_DEBUG, fmt, ap);
  va_end (ap);
}

/* Show a line to the user: on the controlling terminal when there is one (sudo), otherwise
 * through the application's conversation function. */
static void
say (pam_handle_t *pamh, const Options *o, const char *msg)
{
  const void *item = NULL;
  const struct pam_conv *conv;
  struct pam_message m;
  const struct pam_message *mp = &m;
  struct pam_response *resp = NULL;
  int tty;

  if (o->nocue)
    return;
  tty = open ("/dev/tty", O_WRONLY | O_NOCTTY | O_CLOEXEC);
  if (tty >= 0)
    {
      dprintf (tty, "%s\n", msg);
      close (tty);
      return;
    }
  if (pam_get_item (pamh, PAM_CONV, &item) != PAM_SUCCESS || !item)
    return;
  conv = item;
  if (!conv->conv)
    return;
  m.msg_style = PAM_TEXT_INFO;
  m.msg = (char *) msg;
  conv->conv (1, &mp, &resp, conv->appdata_ptr);
  if (resp)
    {
      free (resp->resp);
      free (resp);
    }
}

static int
is_remote_session (pam_handle_t *pamh)
{
  const void *rhost = NULL;

  if (pam_get_item (pamh, PAM_RHOST, &rhost) == PAM_SUCCESS && rhost && *(const char *) rhost)
    return 1;
  return getenv ("SSH_CONNECTION") || getenv ("SSH_CLIENT") || getenv ("SSH_TTY") ||
         pam_getenv (pamh, "SSH_CONNECTION") || pam_getenv (pamh, "SSH_TTY");
}

/* ------------------------------------------------------------------------------------------ */

/* XPC delivers the connection's final "invalid" event asynchronously, on our queue, after
 * xpc_connection_cancel(). The handler's code lives in this module, and OpenPAM dlclose()s the
 * module in pam_end() - if the event arrives after that, the process (sudo!) jumps into
 * unmapped memory (seen in CI: exit 139). Pin the module so it is never unloaded. */
static void
pin_module (void)
{
  static dispatch_once_t once;

  dispatch_once (&once, ^{
    Dl_info info;

    if (dladdr ((const void *) pin_module, &info) && info.dli_fname)
      (void) dlopen (info.dli_fname, RTLD_LAZY | RTLD_NOLOAD | RTLD_NODELETE);
  });
}

/* Heap-allocated so the XPC and signal handler blocks can share it by pointer. */
typedef struct {
  xpc_connection_t     conn;
  dispatch_queue_t     queue;
  dispatch_semaphore_t done;
  dispatch_semaphore_t closed;         /* signalled by the connection's final invalid event */
  xpc_object_t         reply;          /* retained, set by the reply handler */
  volatile int         interrupted;    /* Ctrl-C seen */
  int                  handler_pending;/* a reply handler may still run - do not free */
} Session;

static void
send_cancel (Session *s)
{
  xpc_object_t msg = xpc_dictionary_create (NULL, NULL, 0);

  xpc_dictionary_set_string (msg, RK_KEY_OP, RK_OP_CANCEL);
  xpc_connection_send_message (s->conn, msg);
  xpc_release (msg);
}

/* Send one request and wait for its reply. Returns a retained reply, or NULL if the daemon is
 * unreachable or did not answer within wait_s seconds (then the operation is cancelled). */
static xpc_object_t
call (Session *s, xpc_object_t msg, long wait_s)
{
  xpc_object_t r;

  s->reply = NULL;
  xpc_connection_send_message_with_reply (s->conn, msg, s->queue, ^(xpc_object_t reply) {
    if (xpc_get_type (reply) == XPC_TYPE_DICTIONARY)
      s->reply = xpc_retain (reply);
    dispatch_semaphore_signal (s->done);
  });
  if (dispatch_semaphore_wait (s->done, dispatch_time (DISPATCH_TIME_NOW, (int64_t) wait_s * (int64_t) NSEC_PER_SEC)) != 0)
    {
      send_cancel (s);
      /* the reply handler runs exactly once; wait for it so it cannot touch freed state */
      if (dispatch_semaphore_wait (s->done, dispatch_time (DISPATCH_TIME_NOW, 5 * (int64_t) NSEC_PER_SEC)) != 0)
        {
          s->handler_pending = 1;
          return NULL;
        }
      if (s->reply)
        xpc_release (s->reply);
      s->reply = NULL;
    }
  r = s->reply;
  s->reply = NULL;
  return r;
}

static int
verify_loop (pam_handle_t *pamh, const Options *o, const char *user)
{
  Session *s = calloc (1, sizeof *s);
  struct sigaction old_int, ign;
  dispatch_source_t sigsrc;
  int result = PAM_IGNORE;
  long attempt;

  if (!s)
    return PAM_IGNORE;
  pin_module ();
  s->queue = dispatch_queue_create ("io.github.labrouss.ridgekey.pam", DISPATCH_QUEUE_SERIAL);
  s->done = dispatch_semaphore_create (0);
  s->closed = dispatch_semaphore_create (0);
  s->conn = xpc_connection_create_mach_service (RK_SERVICE_NAME, s->queue,
                                                XPC_CONNECTION_MACH_SERVICE_PRIVILEGED);
  {
    dispatch_semaphore_t closed = s->closed;   /* the block's own reference, released on invalid */

    dispatch_retain (closed);
    xpc_connection_set_event_handler (s->conn, ^(xpc_object_t ev) {
      if (ev == XPC_ERROR_CONNECTION_INVALID)
        {
          dispatch_semaphore_signal (closed);
          dispatch_release (closed);
        }
    });
  }
  xpc_connection_resume (s->conn);

  /* Ctrl-C while waiting for a finger: cancel and fall back to the password. */
  memset (&ign, 0, sizeof ign);
  ign.sa_handler = SIG_IGN;
  sigaction (SIGINT, &ign, &old_int);
  sigsrc = dispatch_source_create (DISPATCH_SOURCE_TYPE_SIGNAL, SIGINT, 0, s->queue);
  dispatch_source_set_event_handler (sigsrc, ^{
    s->interrupted = 1;
    send_cancel (s);
  });
  dispatch_resume (sigsrc);

  for (attempt = 1; attempt <= o->tries && !s->interrupted; attempt++)
    {
      xpc_object_t msg = xpc_dictionary_create (NULL, NULL, 0);
      xpc_object_t reply;
      const char *err;

      xpc_dictionary_set_string (msg, RK_KEY_OP, RK_OP_VERIFY);
      xpc_dictionary_set_string (msg, RK_KEY_USER, user);
      xpc_dictionary_set_int64 (msg, RK_KEY_TIMEOUT, o->timeout);

      if (attempt == 1)
        say (pamh, o, "Touch the fingerprint sensor (Ctrl-C to type your password)");

      reply = call (s, msg, o->timeout + 10);
      xpc_release (msg);

      if (!reply)
        {
          dbg (o, "pam_ridgekey: no answer from ridgekeyd");
          if (result != PAM_AUTH_ERR)
            result = PAM_IGNORE;
          break;
        }

      if (xpc_dictionary_get_bool (reply, RK_KEY_OK))
        {
          if (xpc_dictionary_get_bool (reply, RK_KEY_MATCHED))
            {
              syslog (LOG_AUTHPRIV | LOG_NOTICE, "pam_ridgekey: fingerprint match for %s (%s)", user,
                      xpc_dictionary_get_string (reply, RK_KEY_FINGER));
              result = PAM_SUCCESS;
              xpc_release (reply);
              break;
            }
          result = PAM_AUTH_ERR;
          if (attempt < o->tries)
            say (pamh, o, "Fingerprint not recognised - try again");
          xpc_release (reply);
          continue;
        }

      err = xpc_dictionary_get_string (reply, RK_KEY_ERROR);
      dbg (o, "pam_ridgekey: %s: %s", err ? err : "?", xpc_dictionary_get_string (reply, RK_KEY_MESSAGE));
      if (err && strcmp (err, RK_ERR_FAILED) == 0 && attempt < o->tries)
        {
          /* bad scans used up the daemon's own retries - give the user another go */
          say (pamh, o, "Could not read the finger - try again");
          xpc_release (reply);
          continue;
        }
      /* no-prints, busy, no-device, timeout, cancelled, permission, ...: use the password */
      if (err && strcmp (err, RK_ERR_TIMEOUT) == 0)
        say (pamh, o, "No finger detected - using your password");
      if (result != PAM_AUTH_ERR)
        result = PAM_IGNORE;
      xpc_release (reply);
      break;
    }

  dispatch_source_cancel (sigsrc);
  dispatch_sync (s->queue, ^{ });   /* drain: no handler of ours is running or queued after this */
  dispatch_release (sigsrc);
  sigaction (SIGINT, &old_int, NULL);

  if (s->interrupted && result != PAM_SUCCESS)
    result = PAM_IGNORE;

  xpc_connection_cancel (s->conn);
  /* Let the final event arrive while we are still here (the module is pinned regardless). */
  dispatch_semaphore_wait (s->closed, dispatch_time (DISPATCH_TIME_NOW, 2 * (int64_t) NSEC_PER_SEC));
  if (!s->handler_pending)
    {
      /* Nothing outstanding: safe to free. (Otherwise leak the few bytes rather than risk a
       * late reply handler writing to freed memory inside sudo.) */
      xpc_release (s->conn);
      dispatch_release (s->done);
      dispatch_release (s->closed);
      dispatch_release (s->queue);
      free (s);
    }
  return result;
}

RK_PAM_EXPORT int
pam_sm_authenticate (pam_handle_t *pamh, int flags, int argc, const char **argv)
{
  Options o;
  const char *user = NULL;
  int rc;

  parse_options (&o, flags, argc, argv);
  if (o.noop)
    return PAM_IGNORE;

  if (pam_get_user (pamh, &user, NULL) != PAM_SUCCESS || !user || !*user)
    return PAM_IGNORE;
  if (!o.allow_remote && is_remote_session (pamh))
    {
      dbg (&o, "pam_ridgekey: remote session for %s - skipping fingerprint", user);
      return PAM_IGNORE;
    }

  rc = verify_loop (pamh, &o, user);
  dbg (&o, "pam_ridgekey: %s -> %s", user, pam_strerror (pamh, rc));
  return rc;
}

RK_PAM_EXPORT int
pam_sm_setcred (pam_handle_t *pamh, int flags, int argc, const char **argv)
{
  return PAM_SUCCESS;
}
