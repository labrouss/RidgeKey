/* SPDX-License-Identifier: MIT */
#include "fprint.h"
#include <stdio.h>
#include <gio/gio.h>
#include <string.h>
struct _FpPrint { GObject parent; FpFinger finger; char *id; };
G_DEFINE_TYPE (FpPrint, fp_print, G_TYPE_OBJECT)
static void fin (GObject *o) { g_free (FP_PRINT (o)->id); G_OBJECT_CLASS (fp_print_parent_class)->finalize (o); }
static void fp_print_class_init (FpPrintClass *k) { G_OBJECT_CLASS (k)->finalize = fin; }
static void fp_print_init (FpPrint *p) {}
FpPrint *mock_print_new (FpFinger f, const char *id) { FpPrint *p = g_object_new (fp_print_get_type (), NULL); p->finger = f; p->id = g_strdup (id); return p; }
const char *mock_print_id (FpPrint *p) { return p->id; }
FpFinger fp_print_get_finger (FpPrint *p) { return p->finger; }
gboolean fp_print_serialize (FpPrint *p, guchar **d, gsize *l, GError **e) { char *s = g_strdup_printf ("MOCK %d %s", p->finger, p->id); *d = (guchar *) s; *l = strlen (s); return TRUE; }
FpPrint *fp_print_deserialize (const guchar *d, gsize l, GError **e) {
  g_autofree char *s = g_strndup ((const char *) d, l); int f; char id[128];
  if (sscanf (s, "MOCK %d %127s", &f, id) != 2) { g_set_error (e, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "bad print data"); return NULL; }
  return mock_print_new ((FpFinger) f, id); }
