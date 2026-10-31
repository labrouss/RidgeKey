/* RidgeKey - a small on-screen notice shown while ridgekeyd waits for a finger.
 * SPDX-License-Identifier: MIT
 *
 * Uses CFUserNotification, which a root launchd daemon can show on the console user's screen
 * (drawn by UserNotificationCenter). One button: pressing it cancels `cancel`, so the waiting
 * operation ends and the caller falls back to the password.
 *
 * Main thread only for show/update/close; the notice is watched on its own thread.
 */
#ifndef RIDGEKEY_PROMPT_H
#define RIDGEKEY_PROMPT_H

#include <gio/gio.h>

typedef struct Prompt Prompt;

/* NULL if the notice could not be shown (the operation still runs). timeout in seconds. */
Prompt *prompt_show (const char *header, const char *message, const char *button,
                     guint timeout, GCancellable *cancel);
void    prompt_update (Prompt *p, const char *message);
/* Removes the notice; pressing the button afterwards does nothing. Frees p. */
void    prompt_close (Prompt *p);

#endif /* RIDGEKEY_PROMPT_H */
