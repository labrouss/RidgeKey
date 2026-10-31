/* RidgeKey - finger actions (see actions.h).
 * SPDX-License-Identifier: MIT
 */
#define G_LOG_DOMAIN "ridgekeyd"

#include <errno.h>
#include <string.h>
#include <sys/stat.h>
#include <glib/gstdio.h>
#include <gio/gio.h>

#include "actions.h"
#include "ridgekey-protocol.h"
#include "store.h"

static char *
actions_dir (void)
{
  const char *root = g_getenv ("RIDGEKEY_STORE_ROOT");   /* tests only */

  return g_build_filename (root && *root ? root : RK_STORE_ROOT_DEFAULT, "actions", NULL);
}

static char *
actions_path (uid_t uid)
{
  g_autofree char *dir = actions_dir ();
  g_autofree char *name = g_strdup_printf ("%u.conf", (unsigned) uid);

  return g_build_filename (dir, name, NULL);
}

void
rk_rule_free (RkRule *rule)
{
  if (!rule)
    return;
  g_free (rule->action);
  g_free (rule->shortcut);
  g_free (rule);
}

gboolean
rk_rule_validate (const RkRule *r, GError **error)
{
  const char *p;

  if (r->finger < RK_FINGER_FIRST || r->finger > RK_FINGER_LAST)
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "unknown finger");
      return FALSE;
    }
  if (!r->hold && (r->taps < 1 || r->taps > RK_TAPS_MAX))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "taps must be 1-%d", RK_TAPS_MAX);
      return FALSE;
    }
  if (r->hold && (r->seconds < RK_HOLD_MIN_SECONDS || r->seconds > RK_HOLD_MAX_SECONDS))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                   "hold time must be %d-%d seconds", RK_HOLD_MIN_SECONDS, RK_HOLD_MAX_SECONDS);
      return FALSE;
    }
  if (!r->action || (strcmp (r->action, "lock") != 0 && strcmp (r->action, "shortcut") != 0))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT, "action must be lock or shortcut");
      return FALSE;
    }
  if (strcmp (r->action, "shortcut") == 0)
    {
      if (!r->shortcut || !*r->shortcut || strlen (r->shortcut) > RK_SHORTCUT_MAX_LEN ||
          !g_utf8_validate (r->shortcut, -1, NULL))
        {
          g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                       "a shortcut needs a name (1-%d characters)", RK_SHORTCUT_MAX_LEN);
          return FALSE;
        }
      for (p = r->shortcut; *p; p++)
        if ((unsigned char) *p < 0x20)
          {
            g_set_error (error, G_IO_ERROR, G_IO_ERROR_INVALID_ARGUMENT,
                         "the shortcut name contains a control character");
            return FALSE;
          }
    }
  return TRUE;
}

gboolean
rk_rule_same_trigger (const RkRule *a, const RkRule *b)
{
  return a->finger == b->finger && a->hold == b->hold &&
         (a->hold ? a->seconds == b->seconds : a->taps == b->taps);
}

static const char *
gesture_label (const RkRule *r, char *buf, size_t len)
{
  if (r->hold)
    g_snprintf (buf, len, "hold %u s", r->seconds);
  else
    g_snprintf (buf, len, "%s", r->taps == 3 ? "triple tap" : r->taps == 2 ? "double tap" : "tap");
  return buf;
}

gboolean
actions_check_unique (GPtrArray *rules, GError **error)
{
  guint i, j;
  char buf[32];

  for (i = 0; i < rules->len; i++)
    for (j = i + 1; j < rules->len; j++)
      if (rk_rule_same_trigger (g_ptr_array_index (rules, i), g_ptr_array_index (rules, j)))
        {
          const RkRule *r = g_ptr_array_index (rules, i);

          g_set_error (error, G_IO_ERROR, G_IO_ERROR_EXISTS, "%s %s is set twice (rules %u and %u)",
                       rk_finger_name (r->finger), gesture_label (r, buf, sizeof buf), i + 1, j + 1);
          return FALSE;
        }
  return TRUE;
}

GPtrArray *
actions_load (uid_t uid)
{
  GPtrArray *rules = g_ptr_array_new_with_free_func ((GDestroyNotify) rk_rule_free);
  g_autofree char *path = actions_path (uid);
  g_autoptr(GKeyFile) kf = g_key_file_new ();
  g_auto(GStrv) groups = NULL;
  guint i;

  if (!g_key_file_load_from_file (kf, path, G_KEY_FILE_NONE, NULL))
    return rules;
  groups = g_key_file_get_groups (kf, NULL);
  for (i = 0; groups[i] && rules->len < RK_ACTIONS_MAX; i++)
    {
      RkRule *r = g_new0 (RkRule, 1);
      g_autofree char *finger = g_key_file_get_string (kf, groups[i], "finger", NULL);
      g_autofree char *gesture = g_key_file_get_string (kf, groups[i], "gesture", NULL);

      r->finger = (FpFinger) rk_finger_from_name (finger);
      r->hold = gesture && strcmp (gesture, "hold") == 0;
      r->seconds = (guint) g_key_file_get_integer (kf, groups[i], "seconds", NULL);
      r->taps = r->hold ? 1 : (guint) MAX (1, g_key_file_get_integer (kf, groups[i], "taps", NULL));
      r->action = g_key_file_get_string (kf, groups[i], "action", NULL);
      r->shortcut = g_key_file_get_string (kf, groups[i], "shortcut", NULL);
      {
        gboolean dup = FALSE;
        guint k;

        for (k = 0; k < rules->len && !dup; k++)
          dup = rk_rule_same_trigger (g_ptr_array_index (rules, k), r);
        if (rk_rule_validate (r, NULL) && !dup)
          g_ptr_array_add (rules, r);
        else
          rk_rule_free (r);     /* invalid or duplicate (hand-edited / older file): ignored */
      }
    }
  return rules;
}

gboolean
actions_save (uid_t uid, GPtrArray *rules, GError **error)
{
  g_autofree char *dir = actions_dir ();
  g_autofree char *path = actions_path (uid);
  g_autoptr(GKeyFile) kf = g_key_file_new ();
  g_autofree char *data = NULL;
  gsize len = 0;
  guint i;

  if (rules->len == 0)
    {
      if (g_unlink (path) != 0 && errno != ENOENT)
        {
          g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                       "cannot remove %s: %s", path, g_strerror (errno));
          return FALSE;
        }
      return TRUE;
    }
  if (g_mkdir_with_parents (dir, 0700) != 0)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   "cannot create %s: %s", dir, g_strerror (errno));
      return FALSE;
    }
  for (i = 0; i < rules->len; i++)
    {
      const RkRule *r = g_ptr_array_index (rules, i);
      g_autofree char *g = g_strdup_printf ("rule-%u", i);

      g_key_file_set_string (kf, g, "finger", rk_finger_name (r->finger));
      g_key_file_set_string (kf, g, "gesture", r->hold ? "hold" : "tap");
      if (r->hold)
        g_key_file_set_integer (kf, g, "seconds", (gint) r->seconds);
      else
        g_key_file_set_integer (kf, g, "taps", (gint) r->taps);
      g_key_file_set_string (kf, g, "action", r->action);
      if (r->shortcut && *r->shortcut)
        g_key_file_set_string (kf, g, "shortcut", r->shortcut);
    }
  data = g_key_file_to_data (kf, &len, NULL);
  if (!g_file_set_contents_full (path, data, (gssize) len, G_FILE_SET_CONTENTS_CONSISTENT, 0600, error))
    return FALSE;
  return TRUE;
}

const RkRule *
actions_pick (GPtrArray *rules, FpFinger finger, double held, guint taps)
{
  const RkRule *best_hold = NULL, *tap = NULL;
  guint i;

  for (i = 0; i < rules->len; i++)
    {
      const RkRule *r = g_ptr_array_index (rules, i);

      if (r->finger != finger)
        continue;
      if (r->hold)
        {
          if (held >= (double) r->seconds && (!best_hold || r->seconds > best_hold->seconds))
            best_hold = r;
        }
      else if (!tap && r->taps == taps)
        tap = r;
    }
  return best_hold ? best_hold : tap;
}

guint
actions_max_taps (GPtrArray *rules, FpFinger finger)
{
  guint i, max = 1;

  for (i = 0; i < rules->len; i++)
    {
      const RkRule *r = g_ptr_array_index (rules, i);

      if (r->finger == finger && !r->hold && r->taps > max)
        max = r->taps;
    }
  return max;
}

gboolean
actions_is_hold (GPtrArray *rules, FpFinger finger, double held)
{
  guint i;

  for (i = 0; i < rules->len; i++)
    {
      const RkRule *r = g_ptr_array_index (rules, i);

      if (r->finger == finger && r->hold && held >= (double) r->seconds)
        return TRUE;
    }
  return FALSE;
}
