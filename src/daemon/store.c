/* RidgeKey - on-disk record of the prints RidgeKey enrolled.
 * SPDX-License-Identifier: MIT
 */
#include "store.h"
#include "ridgekey-protocol.h"

#include <errno.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>
#include <glib/gstdio.h>

static char *store_root = NULL;   /* <root>/prints */

static char *
user_dir (uid_t uid)
{
  return g_strdup_printf ("%s/%u", store_root, (unsigned) uid);
}

static char *
print_path (uid_t uid, FpFinger finger)
{
  return g_strdup_printf ("%s/%u/%s.print", store_root, (unsigned) uid, rk_finger_name (finger));
}

/* Create dir 0700 if missing; refuse symlinks and directories we do not own. */
static gboolean
ensure_private_dir (const char *path, GError **error)
{
  GStatBuf st;

  if (g_mkdir (path, 0700) != 0 && errno != EEXIST)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   "cannot create %s: %s", path, g_strerror (errno));
      return FALSE;
    }
  if (g_lstat (path, &st) != 0 || !S_ISDIR (st.st_mode) || st.st_uid != geteuid ())
    {
      g_set_error (error, G_FILE_ERROR, G_FILE_ERROR_PERM,
                   "%s is not a directory owned by uid %u", path, (unsigned) geteuid ());
      return FALSE;
    }
  if ((st.st_mode & 0777) != 0700 && g_chmod (path, 0700) != 0)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   "cannot chmod %s: %s", path, g_strerror (errno));
      return FALSE;
    }
  return TRUE;
}

gboolean
store_init (const char *root, GError **error)
{
  g_free (store_root);
  store_root = g_build_filename (root, "prints", NULL);

  if (g_mkdir_with_parents (root, 0755) != 0)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   "cannot create %s: %s", root, g_strerror (errno));
      return FALSE;
    }
  return ensure_private_dir (store_root, error);
}

FpPrint *
store_load (uid_t uid, FpFinger finger, GError **error)
{
  g_autofree char *path = print_path (uid, finger);
  g_autofree gchar *data = NULL;
  gsize len = 0;
  FpPrint *print;

  if (!g_file_get_contents (path, &data, &len, error))
    return NULL;
  print = fp_print_deserialize ((const guchar *) data, len, error);
  if (print)
    g_object_set_data (G_OBJECT (print), RK_PRINT_UID_KEY, GUINT_TO_POINTER ((guint) uid));
  return print;
}

GPtrArray *
store_load_user (uid_t uid)
{
  GPtrArray *prints = g_ptr_array_new_with_free_func (g_object_unref);
  int f;

  for (f = RK_FINGER_FIRST; f <= RK_FINGER_LAST; f++)
    {
      g_autoptr(GError) err = NULL;
      FpPrint *p = store_load (uid, (FpFinger) f, &err);

      if (p)
        g_ptr_array_add (prints, p);
      else if (!g_error_matches (err, G_FILE_ERROR, G_FILE_ERROR_NOENT))
        g_warning ("store: skipping %u/%s: %s", (unsigned) uid, rk_finger_name (f), err->message);
    }
  return prints;
}

GPtrArray *
store_load_all (void)
{
  GPtrArray *all = g_ptr_array_new_with_free_func (g_object_unref);
  g_autoptr(GDir) dir = g_dir_open (store_root, 0, NULL);
  const char *name;

  if (!dir)
    return all;
  while ((name = g_dir_read_name (dir)) != NULL)
    {
      char *end = NULL;
      unsigned long uid = strtoul (name, &end, 10);
      g_autoptr(GPtrArray) user = NULL;
      guint i;

      if (end == name || *end != '\0')
        continue;
      user = store_load_user ((uid_t) uid);
      for (i = 0; i < user->len; i++)
        g_ptr_array_add (all, g_object_ref (g_ptr_array_index (user, i)));
    }
  return all;
}

gboolean
store_save (uid_t uid, FpPrint *print, GError **error)
{
  g_autofree char *dir = user_dir (uid);
  g_autofree char *path = print_path (uid, fp_print_get_finger (print));
  g_autofree guchar *data = NULL;
  gsize len = 0;

  if (!ensure_private_dir (dir, error))
    return FALSE;
  if (!fp_print_serialize (print, &data, &len, error))
    return FALSE;
  return g_file_set_contents_full (path, (const gchar *) data, (gssize) len,
                                   G_FILE_SET_CONTENTS_CONSISTENT | G_FILE_SET_CONTENTS_DURABLE,
                                   0600, error);
}

gboolean
store_remove (uid_t uid, FpFinger finger, GError **error)
{
  g_autofree char *path = print_path (uid, finger);
  g_autofree char *dir = user_dir (uid);

  if (g_unlink (path) != 0 && errno != ENOENT)
    {
      g_set_error (error, G_FILE_ERROR, g_file_error_from_errno (errno),
                   "cannot remove %s: %s", path, g_strerror (errno));
      return FALSE;
    }
  g_rmdir (dir);   /* only succeeds when empty */
  return TRUE;
}

GArray *
store_list_fingers (uid_t uid)
{
  GArray *fingers = g_array_new (FALSE, FALSE, sizeof (guint));
  int f;

  for (f = RK_FINGER_FIRST; f <= RK_FINGER_LAST; f++)
    {
      g_autofree char *path = print_path (uid, (FpFinger) f);
      guint v = (guint) f;

      if (g_file_test (path, G_FILE_TEST_IS_REGULAR))
        g_array_append_val (fingers, v);
    }
  return fingers;
}
