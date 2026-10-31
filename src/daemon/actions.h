/* RidgeKey - finger actions: "tap left-index = lock screen", "hold right-thumb 2 s = Shortcut".
 * SPDX-License-Identifier: MIT
 *
 * Per-user rules, root-owned: /Library/Application Support/RidgeKey/actions/<uid>.conf (0600,
 * GKeyFile, one [rule-N] group per rule). Changed only through ridgekeyd (trusted clients).
 * The RidgeKey Agent in the user's session performs the action; ridgekeyd only decides which.
 */
#ifndef RIDGEKEY_ACTIONS_H
#define RIDGEKEY_ACTIONS_H

#include <sys/types.h>
#include <glib.h>
#include <fprint.h>

#define RK_ACTIONS_MAX        20
#define RK_HOLD_MIN_SECONDS   1
#define RK_HOLD_MAX_SECONDS   10
#define RK_SHORTCUT_MAX_LEN   128

typedef struct {
  FpFinger finger;
  gboolean hold;       /* FALSE = tap */
  guint    seconds;    /* hold only */
  char    *action;     /* "lock" | "shortcut" */
  char    *shortcut;   /* Shortcuts app name, for "shortcut" */
} RkRule;

void       rk_rule_free (RkRule *rule);

/* Validates one rule; on error returns FALSE with a message for the user. */
gboolean   rk_rule_validate (const RkRule *rule, GError **error);

/* Rules of uid (never NULL; empty when none). */
GPtrArray *actions_load (uid_t uid);
/* Replaces uid's rules (an empty array removes the file). */
gboolean   actions_save (uid_t uid, GPtrArray *rules, GError **error);

/* The rule for a touch of finger lasting `held` seconds: the longest hold rule it reached,
 * otherwise the tap rule; NULL if none. */
const RkRule *actions_pick (GPtrArray *rules, FpFinger finger, double held);

#endif /* RIDGEKEY_ACTIONS_H */
