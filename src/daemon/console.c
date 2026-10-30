/* RidgeKey - console user and screen-lock state from the IORegistry.
 * SPDX-License-Identifier: MIT
 */
#include "console.h"

#include <CoreFoundation/CoreFoundation.h>
#include <IOKit/IOKitLib.h>

static gboolean
dict_bool (CFDictionaryRef d, CFStringRef key)
{
  CFTypeRef v = CFDictionaryGetValue (d, key);

  return v && CFGetTypeID (v) == CFBooleanGetTypeID () && CFBooleanGetValue ((CFBooleanRef) v);
}

ConsoleState
console_get_state (void)
{
  ConsoleState s = { RK_NO_CONSOLE_USER, FALSE };
  io_registry_entry_t root = IORegistryGetRootEntry (kIOMainPortDefault);
  CFTypeRef users;
  CFIndex i;

  if (!root)
    return s;
  users = IORegistryEntryCreateCFProperty (root, CFSTR ("IOConsoleUsers"), kCFAllocatorDefault, 0);
  IOObjectRelease (root);
  if (!users)
    return s;
  if (CFGetTypeID (users) == CFArrayGetTypeID ())
    {
      for (i = 0; i < CFArrayGetCount ((CFArrayRef) users); i++)
        {
          CFDictionaryRef d = CFArrayGetValueAtIndex ((CFArrayRef) users, i);
          CFTypeRef uidv;
          int uid = -1;

          if (CFGetTypeID (d) != CFDictionaryGetTypeID () ||
              !dict_bool (d, CFSTR ("kCGSSessionOnConsoleKey")))
            continue;
          uidv = CFDictionaryGetValue (d, CFSTR ("kCGSSessionUserIDKey"));
          /* uid < 500: system accounts such as _windowserver (88) own the login window */
          if (uidv && CFGetTypeID (uidv) == CFNumberGetTypeID () &&
              CFNumberGetValue ((CFNumberRef) uidv, kCFNumberIntType, &uid) && uid >= 500)
            {
              s.uid = (uid_t) uid;
              s.locked = dict_bool (d, CFSTR ("CGSSessionScreenIsLocked"));
            }
          break;
        }
    }
  CFRelease (users);
  return s;
}

typedef struct {
  ConsoleChangedFunc fn;
  gpointer           data;
  ConsoleState       last;
} Watch;

static gboolean
poll_cb (gpointer p)
{
  Watch *w = p;
  ConsoleState now = console_get_state ();

  if (now.uid != w->last.uid || now.locked != w->last.locked)
    {
      w->last = now;
      w->fn (&now, w->data);
    }
  return G_SOURCE_CONTINUE;
}

void
console_watch (ConsoleChangedFunc fn, gpointer data)
{
  Watch *w = g_new0 (Watch, 1);

  w->fn = fn;
  w->data = data;
  w->last = console_get_state ();
  fn (&w->last, data);   /* report the initial state */
  g_timeout_add_seconds (1, poll_cb, w);
}
