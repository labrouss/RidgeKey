/* RidgeKey - who is at the console and whether their screen is locked.
 * SPDX-License-Identifier: MIT
 *
 * Read from the IORegistry root's "IOConsoleUsers" (maintained by loginwindow/WindowServer):
 * the on-console session's kCGSSessionUserIDKey and CGSSessionScreenIsLocked. Root daemons
 * can read this without a GUI session, and user processes cannot fake it.
 */
#ifndef RIDGEKEY_CONSOLE_H
#define RIDGEKEY_CONSOLE_H

#include <sys/types.h>
#include <glib.h>

#define RK_NO_CONSOLE_USER ((uid_t) -1)

typedef struct {
  uid_t    uid;      /* RK_NO_CONSOLE_USER at the login window */
  gboolean locked;
} ConsoleState;

typedef void (*ConsoleChangedFunc) (const ConsoleState *state, gpointer data);

ConsoleState console_get_state (void);

/* Poll on the GLib main loop (every second) and call fn on the main thread after each change. */
void console_watch (ConsoleChangedFunc fn, gpointer data);

#endif /* RIDGEKEY_CONSOLE_H */
