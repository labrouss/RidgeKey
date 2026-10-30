/* ridgekey-typetest - feasibility test: can synthetic keystrokes reach the macOS lock screen?
 * SPDX-License-Identifier: MIT
 *
 * Waits DELAY seconds (lock the screen meanwhile), then types a harmless test word into
 * whatever has keyboard focus. It NEVER presses Return and never types a real password.
 *
 *   ridgekey-typetest [-d SECONDS] [-t WORD] [-k] [-n]
 *     -d  delay before typing (default 10)
 *     -t  word to type, letters a-z only (default "ridgekey")
 *     -k  use US virtual key codes instead of Unicode strings (second method to compare)
 *     -n  skip the Accessibility check (when run as a root launchd daemon, which has no
 *         GUI session - see scripts/typetest-as-daemon.sh)
 *
 * Needs Accessibility permission for the app it runs in (Terminal / iTerm): System Settings ->
 * Privacy & Security -> Accessibility. It asks for it on first run.
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <ApplicationServices/ApplicationServices.h>

/* US ANSI virtual key codes for a..z */
static const CGKeyCode us_keycodes[26] = {
  0, 11, 8, 2, 14, 3, 5, 4, 34, 38, 40, 37, 46, 45, 31, 35, 12, 15, 1, 17, 32, 9, 13, 7, 16, 6
};

static int
screen_is_locked (void)
{
  CFDictionaryRef d = CGSessionCopyCurrentDictionary ();
  int locked = -1;

  if (d)
    {
      CFBooleanRef v = CFDictionaryGetValue (d, CFSTR ("CGSSessionScreenIsLocked"));

      locked = (v && CFBooleanGetValue (v)) ? 1 : 0;
      CFRelease (d);
    }
  return locked;
}

static void
post_key (CGEventSourceRef src, CGKeyCode code, UniChar ch, int use_keycodes)
{
  int down;

  for (down = 1; down >= 0; down--)
    {
      CGEventRef ev = CGEventCreateKeyboardEvent (src, use_keycodes ? code : 0, down ? true : false);

      if (!ev)
        continue;
      if (!use_keycodes)
        CGEventKeyboardSetUnicodeString (ev, 1, &ch);
      CGEventPost (kCGHIDEventTap, ev);
      CFRelease (ev);
      usleep (15000);
    }
}

int
main (int argc, char **argv)
{
  long delay = 10;
  const char *word = "ridgekey";
  int use_keycodes = 0, skip_ax = 0, opt, locked;
  size_t i;
  CGEventSourceRef src;
  const void *keys[] = { kAXTrustedCheckOptionPrompt };
  const void *vals[] = { kCFBooleanTrue };
  CFDictionaryRef ax_opts;

  while ((opt = getopt (argc, argv, "d:t:knh")) != -1)
    switch (opt)
      {
      case 'd': delay = strtol (optarg, NULL, 10); break;
      case 't': word = optarg; break;
      case 'k': use_keycodes = 1; break;
      case 'n': skip_ax = 1; break;
      default:
        fprintf (stderr, "usage: %s [-d SECONDS] [-t WORD] [-k] [-n]\n", argv[0]);
        return 2;
      }
  for (i = 0; word[i]; i++)
    if (!islower ((unsigned char) word[i]))
      {
        fprintf (stderr, "test word must be lowercase letters a-z only (never use your password)\n");
        return 2;
      }

  printf ("ridgekey-typetest: euid %u\n", (unsigned) geteuid ());
  ax_opts = CFDictionaryCreate (NULL, keys, vals, 1, &kCFTypeDictionaryKeyCallBacks,
                                &kCFTypeDictionaryValueCallBacks);
  if (!skip_ax && !AXIsProcessTrustedWithOptions (ax_opts))
    {
      printf ("Accessibility permission is missing for this terminal app.\n"
              "Grant it in System Settings -> Privacy & Security -> Accessibility, then run again.\n");
      CFRelease (ax_opts);
      return 1;
    }
  CFRelease (ax_opts);

  printf ("Method: %s. Lock the screen now (Ctrl-Cmd-Q). Typing \"%s\" in %ld s...\n",
          use_keycodes ? "US key codes" : "Unicode strings", word, delay);
  fflush (stdout);
  sleep ((unsigned) delay);

  locked = screen_is_locked ();
  src = CGEventSourceCreate (kCGEventSourceStatePrivate);
  for (i = 0; word[i]; i++)
    post_key (src, us_keycodes[word[i] - 'a'], (UniChar) word[i], use_keycodes);
  if (src)
    CFRelease (src);

  printf ("Typed %zu characters (screen locked at that moment: %s).\n"
          "Unlock normally, then report: did %zu dots appear in the password field?\n",
          strlen (word), locked == 1 ? "yes" : locked == 0 ? "NO - test not valid" : "unknown",
          strlen (word));
  return 0;
}
