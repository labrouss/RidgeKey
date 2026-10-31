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
      r->action = g_key_file_get_string (kf, groups[i], "action", NULL);
      r->shortcut = g_key_file_get_string (kf, groups[i], "shortcut", NULL);
      if (rk_rule_validate (r, NULL))
        g_ptr_array_add (rules, r);
      else
        rk_rule_free (r);       /* a hand-edited, invalid rule is simply ignored */
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
actions_pick (GPtrArray *rules, FpFinger finger, double held)
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
      else if (!tap)
        tap = r;
    }
  return best_hold ? best_hold : tap;
}
