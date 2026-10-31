/* Unit tests for src/daemon/actions.c: validation, save/load round trip, tap/hold choice. */
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include "actions.h"

static RkRule *
rule (FpFinger f, gboolean hold, guint secs, const char *action, const char *sc)
{
  RkRule *r = g_new0 (RkRule, 1);
  r->finger = f; r->hold = hold; r->seconds = secs; r->taps = 1;
  r->action = g_strdup (action); r->shortcut = g_strdup (sc);
  return r;
}

static void
test_validate (void)
{
  RkRule *r;

  r = rule (FP_FINGER_LEFT_INDEX, FALSE, 0, "lock", NULL);   g_assert_true (rk_rule_validate (r, NULL)); rk_rule_free (r);
  r = rule (FP_FINGER_LEFT_INDEX, TRUE, 0, "lock", NULL);    g_assert_false (rk_rule_validate (r, NULL)); rk_rule_free (r);
  r = rule (FP_FINGER_LEFT_INDEX, TRUE, 11, "lock", NULL);   g_assert_false (rk_rule_validate (r, NULL)); rk_rule_free (r);
  r = rule (FP_FINGER_LEFT_INDEX, TRUE, 3, "lock", NULL);    g_assert_true (rk_rule_validate (r, NULL)); rk_rule_free (r);
  r = rule (FP_FINGER_LEFT_INDEX, FALSE, 0, "shortcut", ""); g_assert_false (rk_rule_validate (r, NULL)); rk_rule_free (r);
  r = rule (FP_FINGER_LEFT_INDEX, FALSE, 0, "shortcut", "Work mode"); g_assert_true (rk_rule_validate (r, NULL)); rk_rule_free (r);
  r = rule (FP_FINGER_LEFT_INDEX, FALSE, 0, "shortcut", "bad\nname"); g_assert_false (rk_rule_validate (r, NULL)); rk_rule_free (r);
  r = rule (FP_FINGER_LEFT_INDEX, FALSE, 0, "rm -rf", NULL); g_assert_false (rk_rule_validate (r, NULL)); rk_rule_free (r);
  r = rule (FP_FINGER_UNKNOWN, FALSE, 0, "lock", NULL);      g_assert_false (rk_rule_validate (r, NULL)); rk_rule_free (r);
  r = rule (FP_FINGER_LEFT_INDEX, FALSE, 0, "lock", NULL); r->taps = 3; g_assert_true (rk_rule_validate (r, NULL)); rk_rule_free (r);
  r = rule (FP_FINGER_LEFT_INDEX, FALSE, 0, "lock", NULL); r->taps = 4; g_assert_false (rk_rule_validate (r, NULL)); rk_rule_free (r);
}

static void
test_pick (void)
{
  g_autoptr(GPtrArray) rules = g_ptr_array_new_with_free_func ((GDestroyNotify) rk_rule_free);
  const RkRule *p;

  g_ptr_array_add (rules, rule (FP_FINGER_RIGHT_THUMB, FALSE, 0, "lock", NULL));
  g_ptr_array_add (rules, rule (FP_FINGER_RIGHT_THUMB, TRUE, 2, "shortcut", "two"));
  g_ptr_array_add (rules, rule (FP_FINGER_RIGHT_THUMB, TRUE, 5, "shortcut", "five"));
  g_ptr_array_add (rules, rule (FP_FINGER_LEFT_INDEX, TRUE, 3, "lock", NULL));

  {
    RkRule *dbl = rule (FP_FINGER_RIGHT_THUMB, FALSE, 0, "shortcut", "double");
    dbl->taps = 2;
    g_ptr_array_add (rules, dbl);
  }

  p = actions_pick (rules, FP_FINGER_RIGHT_THUMB, 0.4, 1);  g_assert_nonnull (p); g_assert_false (p->hold);
  g_assert_cmpstr (p->action, ==, "lock");
  p = actions_pick (rules, FP_FINGER_RIGHT_THUMB, 0.3, 2);  g_assert_cmpstr (p->shortcut, ==, "double");
  p = actions_pick (rules, FP_FINGER_RIGHT_THUMB, 0.3, 3);  g_assert_null (p);       /* no triple rule */
  p = actions_pick (rules, FP_FINGER_RIGHT_THUMB, 2.3, 1);  g_assert_cmpstr (p->shortcut, ==, "two");
  p = actions_pick (rules, FP_FINGER_RIGHT_THUMB, 7.0, 1);  g_assert_cmpstr (p->shortcut, ==, "five");
  p = actions_pick (rules, FP_FINGER_LEFT_INDEX, 1.0, 1);   g_assert_null (p);       /* hold only, too short */
  p = actions_pick (rules, FP_FINGER_LEFT_INDEX, 3.0, 1);   g_assert_nonnull (p);
  p = actions_pick (rules, FP_FINGER_RIGHT_INDEX, 1.0, 1);  g_assert_null (p);       /* no rule for it */
  g_assert_cmpuint (actions_max_taps (rules, FP_FINGER_RIGHT_THUMB), ==, 2);
  g_assert_cmpuint (actions_max_taps (rules, FP_FINGER_LEFT_INDEX), ==, 1);
  g_assert_true (actions_check_unique (rules, NULL));        /* tap, hold 2, hold 5, double: all different */
  {
    GError *e = NULL;
    g_ptr_array_add (rules, rule (FP_FINGER_RIGHT_THUMB, TRUE, 2, "lock", NULL));   /* hold 2 again */
    g_assert_false (actions_check_unique (rules, &e));
    g_assert_nonnull (strstr (e->message, "right-thumb hold 2 s is set twice"));
    g_error_free (e);
    g_ptr_array_remove_index (rules, rules->len - 1);
  }
  g_assert_true (actions_is_hold (rules, FP_FINGER_RIGHT_THUMB, 2.0));
  g_assert_false (actions_is_hold (rules, FP_FINGER_RIGHT_THUMB, 1.9));
}

static void
test_roundtrip (void)
{
  g_autofree char *root = g_dir_make_tmp ("rk-actions-XXXXXX", NULL);
  g_autoptr(GPtrArray) rules = g_ptr_array_new_with_free_func ((GDestroyNotify) rk_rule_free);
  g_autoptr(GPtrArray) back = NULL;
  g_autoptr(GPtrArray) none = NULL;
  g_autoptr(GPtrArray) empty = g_ptr_array_new ();

  g_setenv ("RIDGEKEY_STORE_ROOT", root, TRUE);
  g_ptr_array_add (rules, rule (FP_FINGER_LEFT_INDEX, FALSE, 0, "lock", NULL));
  g_ptr_array_add (rules, rule (FP_FINGER_RIGHT_THUMB, TRUE, 2, "shortcut", "Work mode ✓"));
  ((RkRule *) rules->pdata[0])->taps = 2;
  g_assert_true (actions_save (501, rules, NULL));
  {
    /* a hand-edited file with a duplicate trigger: the later one is ignored */
    g_autofree char *path = g_build_filename (root, "actions", "502.conf", NULL);
    g_autoptr(GPtrArray) d = NULL;
    g_assert_true (g_file_set_contents (path,
        "[a]\nfinger=left-index\ngesture=tap\ntaps=1\naction=lock\n"
        "[b]\nfinger=left-index\ngesture=tap\ntaps=1\naction=shortcut\nshortcut=X\n", -1, NULL));
    d = actions_load (502);
    g_assert_cmpuint (d->len, ==, 1);
    g_assert_cmpstr (((RkRule *) d->pdata[0])->action, ==, "lock");
  }
  back = actions_load (501);
  g_assert_cmpuint (back->len, ==, 2);
  g_assert_cmpint (((RkRule *) back->pdata[1])->seconds, ==, 2);
  g_assert_cmpint (((RkRule *) back->pdata[0])->taps, ==, 2);
  g_assert_cmpstr (((RkRule *) back->pdata[1])->shortcut, ==, "Work mode ✓");
  {
    g_autofree char *path = g_build_filename (root, "actions", "501.conf", NULL);
    GStatBuf st;
    g_assert_cmpint (g_stat (path, &st), ==, 0);
    g_assert_cmpint (st.st_mode & 0777, ==, 0600);
  }
  g_assert_true (actions_save (501, empty, NULL));          /* empty = remove */
  none = actions_load (501);
  g_assert_cmpuint (none->len, ==, 0);
}

int
main (int argc, char **argv)
{
  g_test_init (&argc, &argv, NULL);
  g_test_add_func ("/actions/validate", test_validate);
  g_test_add_func ("/actions/pick", test_pick);
  g_test_add_func ("/actions/roundtrip", test_roundtrip);
  return g_test_run ();
}
