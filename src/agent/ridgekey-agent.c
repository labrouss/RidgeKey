/* ridgekey-agent - unlocks the lock screen with a fingerprint (per-user LaunchAgent)
 * SPDX-License-Identifier: MIT
 *
 * When the screen locks, the agent asks ridgekeyd to wait for a finger ("unlock"). ridgekeyd
 * only answers this program (checked by path, signature and hardened runtime), only for the
 * locked console user, and only after a match. The agent then types the password into
 * Apple's own lock screen, presses Return, and wipes its copy.
 *
 * Needs Accessibility permission (System Settings -> Privacy & Security -> Accessibility).
 * Log: ~/Library/Logs/RidgeKey-agent.log (never contains the password).
 */
#define __STDC_WANT_LIB_EXT1__ 1   /* memset_s */
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pwd.h>
#include <time.h>
#include <unistd.h>

#include <ApplicationServices/ApplicationServices.h>
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <xpc/xpc.h>

#include "ridgekey-protocol.h"

#define MAX_NO_MATCH   5      /* wrong fingers per lock before giving up (password still works) */
#define WAIT_SECONDS   60     /* one unlock request; renewed while the screen stays locked */

#define KEY_RETURN 36
#define KEY_SHIFT  56
#define KEY_A      0

/* All state below is only touched on `q`. */
static dispatch_queue_t q;
static xpc_connection_t conn;
static int locked;
static int in_flight;
static int gave_up;           /* stop for this lock (no prints, no password, too many misses) */
static int no_match;
static unsigned lock_gen;     /* increments on every lock / unlock */

static void start_unlock (void);

static void __attribute__ ((format (printf, 1, 2)))
logf_ (const char *fmt, ...)
{
  char ts[32];
  time_t t = time (NULL);
  va_list ap;

  strftime (ts, sizeof ts, "%Y-%m-%d %H:%M:%S", localtime (&t));
  fprintf (stderr, "%s ", ts);
  va_start (ap, fmt);
  vfprintf (stderr, fmt, ap);
  va_end (ap);
  fputc ('\n', stderr);
  fflush (stderr);
}

/* ------------------------------------------------------------------------------------------ */
/* typing                                                                                      */

static void
post_key (CGEventSourceRef src, CGKeyCode code, CGEventFlags flags)
{
  int down;

  for (down = 1; down >= 0; down--)
    {
      CGEventRef ev = CGEventCreateKeyboardEvent (src, code, down ? true : false);

      if (!ev)
        continue;
      CGEventSetFlags (ev, flags);
      CGEventPost (kCGHIDEventTap, ev);
      CFRelease (ev);
      usleep (12000);
    }
}

static void
post_units (CGEventSourceRef src, const UniChar *u, unsigned long n)
{
  int down;

  for (down = 1; down >= 0; down--)
    {
      CGEventRef ev = CGEventCreateKeyboardEvent (src, 0, down ? true : false);

      if (!ev)
        continue;
      CGEventKeyboardSetUnicodeString (ev, n, u);
      CGEventPost (kCGHIDEventTap, ev);
      CFRelease (ev);
      usleep (12000);
    }
}

/* Type pw (UTF-8) into the focused lock screen field and press Return. */
static void
type_password (const char *pw)
{
  CFStringRef s;
  CFIndex len, i;
  UniChar *units;
  CGEventSourceRef src;
  int waited;

  if (!AXIsProcessTrusted ())
    {
      logf_ ("cannot type: Accessibility permission missing for %s", RK_AGENT_PATH);
      return;
    }
  s = CFStringCreateWithCString (NULL, pw, kCFStringEncodingUTF8);
  if (!s)
    return;
  len = CFStringGetLength (s);
  units = calloc ((size_t) len + 1, sizeof *units);
  if (!units)
    {
      CFRelease (s);
      return;
    }
  CFStringGetCharacters (s, CFRangeMake (0, len), units);
  CFRelease (s);

  src = CGEventSourceCreate (kCGEventSourceStateHIDSystemState);

  /* Wake the display and bring up the password field. */
  post_key (src, KEY_SHIFT, 0);
  for (waited = 0; CGDisplayIsAsleep (CGMainDisplayID ()) && waited < 3000; waited += 100)
    usleep (100000);
  usleep (waited ? 900000 : 300000);

  /* Replace anything already typed in the field. */
  post_key (src, KEY_A, kCGEventFlagMaskCommand);

  for (i = 0; i < len; i++)
    {
      /* keep surrogate pairs (emoji etc.) in one event */
      if (units[i] >= 0xD800 && units[i] <= 0xDBFF && i + 1 < len)
        {
          post_units (src, &units[i], 2);
          i++;
        }
      else
        post_units (src, &units[i], 1);
    }
  memset_s (units, ((size_t) len + 1) * sizeof *units, 0, ((size_t) len + 1) * sizeof *units);
  free (units);

  post_key (src, KEY_RETURN, 0);
  if (src)
    CFRelease (src);
  logf_ ("typed the password into the lock screen");
}

/* ------------------------------------------------------------------------------------------ */
/* unlock requests                                                                             */

static void
send_cancel (void)
{
  xpc_object_t msg = xpc_dictionary_create (NULL, NULL, 0);

  xpc_dictionary_set_string (msg, RK_KEY_OP, RK_OP_CANCEL);
  xpc_connection_send_message (conn, msg);
  xpc_release (msg);
}

static void
retry_later (unsigned gen, int seconds)
{
  dispatch_after (dispatch_time (DISPATCH_TIME_NOW, (int64_t) seconds * (int64_t) NSEC_PER_SEC), q, ^{
    if (gen == lock_gen)
      start_unlock ();
  });
}

static void
handle_reply (xpc_object_t reply, unsigned gen)
{
  const char *err;

  in_flight = 0;
  if (gen != lock_gen || !locked)
    return;   /* the lock state changed while we waited */

  if (xpc_get_type (reply) != XPC_TYPE_DICTIONARY)
    {
      logf_ ("ridgekeyd not reachable - retrying");
      retry_later (gen, 5);
      return;
    }

  if (xpc_dictionary_get_bool (reply, RK_KEY_OK))
    {
      const char *pw = xpc_dictionary_get_string (reply, RK_KEY_PASSWORD);

      if (xpc_dictionary_get_bool (reply, RK_KEY_MATCHED) && pw)
        {
          char *copy = strdup (pw);

          if (copy)
            {
              type_password (copy);
              memset_s (copy, strlen (copy), 0, strlen (copy));
              free (copy);
            }
          /* Normally the unlock notification arrives now. If the screen is still locked
           * (e.g. the display was still waking), wait for another touch. */
          retry_later (gen, 4);
          return;
        }
      if (++no_match >= MAX_NO_MATCH)
        {
          logf_ ("%d fingers did not match - waiting for the password this time", no_match);
          gave_up = 1;
          return;
        }
      logf_ ("finger not recognised (%d/%d)", no_match, MAX_NO_MATCH);
      start_unlock ();
      return;
    }

  err = xpc_dictionary_get_string (reply, RK_KEY_ERROR);
  if (!err)
    err = "?";
  if (!strcmp (err, RK_ERR_TIMEOUT))
    start_unlock ();                       /* nobody touched it yet: keep waiting */
  else if (!strcmp (err, RK_ERR_FAILED))
    retry_later (gen, 1);                  /* bad scans or a sensor hiccup: try again shortly */
  else if (!strcmp (err, RK_ERR_BUSY))
    retry_later (gen, 3);
  else if (!strcmp (err, RK_ERR_CANCELLED))
    ;                                      /* we cancelled because the screen was unlocked */
  else
    {
      /* no-prints, no-password, password-outdated, permission, no-device, not-locked */
      logf_ ("not unlocking by fingerprint this time: %s (%s)",
             xpc_dictionary_get_string (reply, RK_KEY_MESSAGE), err);
      gave_up = 1;
    }
}

static void
start_unlock (void)
{
  xpc_object_t msg;
  unsigned gen = lock_gen;

  if (!locked || in_flight || gave_up)
    return;
  in_flight = 1;
  msg = xpc_dictionary_create (NULL, NULL, 0);
  xpc_dictionary_set_string (msg, RK_KEY_OP, RK_OP_UNLOCK);
  xpc_dictionary_set_int64 (msg, RK_KEY_TIMEOUT, WAIT_SECONDS);
  xpc_connection_send_message_with_reply (conn, msg, q, ^(xpc_object_t reply) {
    handle_reply (reply, gen);
  });
  xpc_release (msg);
}

static void
set_locked (int now_locked)
{
  if (now_locked == locked)
    return;
  locked = now_locked;
  lock_gen++;
  gave_up = 0;
  no_match = 0;
  logf_ ("screen %s", locked ? "locked - waiting for a finger" : "unlocked");
  if (locked)
    start_unlock ();
  else if (in_flight)
    send_cancel ();
}

static void
on_notification (CFNotificationCenterRef center, void *observer, CFNotificationName name,
                 const void *object, CFDictionaryRef info)
{
  int now_locked = CFStringCompare (name, CFSTR ("com.apple.screenIsLocked"), 0) == kCFCompareEqualTo;

  dispatch_async (q, ^{ set_locked (now_locked); });
}

static int
screen_locked_now (void)
{
  CFDictionaryRef d = CGSessionCopyCurrentDictionary ();
  int is_locked = 0;

  if (d)
    {
      CFBooleanRef v = CFDictionaryGetValue (d, CFSTR ("CGSSessionScreenIsLocked"));

      is_locked = v && CFBooleanGetValue (v);
      CFRelease (d);
    }
  return is_locked;
}

int
main (int argc, char **argv)
{
  struct passwd *me = getpwuid (getuid ());
  const char *home = me && me->pw_dir ? me->pw_dir : getenv ("HOME");
  const void *keys[] = { kAXTrustedCheckOptionPrompt };
  const void *vals[] = { kCFBooleanTrue };
  CFDictionaryRef ax_opts;
  CFNotificationCenterRef dc;

  if (argc > 1 && strcmp (argv[1], "--version") == 0)
    {
      printf ("ridgekey-agent %s\n", RK_VERSION);
      return 0;
    }

  if (home && *home)
    {
      char path[1024];

      snprintf (path, sizeof path, "%s/Library/Logs/RidgeKey-agent.log", home);
      if (!freopen (path, "a", stderr))
        perror ("ridgekey-agent: log");
    }
  logf_ ("ridgekey-agent %s started (uid %u)", RK_VERSION, (unsigned) getuid ());

  ax_opts = CFDictionaryCreate (NULL, keys, vals, 1, &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks);
  if (!AXIsProcessTrustedWithOptions (ax_opts))
    logf_ ("Accessibility permission needed: System Settings -> Privacy & Security -> "
           "Accessibility -> enable ridgekey-agent (%s)", RK_AGENT_PATH);
  CFRelease (ax_opts);

  q = dispatch_queue_create ("io.github.labrouss.ridgekey.agent", DISPATCH_QUEUE_SERIAL);
  conn = xpc_connection_create_mach_service (RK_SERVICE_NAME, q, XPC_CONNECTION_MACH_SERVICE_PRIVILEGED);
  xpc_connection_set_event_handler (conn, ^(xpc_object_t ev) {
    if (ev == XPC_ERROR_CONNECTION_INTERRUPTED)
      logf_ ("ridgekeyd restarted");
  });
  xpc_connection_resume (conn);

  /* Ask ridgekeyd to check us now, so a signature/permission problem shows up in both logs
   * at login instead of at the first lock. */
  {
    xpc_object_t hello = xpc_dictionary_create (NULL, NULL, 0);

    xpc_dictionary_set_string (hello, RK_KEY_OP, RK_OP_AGENT_HELLO);
    xpc_connection_send_message_with_reply (conn, hello, q, ^(xpc_object_t reply) {
      if (xpc_get_type (reply) != XPC_TYPE_DICTIONARY)
        logf_ ("ridgekeyd not reachable yet");
      else if (xpc_dictionary_get_bool (reply, RK_KEY_OK))
        logf_ ("ridgekeyd accepted this agent");
      else
        logf_ ("ridgekeyd REFUSED this agent: %s", xpc_dictionary_get_string (reply, RK_KEY_MESSAGE));
    });
    xpc_release (hello);
  }

  dc = CFNotificationCenterGetDistributedCenter ();
  CFNotificationCenterAddObserver (dc, NULL, on_notification, CFSTR ("com.apple.screenIsLocked"),
                                   NULL, CFNotificationSuspensionBehaviorDeliverImmediately);
  CFNotificationCenterAddObserver (dc, NULL, on_notification, CFSTR ("com.apple.screenIsUnlocked"),
                                   NULL, CFNotificationSuspensionBehaviorDeliverImmediately);

  {
    int initially = screen_locked_now ();

    dispatch_async (q, ^{ set_locked (initially); });
  }
  CFRunLoopRun ();
  return 0;
}
