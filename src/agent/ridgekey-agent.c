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
#include <dlfcn.h>
#include <pwd.h>
#include <spawn.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#include <ApplicationServices/ApplicationServices.h>
#include <Carbon/Carbon.h>          /* Text Input Sources + UCKeyTranslate: real key codes */
#include <CoreFoundation/CoreFoundation.h>
#include <dispatch/dispatch.h>
#include <xpc/xpc.h>

#include "ridgekey-protocol.h"

#define MAX_NO_MATCH   5      /* wrong fingers per lock before giving up (password still works) */
#define MAX_TYPED_FAIL 2      /* typed but still locked: stop, to avoid repeated wrong-password attempts */
#define WAIT_SECONDS   60     /* one unlock request; renewed while the screen stays locked */

#define KEY_RETURN    36
#define KEY_SHIFT     56
#define KEY_BACKSPACE 51

/* NEVER send Command: macOS 0.4.0 kept Command "held" after a Cmd-A, so the password went in
 * as Cmd shortcuts (Cmd-Shift-3/4 = screenshots). Every event now carries explicit flags and
 * comes from a private event source that cannot change or inherit the system modifier state. */

/* All state below is only touched on `q`. */
static dispatch_queue_t q;
static xpc_connection_t conn;
static int locked;
static int in_flight;
static int gave_up;           /* stop for this lock (no prints, no password, too many misses) */
static int no_match;
static unsigned lock_gen;     /* increments on every lock / unlock */
static int typed_fail;        /* typed the password but the screen stayed locked */
static int test_mode;         /* --type-test: type a sample into the focused app, no Return */

static void start_unlock (void);
static int screen_locked_now (void);

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

/* ---- key map: character -> (virtual key code, Shift/Option) for a keyboard layout ---- */

typedef struct {
  UniChar      ch;
  CGKeyCode    code;
  CGEventFlags flags;
} RkKeyEntry;

typedef struct {
  RkKeyEntry e[1024];
  int      n;
} RkKeyMap;

static int
keymap_find (const RkKeyMap *m, UniChar ch)
{
  int i;

  for (i = 0; i < m->n; i++)
    if (m->e[i].ch == ch)
      return i;
  return -1;
}

/* Main thread only (Text Input Source calls assert that on recent macOS). */
static int
keymap_build (TISInputSourceRef src, RkKeyMap *m)
{
  /* fewest modifiers first, so 'a' maps to plain a, not Option-something */
  static const struct { UInt32 carbon; CGEventFlags cg; } mods[] = {
    { 0, 0 },
    { shiftKey, kCGEventFlagMaskShift },
    { optionKey, kCGEventFlagMaskAlternate },
    { shiftKey | optionKey, kCGEventFlagMaskShift | kCGEventFlagMaskAlternate },
  };
  CFDataRef data;
  const UCKeyboardLayout *layout;
  unsigned mi;
  UInt16 kc;

  m->n = 0;
  if (!src)
    return 0;
  data = TISGetInputSourceProperty (src, kTISPropertyUnicodeKeyLayoutData);
  if (!data)
    return 0;
  layout = (const UCKeyboardLayout *) CFDataGetBytePtr (data);
  for (mi = 0; mi < sizeof mods / sizeof mods[0]; mi++)
    for (kc = 0; kc < 128; kc++)
      {
        UInt32 dead = 0;
        UniCharCount len = 0;
        UniChar out[4];

        if (UCKeyTranslate (layout, kc, kUCKeyActionDown, (mods[mi].carbon >> 8) & 0xFF,
                            LMGetKbdType (), kUCKeyTranslateNoDeadKeysMask, &dead,
                            4, &len, out) != noErr || len != 1)
          continue;
        if (keymap_find (m, out[0]) < 0 && m->n < (int) (sizeof m->e / sizeof m->e[0]))
          {
            m->e[m->n].ch = out[0];
            m->e[m->n].code = kc;
            m->e[m->n].flags = mods[mi].cg;
            m->n++;
          }
      }
  return m->n;
}

static int
keymap_covers (const RkKeyMap *m, const UniChar *u, CFIndex len)
{
  CFIndex i;

  for (i = 0; i < len; i++)
    if (keymap_find (m, u[i]) < 0)
      return 0;
  return 1;
}

/* One character: the real key (with modifiers) when the layout has it, and the character
 * attached as well; Unicode-only as a last resort. */
static void
post_char (CGEventSourceRef src, const RkKeyMap *m, const UniChar *u, unsigned long n, int *unmapped)
{
  int idx = n == 1 ? keymap_find (m, u[0]) : -1;
  int down;

  if (idx < 0)
    (*unmapped)++;
  for (down = 1; down >= 0; down--)
    {
      CGEventRef ev = CGEventCreateKeyboardEvent (src, idx >= 0 ? m->e[idx].code : 0, down ? true : false);

      if (!ev)
        continue;
      CGEventSetFlags (ev, idx >= 0 ? m->e[idx].flags : 0);
      CGEventKeyboardSetUnicodeString (ev, n, u);
      CGEventPost (kCGHIDEventTap, ev);
      CFRelease (ev);
      usleep (15000);
    }
}

/* Type pw (UTF-8) into the lock screen field and press Return. Runs on q. */
static void
type_password (const char *pw)
{
  CFStringRef s;
  CFIndex len, i;
  UniChar *units;
  CGEventSourceRef src;
  RkKeyMap *map = calloc (1, sizeof *map);
  __block TISInputSourceRef restore = NULL;
  __block int switched = 0;
  int waited, was_asleep, unmapped = 0, aborted = 0;

  if (!map)
    return;
  if (!AXIsProcessTrusted ())
    {
      logf_ ("cannot type: Accessibility permission missing for %s", RK_AGENT_PATH);
      free (map);
      return;
    }
  s = CFStringCreateWithCString (NULL, pw, kCFStringEncodingUTF8);
  if (!s)
    {
      free (map);
      return;
    }
  len = CFStringGetLength (s);
  units = calloc ((size_t) len + 1, sizeof *units);
  if (!units)
    {
      CFRelease (s);
      free (map);
      return;
    }
  CFStringGetCharacters (s, CFRangeMake (0, len), units);
  CFRelease (s);

  /* Key map of the active layout; if it cannot produce every character (e.g. Greek active,
   * Latin password), switch to the user's Latin layout while typing. */
  dispatch_sync (dispatch_get_main_queue (), ^{
    TISInputSourceRef cur = TISCopyCurrentKeyboardLayoutInputSource ();

    keymap_build (cur, map);
    if (!keymap_covers (map, units, len))
      {
        TISInputSourceRef ascii = TISCopyCurrentASCIICapableKeyboardLayoutInputSource ();
        RkKeyMap *alt = calloc (1, sizeof *alt);

        if (ascii && alt && keymap_build (ascii, alt) && keymap_covers (alt, units, len))
          {
            TISInputSourceRef prev = TISCopyCurrentKeyboardInputSource ();   /* may be an IME */

            if (TISSelectInputSource (ascii) == noErr)
              {
                memcpy (map, alt, sizeof *map);
                restore = prev;          /* released after restoring */
                switched = 1;
              }
            else if (prev)
              CFRelease (prev);
          }
        free (alt);
        if (ascii)
          CFRelease (ascii);
      }
    if (cur)
      CFRelease (cur);
  });
  if (switched)
    logf_ ("switched to the Latin keyboard layout for typing");

  src = CGEventSourceCreate (kCGEventSourceStatePrivate);

  /* Wake the display and let the password field appear (it fades in on Tahoe). */
  was_asleep = CGDisplayIsAsleep (CGMainDisplayID ()) ? 1 : 0;
  post_key (src, KEY_SHIFT, 0);
  for (waited = 0; CGDisplayIsAsleep (CGMainDisplayID ()) && waited < 4000; waited += 100)
    usleep (100000);
  usleep (was_asleep ? 1500000 : 800000);

  /* From here on, every step first checks that the screen is STILL locked: if the user
   * unlocked meanwhile, the keystrokes (and the password, and Return) would go into whatever
   * app has focus. Test mode types into a normal app on purpose and skips the check. */
#define STILL_LOCKED() (test_mode || screen_locked_now ())
  if (!STILL_LOCKED ())
    {
      aborted = 1;
      goto done;
    }

  /* Clear anything already in the lock screen field (not in test mode). */
  if (!test_mode)
    for (i = 0; i < 32; i++)
      post_key (src, KEY_BACKSPACE, 0);
  usleep (100000);

  for (i = 0; i < len; i++)
    {
      if (!STILL_LOCKED ())
        {
          aborted = 1;
          goto done;
        }
      /* keep surrogate pairs (emoji etc.) in one event */
      if (units[i] >= 0xD800 && units[i] <= 0xDBFF && i + 1 < len)
        {
          post_char (src, map, &units[i], 2, &unmapped);
          i++;
        }
      else
        post_char (src, map, &units[i], 1, &unmapped);
    }
  usleep (100000);
  if (!STILL_LOCKED ())
    aborted = 1;
  else if (!test_mode)
    post_key (src, KEY_RETURN, 0);
#undef STILL_LOCKED

done:
  memset_s (units, ((size_t) len + 1) * sizeof *units, 0, ((size_t) len + 1) * sizeof *units);
  free (units);
  memset_s (map, sizeof *map, 0, sizeof *map);
  free (map);
  if (src)
    CFRelease (src);

  if (switched)
    {
      usleep (300000);
      dispatch_sync (dispatch_get_main_queue (), ^{
        if (restore)
          {
            TISSelectInputSource (restore);
            CFRelease (restore);
          }
      });
    }
  if (aborted)
    logf_ ("screen was unlocked while typing - stopped (no Return sent)");
  else
    logf_ ("typed the password into the lock screen (%ld characters, %d without a key on this layout)",
           (long) len, unmapped);
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
          /* Normally the unlock notification arrives now. If the screen is still locked,
           * the password was not accepted: allow one more touch, then stop for this lock so
           * we never pile up wrong-password attempts. */
          dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 4 * (int64_t) NSEC_PER_SEC), q, ^{
            if (gen != lock_gen || !locked)
              return;
            if (++typed_fail >= MAX_TYPED_FAIL)
              {
                logf_ ("typed the password %d times but the screen stayed locked - giving up "
                       "for this lock (please report this)", typed_fail);
                gave_up = 1;
                return;
              }
            logf_ ("typed but the screen is still locked - touch the sensor again");
            start_unlock ();
          });
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
  typed_fail = 0;
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

/* ------------------------------------------------------------------------------------------ */
/* finger actions (ridgekeyd decides, the agent performs them as the user)                     */

extern char **environ;

static void
lock_screen_now (void)
{
  /* login.framework's SACLockScreenImmediate is what Ctrl-Cmd-Q and the menu use; private, so
   * looked up at run time. Fallback: post Ctrl-Cmd-Q (needs Accessibility, which we have). */
  static void *handle;
  int (*sac_lock) (void) = NULL;

  if (!handle)
    handle = dlopen ("/System/Library/PrivateFrameworks/login.framework/Versions/Current/login", RTLD_LAZY);
  if (handle)
    sac_lock = (int (*) (void)) dlsym (handle, "SACLockScreenImmediate");
  if (sac_lock)
    {
      sac_lock ();
      logf_ ("action: screen locked");
      return;
    }
  {
    CGEventSourceRef src = CGEventSourceCreate (kCGEventSourceStatePrivate);
    CGEventRef down = CGEventCreateKeyboardEvent (src, (CGKeyCode) 12 /* Q */, true);
    CGEventRef up = CGEventCreateKeyboardEvent (src, (CGKeyCode) 12, false);
    CGEventFlags f = kCGEventFlagMaskCommand | kCGEventFlagMaskControl;

    /* the one place a Command flag is used: a single explicit chord, released right away */
    CGEventSetFlags (down, f);
    CGEventSetFlags (up, f);
    CGEventPost (kCGHIDEventTap, down);
    CGEventPost (kCGHIDEventTap, up);
    CFRelease (down);
    CFRelease (up);
    if (src)
      CFRelease (src);
    logf_ ("action: screen locked (Ctrl-Cmd-Q)");
  }
}

static void
run_shortcut (const char *name)
{
  char *const argv[] = { "/usr/bin/shortcuts", "run", (char *) name, NULL };
  pid_t pid;
  int rc = posix_spawn (&pid, argv[0], NULL, NULL, argv, environ);

  if (rc == 0)
    logf_ ("action: running Shortcut \"%s\" (pid %d)", name, (int) pid);
  else
    logf_ ("action: could not start shortcuts: %s", strerror (rc));
  /* reap it in the background, whatever it takes */
  if (rc == 0)
    dispatch_async (dispatch_get_global_queue (QOS_CLASS_UTILITY, 0), ^{
      int st;
      waitpid (pid, &st, 0);
    });
}

static void
handle_action (xpc_object_t ev)
{
  const char *action = xpc_dictionary_get_string (ev, RK_KEY_ACTION);
  const char *shortcut = xpc_dictionary_get_string (ev, RK_KEY_SHORTCUT);

  if (locked || !action)
    return;                       /* never act on the lock screen */
  logf_ ("action: %s x%lld %s (%.1f s)", xpc_dictionary_get_string (ev, RK_KEY_GESTURE) ?: "?",
         (long long) xpc_dictionary_get_int64 (ev, RK_KEY_TAPS),
         xpc_dictionary_get_string (ev, RK_KEY_FINGER) ?: "?", xpc_dictionary_get_double (ev, RK_KEY_HELD));
  if (strcmp (action, "lock") == 0)
    lock_screen_now ();
  else if (strcmp (action, "shortcut") == 0 && shortcut && *shortcut)
    run_shortcut (shortcut);
}

static void
say_hello (void)
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
  if (argc > 1 && strcmp (argv[1], "--type-test") == 0)
    {
      /* Same typing code as the lock screen, with a sample instead of the password and no
       * Return. Run it from Terminal, click into an empty TextEdit document within 5 s, and
       * compare what appears with the sample. */
      static const char sample[] = "RidgeKey-3#Ab$9 xyz \xc3\xa9";   /* ends in e-acute */

      test_mode = 1;
      if (!AXIsProcessTrusted ())
        {
          fprintf (stderr, "Grant Accessibility to the app you run this from (Terminal), then retry.\n");
          return 1;
        }
      printf ("Click into an empty TextEdit document. Typing in 5 s:\n  expected: %s\n", sample);
      fflush (stdout);
      q = dispatch_queue_create ("io.github.labrouss.ridgekey.agent.test", DISPATCH_QUEUE_SERIAL);
      dispatch_async (q, ^{
        sleep (5);
        type_password (sample);
        printf ("done - does TextEdit show exactly the expected text?\n");
        exit (0);
      });
      CFRunLoopRun ();
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
      {
        /* ridgekeyd restarted: introduce ourselves again (finger actions are sent to us) */
        logf_ ("ridgekeyd restarted");
        dispatch_after (dispatch_time (DISPATCH_TIME_NOW, 2 * NSEC_PER_SEC), q, ^{ say_hello (); });
      }
    else if (xpc_get_type (ev) == XPC_TYPE_DICTIONARY)
      {
        const char *name = xpc_dictionary_get_string (ev, RK_KEY_EVENT);

        if (name && strcmp (name, RK_EV_ACTION) == 0)
          handle_action (ev);
      }
  });
  xpc_connection_resume (conn);

  /* Ask ridgekeyd to check us now, so a signature/permission problem shows up in both logs
   * at login instead of at the first lock. This also registers us for finger actions. */
  say_hello ();

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
