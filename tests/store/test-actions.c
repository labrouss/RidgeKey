/* Unit tests for src/daemon/actions.c: validation, save/load round trip, tap/hold choice. */
#include <glib.h>
#include <glib/gstdio.h>
#include <string.h>
#include "actions.h"

static RkRule *
rule (FpFinger f, gboolean hold, guint secs, const char *action, const char *sc)
{
  RkRule *r = g_new0 (RkRule, 1);
  r->finger = f; r->hold = hold; r->seconds = secs;
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

  p = actions_pick (rules, FP_FINGER_RIGHT_THUMB, 0.4);  g_assert_nonnull (p); g_assert_false (p->hold);
  p = actions_pick (rules, FP_FINGER_RIGHT_THUMB, 2.3);  g_assert_cmpstr (p->shortcut, ==, "two");
  p = actions_pick (rules, FP_FINGER_RIGHT_THUMB, 7.0);  g_assert_cmpstr (p->shortcut, ==, "five");
  p = actions_pick (rules, FP_FINGER_LEFT_INDEX, 1.0);   g_assert_null (p);          /* hold only, too short */
  p = actions_pick (rules, FP_FINGER_LEFT_INDEX, 3.0);   g_assert_nonnull (p);
  p = actions_pick (rules, FP_FINGER_RIGHT_INDEX, 1.0);  g_assert_null (p);          /* no rule for it */
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
  g_assert_true (actions_save (501, rules, NULL));
  back = actions_load (501);
  g_assert_cmpuint (back->len, ==, 2);
  g_assert_cmpint (((RkRule *) back->pdata[1])->seconds, ==, 2);
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
