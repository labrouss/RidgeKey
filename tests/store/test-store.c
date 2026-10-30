/* SPDX-License-Identifier: MIT */
#include "store.h"
#include "ridgekey-protocol.h"
#include <sys/stat.h>
#include <glib/gstdio.h>
#include <gio/gio.h>
static int fails = 0;
#define CHECK(c) do { if (!(c)) { g_printerr ("FAIL line %d: %s\n", __LINE__, #c); fails++; } else g_print ("ok   %s\n", #c); } while (0)
int main (void) {
  g_autofree char *root = g_dir_make_tmp ("rk-store-XXXXXX", NULL);
  g_autofree char *rr = g_build_filename (root, "RidgeKey", NULL);
  GError *e = NULL; struct stat st; uid_t me = getuid ();
  CHECK (store_init (rr, &e));
  g_autofree char *pd = g_build_filename (rr, "prints", NULL);
  CHECK (stat (pd, &st) == 0 && (st.st_mode & 0777) == 0700);
  CHECK (store_load_user (me)->len == 0);
  CHECK (store_load_all ()->len == 0);
  FpPrint *a = mock_print_new (FP_FINGER_RIGHT_INDEX, "FP1-a");
  FpPrint *b = mock_print_new (FP_FINGER_LEFT_THUMB, "FP1-b");
  CHECK (store_save (me, a, &e));
  CHECK (store_save (me, b, &e));
  CHECK (store_save (4242, a, &e));
  g_autofree char *f = g_strdup_printf ("%s/%u/right-index.print", pd, (unsigned) me);
  CHECK (stat (f, &st) == 0 && (st.st_mode & 0777) == 0600);
  g_autofree char *ud = g_strdup_printf ("%s/%u", pd, (unsigned) me);
  CHECK (stat (ud, &st) == 0 && (st.st_mode & 0777) == 0700);
  GArray *fl = store_list_fingers (me);
  CHECK (fl->len == 2 && g_array_index (fl, guint, 0) == FP_FINGER_LEFT_THUMB && g_array_index (fl, guint, 1) == FP_FINGER_RIGHT_INDEX);
  GPtrArray *u = store_load_user (me);
  CHECK (u->len == 2);
  GPtrArray *all = store_load_all ();
  CHECK (all->len == 3);
  int tagged4242 = 0; for (guint i = 0; i < all->len; i++) if (GPOINTER_TO_UINT (g_object_get_data (all->pdata[i], RK_PRINT_UID_KEY)) == 4242) tagged4242++;
  CHECK (tagged4242 == 1);
  FpPrint *l = store_load (me, FP_FINGER_RIGHT_INDEX, &e);
  CHECK (l && g_strcmp0 (mock_print_id (l), "FP1-a") == 0 && fp_print_get_finger (l) == FP_FINGER_RIGHT_INDEX);
  GError *e2 = NULL;
  CHECK (store_load (me, FP_FINGER_LEFT_RING, &e2) == NULL && g_error_matches (e2, G_FILE_ERROR, G_FILE_ERROR_NOENT));
  /* overwrite same finger (re-enroll) */
  FpPrint *a2 = mock_print_new (FP_FINGER_RIGHT_INDEX, "FP1-a2");
  CHECK (store_save (me, a2, &e));
  CHECK (g_strcmp0 (mock_print_id (store_load (me, FP_FINGER_RIGHT_INDEX, NULL)), "FP1-a2") == 0);
  /* corrupt file is skipped, not fatal */
  g_autofree char *bad = g_strdup_printf ("%s/%u/left-middle.print", pd, (unsigned) me);
  g_file_set_contents (bad, "garbage", -1, NULL);
  CHECK (store_load_user (me)->len == 2);
  g_unlink (bad);
  /* remove */
  CHECK (store_remove (me, FP_FINGER_RIGHT_INDEX, &e));
  CHECK (store_remove (me, FP_FINGER_RIGHT_INDEX, &e));   /* idempotent */
  CHECK (store_list_fingers (me)->len == 1);
  CHECK (store_remove (me, FP_FINGER_LEFT_THUMB, &e));
  CHECK (!g_file_test (ud, G_FILE_TEST_EXISTS));            /* empty user dir removed */
  CHECK (store_load_all ()->len == 1);
  /* non-numeric entries ignored */
  g_autofree char *junk = g_build_filename (pd, "notauid", NULL); g_mkdir (junk, 0700);
  CHECK (store_load_all ()->len == 1);
  /* symlinked prints dir refused */
  g_autofree char *r2 = g_build_filename (root, "R2", NULL); g_mkdir (r2, 0755);
  g_autofree char *p2 = g_build_filename (r2, "prints", NULL); symlink ("/tmp", p2);
  GError *e3 = NULL;
  CHECK (!store_init (r2, &e3));
  g_print (fails ? "\n%d FAILED\n" : "\nall store tests passed\n", fails);
  return fails != 0;
}
