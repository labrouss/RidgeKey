/* RidgeKey authorization plugin - admin dialogs by fingerprint.
 * SPDX-License-Identifier: MIT
 *
 * Installed as /Library/Security/SecurityAgentPlugins/RidgeKey.bundle and loaded by macOS's
 * authorizationhost (as root) for rights that ridgekey-admin has switched to:
 *
 *   class rule, k-of-n 1, rule = [ (is-root), io.github.labrouss.ridgekey.fingerprint,
 *                                  io.github.labrouss.ridgekey.orig.<right> ]
 *   io.github.labrouss.ridgekey.fingerprint = evaluate-mechanisms, tries 1,
 *       [ RidgeKey:fingerprint,privileged   builtin:authenticate,privileged ]
 *
 * Mechanisms:
 *   fingerprint  ask ridgekeyd (op "authorize") to match an administrator's finger. On a match
 *                it returns that user's stored password; we put username + password into the
 *                context and macOS's own builtin:authenticate verifies them. Anything else ->
 *                Deny, and authd moves on to the original rule = the normal password dialog.
 *   selftest     always Allow. ridgekey-admin uses it to prove the bundle loads before it
 *                changes any real right.
 *
 * RULES FOR THIS FILE: it runs inside authorizationhost. The result is ALWAYS Allow or Deny,
 * never UserCanceled or Undefined (either one aborts the whole k-of-n rule, so the password
 * fallback would not run). System libraries only. No allocation-heavy work, no UI.
 */
#include <dispatch/dispatch.h>
#include <dlfcn.h>
#include <os/log.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <xpc/xpc.h>

#include <Security/AuthorizationPlugin.h>
#include <Security/AuthorizationTags.h>

#include "ridgekey-protocol.h"

#define RK_AUTH_TIMEOUT   20          /* seconds the daemon waits for a finger */
#define RK_AUTH_GRACE     15          /* extra seconds before we give up on the daemon */

/* Hint / context keys used by authd (AuthorizationTagsPriv.h). */
#define HINT_RIGHT        "authorize-right"
#define HINT_CLIENT_PATH  "client-path"
#define HINT_CLIENT_PID   "client-pid"    /* the process calling authd (may be a system helper) */
#define HINT_CREATOR_PID  "creator-pid"   /* the app that created the request = client-path's pid */
#define CTX_USERNAME      kAuthorizationEnvironmentUsername   /* "username" */
#define CTX_PASSWORD      kAuthorizationEnvironmentPassword   /* "password" */

static os_log_t rk_log;

typedef enum { MECH_FINGERPRINT, MECH_SELFTEST, MECH_UNKNOWN } MechKind;

typedef struct {
  const AuthorizationCallbacks *callbacks;
} Plugin;

typedef struct {
  Plugin                *plugin;
  AuthorizationEngineRef engine;
  MechKind               kind;
} Mechanism;

/* Copies a hint string (NUL-terminated or not) into buf; returns buf or NULL. */
static const char *
get_hint (Mechanism *m, const char *key, char *buf, size_t len)
{
  const AuthorizationValue *v = NULL;

  if (m->plugin->callbacks->GetHintValue (m->engine, key, &v) != errAuthorizationSuccess ||
      !v || !v->data || v->length == 0)
    return NULL;
  size_t n = v->length < len - 1 ? v->length : len - 1;
  memcpy (buf, v->data, n);
  buf[n] = '\0';
  return buf;
}

static OSStatus
set_context_string (Mechanism *m, const char *key, const char *value)
{
  AuthorizationValue v = { strlen (value) + 1, (void *) value };   /* with the NUL, like loginwindow */

  return m->plugin->callbacks->SetContextValue (m->engine, key,
                                                kAuthorizationContextFlagExtractable, &v);
}

static void
set_result (Mechanism *m, AuthorizationResult result)
{
  m->plugin->callbacks->SetResult (m->engine, result);
}

/* authd stores pids as a native int. Returns 0 when missing. */
static int64_t
get_hint_pid (Mechanism *m, const char *key)
{
  const AuthorizationValue *v = NULL;
  int32_t pid32;

  if (m->plugin->callbacks->GetHintValue (m->engine, key, &v) != errAuthorizationSuccess ||
      !v || !v->data)
    return 0;
  if (v->length == sizeof (int32_t))
    {
      memcpy (&pid32, v->data, sizeof pid32);
      return pid32 > 0 ? pid32 : 0;
    }
  if (v->length == sizeof (int64_t))
    {
      int64_t pid64;
      memcpy (&pid64, v->data, sizeof pid64);
      return pid64 > 0 ? pid64 : 0;
    }
  return 0;
}

/* Ask ridgekeyd. Returns a retained reply dictionary, or NULL. Never blocks forever. */
static xpc_object_t
ask_daemon (const char *right, const char *client, int64_t pid, int64_t caller)
{
  xpc_connection_t conn;
  xpc_object_t msg;
  __block xpc_object_t reply = NULL;
  dispatch_semaphore_t done = dispatch_semaphore_create (0);
  dispatch_queue_t q = dispatch_queue_create ("io.github.labrouss.ridgekey.authplugin", NULL);

  conn = xpc_connection_create_mach_service (RK_SERVICE_NAME, q, XPC_CONNECTION_MACH_SERVICE_PRIVILEGED);
  xpc_connection_set_event_handler (conn, ^(xpc_object_t ev) { (void) ev; /* progress events: ignored */ });
  xpc_connection_resume (conn);

  msg = xpc_dictionary_create (NULL, NULL, 0);
  xpc_dictionary_set_string (msg, RK_KEY_OP, RK_OP_AUTHORIZE);
  xpc_dictionary_set_string (msg, RK_KEY_RIGHT, right ? right : "");
  xpc_dictionary_set_string (msg, RK_KEY_CLIENT, client ? client : "");
  xpc_dictionary_set_int64 (msg, RK_KEY_CLIENT_PID, pid);
  xpc_dictionary_set_int64 (msg, RK_KEY_CALLER_PID, caller);
  xpc_dictionary_set_int64 (msg, RK_KEY_TIMEOUT, RK_AUTH_TIMEOUT);

  xpc_connection_send_message_with_reply (conn, msg, q, ^(xpc_object_t r) {
    if (xpc_get_type (r) == XPC_TYPE_DICTIONARY)
      reply = xpc_retain (r);
    dispatch_semaphore_signal (done);
  });
  xpc_release (msg);

  bool answered = dispatch_semaphore_wait (done, dispatch_time (DISPATCH_TIME_NOW,
                     (int64_t) (RK_AUTH_TIMEOUT + RK_AUTH_GRACE) * NSEC_PER_SEC)) == 0;
  if (!answered)
    {
      os_log_error (rk_log, "ridgekeyd did not answer in time");
      xpc_connection_cancel (conn);          /* the daemon cancels the operation when we go away */
      /* Cancelling makes XPC call the reply block with an error; wait for that. */
      answered = dispatch_semaphore_wait (done, dispatch_time (DISPATCH_TIME_NOW, 5 * NSEC_PER_SEC)) == 0;
      if (reply)
        {
          xpc_release (reply);               /* too late - deny */
          reply = NULL;
        }
    }
  else
    xpc_connection_cancel (conn);

  xpc_release (conn);
  if (answered)
    {
      /* The reply block has run, so nothing refers to these any more. If it never ran, keep
       * them (a tiny leak beats a late block touching freed objects). */
      dispatch_release (done);
      dispatch_release (q);
    }
  return reply;
}

static void
run_fingerprint (Mechanism *m)
{
  char right[256], client[1024];
  const char *r = get_hint (m, HINT_RIGHT, right, sizeof right);
  const char *c = get_hint (m, HINT_CLIENT_PATH, client, sizeof client);
  xpc_object_t reply = ask_daemon (r, c, get_hint_pid (m, HINT_CREATOR_PID), get_hint_pid (m, HINT_CLIENT_PID));
  AuthorizationResult result = kAuthorizationResultDeny;

  if (!reply)
    {
      os_log (rk_log, "%{public}s: no answer from ridgekeyd - falling back to the password", r ?: "?");
    }
  else if (!xpc_dictionary_get_bool (reply, RK_KEY_OK))
    {
      os_log (rk_log, "%{public}s: %{public}s (%{public}s) - falling back to the password", r ?: "?",
              xpc_dictionary_get_string (reply, RK_KEY_MESSAGE) ?: "failed",
              xpc_dictionary_get_string (reply, RK_KEY_ERROR) ?: "?");
    }
  else
    {
      const char *user = xpc_dictionary_get_string (reply, RK_KEY_USER);
      const char *pass = xpc_dictionary_get_string (reply, RK_KEY_PASSWORD);

      if (user && *user && pass && *pass &&
          set_context_string (m, CTX_USERNAME, user) == errAuthorizationSuccess &&
          set_context_string (m, CTX_PASSWORD, pass) == errAuthorizationSuccess)
        {
          os_log (rk_log, "%{public}s: fingerprint matched %{public}s - handing over to builtin:authenticate",
                  r ?: "?", user);
          result = kAuthorizationResultAllow;
        }
      else
        os_log (rk_log, "%{public}s: no match - falling back to the password", r ?: "?");
    }

  if (reply)
    xpc_release (reply);   /* the reply's copy of the password goes with it */
  set_result (m, result);
}

/* ------------------------------------------------------------------------------------------ */
/* plugin interface                                                                            */

static OSStatus
mech_create (AuthorizationPluginRef plugin, AuthorizationEngineRef engine,
             AuthorizationMechanismId id, AuthorizationMechanismRef *out)
{
  Mechanism *m = calloc (1, sizeof *m);

  if (!m)
    return errAuthorizationInternal;
  m->plugin = (Plugin *) plugin;
  m->engine = engine;
  m->kind = strcmp (id, "fingerprint") == 0 ? MECH_FINGERPRINT
          : strcmp (id, "selftest") == 0    ? MECH_SELFTEST : MECH_UNKNOWN;
  *out = m;
  return errAuthorizationSuccess;
}

static OSStatus
mech_invoke (AuthorizationMechanismRef ref)
{
  Mechanism *m = ref;

  switch (m->kind)
    {
    case MECH_FINGERPRINT:
      run_fingerprint (m);
      break;
    case MECH_SELFTEST:
      os_log (rk_log, "selftest: plugin loaded");
      set_result (m, kAuthorizationResultAllow);
      break;
    default:
      os_log_error (rk_log, "unknown mechanism - deny");
      set_result (m, kAuthorizationResultDeny);
      break;
    }
  return errAuthorizationSuccess;
}

static OSStatus
mech_deactivate (AuthorizationMechanismRef ref)
{
  Mechanism *m = ref;

  return m->plugin->callbacks->DidDeactivate (m->engine);
}

static OSStatus
mech_destroy (AuthorizationMechanismRef ref)
{
  free (ref);
  return errAuthorizationSuccess;
}

static OSStatus
plugin_destroy (AuthorizationPluginRef ref)
{
  free (ref);
  return errAuthorizationSuccess;
}

static const AuthorizationPluginInterface rk_interface = {
  kAuthorizationPluginInterfaceVersion,
  plugin_destroy,
  mech_create,
  mech_invoke,
  mech_deactivate,
  mech_destroy,
};

__attribute__((visibility ("default"))) OSStatus
AuthorizationPluginCreate (const AuthorizationCallbacks *callbacks, AuthorizationPluginRef *plugin,
                           const AuthorizationPluginInterface **interface)
{
  Plugin *p = calloc (1, sizeof *p);
  Dl_info info;

  if (!p)
    return errAuthorizationInternal;
  /* XPC may run our connection handler blocks after a request ends; never let the host unload
   * this code while that can happen. */
  if (dladdr ((const void *) AuthorizationPluginCreate, &info) && info.dli_fname)
    (void) dlopen (info.dli_fname, RTLD_LAZY | RTLD_NOLOAD | RTLD_NODELETE);
  rk_log = os_log_create ("io.github.labrouss.ridgekey", "authplugin");
  p->callbacks = callbacks;
  *plugin = p;
  *interface = &rk_interface;
  return errAuthorizationSuccess;
}
