/* SPDX-License-Identifier: MIT */
/* Mock of the libfprint API used by store.c - for testing store.c logic only. */
#pragma once
#include <glib-object.h>
typedef enum { FP_FINGER_UNKNOWN = 0, FP_FINGER_LEFT_THUMB, FP_FINGER_LEFT_INDEX, FP_FINGER_LEFT_MIDDLE,
  FP_FINGER_LEFT_RING, FP_FINGER_LEFT_LITTLE, FP_FINGER_RIGHT_THUMB, FP_FINGER_RIGHT_INDEX,
  FP_FINGER_RIGHT_MIDDLE, FP_FINGER_RIGHT_RING, FP_FINGER_RIGHT_LITTLE } FpFinger;
G_DECLARE_FINAL_TYPE (FpPrint, fp_print, FP, PRINT, GObject)
FpPrint *mock_print_new (FpFinger f, const char *id);
const char *mock_print_id (FpPrint *p);
FpFinger fp_print_get_finger (FpPrint *p);
gboolean fp_print_serialize (FpPrint *p, guchar **data, gsize *len, GError **e);
FpPrint *fp_print_deserialize (const guchar *data, gsize len, GError **e);
